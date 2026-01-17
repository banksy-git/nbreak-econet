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

#include "config.h"
#define ECONET_PRIVATE_API
#include "econet.h"

#define ECONET_PARLIO_WIDTH 2
#define ECONET_FLAGSTREAM_PADDING 6

typedef struct
{
    uint8_t *bits;
    size_t bits_size;
    uint32_t byte_pos;
    uint32_t bit_pos;
    uint8_t one_count;
    uint8_t c;
} tx_bitstuff_ctx;

TaskHandle_t DRAM_ATTR tx_task = NULL;
QueueHandle_t DRAM_ATTR tx_command_queue;
volatile bool DRAM_ATTR tx_is_in_progress;
volatile bool DRAM_ATTR tx_is_awaiting_imm_reply;

static parlio_tx_unit_handle_t DRAM_ATTR tx_unit;
static TaskHandle_t DRAM_ATTR tx_sender_task = NULL;
static econet_acktype_t DRAM_ATTR tx_sent_ack;

// Immediate reply
static uint8_t *_imm_reply;
static uint16_t _imm_reply_len;

// Outgoing frame
static uint8_t DRAM_ATTR tx_flag_stream[ECONET_FLAGSTREAM_PADDING * ECONET_PARLIO_WIDTH];
static uint32_t DRAM_ATTR tx_flag_stream_length;
static size_t DRAM_ATTR scout_bits_len;
static uint8_t DRAM_ATTR scout_bits[32 * ECONET_PARLIO_WIDTH];
static uint8_t DRAM_ATTR tx_bits[ECONET_MTU * ECONET_PARLIO_WIDTH];
static size_t DRAM_ATTR tx_bits_len;

// Custom ParlIO driver
static volatile bool DRAM_ATTR is_flagstream_queued;
void parlio_tx_neg_edge(parlio_tx_unit_handle_t tx_unit);
void parlio_tx_go(parlio_tx_unit_handle_t tx_unit);
esp_err_t parlio_tx_unit_pretransmit(parlio_tx_unit_handle_t tx_unit, const void *payload, size_t payload_bits, const parlio_transmit_config_t *config);
void IRAM_ATTR econet_tx_pre_go(void)
{
    tx_is_in_progress = true;
    if (is_flagstream_queued)
    {
        parlio_tx_go(tx_unit);
        is_flagstream_queued = false;
    }
}

static esp_err_t _queue_flagstream()
{
    if (is_flagstream_queued)
    {
        return ESP_OK;
    }
    parlio_transmit_config_t flax_tx_cfg = {
        .idle_value = 0x00,
    };
    esp_err_t ret = parlio_tx_unit_pretransmit(tx_unit, tx_flag_stream, tx_flag_stream_length * 8, &flax_tx_cfg);
    is_flagstream_queued = true;
    return ret;
}

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

static inline void IRAM_ATTR _add_raw_bit(tx_bitstuff_ctx *ctx, uint8_t b)
{
    ctx->c = ctx->c << ECONET_PARLIO_WIDTH | b;
    ctx->bit_pos += ECONET_PARLIO_WIDTH;
    if (ctx->bit_pos >= 8)
    {
        if (ctx->byte_pos < ctx->bits_size)
        {
            ctx->bits[ctx->byte_pos] = ctx->c;
        }
        ctx->c = 0;
        ctx->bit_pos = 0;
        ctx->byte_pos++;
    }
}

static inline void IRAM_ATTR _add_bit(tx_bitstuff_ctx *ctx, uint8_t bit)
{
    _add_raw_bit(ctx, (bit ? 1 : 0) | 2);
}

static inline void IRAM_ATTR _add_byte_unstuffed(tx_bitstuff_ctx *ctx, uint8_t c)
{
    for (int j = 0; j < 8; j++)
    {
        _add_bit(ctx, c & 1);
        c >>= 1;
    }
}

