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

#include <stdint.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "utils.h"
#include "config.h"
#include "econet.h"
#include "aun_bridge.h"
#include "trunk.h"

aunbridge_stats_t aunbridge_stats;
uint8_t udp_rx_buffer[ECONET_MTU + 64];

static const char *TAG = "AUN";
static const char *ECONETTAG = "ECONET";

static bool is_running;
static volatile TaskHandle_t shutdown_notify_handle;
static QueueHandle_t ack_queue;
static int rx_udp_ctl_pipe[2];

typedef struct
{
    uint8_t station_id;
    uint8_t network_id;
    uint16_t local_udp_port;
    int socket;
    bool is_open;
} econet_station_t;
static econet_station_t econet_stations[5];

typedef struct
{
    char remote_address[64];
    uint8_t station_id;
    uint8_t network_id;
    uint16_t udp_port;
    uint32_t last_acked_seq;
    econet_acktype_t last_tx_result;
} aun_station_t;
static aun_station_t aun_stations[20];

static econet_station_t *_get_econet_station_by_id(uint8_t station_id)
{
    for (int i = 0; i < ARRAY_SIZE(econet_stations); i++)
    {
        if (econet_stations[i].station_id == station_id)
        {
            return &econet_stations[i];
        }
    }
    return NULL;
}

static aun_station_t *_get_aun_station_by_id(uint8_t station_id)
{
    for (int i = 0; i < ARRAY_SIZE(aun_stations); i++)
    {
        if (aun_stations[i].station_id == station_id)
        {
            return &aun_stations[i];
        }
    }
    return NULL;
}

static aun_station_t *_get_aun_station_by_port(uint16_t udp_port)
{
    for (int i = 0; i < ARRAY_SIZE(aun_stations); i++)
    {
        if (aun_stations[i].udp_port == udp_port)
        {
            return &aun_stations[i];
        }
    }
    return NULL;
}

static bool _econet_rx(econet_rx_packet_t *pkt, uint32_t timeout)
{
    if (xQueueReceive(econet_rx_packet_queue, pkt, timeout) == pdFALSE)
    {
        return false;
    }

    if (pkt->type == 'S')
    {
        econet_rx_clear_bitmaps();
        ESP_LOGI(TAG, "Econet RX shutdown");
        xTaskNotifyGive(shutdown_notify_handle);
        vTaskDelete(NULL);
    }

    return true;
}

void aunbridge_signal_ack(uint32_t seq)
{
    xQueueSend(ack_queue, &seq, 0);
}

bool aunbridge_wait_ack(uint32_t seq)
{
    uint32_t ack_seq;
    for (int i = 0; i < 5; i++)
    {
        if (xQueueReceive(ack_queue, &ack_seq, 500) == pdPASS)
        {
            if (ack_seq == seq)
            {
                return true;
            }
            else
            {
                ESP_LOGW(TAG, "Ignoring out-of-sequence ACK received=0x%x expecting=0x%x", ack_seq, seq);
            }
        }
        else
        {
            return false;
        }
    }
    ESP_LOGW(TAG, "Too many out-of-sequence ACK!");
    return false;
}

