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

#include <stdbool.h>
#include "cJSON.h"
#include "esp_wifi.h"

typedef struct
{
    bool sta_enabled;
    bool ap_enabled;
    wifi_config_t sta; //< SSID/pass for client mode
    wifi_config_t ap;  //< SSID/pass for AP mode
} config_wifi_t;
extern config_wifi_t config_wifi;

typedef enum
{
    ECONET_CLOCK_INTERNAL,
    ECONET_CLOCK_EXTERNAL,
} econet_clock_mode_t;

typedef struct
{
    uint32_t frequency_hz;
    uint32_t duty_pc;
    econet_clock_mode_t mode;
    bool invert_clock;
} config_econet_clock_t;

typedef struct
{
    uint8_t station_id;
    uint8_t network_id;
    uint16_t local_udp_port;
} config_econet_station_t;

typedef struct
{
    char remote_address[64];
    uint8_t station_id;
    uint8_t network_id;
    uint16_t udp_port;
} config_aun_station_t;

typedef struct
{
    char remote_address[64];
    uint16_t udp_port;
    uint8_t key[32];
    uint8_t key_len;
} config_trunk_t;

typedef esp_err_t (*config_cb_econet_station)(config_econet_station_t *cfg);
typedef esp_err_t (*config_cb_aun_station)(config_aun_station_t *cfg);
typedef esp_err_t (*config_cb_trunk)(config_trunk_t *cfg);

void config_init(void);
esp_err_t config_save_wifi(void);
esp_err_t config_load_wifi(void);

cJSON *config_load_econet_json(void);
esp_err_t config_save_econet(const cJSON *settings);
esp_err_t config_load_econet(config_cb_econet_station eco_cb, config_cb_aun_station aun_cb, config_cb_trunk trunk_cb);

esp_err_t config_save_econet_clock(const config_econet_clock_t *clk);
esp_err_t config_load_econet_clock(config_econet_clock_t *clk);