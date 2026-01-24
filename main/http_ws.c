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
#include "aun_bridge.h"
#include "econet.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "http.h"
#include "wifi.h"
#include <inttypes.h>

static const char *TAG = "ws";

#define MAX_WS_CLIENTS 4

static MessageBufferHandle_t _broadcast_messages;
static portMUX_TYPE _broadcast_messages_lock = portMUX_INITIALIZER_UNLOCKED;

static bool _ws_init_complete;
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

esp_err_t _ws_send(httpd_req_t *req, const char *json)
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

    esp_err_t ret = httpd_ws_send_frame(req, &frame);
    if (ret != ESP_OK)
    {
        int fd = httpd_req_to_sockfd(req);
        ESP_LOGW(TAG, "Async send failed to fd=%d: %d, dropping client", fd, ret);
        ws_client_remove(fd);
    }
    return ret;
}

static esp_err_t send_ok_response(httpd_req_t *req, int request_id)
{
    char response[128];
    snprintf(response, sizeof(response),
             "{\"type\":\"response\",\"id\": %d, \"ok\":true}",
             request_id);
    return _ws_send(req, response);
}

static esp_err_t send_err_response(httpd_req_t *req, int request_id, const char *msg)
{
    char response[128];
    snprintf(response, sizeof(response),
             "{\"type\":\"response\",\"id\": %d, \"error\":\"%s\"}",
             request_id, msg);
    return _ws_send(req, response);
}

static esp_err_t ws_handle_ping(httpd_req_t *req, int request_id, const cJSON *payload)
{
    char response[64];
    snprintf(response, sizeof(response), "{\"type\":\"pong\"}");
    return _ws_send(req, response);
}

static esp_err_t _ws_save_econet(httpd_req_t *req, int request_id, const cJSON *payload)
{
    const cJSON *settings = cJSON_GetObjectItemCaseSensitive(payload, "settings");
    if (!cJSON_IsObject(settings))
    {
        return send_err_response(req, request_id, "Missing settings");
    }

    config_save_econet(settings);

    aunbridge_reconfigure();

    return send_ok_response(req, request_id);
}

static esp_err_t _ws_get_econet(httpd_req_t *req, int request_id, const cJSON *payload)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "response");
    cJSON_AddNumberToObject(root, "id", request_id);
    cJSON_AddBoolToObject(root, "ok", cJSON_True);

    cJSON *settings = cJSON_CreateObject();

    // Get econet and trunks sections from config
    cJSON *econet = config_get_econet();
    if (econet)
    {
        cJSON_AddItemToObject(settings, "econet", cJSON_Duplicate(econet, 1));
    }

    cJSON *trunks = config_get_trunks();
    if (trunks)
    {
        // Remove aes_key fields from uplinks (write-only)
        cJSON *trunks_copy = cJSON_Duplicate(trunks, 1);
        cJSON *uplinks = cJSON_GetObjectItem(trunks_copy, "uplinks");
        if (uplinks)
        {
            for (cJSON *uplink = uplinks->child; uplink != NULL; uplink = uplink->next)
            {
                cJSON_DeleteItemFromObject(uplink, "aesKey");
            }
        }
        cJSON_AddItemToObject(settings, "trunks", trunks_copy);
    }

    cJSON_AddItemToObject(root, "settings", settings);

    char *response = cJSON_PrintUnformatted(root);
    esp_err_t err = _ws_send(req, response);

    free(response);
    cJSON_Delete(root);
    return err;
}

static void reconfig_wifi(TimerHandle_t t)
{
    xTimerDelete(t, 0);
    wifi_reconfigure();
    config_save();
}

