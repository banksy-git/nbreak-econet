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

// Global parsed configuration
extern cJSON *g_config;

void config_init(void);
esp_err_t config_save(void);
esp_err_t config_reload(void);

typedef void (*config_local_station_iterator)(void *ctx, const config_econet_station_t *station);
typedef void (*config_remote_station_iterator)(void *ctx, const config_aun_station_t *station);
typedef void (*config_trunk_iterator)(void *ctx, const config_trunk_t *trunk);

void config_foreach_local_station(config_local_station_iterator iter, void *ctx);
void config_foreach_remote_station(config_remote_station_iterator iter, void *ctx);
void config_foreach_trunk(config_trunk_iterator iter, void *ctx);

uint8_t config_get_trunk_network(void);

// Internal: Get pointers to config sections
// This is a shortcut for http_ws.c because it already understands cJSON...
cJSON *config_get_wifi(void);
cJSON *config_get_econet(void);
cJSON *config_get_trunks(void);

esp_err_t config_save_wifi_secrets(const char *sta_password, const char *ap_password);

esp_err_t config_save_trunk_key(int trunk_index, const uint8_t *key, size_t key_len);
esp_err_t config_load_trunk_key(int trunk_index, uint8_t *key, size_t *key_len);

void config_get_econet_clock(config_econet_clock_t *clock);
void config_set_econet_clock(const config_econet_clock_t *clock);

esp_err_t config_save_econet(const cJSON *settings);