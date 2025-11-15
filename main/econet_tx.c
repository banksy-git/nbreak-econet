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
#include "esp_log.h"

#include "driver/parlio_tx.h"
#include "driver/gpio.h"

#define ECONET_PRIVATE_API
#include "econet.h"

static parlio_tx_unit_handle_t tx_unit;
static TaskHandle_t tx_sender_task = NULL;
static bool tx_sent_ack;
static uint8_t tx_frame[2048];
static uint8_t tx_bits[16384];
static uint32_t tx_write_byte_pos;
static uint32_t tx_write_bit_pos;
static uint8_t tx_write_one_count;

TaskHandle_t tx_task = NULL;
MessageBufferHandle_t tx_frame_buffer;
portMUX_TYPE tx_frame_buffer_lock = portMUX_INITIALIZER_UNLOCKED;

static inline uint16_t IRAM_ATTR crc16_x25(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
        {
            crc = (crc & 0x0001) ? (uint16_t)((crc >> 1) ^ 0x8408)
                                 : (uint16_t)(crc >> 1);
        }
    }
    return (uint16_t)(crc ^ 0xFFFF);
}

static inline void IRAM_ATTR _add_raw_bit(uint8_t b)
{
    const int shift_width = 4;
    tx_bits[tx_write_byte_pos] = (tx_bits[tx_write_byte_pos] << shift_width) | b;
    tx_write_bit_pos += shift_width;
    if (tx_write_bit_pos >= 8)
    {
        tx_write_bit_pos = 0;
        tx_write_byte_pos++;
    }
}

static inline void IRAM_ATTR _add_bit(uint8_t bit)
{
    _add_raw_bit((bit ? 1 : 0) | 2);
}

static inline void IRAM_ATTR _add_byte_unstuffed(uint8_t c)
{
    for (int j = 0; j < 8; j++)
    {
        _add_bit(c & 1);
        c >>= 1;
    }
}

static inline void IRAM_ATTR _add_byte_stuffed(uint8_t c)
{

    for (int j = 0; j < 8; j++)
    {
        uint8_t bit = (c & 1);
        _add_bit(bit);
        c >>= 1;
        if (bit != 0)
        {
            tx_write_one_count += 1;
        }
        else
        {
            tx_write_one_count = 0;
        }

        // Bit stuffing
        if (tx_write_one_count == 5)
        {
            _add_bit(0);
            tx_write_one_count = 0;
        }
    }
}

uint32_t IRAM_ATTR _generate_frame_bits(const uint8_t *payload, size_t payload_length)
{
    tx_write_byte_pos = 0;
    tx_write_bit_pos = 0;
    tx_write_one_count = 0;

    _add_byte_unstuffed(0x7e);

    for (int i = 0; i < payload_length; i++)
    {
        _add_byte_stuffed(payload[i]);
    }

    // Compute CRC over unstuffed payload bytes
    uint16_t fcs = crc16_x25(payload, payload_length);

    // Emit CRC (16 bits)
    uint8_t fcs_bytes[2] = {(uint8_t)(fcs & 0xFF), (uint8_t)(fcs >> 8)};
    for (int i = 0; i < 2; i++)
    {
        _add_byte_stuffed(fcs_bytes[i]);
    }

    // Flag must be unstuffed (but still packed)
    _add_byte_unstuffed(0x7e);

    // Pad out block so it's on correct boundary
    // otherwise subequent transactions are screwed up
    while (tx_write_bit_pos || (tx_write_byte_pos % 4) != 0)
    {
        _add_raw_bit(0);
    }

    return tx_write_byte_pos;
}