static void _aun_econet_rx_task(void *params)
{
    static uint32_t rx_seq;
    econet_rx_packet_t econet_pkt;

    econet_scout_t scout;
    econet_hdr_t econet_hdr;

    for (;;)
    {
        // Get scout / immediate frame
        _econet_rx(&econet_pkt, portMAX_DELAY);
        if (econet_pkt.type == 'I')
        {
            continue; // Idle notification
        }
        else if (econet_pkt.length < 6)
        {
            ESP_LOGW(ECONETTAG, "Unexpected short scout frame (len=%d) discarded", econet_pkt.length);
            continue;
        }
        memcpy(&scout, econet_pkt.data + ECONET_RX_BUFFER_WORKSPACE, sizeof(scout));
        if (econet_pkt.length != 6)
        {
            ESP_LOGW(ECONETTAG, "Expected scout but got a %d byte frame from %d.%d to %d.%d. (P0x%x C0x%x) Discarding",
                     econet_pkt.length, scout.hdr.src_net, scout.hdr.src_stn, scout.hdr.dst_net, scout.hdr.dst_stn, scout.port, scout.control);
            continue;
        }

        // Get data packet
        if (!_econet_rx(&econet_pkt, 10000))
        {
            ESP_LOGW(ECONETTAG, "Timeout waiting for data packet from %d.%d to %d.%d (ctrl=0x%x, port=0x%x). No clock?",
                     scout.hdr.src_net, scout.hdr.src_stn, scout.hdr.dst_net, scout.hdr.dst_stn, scout.control, scout.port);
            continue;
        }
        else if (econet_pkt.type == 'I')
        {
            ESP_LOGW(ECONETTAG, "Idle whilst getting data packet from %d.%d to %d.%d (ctrl=0x%x, port=0x%x)",
                     scout.hdr.src_net, scout.hdr.src_stn, scout.hdr.dst_net, scout.hdr.dst_stn, scout.control, scout.port);
            continue;
        }
        else if (econet_pkt.length < 6)
        {
            ESP_LOGW(ECONETTAG, "Unexpected short frame discarded");
            continue;
        }
        memcpy(&econet_hdr, econet_pkt.data + ECONET_RX_BUFFER_WORKSPACE, sizeof(econet_hdr));
        ESP_LOGI(ECONETTAG, "Data packet %d bytes from %d.%d to %d.%d (ctrl=0x%x, port=0x%x)",
                 econet_pkt.length - 4,
                 econet_hdr.src_net, econet_hdr.src_stn,
                 econet_hdr.dst_net, econet_hdr.dst_stn,
                 scout.control, scout.port);

        if (memcmp(&econet_hdr, &scout, sizeof(econet_hdr)) != 0)
        {
            ESP_LOGW(ECONETTAG, "Address mismatch on scout/data packet. Discarded.");
            continue;
        }

        // See if trunk wants this packet.
        if (trunk_tx_packet(&scout,
                            econet_pkt.data + ECONET_RX_BUFFER_WORKSPACE, econet_pkt.length, ECONET_MTU + 16, ECONET_RX_BUFFER_WORKSPACE))
        {
            continue;
        }

        econet_station_t *econet_station = _get_econet_station_by_id(econet_hdr.src_stn);
        if (econet_station == NULL)
        {
            // FUTURE: Dynamically make a socket for it...
            ESP_LOGW(TAG, "Econet station %d is not configured. Not forwarding packet", econet_hdr.src_stn);
            continue;
        }

        aun_station_t *aun_station = _get_aun_station_by_id(econet_hdr.dst_stn);
        if (aun_station == NULL)
        {
            ESP_LOGE(TAG, "AUN station %d is not configured but we accepted a packet for it!", econet_hdr.dst_stn);
            continue;
        }

        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(aun_station->remote_address);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(aun_station->udp_port);

        aunbridge_stats.tx_count++;

        rx_seq += 4;

        uint8_t *aun_packet = econet_pkt.data + ECONET_RX_BUFFER_WORKSPACE - 4;
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

            int err = sendto(econet_station->socket, aun_packet, econet_pkt.length - sizeof(econet_hdr) + 8, 0,
                             (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (err < 0)
            {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                aunbridge_stats.tx_error_count++;
            }

            if (aunbridge_wait_ack(rx_seq))
            {
                break;
            }

            aunbridge_stats.tx_retry_count++;
            ESP_LOGI(TAG, "Retry! %d remain", retries - 1);
        }

        if (retries == 0)
        {
            ESP_LOGW(TAG, "Retries exhausted, no response from server %s:%d", inet_ntoa(dest_addr.sin_addr), ntohs(dest_addr.sin_port));
            aunbridge_stats.tx_abort_count++;
        }
    }
}

static void _aun_udp_rx_process(econet_station_t *econet_station)
{
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);
    int len = recvfrom(econet_station->socket, udp_rx_buffer, sizeof(udp_rx_buffer), 0,
                       (struct sockaddr *)&source_addr, &socklen);
    if (len < 0)
    {
        ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
        return;
    }

    aun_hdr_t hdr;
    memcpy(&hdr, udp_rx_buffer, sizeof(hdr));
    uint32_t ack_seq =
        hdr.sequence[0] |
        (hdr.sequence[1] << 8) |
        (hdr.sequence[2] << 16) |
        (hdr.sequence[3] << 24);

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
        aunbridge_signal_ack(ack_seq);
        return;
    case AUN_TYPE_NACK:
        aunbridge_stats.rx_nack_count++;
        aunbridge_signal_ack(ack_seq);
        return;
    default:
        ESP_LOGW(TAG, "Received AUN packet of unknown type 0x%02x. Ignored.", udp_rx_buffer[0]);
        aunbridge_stats.rx_unknown_count++;
        return;
    }

    // Look up sending AUN station
    aun_station_t *aun_station = _get_aun_station_by_port(ntohs(source_addr.sin_port));
    if (aun_station == NULL)
    {
        ESP_LOGW(TAG, "Received AUN packet but can't identify station ID. Ignored.");
        return;
    }

    // Change AUN header to Econet style
    len -= 2;
    udp_rx_buffer[2] = (hdr.transaction_type==AUN_TYPE_BROADCAST) ? 255 : econet_station->station_id;
    udp_rx_buffer[3] = 0x00;
    udp_rx_buffer[4] = aun_station->station_id;
    udp_rx_buffer[5] = 0x00;
    udp_rx_buffer[6] = hdr.econet_control | 0x80;
    udp_rx_buffer[7] = hdr.econet_port;

    // Send to Beeb (but only if we didn't get acknowledgement before for this packet.)
    // NOTE: We're not encountering out of order but if we do then we'll need a different strategy to reorder them.
    uint8_t *imm_reply = NULL;
    uint16_t imm_reply_len;
    if (ack_seq != aun_station->last_acked_seq || aun_station->last_tx_result == ECONET_NACK || aun_station->last_tx_result == ECONET_IMM_REPLY)
    {
        ESP_LOGI(TAG, "[%05d] Delivering %d byte frame from %d.%d (%s) to Econet %d.%d (P0x%x C0x%x)",
                 ack_seq, len,
                 aun_station->network_id, aun_station->station_id,
                 inet_ntoa(source_addr.sin_addr),
                 econet_station->network_id, econet_station->station_id,
                 hdr.econet_port, hdr.econet_control);

        aun_station->last_tx_result = econet_send(&udp_rx_buffer[2], len, &imm_reply, &imm_reply_len);
        aun_station->last_acked_seq = ack_seq;
    }
    else
    {
        ESP_LOGI(TAG, "[%05d] Re-acknowledging duplicate (Econet ack was %d)", ack_seq, aun_station->last_tx_result);
    }

    // Send AUN ack/nack
    switch (aun_station->last_tx_result)
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

    // Send (N)ACK to calling station at port we have on file
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(aun_station->remote_address);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(aun_station->udp_port);
    memcpy(udp_rx_buffer, &hdr, sizeof(hdr));
    if (imm_reply != NULL)
    {
        memcpy(udp_rx_buffer + sizeof(hdr), imm_reply, imm_reply_len);
    }
    else
    {
        imm_reply_len = 0;
    }
    sendto(econet_station->socket, udp_rx_buffer, sizeof(hdr) + imm_reply_len, 0,
           (struct sockaddr *)&dest_addr, sizeof(dest_addr));
}