static inline void IRAM_ATTR _add_byte_stuffed(tx_bitstuff_ctx *ctx, uint8_t c)
{

    for (int j = 0; j < 8; j++)
    {
        uint8_t bit = (c & 1);
        _add_bit(ctx, bit);
        c >>= 1;
        if (bit != 0)
        {
            ctx->one_count += 1;
        }
        else
        {
            ctx->one_count = 0;
        }

        // Bit stuffing
        if (ctx->one_count == 5)
        {
            _add_bit(ctx, 0);
            ctx->one_count = 0;
        }
    }
}

size_t IRAM_ATTR _generate_frame_bits(uint8_t *bits, size_t bits_size, const uint8_t *payload, size_t payload_length)
{

    tx_bitstuff_ctx stuff_ctx = {
        .bits = bits,
        .bits_size = bits_size,
    };

    // Double flag - this is because the handover from flagstream to
    // our stream is problematic. Future version we'll customise the
    // PARLIO driver fully to get rid of this nonsense
    _add_byte_unstuffed(&stuff_ctx, 0x7e);
    _add_byte_unstuffed(&stuff_ctx, 0x7e);

    for (int i = 0; i < payload_length; i++)
    {
        _add_byte_stuffed(&stuff_ctx, payload[i]);
    }

    // Compute CRC over unstuffed payload bytes
    uint16_t fcs = crc16_x25(payload, payload_length);

    // Emit CRC (16 bits)
    uint8_t fcs_bytes[2] = {(uint8_t)(fcs & 0xFF), (uint8_t)(fcs >> 8)};
    for (int i = 0; i < 2; i++)
    {
        _add_byte_stuffed(&stuff_ctx, fcs_bytes[i]);
    }

    // Flag must be unstuffed (but still packed)
    _add_byte_unstuffed(&stuff_ctx, 0x7e);

    // Pad out block so it's on correct boundary
    // otherwise subequent transactions are screwed up
    while (stuff_ctx.bit_pos || (stuff_ctx.byte_pos % 4) != 0)
    {
        _add_raw_bit(&stuff_ctx, 0);
    }

    // Check for overflow
    if (stuff_ctx.byte_pos > stuff_ctx.bits_size)
    {
        return 0;
    }

    return stuff_ctx.byte_pos;
}

size_t IRAM_ATTR _generate_flag_stream(uint8_t *bits, size_t bits_size, int number_of_flags)
{
    tx_bitstuff_ctx stuff_ctx = {
        .bits = bits,
        .bits_size = bits_size,
    };
    for (int i = 0; i < number_of_flags; i++)
    {
        _add_byte_unstuffed(&stuff_ctx, 0x7e);
    }
    if (stuff_ctx.byte_pos > stuff_ctx.bits_size)
    {
        return 0;
    }
    return stuff_ctx.byte_pos;
}

static void IRAM_ATTR _transmit_bits(const uint8_t *bits, size_t length)
{
    econet_tx_pre_go();
    parlio_transmit_config_t transmit_config = {
        .idle_value = 0x0,
    };
    tx_is_in_progress = true;
    ESP_ERROR_CHECK(parlio_tx_unit_transmit(tx_unit, bits, length * 8, &transmit_config));
    parlio_tx_unit_wait_all_done(tx_unit, -1);
    tx_is_in_progress = false;
}

static void IRAM_ATTR _complete_tx_command(econet_acktype_t result)
{
    tx_sent_ack = result;
    econet_stats.tx_frame_count++;
    switch (result)
    {
    case ECONET_NACK:
    case ECONET_NACK_CORRUPT:
        econet_stats.rx_nack_count++;
    default:
    }
    xTaskNotifyGive(tx_sender_task);
}

