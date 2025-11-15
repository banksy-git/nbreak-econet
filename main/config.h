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
#include "esp_wifi.h"

typedef struct
{
    bool sta_enabled;
    bool ap_enabled;
    wifi_config_t sta; //< SSID/pass for client mode
    wifi_config_t ap;  //< SSID/pass for AP mode
} config_wifi_t;
extern config_wifi_t config_wifi;

typedef struct
{
    uint8_t remote_station_id;
    uint8_t this_station_id;
    char server_address[128];
    uint16_t server_port;
} config_econet_t;
extern config_econet_t config_econet;

void config_init(void);
esp_err_t config_save_wifi(void);
esp_err_t config_load_wifi(void);
esp_err_t config_save_econet(void);
esp_err_t config_load_econet(void);