static void _aun_udp_rx_task(void *params)
{
    ESP_LOGI(TAG, "Waiting for UDP packets...");

    int64_t last_tick_time = esp_timer_get_time();

    for (;;)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(rx_udp_ctl_pipe[0], &rfds);
        int max_fd = rx_udp_ctl_pipe[0];
        for (int i = 0; i < ARRAY_SIZE(econet_stations); i++)
        {
            if (econet_stations[i].is_open)
            {
                FD_SET(econet_stations[i].socket, &rfds);
                if (econet_stations[i].socket > max_fd)
                {
                    max_fd = econet_stations[i].socket;
                }
            }
        }
        for (int i = 0; i < ARRAY_SIZE(trunks); i++)
        {
            if (trunks[i].is_open)
            {
                FD_SET(trunks[i].socket, &rfds);
                if (trunks[i].socket > max_fd)
                {
                    max_fd = trunks[i].socket;
                }
            }
        }

        struct timeval tv = {.tv_sec = 1};
        int err = select(max_fd + 1, &rfds, NULL, NULL, &tv);
        if (err < 0)
        {
            ESP_LOGE(TAG, "select error: errno %d", errno);
            continue;
        }

        if (FD_ISSET(rx_udp_ctl_pipe[0], &rfds))
        {
            ESP_LOGI(TAG, "AUN: RX shutdown");
            char tmp[1];
            read(rx_udp_ctl_pipe[0], &tmp, sizeof(tmp));
            xTaskNotifyGive(shutdown_notify_handle);
            vTaskDelete(NULL);
            continue;
        }

        for (int i = 0; i < ARRAY_SIZE(econet_stations); i++)
        {
            if (FD_ISSET(econet_stations[i].socket, &rfds))
            {
                _aun_udp_rx_process(&econet_stations[i]);
            }
        }

        for (int i = 0; i < ARRAY_SIZE(trunks); i++)
        {
            if (!trunks->is_open)
            {
                continue;
            }
            if (FD_ISSET(trunks[i].socket, &rfds))
            {
                trunk_rx_process(&trunks[i]);
            }
        }

        // Process periodic activity (keepalives, etc)
        int64_t now_us = esp_timer_get_time();
        if (now_us - last_tick_time > 1000000LL)
        {
            trunk_tick();
            last_tick_time = now_us;
        }
    }
}