static void IRAM_ATTR _tx_task(void *params)
{
    bool is_data_ready = false;
    uint8_t ack_bits[128];

    tx_task = xTaskGetCurrentTaskHandle();

    for (;;)
    {
        tx_is_awaiting_imm_reply = false;

        _queue_flagstream();

        econet_tx_command_t cmd;
        if (xQueueReceive(tx_command_queue, &cmd, portMAX_DELAY) != pdTRUE)
        {
            ESP_LOGE(TAG, "Failed to get TX command queue item");
            vTaskDelete(NULL);
            return;
        }

        // Generate ACK.
        if (cmd.cmd == 'A')
        {
            // Generate and send ack
            size_t tx_len = _generate_frame_bits(ack_bits, sizeof(ack_bits), &cmd.dst_stn, 4);
            _transmit_bits(ack_bits, tx_len);
            econet_stats.tx_ack_count++;
            continue;
        }

        if (cmd.cmd == 'R')
        {
            ESP_LOGE(TAG, "Unexpected IMM reply. tx_is_awaiting_imm_reply=%d", tx_is_awaiting_imm_reply);
            continue;
        }

        if (cmd.cmd == 'S')
        {
            is_data_ready = true;
        }

        if (!is_data_ready || !econet_rx_is_idle())
        {
            continue;
        }

        is_data_ready = false;

        // Send scout
        if (cmd.flags == ECONET_TX_IMM_WITH_REPLY)
        {
            tx_is_awaiting_imm_reply = true;
        }
        _transmit_bits(scout_bits, scout_bits_len);
        _queue_flagstream();

        // Wait for ack or imm data
        econet_tx_command_t response_cmd;
        if (xQueueReceive(tx_command_queue, &response_cmd, 200) == pdFALSE)
        {
            ESP_LOGW(TAG, "Timeout waiting for scout ack");
            _complete_tx_command(ECONET_NACK);
            continue;
        }
        if (response_cmd.cmd == 'I')
        {
            ESP_LOGI(TAG, "Bus became idle whilst waiting for scout ack (%d)", econet_rx_is_idle());
            _complete_tx_command(ECONET_NACK);
            continue;
        }
        if (response_cmd.cmd == 'R')
        {
            if (cmd.flags == ECONET_TX_IMM_WITH_REPLY)
            {
                _imm_reply = response_cmd.imm_reply + ECONET_RX_BUFFER_WORKSPACE;
                _imm_reply_len = response_cmd.imm_length;
                _complete_tx_command(ECONET_IMM_REPLY);
                continue;
            }
            else
            {
                ESP_LOGE(TAG, "Unexpected IMM reply after scout. tx_is_awaiting_imm_reply=%d", tx_is_awaiting_imm_reply);
                continue;
            }
        }
        if (cmd.flags == ECONET_TX_IMM_NO_DATA)
        {
            _complete_tx_command(ECONET_ACK);
            continue;
        }

        // Send payload frame
        _transmit_bits(tx_bits, tx_bits_len);

        // Wait for ack
        if (xQueueReceive(tx_command_queue, &response_cmd, 200) == pdFALSE)
        {
            ESP_LOGW(TAG, "Timeout waiting for data ack");
            _complete_tx_command(ECONET_NACK_CORRUPT);
            continue;
        }
        if (response_cmd.cmd == 'I')
        {
            ESP_LOGW(TAG, "Bus became idle whilst waiting for data ack");
            _complete_tx_command(ECONET_NACK_CORRUPT);
            continue;
        }

        _complete_tx_command(ECONET_ACK);
    }
}