static void IRAM_ATTR _tx_task(void *params)
{
    static uint8_t DRAM_ATTR bus_grab[] = {0xBF, 0xFE, 0xBF, 0xFE};
    static uint8_t DRAM_ATTR scout_bits[512];

    tx_task = xTaskGetCurrentTaskHandle();

    // Configure TX unit transmission parameters
    parlio_transmit_config_t transmit_config = {
        .idle_value = 0x0,
    };

    for (;;)
    {
        size_t tx_frame_length = xMessageBufferReceive(tx_frame_buffer, tx_frame, sizeof(tx_frame), portMAX_DELAY);

        // If this is an ACK, generate it and send
        if (tx_frame_length == 4)
        {
            // Grab bus - this is cheeky hack; I tried flag but it didn't
            // emit properly. Will use this for now.
            parlio_transmit_config_t flax_tx_cfg = {
                .idle_value = 0xFF,
            };
            parlio_tx_unit_transmit(tx_unit, bus_grab, sizeof(bus_grab) * 8, &flax_tx_cfg);

            size_t tx_bits_length = _generate_frame_bits(tx_frame, 4);
            ESP_ERROR_CHECK(parlio_tx_unit_transmit(tx_unit, tx_bits, tx_bits_length * 8, &transmit_config));
            parlio_tx_unit_wait_all_done(tx_unit, -1);
            econet_stats.tx_ack_count++;
            continue;
        }

        // Generate scout frame
        size_t scout_bits_len = _generate_frame_bits(tx_frame, 6);
        memcpy(scout_bits, tx_bits, scout_bits_len);

        // Generate payload frame
        tx_frame[5] = tx_frame[3];
        tx_frame[4] = tx_frame[2];
        tx_frame[3] = tx_frame[1];
        tx_frame[2] = tx_frame[0];
        size_t tx_bits_length = _generate_frame_bits(&tx_frame[2], tx_frame_length - 2);

        // Send scout
        ESP_ERROR_CHECK(parlio_tx_unit_transmit(tx_unit, scout_bits, scout_bits_len * 8, &transmit_config));
        parlio_tx_unit_wait_all_done(tx_unit, -1);

        // Wait for ack
        if (ulTaskNotifyTake(pdTRUE, 200) == 0)
        {
            ESP_LOGW(TAG, "Timeout waiting for scout ack");
            tx_sent_ack = false;
            xTaskNotifyGive(tx_sender_task);
            econet_stats.rx_nack_count++;
            continue;
        }

        // Send payload frame
        ESP_ERROR_CHECK(parlio_tx_unit_transmit(tx_unit, tx_bits, tx_bits_length * 8, &transmit_config));
        parlio_tx_unit_wait_all_done(tx_unit, -1);

        // Wait for ack
        if (ulTaskNotifyTake(pdTRUE, 200) == 0)
        {
            ESP_LOGW(TAG, "Timeout waiting for payload ack");
            tx_sent_ack = false;
            xTaskNotifyGive(tx_sender_task);
            econet_stats.rx_nack_count++;
            continue;
        }

        tx_sent_ack = true;
        xTaskNotifyGive(tx_sender_task);
        econet_stats.tx_frame_count++;
    }
}

bool econet_send(const uint8_t *data, uint16_t length)
{
    tx_sender_task = xTaskGetCurrentTaskHandle();

    portENTER_CRITICAL(&tx_frame_buffer_lock);
    xMessageBufferSend(tx_frame_buffer, data, length, portMAX_DELAY);
    portEXIT_CRITICAL(&tx_frame_buffer_lock);

    ulTaskNotifyTake(pdTRUE, -1); // Wait for send completion (Full 4-way ACK or NACK)

    return tx_sent_ack;
}

void econet_tx_setup(void)
{
    parlio_tx_unit_config_t tx_config = {
        .clk_src = PARLIO_CLK_SRC_EXTERNAL,
        .data_width = 4,
        .clk_in_gpio_num = econet_cfg.clk_pin,
        .input_clk_src_freq_hz = econet_cfg.clk_freq_hz,
        .valid_gpio_num = -1,
        .clk_out_gpio_num = -1,
        .data_gpio_nums = {
            econet_cfg.data_out_pin,
            econet_cfg.data_driver_en_pin,
            -1,
            -1,
            -1,
            -1,
            -1,
            -1,
        },
        .output_clk_freq_hz = econet_cfg.clk_freq_hz,
        .trans_queue_depth = 32,
        .max_transfer_size = 16384,
        .sample_edge = PARLIO_SAMPLE_EDGE_POS,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_MSB,
    };
    ESP_ERROR_CHECK(parlio_new_tx_unit(&tx_config, &tx_unit));

    tx_frame_buffer = xMessageBufferCreate(4096);
}

void econet_tx_start(void)
{
    ESP_ERROR_CHECK(parlio_tx_unit_enable(tx_unit));
    xTaskCreate(_tx_task, "adlc_tx", 8192, NULL, 24, NULL);
}