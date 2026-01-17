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

#include "econet.h"

#define AUN_TYPE_BROADCAST 0x01
#define AUN_TYPE_DATA 0x02
#define AUN_TYPE_ACK 0x03
#define AUN_TYPE_NACK 0x04
#define AUN_TYPE_IMM 0x05
#define AUN_TYPE_IMM_REPLY 0x06

typedef struct
{
    uint8_t transaction_type;
    uint8_t econet_port;
    uint8_t econet_control;
    uint8_t zero;
    uint8_t sequence[4];
} aun_hdr_t;

typedef struct
{
    uint32_t tx_count;
    uint32_t tx_retry_count;
    uint32_t tx_abort_count;
    uint32_t tx_error_count;
    uint32_t tx_ack_count;
    uint32_t tx_nack_count;
    uint32_t tx_bridge_control;
    uint32_t tx_broadcast_count;
    uint32_t rx_imm_count;
    uint32_t rx_data_count;
    uint32_t rx_ack_count;
    uint32_t rx_nack_count;
    uint32_t rx_unknown_count;
    uint32_t rx_bridge_control;
    uint32_t rx_broadcast_count;
} aunbridge_stats_t;

extern aunbridge_stats_t aunbridge_stats;
extern uint8_t udp_rx_buffer[ECONET_MTU + 64];

void aunbrige_on_econet_frame_rx(uint8_t *data, uint16_t length, void *user_ctx);
void aunbrige_start(void);
void aunbridge_reconfigure(void);
void aunbridge_signal_ack(uint32_t seq);
bool aunbridge_wait_ack(uint32_t seq);
