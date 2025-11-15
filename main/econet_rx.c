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

#define ECONET_PRIVATE_API
#include "econet.h"

MessageBufferHandle_t econet_rx_frame_buffer;

static parlio_rx_unit_handle_t rx_unit;
static parlio_rx_delimiter_handle_t rx_delimiter;
static uint8_t rx_payload_dma_buffer[1];
static uint8_t _raw_shift_in;
static uint8_t _recv_data_shift_in;
static uint32_t _recv_data_bit;
static uint32_t is_frame_active;
static uint8_t rx_bytes[2048];
static uint16_t rx_frame_len;
static uint16_t rx_crc;

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
    if (rx_bytes[0] == econet_station_id && rx_bytes[1] == 0x00)
    {

        uint32_t data_len = rx_frame_len - 2;

        // Send ACK immediately
        BaseType_t is_awoken = true;
        if (data_len > 4)
        {
            uint8_t ack_frame[4] = {rx_bytes[2], rx_bytes[3], rx_bytes[0], rx_bytes[1]};
            portENTER_CRITICAL_ISR(&tx_frame_buffer_lock);
            xMessageBufferSend(tx_frame_buffer, ack_frame, sizeof(ack_frame), 0);
            portEXIT_CRITICAL_ISR(&tx_frame_buffer_lock);
            xMessageBufferSend(econet_rx_frame_buffer, rx_bytes, data_len, 0);
        }
        else
        {
            // Received ACK, let TX side know
            econet_stats.rx_ack_count++;
            vTaskNotifyGiveFromISR(tx_task, &is_awoken);
        }

        portYIELD_FROM_ISR(is_awoken);
    }
}

static inline void IRAM_ATTR _clk_bit(uint8_t c)
{

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

void econet_rx_setup(void)
{
    parlio_rx_unit_config_t rx_config = {
        .trans_queue_depth = 512,
        .max_recv_size = sizeof(rx_payload_dma_buffer),
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
        .eof_data_len = sizeof(rx_payload_dma_buffer),
    };
    ESP_ERROR_CHECK(parlio_new_rx_soft_delimiter(&delimiter_cfg, &rx_delimiter));

    parlio_rx_event_callbacks_t cbs = {
        .on_partial_receive = _on_recv_callback,
    };
    ESP_ERROR_CHECK(parlio_rx_unit_register_event_callbacks(rx_unit, &cbs, NULL));

    econet_rx_frame_buffer = xMessageBufferCreate(4096);
}

void econet_rx_start(void)
{
    ESP_ERROR_CHECK(parlio_rx_unit_enable(rx_unit, true));
    ESP_ERROR_CHECK(parlio_rx_soft_delimiter_start_stop(rx_unit, rx_delimiter, true));

    parlio_receive_config_t rx_cfg = {
        .delimiter = rx_delimiter,
        .flags = {
            .partial_rx_en = true,
        }};
    ESP_ERROR_CHECK(parlio_rx_unit_receive(rx_unit, rx_payload_dma_buffer, sizeof(rx_payload_dma_buffer), &rx_cfg));
}
