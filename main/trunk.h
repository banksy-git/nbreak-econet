/*
 * EconetWiFi
 * Copyright (c) 2026 Paul G. Banks <https://paulbanks.org/projects/econet>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the LICENSE file in the project root for full license information.
 */

#pragma once

#include "econet.h"

#define BRIDGE_PORT 0x9C
#define BRIDGE_KEEPALIVE 0xD0
#define BRIDGE_RESET 0x80
#define BRIDGE_UPDATE 0x81
#define BRIDGE_WHATNET 0x82
#define BRIDGE_ISNET 0x83

typedef struct
{
    char remote_address[64];
    uint8_t key[32];
    int socket;
    bool is_open;
    uint32_t seq;
    uint16_t remote_udp_port;
    uint32_t last_acked_seq;
    econet_acktype_t last_tx_result;
    uint16_t time_to_next_update;
    bitmap256_t nets;
} trunk_t;

extern trunk_t trunks[3];
extern uint8_t trunk_our_net;

typedef struct
{
    econet_hdr_t ecohdr;
    uint8_t transaction_type;
    uint8_t port;
    uint8_t control;
    uint8_t padding;
    uint32_t sequence;
} trunk_hdr_t;

bool trunk_tx_packet(econet_scout_t *scout, uint8_t *data, size_t data_length, size_t data_capacity, size_t workspace_length);
void trunk_rx_process(trunk_t *trunk);
void trunk_tick(void);
void trunk_reconfigure(void);
