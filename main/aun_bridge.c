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

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_log.h"

#include "config.h"
#include "econet.h"
#include "aun_bridge.h"

aunbridge_stats_t aunbridge_stats;

static const char *TAG = "AUN";

static QueueHandle_t ack_queue;
static int aun_sock;

static void _aun_econet_rx_task(void *params)
{
    static uint32_t rx_seq;
    static uint8_t econet_packet[2048]; //TODO: Don't need two buffers for this!
    static uint8_t aun_packet[2048];
    econet_hdr_t scout;
    econet_hdr_t *econet_hdr = (econet_hdr_t *)econet_packet;

    for (;;)
    {

        // Get scout
        size_t length = xMessageBufferReceive(econet_rx_frame_buffer, econet_packet, sizeof(econet_packet), portMAX_DELAY);

        // Store scout frame info
        if (length != 6)
        {
            ESP_LOGW(TAG, "Expected scout but got something else from %d.%d to %d.%d. Discarding.",
                     econet_hdr->src_stn, econet_hdr->src_net,
                     econet_hdr->dst_stn, econet_hdr->dst_net);
            continue;
        }
        memcpy(&scout, econet_packet, sizeof(scout));
        ESP_LOGI(TAG, "Got scout from %d.%d to %d.%d control 0x%02x port 0x%02x.",
                 scout.src_stn, scout.src_net,
                 scout.dst_stn, scout.dst_net,
                 scout.control, scout.port);

        // Get data packet
        length = xMessageBufferReceive(econet_rx_frame_buffer, econet_packet, sizeof(econet_packet), 200);
        if (length == 0)
        {
            ESP_LOGW(TAG, "Timeout waiting for data packet");
            continue;
        }
        ESP_LOGI(TAG, "Data packet %d bytes from %d.%d to %d.%d.",
                 length - 4,
                 econet_hdr->src_stn, econet_hdr->src_net,
                 econet_hdr->dst_stn, econet_hdr->dst_net);

        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(config_econet.server_address);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(config_econet.server_port);

        aunbridge_stats.tx_count++;

        rx_seq += 4;

        int retries = 5;
        while (--retries > 0)
        {

            aun_packet[0] = AUN_TYPE_DATA;
            aun_packet[1] = scout.port;
            aun_packet[2] = scout.control & 0x7F;
            aun_packet[3] = 0x00;
            aun_packet[4] = (rx_seq >> 0) & 0xFF;
            aun_packet[5] = (rx_seq >> 8) & 0xFF;
            aun_packet[6] = (rx_seq >> 16) & 0xFF;
            aun_packet[7] = (rx_seq >> 24) & 0xFF;
            memcpy(&aun_packet[8], econet_packet + 4, length - 4);

            int err = sendto(aun_sock, aun_packet, length + 8 - 4, 0,
                             (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (err < 0)
            {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                aunbridge_stats.tx_error_count++;
            }

            aun_hdr_t ack;
            if (xQueueReceive(ack_queue, &ack, 200) == pdPASS)
            {
                uint32_t ack_seq =
                    ack.sequence[0] |
                    (ack.sequence[1] << 8) |
                    (ack.sequence[2] << 16) |
                    (ack.sequence[3] << 24);

                if (ack_seq == rx_seq)
                {
                    break;
                }
                else
                {
                    ESP_LOGW(TAG, "Ignoring out-of-sequence ACK");
                }
            }

            aunbridge_stats.tx_retry_count++;
        }

        if (retries == 0)
        {
            ESP_LOGW(TAG, "Retries exhausted, no response from server.");
            aunbridge_stats.tx_abort_count++;
        }

    }
}

static void _aun_udp_rx_task(void *params)
{
    static uint8_t aun_rx_buffer[1500];
  
    ESP_LOGI(TAG, "Waiting for data...");

    uint32_t last_acked_seq = UINT32_MAX;
    bool last_acq_result = false;
    for (;;)
    {
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(aun_sock, aun_rx_buffer, sizeof(aun_rx_buffer), 0,
                           (struct sockaddr *)&source_addr, &socklen);

        if (len < 0)
        {
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            continue;
        }

        switch (aun_rx_buffer[0])
        {
        case AUN_TYPE_DATA:
            aunbridge_stats.rx_data_count++;
            break;
        case AUN_TYPE_ACK:
             aunbridge_stats.rx_ack_count++;
             xQueueSend(ack_queue, aun_rx_buffer, 0);
             continue;
        case AUN_TYPE_NACK:
            aunbridge_stats.rx_nack_count++;
            xQueueSend(ack_queue, aun_rx_buffer, 0);
            continue;
        default:
            ESP_LOGW(TAG, "Received packet of unknown type 0x%02x", aun_rx_buffer[0]);
            aunbridge_stats.rx_unknown_count++;
            continue;
        }

        aun_hdr_t hdr;
        memcpy(&hdr, aun_rx_buffer, sizeof(hdr));
        uint32_t ack_seq =
            hdr.sequence[0] |
            (hdr.sequence[1] << 8) |
            (hdr.sequence[2] << 16) |
            (hdr.sequence[3] << 24);

        // Change AUN header to Econet style
        aun_rx_buffer[2] = config_econet.remote_station_id;
        aun_rx_buffer[3] = 0x00;
        aun_rx_buffer[4] = config_econet.this_station_id;
        aun_rx_buffer[5] = 0x00;
        aun_rx_buffer[6] = hdr.econet_control | 0x80;
        aun_rx_buffer[7] = hdr.econet_port;

        // Send to Beeb (but only if we didn't already because sometimes we get packets more than once.)
        // NOTE: We're not encountering out of order but if we do then we'll need a different strategy to reorder them.
        if (ack_seq != last_acked_seq) {
            ESP_LOGI(TAG, "[%05d] Sending %d byte frame from %s to Econet %d.%d", 
                ack_seq, len, inet_ntoa(source_addr.sin_addr), aun_rx_buffer[2], aun_rx_buffer[3]);
            last_acq_result = econet_send(&aun_rx_buffer[2], len - 2);
            last_acked_seq = ack_seq;
        } else {
            ESP_LOGI(TAG, "[%05d] Re-acknowledging duplicate", ack_seq);
        }
        
        // Send AUN ack/nack
        if (last_acq_result) {
            hdr.transaction_type = AUN_TYPE_ACK;
            aunbridge_stats.tx_ack_count++;
        } else {
            hdr.transaction_type = AUN_TYPE_NACK;
            aunbridge_stats.tx_nack_count++;
        }
        memcpy(aun_rx_buffer, &hdr, sizeof(hdr));
        sendto(aun_sock, aun_rx_buffer, 8, 0,
               (struct sockaddr *)&source_addr, sizeof(source_addr));
    }
}

void aunbrige_start(void)
{
    struct sockaddr_in listen_addr = {
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_family = AF_INET,
        .sin_port = htons(32768),
    };

    aun_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (aun_sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return;
    }

    int err = bind(aun_sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr));
    if (err < 0)
    {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(aun_sock);
        return;
    }

    ack_queue = xQueueCreate(10, sizeof(aun_hdr_t));
    xTaskCreate(_aun_udp_rx_task, "aun_udp_rx", 4096, NULL, 1, NULL);
    xTaskCreate(_aun_econet_rx_task, "aun_econet_rx", 4096, NULL, 1, NULL);
}