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

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "logging.h"
#include "http.h"

static vprintf_like_t original_logger;

static QueueHandle_t s_log_queue;

#define LOG_LINE_MAX 256

typedef struct {
    char line[LOG_LINE_MAX];
} log_msg_t;

static void _json_escape_append(char *dst, size_t dst_len, const char *src)
{
    size_t di = strlen(dst);
    const size_t max = dst_len - 1;

    for (size_t si = 0; src[si] && di + 2 < max; si++) {
        char c = src[si];
        switch (c) {
            case '\\': dst[di++] = '\\'; dst[di++] = '\\'; break;
            case '\"': dst[di++] = '\\'; dst[di++] = '\"'; break;
            case '\n': dst[di++] = '\\'; dst[di++] = 'n';  break;
            case '\r': dst[di++] = '\\'; dst[di++] = 'r';  break;
            case '\t': dst[di++] = '\\'; dst[di++] = 't';  break;
            default:   dst[di++] = c; break;
        }
    }
    dst[di] = '\0';
}

static int _logging_func(const char *fmt, va_list args)
{
    char buf[LOG_LINE_MAX];
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    if (len < 0) {
        return len;
    }

    // Normal task context, so use xQueueSend
    if (s_log_queue) {
        log_msg_t msg;
        size_t copy_len = (len >= LOG_LINE_MAX) ? LOG_LINE_MAX - 1 : len;
        memcpy(msg.line, buf, copy_len);
        msg.line[copy_len] = '\0';

        // Best-effort, non-blocking
        xQueueSend(s_log_queue, &msg, 0);
    }

    // Send to serial console
    fputs(buf, stdout);

    return len;
}

static void _log_to_ws(void *arg)
{
    log_msg_t msg;
    char json[LOG_LINE_MAX + 64];

    while (1) {
        if (xQueueReceive(s_log_queue, &msg, portMAX_DELAY) == pdTRUE) {

            // Send log to web listeners
            strncpy(json, "{\"type\":\"log\",\"line\":\"", sizeof(json));
            _json_escape_append(json, sizeof(json)-3,  msg.line);
            strlcat(json, "\"}", sizeof(json));
            http_ws_broadcast_json(json);
        }
    }
}

void logging_init(void)
{
    s_log_queue = xQueueCreate(32, sizeof(log_msg_t));
    original_logger = esp_log_set_vprintf(_logging_func);
    xTaskCreate(_log_to_ws, "logging", 4096, NULL, 5, NULL);
}
