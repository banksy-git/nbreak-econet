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
#include "econet.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "http.h"
#include "wifi.h"

static const char *TAG = "ws";

#define MAX_WS_CLIENTS 4

static int s_ws_fds[MAX_WS_CLIENTS];

static void ws_clients_init(void)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        s_ws_fds[i] = -1;
    }
}

static void ws_client_add(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (s_ws_fds[i] == -1)
        {
            s_ws_fds[i] = fd;
            ESP_LOGI("ws", "Client added on fd=%d (slot %d)", fd, i);
            return;
        }
    }
    ESP_LOGW("ws", "No space for more WS clients");
}

static void ws_client_remove(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (s_ws_fds[i] == fd)
        {
            s_ws_fds[i] = -1;
            ESP_LOGI("ws", "Client removed fd=%d (slot %d)", fd, i);
            return;
        }
    }
}

static esp_err_t send_ok_response(int request_id, int fd)
{
    char response[128];
    snprintf(response, sizeof(response),
             "{\"type\":\"response\",\"id\": %d, \"ok\":true}",
             request_id);
    return http_ws_send(fd, response);
}

static esp_err_t _ws_save_econet(int request_id, const cJSON *payload, int fd)
{
    const cJSON *settings = cJSON_GetObjectItemCaseSensitive(payload, "settings");
    if (!cJSON_IsObject(settings))
    {
        ESP_LOGW(TAG, "JSON missing 'settings' from fd=%d", fd);
        return ESP_FAIL;
    }

    const cJSON *remote_station_id = cJSON_GetObjectItemCaseSensitive(settings, "remote_station_id");
    const cJSON *this_station_id = cJSON_GetObjectItemCaseSensitive(settings, "this_station_id");
    const cJSON *server_ip = cJSON_GetObjectItemCaseSensitive(settings, "server_ip");
    const cJSON *server_port = cJSON_GetObjectItemCaseSensitive(settings, "server_port");

    config_econet.remote_station_id = remote_station_id->valueint;
    config_econet.this_station_id = this_station_id->valueint;
    if (cJSON_IsString(server_ip))
    {
        snprintf((char *)config_econet.server_address,
                 sizeof(config_econet.server_address),
                 "%s",
                 server_ip->valuestring);
    }
    config_econet.server_port = server_port->valueint;

    config_save_econet();

    econet_reconfigure();

    return send_ok_response(request_id, fd);
}

static esp_err_t _ws_get_econet(int request_id, const cJSON *payload, int fd)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "response");
    cJSON_AddNumberToObject(root, "id", request_id);
    cJSON_AddBoolToObject(root, "ok", cJSON_True);

    cJSON *settings = cJSON_AddObjectToObject(root, "settings");
    cJSON_AddNumberToObject(settings, "remote_station_id",
                            config_econet.remote_station_id);
    cJSON_AddNumberToObject(settings, "this_station_id",
                            config_econet.this_station_id);
    cJSON_AddStringToObject(settings, "server_ip", config_econet.server_address);
    cJSON_AddNumberToObject(settings, "server_port",
                            config_econet.server_port);

    char* response = cJSON_PrintUnformatted(root);
    esp_err_t err = http_ws_send(fd, response);

    free(response);
    cJSON_Delete(root);
    return err;
}

static void reconfig_wifi(TimerHandle_t t)
{
    xTimerDelete(t, 0);
    wifi_reconfigure();
    config_save_wifi();
}

static esp_err_t _ws_save_wifi(int request_id, const cJSON *payload, int fd)
{
    const cJSON *settings = cJSON_GetObjectItemCaseSensitive(payload, "settings");
    if (!cJSON_IsObject(settings))
    {
        ESP_LOGW(TAG, "JSON missing 'settings' from fd=%d", fd);
        return ESP_FAIL;
    }

    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(settings, "ssid");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(settings, "password");

    config_wifi.sta_enabled = false;
    if (cJSON_IsString(ssid) && ssid->valuestring)
    {
        config_wifi.sta_enabled = strlen(ssid->valuestring) ? true : false;
        snprintf((char *)config_wifi.sta.sta.ssid,
                 sizeof(config_wifi.sta.sta.ssid),
                 "%s",
                 ssid->valuestring);
    }

    if (cJSON_IsString(password) && password->valuestring)
    {
        snprintf((char *)config_wifi.sta.sta.password,
                 sizeof(config_wifi.sta.sta.password),
                 "%s",
                 password->valuestring);
    }

    esp_err_t ret = send_ok_response(request_id, fd);
    if (ret == ESP_OK)
    {
        ESP_LOGW(TAG, "Reconfiguring WiFi...", fd);
        TimerHandle_t t = xTimerCreate(
            "wifi_reconfig",
            pdMS_TO_TICKS(3000),
            pdFALSE,
            NULL,
            reconfig_wifi);
        xTimerStart(t, 0);
    }
    return ret;
}

