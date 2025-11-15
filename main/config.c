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

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_check.h"
#include "config.h"

config_wifi_t config_wifi;
config_econet_t config_econet;

static const char *TAG = "config";

static esp_err_t _save_config(const char* config_name, const void* value, size_t length)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open("config", NVS_READWRITE, &h), TAG, "nvs_open");
    ESP_RETURN_ON_ERROR(nvs_set_blob(h, config_name, value, length), TAG, "set_blob");
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static esp_err_t _load_config(const char* config_name, void* value, size_t length)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("config", NVS_READONLY, &h);
    if (err != ESP_OK)
        return err;
    size_t size = length;
    err = nvs_get_blob(h, config_name, value, &size);
    nvs_close(h);
    return err;
}

esp_err_t config_save_wifi(void)
{
    return _save_config("wifi", &config_wifi, sizeof(config_wifi));
}

esp_err_t config_load_wifi(void)
{
    esp_err_t err = _load_config("wifi", &config_wifi, sizeof(config_wifi));
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Using default WiFi configuration");
        memset(&config_wifi, 0, sizeof(config_wifi));
        strcpy((char *)config_wifi.ap.ap.ssid, "nbreak-econet");
        config_wifi.ap_enabled = true;
        config_wifi.ap.ap.authmode = WIFI_AUTH_OPEN;
        config_wifi.ap.ap.max_connection = 3;
    }
    return ESP_OK;
}

esp_err_t config_save_econet(void)
{
    return _save_config("econet", &config_econet, sizeof(config_econet));
}

esp_err_t config_load_econet(void)
{
    esp_err_t err = _load_config("econet", &config_econet, sizeof(config_econet));
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Using default Econet configuration");
        memset(&config_econet, 0, sizeof(config_econet));
        config_econet.this_station_id = 254;
        config_econet.remote_station_id = 127;
        strcpy(config_econet.server_address, "192.168.4.2");
        config_econet.server_port = 32768;
    }
    return ESP_OK;
}

void config_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    config_load_wifi();
    config_load_econet();

}
