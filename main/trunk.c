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

#include <stdint.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_log.h"

#include "utils.h"
#include "config.h"
#include "econet.h"
#include "aun_bridge.h"
#include "trunk.h"
#include "crypt.h"

#define CRYPT_WORKSPACE_SIZE 19 // EncryptType + IV + PayloadLength

static const char *TAG = "TRUNK";

trunk_t trunks[3];
uint8_t trunk_our_net;
static int trunk_count = 0;

static bool _encrypt_and_send_using_workspace(trunk_t *trunk, uint8_t *data, size_t data_len, size_t data_capacity, size_t workspace_len)
{
    if (workspace_len < CRYPT_WORKSPACE_SIZE)
    {
        ESP_LOGE(TAG, "Internal error: Insufficient workspace");
        return false;
    }
    uint8_t *packet = data - CRYPT_WORKSPACE_SIZE;
    packet[0] = 1;                       // 0       Encryption type = 1
    crypt_gen_iv(&packet[1]);            // 1-16    IV
    packet[17] = (data_len >> 8) & 0xFF; // 17-18   Plaintext length (big endian)
    packet[18] = data_len & 0xFF;

    // Encrypt in place
    size_t ct_len = 0;
    if (crypt_aes256_cbc_encrypt(trunk->key, &packet[1], &packet[17], data_len + 2, &packet[17], data_capacity, &ct_len))
    {
        ESP_LOGE(TAG, "Internal error: Encryption failed");
        return false;
    }

    // Send it
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(trunk->remote_address);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(trunk->remote_udp_port);
    int err = sendto(trunk->socket, packet, 1 + 16 + ct_len, 0,
                     (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0)
    {
        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
        aunbridge_stats.tx_error_count++;
        return false;
    }

    return true;
}

static void _send_trunk_update(trunk_t *trunk)
{
    trunk_hdr_t hdr = {
        .ecohdr.dst_net = 0xFF,
        .ecohdr.dst_stn = 0xFF,
        .ecohdr.src_net = 0,
        .ecohdr.src_stn = 2,
        .transaction_type = AUN_TYPE_BROADCAST,
        .port = BRIDGE_PORT,
        .control = BRIDGE_UPDATE,
        .padding = 0,
    };

    uint8_t *packet = udp_rx_buffer + CRYPT_WORKSPACE_SIZE;
    memcpy(packet, &hdr, sizeof(hdr));
    packet[sizeof(hdr)] = trunk_our_net;
    uint8_t len = sizeof(hdr) + 1;

    _encrypt_and_send_using_workspace(trunk, packet, len, sizeof(udp_rx_buffer) - CRYPT_WORKSPACE_SIZE, CRYPT_WORKSPACE_SIZE);
}

static void _update_econet_rx_nets(void)
{
    // Get aggregate of all trunks
    bitmap256_t new_nets = {};
    for (int i = 0; i < ARRAY_SIZE(trunks); i++)
    {
        for (int n = 0; n < ARRAY_SIZE(new_nets.w); n++)
        {
            new_nets.w[n] |= trunks[i].nets.w[n];
        }
    }
    econet_rx_set_networks(&new_nets);
}

static void _bridge_control_udp(trunk_t *trunk, trunk_hdr_t *hdr, uint8_t *payload, size_t payload_len)
{

    if (hdr->control == BRIDGE_KEEPALIVE)
    {
        return;
    }

    if (hdr->control == BRIDGE_UPDATE || hdr->control == BRIDGE_RESET)
    {

        bm256_reset(&trunk->nets);
        for (int i = 0; i < payload_len; i++)
        {
            uint8_t net = payload[i];
            if (net != trunk_our_net)
            {
                bm256_set(&trunk->nets, net);
            }
        }

        _update_econet_rx_nets();

        return;
    }

    ESP_LOGW(TAG, "Unhandled bridge control packet with control=0x%x", hdr->control);
}

void trunk_tick(void)
{
    for (int i = 0; i < ARRAY_SIZE(trunks); i++)
    {
        trunk_t *trunk = &trunks[i];
        if (--trunk->time_to_next_update == 0)
        {
            _send_trunk_update(trunk);
            trunk->time_to_next_update = 10;
        }
    }
}

bool trunk_tx_packet(econet_scout_t *scout, uint8_t *data, size_t data_length, size_t data_capacity, size_t workspace_length)
{
    // Local net not handled by trunk.
    // TODO: Bridge queries
    if (scout->hdr.dst_net == 0 || scout->hdr.dst_net == trunk_our_net)
    {
        return false;
    }

    // Find trunk where we can send this
    trunk_t *trunk = NULL;
    for (int i = 0; i < ARRAY_SIZE(trunks); i++)
    {
        if (bm256_test(&trunks[i].nets, scout->hdr.dst_net))
        {
            trunk = &trunks[i];
            break;
        }
    }
    if (trunk == NULL)
    {
        return false;
    }

    uint8_t *trunk_packet = data - sizeof(trunk_hdr_t) + 4;
    int retries = 5;
    trunk->seq += 4;
    while (--retries > 0)
    {
        trunk_hdr_t hdr = {
            .transaction_type = AUN_TYPE_DATA,
            .ecohdr.dst_net = scout->hdr.dst_net,
            .ecohdr.dst_stn = scout->hdr.dst_stn,
            .ecohdr.src_net = trunk_our_net,
            .ecohdr.src_stn = scout->hdr.src_stn,
            .control = scout->control,
            .port = scout->port,
            .padding = 0,
            .sequence = trunk->seq,
        };
        memcpy(trunk_packet, &hdr, sizeof(hdr));

        _encrypt_and_send_using_workspace(trunk,
                                          trunk_packet, data_length + sizeof(hdr) - 4,
                                          data_capacity + sizeof(hdr) - 4,
                                          ECONET_RX_BUFFER_WORKSPACE - sizeof(trunk_hdr_t) + 4);

        if (aunbridge_wait_ack(trunk->seq))
        {
            break;
        }

        aunbridge_stats.tx_retry_count++;
        ESP_LOGI(TAG, "Retry! %d remain", retries - 1);
    }

    if (retries == 0)
    {
        ESP_LOGW(TAG, "Retries exhausted, no response from bridge");
        aunbridge_stats.tx_abort_count++;
    }

    return true;
}

void trunk_rx_process(trunk_t *trunk)
{
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);
    int len = recvfrom(trunk->socket, udp_rx_buffer, sizeof(udp_rx_buffer), 0,
                       (struct sockaddr *)&source_addr, &socklen);
    if (len < 0)
    {
        ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
        return;
    }

    if (len < 33)
    {
        ESP_LOGE(TAG, "dropped short packet len=%d", len);
        return;
    }

    if (udp_rx_buffer[0] != 1)
    {
        ESP_LOGW(TAG, "Unsupported encryption type %d", udp_rx_buffer[0]);
        return;
    }

    size_t pt_len = 0;
    crypt_aes256_cbc_decrypt(trunk->key, &udp_rx_buffer[1], &udp_rx_buffer[17], len - 17, udp_rx_buffer, sizeof(udp_rx_buffer), &pt_len);

    // Validate length in header against PT length
    len = (udp_rx_buffer[0] << 8) | udp_rx_buffer[1];
    uint8_t *payload = &udp_rx_buffer[2];
    if (len != pt_len - 2)
    {
        ESP_LOGW(TAG, "Packet len %d does not match payload length %d", len, pt_len - 2);
        return;
    }

    // Extract hdr
    trunk_hdr_t hdr;
    memcpy(&hdr, payload, sizeof(hdr));
    payload += sizeof(hdr);
    len -= sizeof(hdr);

    // Bridge control handling
    if (hdr.transaction_type == AUN_TYPE_BROADCAST || hdr.ecohdr.dst_net == 255 || hdr.ecohdr.dst_stn == 255)
    {
        if (hdr.port == BRIDGE_PORT)
        {
            aunbridge_stats.rx_bridge_control++;
            _bridge_control_udp(trunk, &hdr, payload, len);
            return;
        }
    }

    // Other handling
    switch (hdr.transaction_type)
    {
    case AUN_TYPE_BROADCAST:
        aunbridge_stats.rx_broadcast_count++;
        break;
    case AUN_TYPE_IMM:
        aunbridge_stats.rx_imm_count++;
        break;
    case AUN_TYPE_DATA:
        aunbridge_stats.rx_data_count++;
        break;
    case AUN_TYPE_ACK:
        aunbridge_stats.rx_ack_count++;
        aunbridge_signal_ack(hdr.sequence);
        return;
    case AUN_TYPE_NACK:
        aunbridge_stats.rx_nack_count++;
        aunbridge_signal_ack(hdr.sequence);
        return;
    default:
        ESP_LOGW(TAG, "Received packet of unknown type 0x%02x. Ignored.", udp_rx_buffer[0]);
        aunbridge_stats.rx_unknown_count++;
        return;
    }

    if (hdr.ecohdr.dst_net != trunk_our_net && hdr.ecohdr.dst_net != 255)
    {
        ESP_LOGW(TAG, "Packet arrived destined for %d.%d but our net is %d. Packet discarded.", hdr.ecohdr.dst_net, hdr.ecohdr.dst_stn, trunk_our_net);
        return;
    }

    // Change trunk, header to Econet style
    econet_scout_t ecohdr = {
        .hdr.dst_net = 0, // Clear destination net for local delivery
        .hdr.dst_stn = hdr.ecohdr.dst_stn,
        .hdr.src_net = hdr.ecohdr.src_net,
        .hdr.src_stn = hdr.ecohdr.src_stn,
        .control = hdr.control,
        .port = hdr.port,
    };

    payload -= sizeof(ecohdr);
    len += sizeof(ecohdr);
    if (len > sizeof(udp_rx_buffer))
    {
        ESP_LOGE(TAG, "Internal error. Packet exceeds buffer.");
        return;
    }

    memcpy(payload, &ecohdr, sizeof(ecohdr));

    // Send to Beeb (but only if we didn't get acknowledgement before for this packet.)
    // NOTE: We're not encountering out of order but if we do then we'll need a different strategy to reorder them.
    uint8_t *imm_reply = NULL;
    uint16_t imm_reply_len;
    if (hdr.sequence != trunk->last_acked_seq || trunk->last_tx_result == ECONET_NACK || trunk->last_tx_result == ECONET_IMM_REPLY)
    {
        ESP_LOGI(TAG, "[%05d] Delivering %d byte frame from %d.%d to Econet %d.%d (P0x%x C0x%x)",
                 hdr.sequence, len,
                 hdr.ecohdr.src_net, hdr.ecohdr.src_stn,
                 hdr.ecohdr.dst_net, hdr.ecohdr.dst_stn,
                 hdr.port, hdr.control);

        trunk->last_tx_result = econet_send(payload, len, &imm_reply, &imm_reply_len);
        trunk->last_acked_seq = hdr.sequence;
    }
    else
    {
        ESP_LOGI(TAG, "[%05d] Re-acknowledging duplicate (Econet ack was %d)", hdr.sequence, trunk->last_tx_result);
    }

    // Send AUN ack/nack
    switch (trunk->last_tx_result)
    {
    case ECONET_ACK:
        hdr.transaction_type = AUN_TYPE_ACK;
        aunbridge_stats.tx_ack_count++;
        break;
    case ECONET_IMM_REPLY:
        hdr.transaction_type = AUN_TYPE_IMM_REPLY;
        aunbridge_stats.tx_ack_count++;
        break;
    default:
        hdr.transaction_type = AUN_TYPE_NACK;
        aunbridge_stats.tx_nack_count++;
    }

    // Send (N)ACK
    econet_swap_addresses(&hdr.ecohdr);
    uint8_t *packet = udp_rx_buffer + CRYPT_WORKSPACE_SIZE;
    memcpy(packet, &hdr, sizeof(hdr));
    if (imm_reply != NULL)
    {
        memcpy(packet + sizeof(hdr), imm_reply, imm_reply_len);
    }
    else
    {
        imm_reply_len = 0;
    }
    _encrypt_and_send_using_workspace(trunk, packet, sizeof(hdr) + imm_reply_len, sizeof(udp_rx_buffer) - CRYPT_WORKSPACE_SIZE, CRYPT_WORKSPACE_SIZE);
}