econet_acktype_t econet_send(uint8_t *data, uint16_t length, uint8_t **imm_reply, uint16_t *imm_reply_len)
{
    tx_sender_task = xTaskGetCurrentTaskHandle();

    if (length < sizeof(econet_scout_t))
    {
        ESP_LOGE(TAG, "Refusing to send short packet len=%d", length);
        return ECONET_SEND_ERROR;
    }

    // Generate scout
    uint8_t tx_cmd_flags = ECONET_TX_NORMAL;
    econet_scout_t scout;
    memcpy(&scout, data, sizeof(scout));

    if (scout.port == 0) // Immedate frame handling
    {
        if (scout.control == ECONET_CTRL_PEEK || scout.control == ECONET_CTRL_MACHINETYPE || scout.control == ECONET_CTRL_GETREGISTERS)
        {
            tx_cmd_flags = ECONET_TX_IMM_WITH_REPLY;
        }
        if (scout.control == ECONET_CTRL_HALT || scout.control == ECONET_CTRL_CONTINUE)
        {
            tx_cmd_flags = ECONET_TX_IMM_NO_DATA;
        }
    }

    if (tx_cmd_flags == ECONET_TX_IMM_WITH_REPLY || tx_cmd_flags == ECONET_TX_IMM_NO_DATA)
    {
        // Generate IMM scout frame with attached data
        if (length - sizeof(econet_scout_t) > 8)
        {
            ESP_LOGE(TAG, "Refusing to send immediate frame with too much data. length=%d", length);
            return ECONET_SEND_ERROR;
        }
        scout_bits_len = _generate_frame_bits(scout_bits, sizeof(scout_bits), data, length);
    }
    else
    {
        // Generate normal scout frame
        scout_bits_len = _generate_frame_bits(scout_bits, sizeof(scout_bits), (uint8_t *)&scout, sizeof(scout));
    }

    // Generate payload frame
    data[2] = scout.hdr.dst_stn;
    data[3] = scout.hdr.dst_net;
    data[4] = scout.hdr.src_stn;
    data[5] = scout.hdr.src_net;
    tx_bits_len = _generate_frame_bits(tx_bits, sizeof(tx_bits), &data[2], length - 2);

    // Notify sender task
    econet_tx_command_t cmd = {.cmd = 'S', .flags = tx_cmd_flags};
    if (xQueueSend(tx_command_queue, &cmd, 1000) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to post econet send command. This is a bug.");
        return ECONET_SEND_ERROR;
    }

    // Wait for send completion
    if (ulTaskNotifyTake(pdTRUE, 10000) != pdTRUE)
    {
        ESP_LOGE(TAG, "Timeout waiting for send. Missing clock or line jammed?");
        return ECONET_SEND_ERROR;
    };

    if (imm_reply)
    {
        if (_imm_reply_len >= 4)
        {
            *imm_reply = _imm_reply + 4;
            *imm_reply_len = _imm_reply_len - 4;
        }
    }
    return tx_sent_ack;
}

void econet_tx_setup(void)
{
    parlio_tx_unit_config_t tx_config = {
        .clk_src = PARLIO_CLK_SRC_EXTERNAL,
        .data_width = ECONET_PARLIO_WIDTH,
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
        .trans_queue_depth = 4,
        .max_transfer_size = sizeof(tx_bits),
        .sample_edge = PARLIO_SAMPLE_EDGE_POS, // This is no-op - we're not using the output clock. See econet_tx_start.
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_MSB,
    };
    ESP_ERROR_CHECK(parlio_new_tx_unit(&tx_config, &tx_unit));

    tx_command_queue = xQueueCreate(8, sizeof(econet_tx_command_t));

    // Pre-calculate flag bitstream
    tx_flag_stream_length = _generate_flag_stream(tx_flag_stream, sizeof(tx_flag_stream), ECONET_FLAGSTREAM_PADDING);
    if (tx_flag_stream_length == 0)
    {
        ESP_LOGE(TAG, "Insufficient buffer for flag stream!");
        vTaskDelete(NULL);
        return;
    }
}

void econet_tx_start(void)
{
    // Load clock configuration to check if we should invert the clock
    config_econet_clock_t clock_cfg;
    config_get_econet_clock(&clock_cfg);

    // When invert_clock is false (default), we invert via GPIO matrix to work around
    // the broken .sample_edge parameter. When invert_clock is true, we disable this
    // inversion so the output clock is not inverted.
    if (!clock_cfg.invert_clock)
    {
        // Hack: The .sample_edge parameter doesn't actually work.
        // Whatever you set it to it always seems to make output changes on the POS edge
        // So this function uses the GPIO matrix to invert the clock signal prior to
        // delivery to the peripheral.
        parlio_tx_neg_edge(tx_unit);
    }

    ESP_ERROR_CHECK(parlio_tx_unit_enable(tx_unit));
    xTaskCreate(_tx_task, "adlc_tx", 8192, NULL, 24, NULL);
}