static void _setup_aun_station(void *ctx, const config_aun_station_t *cfg)
{
    aun_station_t *station = NULL;
    for (int i = 0; i < ARRAY_SIZE(aun_stations); i++)
    {
        if (aun_stations[i].station_id == 0)
        {
            station = &aun_stations[i];
            break;
        }
    }
    if (station == NULL)
    {
        ESP_LOGE(TAG, "No free AUN station slots.");
        return;
    }

    snprintf(station->remote_address, sizeof(station->remote_address), "%s", cfg->remote_address);
    station->station_id = cfg->station_id;
    station->network_id = cfg->network_id;
    station->udp_port = cfg->udp_port;
    station->last_acked_seq = UINT32_MAX;
    station->last_tx_result = ECONET_NACK;
}

static void _setup_econet_station(void *ctx, const config_econet_station_t *cfg)
{
    econet_station_t *station = NULL;
    for (int i = 0; i < ARRAY_SIZE(econet_stations); i++)
    {
        if (!econet_stations[i].is_open)
        {
            station = &econet_stations[i];
            break;
        }
    }
    if (station == NULL)
    {
        ESP_LOGE(TAG, "Failed to add station %d. No free slots.", cfg->station_id);
        return;
    }

    struct sockaddr_in listen_addr = {
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_family = AF_INET,
        .sin_port = htons(cfg->local_udp_port),
    };

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Failed to add station %d. Unable to create socket: errno %d", cfg->station_id, errno);
        return;
    }

    int err = bind(sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr));
    if (err < 0)
    {
        ESP_LOGE(TAG, "Failed to add station %d. Socket unable to bind: errno %d", cfg->station_id, errno);
        close(sock);
        return;
    }

    ESP_LOGI(TAG, "Added Econet station %d on port %d", cfg->station_id, cfg->local_udp_port);

    station->station_id = cfg->station_id;
    station->network_id = 0;
    station->local_udp_port = cfg->local_udp_port;
    station->socket = sock;
    station->is_open = true;
}

void aunbridge_shutdown(void)
{
    if (is_running)
    {
        shutdown_notify_handle = xTaskGetCurrentTaskHandle();

        // Shutdown Econet RX
        econet_rx_shutdown();
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Shut down AUN RX
        char tmp = 0;
        write(rx_udp_ctl_pipe[1], &tmp, sizeof(tmp));
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        is_running = false;
    }
}

void aunbridge_reconfigure(void)
{
    // Shut down receivers so we can safely modify state
    aunbridge_shutdown();

    // Clear down stations
    for (int i = 0; i < ARRAY_SIZE(econet_stations); i++)
    {
        if (econet_stations[i].is_open)
        {
            closesocket(econet_stations[i].socket);
            econet_stations[i].is_open = false;
        }
        econet_stations[i].station_id = 0;
    }
    for (int i = 0; i < ARRAY_SIZE(aun_stations); i++)
    {
        aun_stations[i].station_id = 0;
    }

    // Load configuration from config file
    config_foreach_local_station(_setup_econet_station, NULL);
    config_foreach_remote_station(_setup_aun_station, NULL);

    // Enable Econet RX for the AUN stations
    econet_rx_clear_bitmaps();
    for (int i = 0; i < ARRAY_SIZE(aun_stations); i++)
    {
        if (aun_stations[i].station_id != 0)
        {
            econet_rx_enable_station(aun_stations[i].station_id);
        }
    }

    trunk_reconfigure();

    // Start receivers
    xTaskCreate(_aun_udp_rx_task, "aun_udp_rx", 4096, NULL, 1, NULL);
    xTaskCreate(_aun_econet_rx_task, "aun_econet_rx", 4096, NULL, 1, NULL);
    is_running = true;
}

void aunbrige_start(void)
{
    ack_queue = xQueueCreate(10, sizeof(uint32_t));
    pipe(rx_udp_ctl_pipe); // Ugh. I feel dirty using sockets on embedded!
    is_running = false;
    aunbridge_reconfigure();
}
