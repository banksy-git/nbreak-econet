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

#include "esp_log.h"
#include "http.h"

httpd_handle_t http_server = NULL;

static const char *TAG = "httpd";

static esp_err_t _file_handler(httpd_req_t *req)
{
    char filepath[256];
    const char *base_path = "/fs/web";

    snprintf(filepath, sizeof(filepath), "%s%s", base_path, req->uri);

    // Default file if request is "/"
    if (strcmp(req->uri, "/") == 0)
    {
        snprintf(filepath, sizeof(filepath), "%s/index.html", base_path);
    }

    FILE *f = fopen(filepath, "r");
    if (!f)
    {
        httpd_resp_send_404(req);
        return ESP_OK;
    }

    // Determine MIME type (super basic)
    if (strstr(filepath, ".html"))
        httpd_resp_set_type(req, "text/html");
    else if (strstr(filepath, ".css"))
        httpd_resp_set_type(req, "text/css");
    else if (strstr(filepath, ".js"))
        httpd_resp_set_type(req, "application/javascript");
    else if (strstr(filepath, ".png"))
        httpd_resp_set_type(req, "image/png");
    else if (strstr(filepath, ".jpg"))
        httpd_resp_set_type(req, "image/jpeg");
    else if (strstr(filepath, ".svg"))
        httpd_resp_set_type(req, "image/svg+xml");
    else
    {
        httpd_resp_set_type(req, "application/octet-stream");
    }

    // Stream file
    char chunk[1024];
    size_t read_len;

    while ((read_len = fread(chunk, 1, sizeof(chunk), f)) > 0)
    {
        if (httpd_resp_send_chunk(req, chunk, read_len) != ESP_OK)
        {
            fclose(f);
            return ESP_FAIL;
        }
    }

    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0); // terminate chunked response
    return ESP_OK;
}

httpd_handle_t http_server_start(void)
{
    http_ws_init();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.ctrl_port = 40404; // We want the default for AUN (Econet/IP)
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.close_fn = http_ws_close_handler;

    ESP_LOGI(TAG, "Starting server on port: %d", config.server_port);

    httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = http_ws_handler,
        .user_ctx = NULL,
        .is_websocket = true};

    httpd_uri_t file_server = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = _file_handler,
        .user_ctx = NULL};

    if (httpd_start(&http_server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(http_server, &ws);
        httpd_register_uri_handler(http_server, &file_server);
        return http_server;
    }

    ESP_LOGE(TAG, "Error starting server!");
    return NULL;
}
