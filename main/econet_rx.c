/*
 * EconetWiFi
 * Copyright (c) 2025 Paul G. Banks <https://paulbanks.org/projects/econet>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the LICENSE file in the project root for full license information.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/message_buffer.h"
#include "driver/parlio_rx.h"
#include "driver/gpio.h"

#define ECONET_PRIVATE_API
#include "econet.h"

#define ECONET_IDLE_BITS 15

MessageBufferHandle_t DRAM_ATTR econet_rx_frame_buffer;

static parlio_rx_unit_handle_t rx_unit;
static parlio_rx_delimiter_handle_t rx_delimiter;
static uint8_t DRAM_ATTR rx_payload_dma_buffer[16];

static uint8_t DRAM_ATTR _raw_shift_in;
static uint8_t DRAM_ATTR _recv_data_shift_in;
static uint32_t DRAM_ATTR _recv_data_bit;
static uint32_t DRAM_ATTR is_frame_active;
static uint8_t DRAM_ATTR rx_bytes[ECONET_MTU];
static uint16_t DRAM_ATTR rx_frame_len;
static uint16_t DRAM_ATTR rx_crc;
static uint8_t DRAM_ATTR rx_idle_one_counter;

portMUX_TYPE DRAM_ATTR econet_rx_interrupt_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct
{
    uint32_t w[8];
} bitmap256_t;

// These bitmaps determine which stations or networks we answer for on the Econet
static DRAM_ATTR bitmap256_t rx_station_bitmap;
static DRAM_ATTR bitmap256_t rx_network_bitmap;

static inline bool bm256_test(const bitmap256_t *bm, uint8_t bit)
{
    uint32_t word = bit >> 5;
    uint32_t offset = bit & 31;
    return (bm->w[word] >> offset) & 1u;
}

static inline void bm256_set(bitmap256_t *bm, uint8_t bit)
{
    uint32_t word = bit >> 5;
    uint32_t offset = bit & 31;
    bm->w[word] |= (1u << offset);
}

static inline void IRAM_ATTR _begin_frame(void)
{
    _recv_data_bit = 0;
    rx_frame_len = 0;
    rx_crc = 0xFFFF;
    is_frame_active = 1;
}

static inline void IRAM_ATTR _complete_frame()
{
    is_frame_active = 0;

    if (rx_frame_len < 6)
    {
        econet_stats.rx_short_frame_count++;
        return;
    }

    // Check CRC residual
    if (rx_crc != 0xF0B8)
    {
        econet_stats.rx_crc_fail_count++;
        return;
    }

    econet_stats.rx_frame_count++;

    // Is this for us?
    if ((bm256_test(&rx_station_bitmap, rx_bytes[0]) && rx_bytes[1] == 0x00) || bm256_test(&rx_network_bitmap, rx_bytes[1]))
    {

        uint32_t data_len = rx_frame_len - 2;

        // Send ACK immediately
        BaseType_t is_awoken = true;
        if (data_len > 4)
        {
            econet_tx_command_t ack_cmd = {
                .cmd = 'A',
                .dst_stn = rx_bytes[2],
                .dst_net = rx_bytes[3],
                .src_stn = rx_bytes[0],
                .src_net = rx_bytes[1]};
            xQueueSendFromISR(tx_command_queue, &ack_cmd, NULL);
            econet_tx_pre_go();

            portENTER_CRITICAL_ISR(&econet_rx_interrupt_lock);
            xMessageBufferSendFromISR(econet_rx_frame_buffer, rx_bytes, data_len, pdFALSE);
            portEXIT_CRITICAL_ISR(&econet_rx_interrupt_lock);
        }
        else
        {
            // Received ACK, let TX side know
            econet_stats.rx_ack_count++;

            econet_tx_command_t ack_cmd = {
                .cmd = 'a',
                .dst_stn = rx_bytes[0],
                .dst_net = rx_bytes[1],
                .src_stn = rx_bytes[2],
                .src_net = rx_bytes[3]};
            xQueueSendFromISR(tx_command_queue, &ack_cmd, NULL);
        }

        portYIELD_FROM_ISR(is_awoken);
    }
}

static inline void IRAM_ATTR _clk_bit(uint8_t c)
{

    if (c && !tx_is_in_progress)
    {
        if (rx_idle_one_counter < ECONET_IDLE_BITS)
        {
            rx_idle_one_counter++;
            if (rx_idle_one_counter == ECONET_IDLE_BITS)
            {
                portENTER_CRITICAL_ISR(&econet_rx_interrupt_lock);
                char idle_rx_cmd = 'I';
                xMessageBufferSendFromISR(econet_rx_frame_buffer, &idle_rx_cmd, 1, NULL);
                portEXIT_CRITICAL_ISR(&econet_rx_interrupt_lock);
                econet_tx_command_t idle_cmd = {
                    .cmd = 'I',
                };
                xQueueSendFromISR(tx_command_queue, &idle_cmd, NULL);
                portYIELD_FROM_ISR(pdTRUE);
            }
        }
    }
    else
    {
        rx_idle_one_counter = 0;
    }

    _raw_shift_in = (_raw_shift_in << 1) | c;

    // Search for flag
    if (_raw_shift_in == 0x7e)
    {
        if (is_frame_active == 0)
        {
            _begin_frame();
        }
        else
        {
            // If, after getting a flag, we've got something other than a flag then we
            // consider this a frame. Otherwise it's just a stream of flags
            // so we remain at the start of the frame
            if (rx_frame_len > 1)
            {
                _complete_frame();
            }
            else
            {
                _begin_frame();
            }
        }
        return;
    }

    if (is_frame_active == 0)
    {
        return;
    }

    // Search for ABORT
    if (_raw_shift_in == 0x7f)
    {
        is_frame_active = 0;
        econet_stats.rx_abort_count++;
        return;
    }

    // Bit stuffing
    if ((_raw_shift_in & 0x3f) == 0x3e)
    {
        return;
    }

    // Data
    _recv_data_shift_in = (_recv_data_shift_in >> 1) | (c << 7); // Data is LSB first
    _recv_data_bit += 1;
    if (_recv_data_bit == 8)
    {

        // Update CRC
        rx_crc ^= _recv_data_shift_in;
        for (int j = 0; j < 8; j++)
        {
            rx_crc = (rx_crc & 0x0001) ? (uint16_t)((rx_crc >> 1) ^ 0x8408)
                                       : (uint16_t)(rx_crc >> 1);
        }

        rx_bytes[rx_frame_len] = _recv_data_shift_in;
        rx_frame_len += 1;
        if (rx_frame_len == sizeof(rx_bytes))
        {
            is_frame_active = 0;
            econet_stats.rx_oversize_count++;
            return;
        }
        _recv_data_bit = 0;
    }
}

static bool IRAM_ATTR _on_recv_callback(parlio_rx_unit_handle_t rx_unit, const parlio_rx_event_data_t *edata, void *user_data)
{
    uint8_t c = *((uint8_t *)edata->data);
    for (int i = 0; i < 4; i++)
    {
        _clk_bit((c & 0x40) >> 6);
        c <<= 2;
    }
    return false;
}

bool econet_rx_is_idle(void)
{
    return rx_idle_one_counter == ECONET_IDLE_BITS;
}

void econet_rx_setup(void)
{
    parlio_rx_unit_config_t rx_config = {
        .trans_queue_depth = sizeof(rx_payload_dma_buffer),
        .max_recv_size = 1,
        .data_width = 2,
        .clk_src = PARLIO_CLK_SRC_EXTERNAL,
        .ext_clk_freq_hz = econet_cfg.clk_freq_hz,
        .clk_in_gpio_num = econet_cfg.clk_pin,
        .exp_clk_freq_hz = econet_cfg.clk_freq_hz,
        .clk_out_gpio_num = GPIO_NUM_NC,
        .valid_gpio_num = GPIO_NUM_NC,
        .flags = {
            .clk_gate_en = false,
        },
        .data_gpio_nums = {
            econet_cfg.data_in_pin,
            -1,
            -1,
            -1,
            -1,
            -1,
            -1,
        },
    };
    ESP_ERROR_CHECK(parlio_new_rx_unit(&rx_config, &rx_unit));

    parlio_rx_soft_delimiter_config_t delimiter_cfg = {
        .sample_edge = PARLIO_SAMPLE_EDGE_POS,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_MSB,
        .timeout_ticks = 0,
        .eof_data_len = 1,
    };
    ESP_ERROR_CHECK(parlio_new_rx_soft_delimiter(&delimiter_cfg, &rx_delimiter));

    parlio_rx_event_callbacks_t cbs = {
        .on_partial_receive = _on_recv_callback,
    };
    ESP_ERROR_CHECK(parlio_rx_unit_register_event_callbacks(rx_unit, &cbs, NULL));

    econet_rx_frame_buffer = xMessageBufferCreate(ECONET_MTU);
}

void econet_rx_start(void)
{
    ESP_ERROR_CHECK(parlio_rx_unit_enable(rx_unit, true));

    parlio_receive_config_t rx_cfg = {
        .delimiter = rx_delimiter,
        .flags = {
            .partial_rx_en = true,
        }};

    for (int i = 0; i < sizeof(rx_payload_dma_buffer); i++)
    {
        ESP_ERROR_CHECK(parlio_rx_unit_receive(rx_unit, &rx_payload_dma_buffer[i], 1, &rx_cfg));
    }

    ESP_ERROR_CHECK(parlio_rx_soft_delimiter_start_stop(rx_unit, rx_delimiter, true));
}

void econet_rx_clear_bitmaps(void)
{
    memset(&rx_station_bitmap, 0, sizeof(rx_station_bitmap));
    memset(&rx_network_bitmap, 0, sizeof(rx_network_bitmap));
}

void exonet_rx_enable_station(uint8_t station_id)
{
    bm256_set(&rx_station_bitmap, station_id);
}

void exonet_rx_enable_network(uint8_t network_id)
{
    bm256_set(&rx_network_bitmap, network_id);
}