static esp_err_t _ws_get_wifi(int request_id, const cJSON *payload, int fd)
{
    char response[256];
    snprintf(response, sizeof(response),
             "{\"type\":\"response\",\"id\": %d, \"ok\":true,"
             "\"settings\": {"
             "\"ssid\": \"%s\","
             "\"password\": \"\","
             "\"enabled\": %s"
             "}}",
             request_id,
             config_wifi.sta.sta.ssid,
             config_wifi.sta_enabled ? "true" : "false");
    return http_ws_send(fd, response);
}

static esp_err_t _ws_save_wifi_ap(int request_id, const cJSON *payload, int fd)
{
    const cJSON *settings = cJSON_GetObjectItemCaseSensitive(payload, "settings");
    if (!cJSON_IsObject(settings))
    {
        ESP_LOGW(TAG, "JSON missing 'settings' from fd=%d", fd);
        return ESP_FAIL;
    }

    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(settings, "ssid");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(settings, "password");
    const cJSON *is_enabled = cJSON_GetObjectItemCaseSensitive(settings, "enabled");

    if (cJSON_IsString(ssid))
    {
        snprintf((char *)config_wifi.ap.ap.ssid,
                 sizeof(config_wifi.ap.ap.ssid),
                 "%s",
                 ssid->valuestring);
    }

    if (cJSON_IsString(password))
    {
        snprintf((char *)config_wifi.ap.ap.password,
                 sizeof(config_wifi.ap.ap.password),
                 "%s",
                 password->valuestring);
        if (strlen(password->valuestring) > 0)
        {
            config_wifi.ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
        }
        else
        {
            config_wifi.ap.ap.authmode = WIFI_AUTH_OPEN;
        }
    }

    config_wifi.ap_enabled = true;
    if (cJSON_IsBool(is_enabled))
    {
        config_wifi.ap_enabled = is_enabled->valueint;
    }

    esp_err_t ret = send_ok_response(request_id, fd);
    if (ret == ESP_OK)
    {
        ESP_LOGW(TAG, "Reconfiguring WiFi...", fd);
        TimerHandle_t t = xTimerCreate(
            "wifi_reconfig",
            pdMS_TO_TICKS(3000),
            pdFALSE,
            NULL,
            reconfig_wifi);
        xTimerStart(t, 0);
    }
    return ret;
}

static esp_err_t _ws_get_wifi_ap(int request_id, const cJSON *payload, int fd)
{
    char response[256];
    snprintf(response, sizeof(response),
             "{\"type\":\"response\",\"id\": %d, \"ok\":true,"
             "\"settings\": {"
             "\"ssid\": \"%s\","
             "\"password\": \"\","
             "\"enabled\": %s"
             "}}",
             request_id,
             config_wifi.ap.ap.ssid,
             config_wifi.ap_enabled ? "true" : "false");
    return http_ws_send(fd, response);
}

static void factory_reset_cb(TimerHandle_t t)
{
    nvs_flash_erase();
    nvs_flash_init();
    esp_restart();
}
static esp_err_t ws_handle_factory_reset(int request_id, const cJSON *payload, int fd)
{
    esp_err_t ret = send_ok_response(request_id, fd);
    if (ret == ESP_OK)
    {
        ESP_LOGW(TAG, "Factory reset...", fd);
        TimerHandle_t t = xTimerCreate(
            "factory_reset",
            pdMS_TO_TICKS(3000),
            pdFALSE,
            NULL,
            factory_reset_cb);
        xTimerStart(t, 0);
    }
    return ret;
}

static void _reboot_callback(TimerHandle_t t)
{
    esp_restart();
}
static esp_err_t ws_handle_reboot(int request_id, const cJSON *payload, int fd)
{
    esp_err_t ret = send_ok_response(request_id, fd);
    if (ret == ESP_OK)
    {
        ESP_LOGW(TAG, "Rebooting...", fd);
        TimerHandle_t t = xTimerCreate(
            "reboot_cb",
            pdMS_TO_TICKS(3000),
            pdFALSE,
            NULL,
            _reboot_callback);
        xTimerStart(t, 0);
    }
    return ret;
}