static void _setup_trunk(void *ctx, const config_trunk_t *cfg)
{
    (void)ctx;

    if (trunk_count >= ARRAY_SIZE(trunks))
    {
        ESP_LOGW(TAG, "Too many trunk configurations (max %d)", ARRAY_SIZE(trunks));
        return;
    }

    trunk_t *trunk = &trunks[trunk_count];
    memset(trunk, 0, sizeof(*trunk));

    snprintf(trunk->remote_address, sizeof(trunk->remote_address), "%s", cfg->remote_address);
    trunk->remote_udp_port = cfg->udp_port;

    // Copy encryption key (should always be 32 bytes for AES-256)
    if (cfg->key_len != sizeof(trunk->key))
    {
        ESP_LOGW(TAG, "Trunk key length is %d, expected %d. Using anyway.", cfg->key_len, sizeof(trunk->key));
    }
    memcpy(trunk->key, cfg->key, sizeof(trunk->key));

    // Open socket
    trunk->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (trunk->socket < 0)
    {
        ESP_LOGE(TAG, "Failed to create socket for trunk %d: errno %d", trunk_count, errno);
        return;
    }

    trunk->is_open = true;
    trunk->last_acked_seq = 1;
    trunk->time_to_next_update = 1;

    ESP_LOGI(TAG, "Configured trunk %d: %s:%d", trunk_count, trunk->remote_address, trunk->remote_udp_port);
    trunk_count++;
}

void trunk_reconfigure(void)
{
    // Clear down trunks
    for (int i = 0; i < ARRAY_SIZE(trunks); i++)
    {
        if (trunks[i].is_open)
        {
            closesocket(trunks[i].socket);
            trunks[i].is_open = false;
        }
    }
    trunk_count = 0;

    // Load configuration from config file
    trunk_our_net = config_get_trunk_network();
    config_foreach_trunk(_setup_trunk, NULL);

    // If still zero after loading config, use default
    if (trunk_our_net == 0)
    {
        trunk_our_net = 88;
        ESP_LOGI(TAG, "Using default trunk network number: %d", trunk_our_net);
    }
    else
    {
        ESP_LOGI(TAG, "Loaded trunk network number from config: %d", trunk_our_net);
    }
}
