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

#include "cJSON.h"
#include "esp_http_server.h"

#define MAX_WS_BROADCAST_SIZE 1024

typedef esp_err_t (*ws_handler_fn)(httpd_req_t* req, int request_id, const cJSON *payload);

httpd_handle_t http_server_start(void);
esp_err_t http_ws_broadcast_json(const char *json);


// Private api
esp_err_t http_ws_handler(httpd_req_t *req);
void http_ws_close_handler(httpd_handle_t hd, int sockfd);
void http_ws_init(void);
extern httpd_handle_t http_server;