static const struct
{
    const char *type;
    ws_handler_fn handler;
} ws_routes[] = {
    {"reboot", ws_handle_reboot},
    {"factory_reset", ws_handle_factory_reset},
    {"get_wifi", _ws_get_wifi},
    {"save_wifi", _ws_save_wifi},
    {"get_wifi_ap", _ws_get_wifi_ap},
    {"save_wifi_ap", _ws_save_wifi_ap},
    {"get_econet", _ws_get_econet},
    {"save_econet", _ws_save_econet},

};

static esp_err_t ws_dispatch(const char *type, int id, const cJSON *payload, int fd)
{
    for (size_t i = 0; i < sizeof(ws_routes) / sizeof(ws_routes[0]); i++)
    {
        if (strcmp(type, ws_routes[i].type) == 0)
        {
            return ws_routes[i].handler(id, payload, fd);
        }
    }

    ESP_LOGW(TAG, "Unknown WS type '%s' from fd=%d", type, fd);
    return ESP_FAIL;
}

static esp_err_t ws_handle_message(const char *msg, size_t len, int fd)
{
    // Basic size guard if you want one
    if (len == 0)
    {
        return ESP_OK;
    }

    cJSON *root = cJSON_ParseWithLength(msg, len);
    if (!root)
    {
        ESP_LOGW(TAG, "Invalid JSON from fd=%d", fd);
        return ESP_FAIL;
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || type->valuestring == NULL)
    {
        ESP_LOGW(TAG, "JSON missing 'type' from fd=%d", fd);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (!cJSON_IsNumber(id))
    {
        ESP_LOGW(TAG, "JSON missing 'id' from fd=%d", fd);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    esp_err_t ret = ws_dispatch(type->valuestring, id->valueint, root, fd);

    cJSON_Delete(root);
    return ret;
}

esp_err_t http_ws_handler(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);

    // First call: HTTP GET upgrade to websocket
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "WS handshake done, new client fd=%d", fd);
        ws_client_add(fd);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // Get frame length
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "ws_recv_frame length failed: %d", ret);
        ws_client_remove(fd);
        return ret;
    }

    // Handle CLOSE frame
    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE)
    {
        ESP_LOGI(TAG, "WS CLOSE from fd=%d", fd);
        ws_client_remove(fd);
        return ESP_OK;
    }

    if (ws_pkt.len == 0)
    {
        // Nothing to do
        return ESP_OK;
    }

    uint8_t *buf = calloc(1, ws_pkt.len + 1);
    if (!buf)
    {
        ESP_LOGE(TAG, "No memory for WS payload");
        return ESP_ERR_NO_MEM;
    }

    ws_pkt.payload = buf;

    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "ws_recv_frame payload failed: %d", ret);
        free(buf);
        ws_client_remove(fd);
        return ret;
    }

    buf[ws_pkt.len] = 0; // null-terminate

    ret = ws_handle_message((char *)buf, ws_pkt.len, fd);

    free(buf);
    return ret;
}

esp_err_t http_ws_send(int fd, const char *json)
{
    if (!http_server || !json)
    {
        return ESP_FAIL;
    }

    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = strlen(json),
    };

    esp_err_t ret = httpd_ws_send_frame_async(http_server, fd, &frame);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Async send failed to fd=%d: %d, dropping client", fd, ret);
        ws_client_remove(fd);
    }
    return ret;
}

esp_err_t http_ws_broadcast_json(const char *json)
{
    if (!http_server || !json)
    {
        return ESP_FAIL;
    }

    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = strlen(json),
    };

    esp_err_t last_err = ESP_OK;

    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        int fd = s_ws_fds[i];
        if (fd < 0)
            continue;

        esp_err_t ret = httpd_ws_send_frame_async(http_server, fd, &frame);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "Async send failed to fd=%d: %d, dropping client", fd, ret);
            ws_client_remove(fd);
            last_err = ret;
        }
    }

    return last_err;
}

void http_ws_close_handler(httpd_handle_t hd, int sockfd)
{
    ws_client_remove(sockfd);
    closesocket(sockfd);
}

void http_ws_init(void)
{
    ws_clients_init();
}