static esp_err_t _ws_save_wifi(httpd_req_t *req, int request_id, const cJSON *payload)
{
    const cJSON *settings = cJSON_GetObjectItemCaseSensitive(payload, "settings");
    if (!cJSON_IsObject(settings))
    {
        return send_err_response(req, request_id, "Missing settings");
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

    esp_err_t ret = send_ok_response(req, request_id);
    if (ret == ESP_OK)
    {
        http_ws_broadcast_json("{\"type\":\"restarting\"}");
        ESP_LOGW(TAG, "Reconfiguring WiFi...");
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

static esp_err_t _ws_get_wifi(httpd_req_t *req, int request_id, const cJSON *payload)
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
    return _ws_send(req, response);
}

static esp_err_t _ws_save_wifi_ap(httpd_req_t *req, int request_id, const cJSON *payload)
{
    const cJSON *settings = cJSON_GetObjectItemCaseSensitive(payload, "settings");
    if (!cJSON_IsObject(settings))
    {
        return send_err_response(req, request_id, "Missing settings");
    }

    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(settings, "ssid");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(settings, "password");
    const cJSON *is_enabled = cJSON_GetObjectItemCaseSensitive(settings, "enabled");

    if (!cJSON_IsString(ssid) || !cJSON_IsString(password) || !cJSON_IsBool(is_enabled))
    {
        return send_err_response(req, request_id, "Missing or incorrect fields");
    }

    snprintf((char *)config_wifi.ap.ap.ssid,
             sizeof(config_wifi.ap.ap.ssid),
             "%s",
             ssid->valuestring);

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

    config_wifi.ap_enabled = is_enabled->valueint ? true : false;

    esp_err_t ret = send_ok_response(req, request_id);
    if (ret == ESP_OK)
    {
        http_ws_broadcast_json("{\"type\":\"restarting\"}");
        ESP_LOGW(TAG, "Reconfiguring WiFi...");
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

static esp_err_t _ws_get_wifi_ap(httpd_req_t *req, int request_id, const cJSON *payload)
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
    return _ws_send(req, response);
}

static void factory_reset_cb(TimerHandle_t t)
{
    // Erase NVS (both config and secrets)
    nvs_flash_erase();
    nvs_flash_init();

    // Remove config file
    remove("/user/config.json");

    esp_restart();
}
static esp_err_t ws_handle_factory_reset(httpd_req_t *req, int request_id, const cJSON *payload)
{
    esp_err_t ret = send_ok_response(req, request_id);
    if (ret == ESP_OK)
    {
        http_ws_broadcast_json("{\"type\":\"restarting\"}");
        ESP_LOGW(TAG, "Factory reset...");
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
static esp_err_t ws_handle_reboot(httpd_req_t *req, int request_id, const cJSON *payload)
{
    esp_err_t ret = send_ok_response(req, request_id);
    if (ret == ESP_OK)
    {
        http_ws_broadcast_json("{\"type\":\"restarting\"}");
        ESP_LOGW(TAG, "Rebooting...");
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

static esp_err_t _ws_save_econet_clock(httpd_req_t *req, int request_id, const cJSON *payload)
{
    const cJSON *settings = cJSON_GetObjectItemCaseSensitive(payload, "settings");
    if (!cJSON_IsObject(settings))
    {
        return send_err_response(req, request_id, "Missing settings");
    }

    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(settings, "mode");
    const cJSON *freq = cJSON_GetObjectItemCaseSensitive(settings, "internalFrequencyHz");
    const cJSON *duty = cJSON_GetObjectItemCaseSensitive(settings, "internalDutyCycle");
    const cJSON *invert = cJSON_GetObjectItemCaseSensitive(settings, "invertClock");

    if (!cJSON_IsString(mode) || !cJSON_IsNumber(freq) || !cJSON_IsNumber(duty))
    {
        return send_err_response(req, request_id, "Missing or incorrect fields");
    }

    if (duty->valueint < 1 || duty->valueint > 100 || freq->valueint < 50000 || freq->valueint > 500000)
    {
        return send_err_response(req, request_id, "Unacceptable clock values");
    }

    config_econet_clock_t clock_cfg = {
        .mode = !strcmp(mode->valuestring, "internal") ? ECONET_CLOCK_INTERNAL : ECONET_CLOCK_EXTERNAL,
        .frequency_hz = freq->valueint,
        .duty_pc = duty->valueint,
        .invert_clock = cJSON_IsTrue(invert),
    };

    config_set_econet_clock(&clock_cfg);
    config_save();

    econet_clock_reconfigure();

    return send_ok_response(req, request_id);
}

static esp_err_t _ws_get_econet_clock(httpd_req_t *req, int request_id, const cJSON *payload)
{
    config_econet_clock_t clock_cfg;
    config_get_econet_clock(&clock_cfg);

    char response[256];
    snprintf(response, sizeof(response),
             "{\"type\":\"response\",\"id\": %d, \"ok\":true,"
             "\"settings\": {"
             "\"mode\": \"%s\","
             "\"internalFrequencyHz\": %" PRIu32 ","
             "\"internalDutyCycle\": %" PRIu32 ","
             "\"invertClock\": %s"
             "}}",
             request_id,
             clock_cfg.mode == ECONET_CLOCK_INTERNAL ? "internal" : "external",
             clock_cfg.frequency_hz,
             clock_cfg.duty_pc,
             clock_cfg.invert_clock ? "true" : "false");
    return _ws_send(req, response);
}

static const struct
{
    const char *type;
    ws_handler_fn handler;
} ws_routes[] = {
    {"ping", ws_handle_ping},
    {"reboot", ws_handle_reboot},
    {"factory_reset", ws_handle_factory_reset},
    {"get_wifi", _ws_get_wifi},
    {"save_wifi", _ws_save_wifi},
    {"get_wifi_ap", _ws_get_wifi_ap},
    {"save_wifi_ap", _ws_save_wifi_ap},
    {"get_econet", _ws_get_econet},
    {"save_econet", _ws_save_econet},
    {"get_econet_clock", _ws_get_econet_clock},
    {"save_econet_clock", _ws_save_econet_clock},
};

static esp_err_t _ws_dispatch(httpd_req_t *req, const char *type, int id, const cJSON *payload)
{
    for (size_t i = 0; i < sizeof(ws_routes) / sizeof(ws_routes[0]); i++)
    {
        if (strcmp(type, ws_routes[i].type) == 0)
        {
            return ws_routes[i].handler(req, id, payload);
        }
    }

    ESP_LOGW(TAG, "Unknown WS type '%s' from fd=%d", type, httpd_req_to_sockfd(req));
    return ESP_FAIL;
}

static esp_err_t _ws_handle_message(httpd_req_t *req, const char *msg, size_t len)
{
    if (len == 0)
    {
        return ESP_OK;
    }

    cJSON *root = cJSON_ParseWithLength(msg, len);
    if (!root)
    {
        ESP_LOGW(TAG, "Invalid JSON from fd=%d", httpd_req_to_sockfd(req));
        return send_err_response(req, 0, "Invalid JSON");
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");

    if (!cJSON_IsString(type) || !cJSON_IsNumber(id))
    {
        ESP_LOGW(TAG, "JSON 'type' or 'id' error from fd=%d", httpd_req_to_sockfd(req));
        cJSON_Delete(root);
        return send_err_response(req, 0, "Missing or incorrect type or ID");
    }

    esp_err_t ret = _ws_dispatch(req, type->valuestring, id->valueint, root);

    cJSON_Delete(root);
    return ret;
}

esp_err_t http_ws_handler(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);

    // First call: HTTP GET upgrade to websocket
    if (req->method == HTTP_GET)
    {
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

    ret = _ws_handle_message(req, (char *)buf, ws_pkt.len);

    free(buf);
    return ret;
}

static void _async_send_worker(void *arg)
{
    uint8_t msg[MAX_WS_BROADCAST_SIZE];

    while (1)
    {
        size_t msg_len = xMessageBufferReceive(_broadcast_messages, msg, sizeof(msg), 0);
        if (msg_len == 0)
        {
            return;
        }

        httpd_ws_frame_t frame = {
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = msg,
            .len = msg_len,
        };

        for (int i = 0; i < MAX_WS_CLIENTS; i++)
        {
            int fd = s_ws_fds[i];
            if (fd < 0)
            {
                continue;
            }

            esp_err_t ret = httpd_ws_send_frame_async(http_server, fd, &frame);
            if (ret != ESP_OK)
            {
                ws_client_remove(fd);
                ESP_LOGW(TAG, "Failed to send broadcast to fd=%d", fd);
            }
        }
    }
}

esp_err_t http_ws_broadcast_json(const char *json)
{

    if (!_ws_init_complete || !json)
    {
        return ESP_FAIL;
    }

    int msg_len = strlen(json);
    if (msg_len > MAX_WS_BROADCAST_SIZE)
    {
        ESP_LOGW(TAG, "Couldn't send broadcast message. Too long.");
        return ESP_FAIL;
    }
    if (msg_len == 0)
    {
        return ESP_FAIL;
    }

    portENTER_CRITICAL(&_broadcast_messages_lock);
    bool is_empty = xStreamBufferNextMessageLengthBytes(_broadcast_messages) > 0;
    size_t len_written = xMessageBufferSend(_broadcast_messages, json, msg_len, 0);
    portEXIT_CRITICAL(&_broadcast_messages_lock);
    if (len_written == 0)
    {
        return ESP_FAIL;
    }

    if (is_empty)
    {
        return httpd_queue_work(http_server, _async_send_worker, NULL);
    }
    return ESP_OK;
}

void http_ws_close_handler(httpd_handle_t hd, int sockfd)
{
    ws_client_remove(sockfd);
    closesocket(sockfd);
}

void http_ws_init(void)
{
    _broadcast_messages = xMessageBufferCreate(MAX_WS_BROADCAST_SIZE * 4);
    ws_clients_init();
    _ws_init_complete = true;
}
