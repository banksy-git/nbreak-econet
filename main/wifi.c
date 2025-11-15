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

#include "config.h"
#include "wifi.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "wifi";
static EventGroupHandle_t s_wifi_ev;

#define WIFI_CONNECTED_BIT BIT0

static bool is_suppress_reconnect;

static wifi_mode_t wifi_mode_from_cfg()
{
    if (config_wifi.sta_enabled && config_wifi.ap_enabled)
        return WIFI_MODE_APSTA;
    if (config_wifi.sta_enabled)
        return WIFI_MODE_STA;
    if (config_wifi.ap_enabled)
        return WIFI_MODE_AP;
    return WIFI_MODE_NULL;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT)
    {
        if (id == WIFI_EVENT_STA_DISCONNECTED)
        {
            ESP_LOGW(TAG, "WiFi disconnected");
            if (!is_suppress_reconnect && config_wifi.sta_enabled)
            {
                esp_wifi_connect();
            }
            xEventGroupClearBits(s_wifi_ev, WIFI_CONNECTED_BIT);
        }
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        xEventGroupSetBits(s_wifi_ev, WIFI_CONNECTED_BIT);
    }
}

void wifi_reconfigure()
{
    wifi_mode_t new_mode = wifi_mode_from_cfg();
    wifi_mode_t cur_mode;
    esp_wifi_get_mode(&cur_mode);
    if (cur_mode != new_mode)
    {
        ESP_ERROR_CHECK(esp_wifi_set_mode(new_mode));
    }

    if (config_wifi.sta_enabled)
    {
        is_suppress_reconnect = true;
        esp_wifi_disconnect();
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config_wifi.sta));
    }

    if (config_wifi.ap_enabled)
    {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config_wifi.ap));
    }

    if (config_wifi.sta_enabled)
    {
        is_suppress_reconnect = false;
        esp_wifi_connect();
    }
}

void wifi_start(void)
{
    s_wifi_ev = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_reconfigure();
}