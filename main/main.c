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
#include "freertos/queue.h"

#include "config.h"
#include "esp_log.h"
#include "esp_littlefs.h"

#include "wifi.h"
#include "http.h"
#include "econet.h"
#include "aun_bridge.h"
#include "logging.h"

#define CLK_PIN 6
#define DATA_OUT_PIN 1
#define OE_PIN 7
#define DATA_IN_PIN 0
#define CLK_OUT_PIN 5
#define CLK_OE_PIN 4
#define CLK_FREQ_HZ 100000

void init_fs(void)
{
    {
        esp_vfs_littlefs_conf_t conf = {
            .base_path = "/app",
            .partition_label = "rootfs",
            .format_if_mount_failed = true,
            .dont_mount = false,
        };
        ESP_ERROR_CHECK(esp_vfs_littlefs_register(&conf));
    }

    {
        esp_vfs_littlefs_conf_t conf = {
            .base_path = "/user",
            .partition_label = "user",
            .format_if_mount_failed = true,
            .dont_mount = false,
        };
        ESP_ERROR_CHECK(esp_vfs_littlefs_register(&conf));
    }
}

void print_task_list(void)
{
    // char *buf;
    // const size_t buf_size = 1024;  // increase if you have many tasks

    // buf = malloc(buf_size);
    // if (!buf) return;

    // vTaskList(buf);
    // printf("Name\tState\tPrio\tStack\tNum\n");
    // printf("%s\n", buf);

    // free(buf);
}

void app_main(void)
{
    init_fs();

    config_init();

    logging_init();

    wifi_start();

    http_server_start();

    econet_config_t econet_cfg = {
        .clk_pin = CLK_PIN,
        .clk_freq_hz = CLK_FREQ_HZ,
        .clk_output_pin = CLK_OUT_PIN,
        .clk_oe_pin = CLK_OE_PIN,
        .data_in_pin = DATA_IN_PIN,
        .data_out_pin = DATA_OUT_PIN,
        .data_driver_en_pin = OE_PIN,
    };
    econet_setup(&econet_cfg);
    econet_start();

    aunbrige_start();

    esp_intr_dump(stderr);

    static char buf[MAX_WS_BROADCAST_SIZE];
    for (int i = 0;; i++)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        if ((i % 10) == 0)
        {
            print_task_list();
        }

        aunbridge_stats_t aun = aunbridge_stats;
        econet_stats_t eco = econet_stats;

        int len = snprintf(buf, sizeof(buf),
                           "{"
                           "\"type\":\"stats_stream\","
                           "\"aunbridge_stats\":{"
                           "\"tx_count\":%lu,"
                           "\"tx_retry_count\":%lu,"
                           "\"tx_abort_count\":%lu,"
                           "\"tx_error_count\":%lu,"
                           "\"tx_ack_count\":%lu,"
                           "\"tx_nack_count\":%lu,"
                           "\"tx_bridge_control\":%lu,"
                           "\"tx_broadcast_count\":%lu,"
                           "\"rx_imm_count\":%lu,"
                           "\"rx_data_count\":%lu,"
                           "\"rx_ack_count\":%lu,"
                           "\"rx_nack_count\":%lu,"
                           "\"rx_unknown_count\":%lu,"
                           "\"rx_bridge_control\":%lu,"
                           "\"rx_broadcast_count\":%lu"
                           "},"
                           "\"econet_stats\":{"
                           "\"rx_frame_count\":%lu,"
                           "\"rx_crc_fail_count\":%lu,"
                           "\"rx_short_frame_count\":%lu,"
                           "\"rx_abort_count\":%lu,"
                           "\"rx_oversize_count\":%lu,"
                           "\"rx_ack_count\":%lu,"
                           "\"rx_nack_count\":%lu,"
                           "\"tx_frame_count\":%lu,"
                           "\"tx_ack_count\":%lu"
                           "}"
                           "}",
                           aun.tx_count,
                           aun.tx_retry_count,
                           aun.tx_abort_count,
                           aun.tx_error_count,
                           aun.tx_ack_count,
                           aun.tx_nack_count,
                           aun.tx_bridge_control,
                           aun.tx_broadcast_count,
                           aun.rx_imm_count,
                           aun.rx_data_count,
                           aun.rx_ack_count,
                           aun.rx_nack_count,
                           aun.rx_unknown_count,
                           aun.rx_bridge_control,
                           aun.rx_broadcast_count,
                           eco.rx_frame_count,
                           eco.rx_crc_fail_count,
                           eco.rx_short_frame_count,
                           eco.rx_abort_count,
                           eco.rx_oversize_count,
                           eco.rx_ack_count,
                           eco.rx_nack_count,
                           eco.tx_frame_count,
                           eco.tx_ack_count);

        if (len > 0 && len < (int)sizeof(buf))
        {
            http_ws_broadcast_json(buf);
        }
        else
        {
            ESP_LOGW("ws", "JSON too long or error building JSON");
        }
    }
}
