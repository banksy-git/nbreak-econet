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
#include "utils.h"
#include "hal/gpio_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"

#define ECONET_MTU 8192
#define ECONET_RX_BUFFER_WORKSPACE 32

// Econet immediate mode packet types.
// Thanks to JGH for info (https://mdfs.net/Docs/Comp/Econet/Specs/Packets)
#define ECONET_CTRL_PEEK 0x81         // Scout->, <-Data
#define ECONET_CTRL_POKE 0x82         // Scout->, <-Ack, Data->, <-Ack
#define ECONET_CTRL_JSR 0x83          // Scout->, <-Ack, Data->, <-Ack
#define ECONET_CTRL_USERPROC 0x84     // Scout->, <-Ack, Data->, <-Ack
#define ECONET_CTRL_OSPROC 0x85       // Scout->, <-Ack, Data->, <-Ack
#define ECONET_CTRL_HALT 0x86         // Scout->, <-Ack
#define ECONET_CTRL_CONTINUE 0x87     // Scout->, <-Ack
#define ECONET_CTRL_MACHINETYPE 0x88  // Scout->, <-Data
#define ECONET_CTRL_GETREGISTERS 0x89 // Scout->, <-Data

typedef void (*econet_frame_callback)(uint8_t *data, uint16_t length, void *user_ctx);

/*** Econet acknowledgement types.
 *
 * Econet defines a single positive acknowledgement packet (ACK). A negative
 * acknowledgement (NACK) is inferred by the sender from the absence of an ACK.
 *
 * A typical Econet transaction is a four-way handshake (two round trips), with
 * an acknowledgement expected for each transmitted packet. The meaning of a
 * NACK depends on the phase in which it occurs.
 *
 * Phase 1 (SCOUT):
 * The sending station transmits a small SCOUT frame to determine reachability
 * and willingness of the remote station to accept a follow-on DATA packet.
 * Bus contention is expected during this phase and may cause corruption. A
 * NACK here may indicate that the remote station was not ready, the SCOUT was
 * not received, or the ACK was not seen by the sender. This phase is
 * idempotent; retransmitting SCOUT is always safe.
 *
 * Phase 2 (DATA):
 * Immediately following a successful SCOUT, with no intervening idle bus
 * condition, the sender transmits the DATA packet. If the receiver accepts and
 * processes the DATA but the ACK is lost, the sender will infer a NACK even
 * though the receiver has advanced state. Retransmitting DATA in this case is
 * unsafe because the receiver may be expecting the next packet.
 *
 * This enum encodes these different meanings.
 */
typedef enum
{
    ECONET_ACK,          ///< Packet was acknowledged
    ECONET_NACK,         ///< Packet was not acknowledged (safe to retry)
    ECONET_NACK_CORRUPT, ///< Packet may have been accepted (not safe to retry)
    ECONET_SEND_ERROR,   ///< Send could not be started
    ECONET_IMM_REPLY,    ///< Packet was acknowledged with immediate reply
} econet_acktype_t;

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
} econet_hdr_t;

typedef struct
{
    econet_hdr_t hdr;
    uint8_t control;
    uint8_t port;
    uint8_t data[0];
} econet_scout_t;

typedef struct
{
    uint8_t *data;
    size_t length;
    char type;
} econet_rx_packet_t;

extern econet_stats_t econet_stats;
extern QueueHandle_t econet_rx_packet_queue;

void econet_setup(const econet_config_t *config);
void econet_clock_reconfigure(void);
void econet_start(void);
econet_acktype_t econet_send(uint8_t *data, uint16_t length, uint8_t **imm_reply, uint16_t *imm_reply_len);
void econet_rx_clear_bitmaps(void);
void econet_rx_enable_station(uint8_t station_id);
void econet_rx_set_networks(bitmap256_t *nets);
void econet_rx_shutdown(void);

ALWAYS_INLINE void econet_swap_addresses(econet_hdr_t *hdr)
{
    uint8_t tmp = hdr->dst_net;
    hdr->dst_net = hdr->src_net;
    hdr->src_net = tmp;
    tmp = hdr->dst_stn;
    hdr->dst_stn = hdr->src_stn;
    hdr->src_stn = tmp;
}

#ifdef ECONET_PRIVATE_API
#define TAG "ECONET"
extern econet_config_t econet_cfg;
extern QueueHandle_t tx_command_queue;
extern TaskHandle_t tx_task;
extern volatile bool tx_is_in_progress;
extern volatile bool tx_is_awaiting_imm_reply;
extern uint32_t rx_ack_wait_time;

void econet_rx_setup(void);
void econet_rx_start(void);
void econet_tx_setup(void);
void econet_tx_start(void);
bool econet_rx_is_idle(void);
void econet_tx_pre_go(void);

typedef enum
{
    ECONET_TX_NORMAL,
    ECONET_TX_IMM_WITH_REPLY, ///< We expect an IMM reply
    ECONET_TX_IMM_NO_DATA,    ///< No data phase, just ACK
    ECONET_TX_BROADCAST,      ///< Send broadcast (no ACK expected)
} econet_tx_command_flags_t;

typedef struct
{
    char cmd;
    uint8_t dst_stn;
    uint8_t dst_net;
    uint8_t src_stn;
    uint8_t src_net;
    uint8_t flags;
    uint8_t *imm_reply;
    size_t imm_length;
} econet_tx_command_t;

#endif
