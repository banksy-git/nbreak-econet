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

#pragma once

#include <stdint.h>
#include "hal/gpio_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"

typedef void (*econet_frame_callback)(uint8_t *data, uint16_t length, void *user_ctx);

typedef struct
{
    gpio_num_t clk_pin;            /*!< ADLC clock input pin */
    uint32_t clk_freq_hz;          /*!< ADLC bit clock frequency */
    gpio_num_t clk_output_pin;     /*!< ADLC clock output pin or -1 if you provide your own */
    gpio_num_t clk_oe_pin;         /*!< ADLC clock output enable pin */
    gpio_num_t data_in_pin;        /*!< ADLC data input pin */
    gpio_num_t data_out_pin;       /*!< ADLC data output pin */
    gpio_num_t data_driver_en_pin; /*!< ADLC data driver enable output pin */

} econet_config_t;

typedef struct
{
    uint32_t rx_frame_count;
    uint32_t rx_crc_fail_count;
    uint32_t rx_short_frame_count;
    uint32_t rx_abort_count;
    uint32_t rx_oversize_count;
    uint32_t rx_ack_count;
    uint32_t rx_nack_count;
    uint32_t tx_frame_count;
    uint32_t tx_ack_count;
} econet_stats_t;

typedef struct
{
    uint8_t dst_stn;
    uint8_t dst_net;
    uint8_t src_stn;
    uint8_t src_net;
    uint8_t control;
    uint8_t port;
    uint8_t data[0];
} econet_hdr_t;

extern econet_stats_t econet_stats;
extern MessageBufferHandle_t econet_rx_frame_buffer;
extern uint8_t econet_station_id;

void econet_setup(const econet_config_t *config);
void econet_start(void);
bool econet_send(const uint8_t *data, uint16_t length);
void econet_reconfigure(void);

#ifdef ECONET_PRIVATE_API
#define TAG "ECONET"
extern econet_config_t econet_cfg;
extern MessageBufferHandle_t tx_frame_buffer;
extern portMUX_TYPE tx_frame_buffer_lock;
extern TaskHandle_t tx_task;
void econet_rx_setup(void);
void econet_rx_start(void);
void econet_tx_setup(void);
void econet_tx_start(void);
#endif