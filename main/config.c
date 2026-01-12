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

#include <stdio.h>
#include <sys/stat.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_check.h"
#include "config.h"

config_wifi_t config_wifi;
cJSON *g_config = NULL;

static const char *TAG = "config";
static const char *CONFIG_FILE = "/user/config.json";
static const char *OLD_ECONET_FILE = "/user/econet_cfg.bin";

// NVS key names for secrets
static const char *NVS_WIFI_STA_PASS = "wifi_sta_pass";
static const char *NVS_WIFI_AP_PASS = "wifi_ap_pass";
static const char *NVS_TRUNK_KEY_PREFIX = "trunk_";

// ==============================================================================
// Helper Functions
// ==============================================================================

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static esp_err_t nvs_save_string(const char *key, const char *value)
{
    if (!value)
        return ESP_OK;

    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open("secrets", NVS_READWRITE, &h), TAG, "nvs_open secrets");
    ESP_RETURN_ON_ERROR(nvs_set_str(h, key, value), TAG, "nvs_set_str %s", key);
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static esp_err_t nvs_load_string(const char *key, char *value, size_t max_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("secrets", NVS_READONLY, &h);
    if (err != ESP_OK)
        return err;

    size_t len = max_len;
    err = nvs_get_str(h, key, value, &len);
    nvs_close(h);
    return err;
}

static esp_err_t nvs_save_blob(const char *key, const void *data, size_t len)
{
    if (!data || len == 0)
        return ESP_OK;

    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open("secrets", NVS_READWRITE, &h), TAG, "nvs_open secrets");
    ESP_RETURN_ON_ERROR(nvs_set_blob(h, key, data, len), TAG, "nvs_set_blob %s", key);
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static esp_err_t nvs_load_blob(const char *key, void *data, size_t *len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("secrets", NVS_READONLY, &h);
    if (err != ESP_OK)
        return err;

    err = nvs_get_blob(h, key, data, len);
    nvs_close(h);
    return err;
}

// ==============================================================================
// Config Section Accessors
// ==============================================================================

cJSON *config_get_wifi(void)
{
    return g_config ? cJSON_GetObjectItem(g_config, "wifi") : NULL;
}

cJSON *config_get_econet(void)
{
    return g_config ? cJSON_GetObjectItem(g_config, "econet") : NULL;
}

cJSON *config_get_trunks(void)
{
    return g_config ? cJSON_GetObjectItem(g_config, "trunks") : NULL;
}

// ==============================================================================
// Load/Save Configuration File
// ==============================================================================

static cJSON *load_json_file(void)
{
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp)
    {
        ESP_LOGW(TAG, "Config file not found: %s", CONFIG_FILE);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buffer = malloc(size + 1);
    if (!buffer)
    {
        fclose(fp);
        ESP_LOGE(TAG, "Could not allocate buffer for config file");
        return NULL;
    }

    fread(buffer, 1, size, fp);
    buffer[size] = '\0';
    fclose(fp);

    cJSON *root = cJSON_Parse(buffer);
    free(buffer);

    if (!root)
    {
        ESP_LOGE(TAG, "Failed to parse config JSON");
    }

    return root;
}

static esp_err_t save_json_file(const cJSON *root)
{
    if (!root)
        return ESP_ERR_INVALID_ARG;

    FILE *fp = fopen("/user/config.tmp", "w");
    if (!fp)
    {
        ESP_LOGE(TAG, "Could not open temp file for writing");
        return ESP_FAIL;
    }

    char *json_str = cJSON_Print(root);
    if (!json_str)
    {
        fclose(fp);
        return ESP_ERR_NO_MEM;
    }

    fputs(json_str, fp);
    free(json_str);
    fclose(fp);

    if (rename("/user/config.tmp", CONFIG_FILE) != 0)
    {
        ESP_LOGE(TAG, "Failed to rename temp config file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Configuration saved to %s", CONFIG_FILE);
    return ESP_OK;
}

esp_err_t config_save(void)
{
    if (!g_config)
        return ESP_ERR_INVALID_STATE;

    // Update JSON from config_wifi global
    cJSON *wifi = config_get_wifi();
    if (!wifi)
    {
        wifi = cJSON_CreateObject();
        cJSON_AddItemToObject(g_config, "wifi", wifi);
    }

    cJSON *sta = cJSON_GetObjectItem(wifi, "sta");
    if (!sta)
    {
        sta = cJSON_CreateObject();
        cJSON_AddItemToObject(wifi, "sta", sta);
    }

    cJSON *ap = cJSON_GetObjectItem(wifi, "ap");
    if (!ap)
    {
        ap = cJSON_CreateObject();
        cJSON_AddItemToObject(wifi, "ap", ap);
    }

    cJSON_ReplaceItemInObject(sta, "enabled", cJSON_CreateBool(config_wifi.sta_enabled));
    cJSON_ReplaceItemInObject(sta, "ssid", cJSON_CreateString((char *)config_wifi.sta.sta.ssid));

    cJSON_ReplaceItemInObject(ap, "enabled", cJSON_CreateBool(config_wifi.ap_enabled));
    cJSON_ReplaceItemInObject(ap, "ssid", cJSON_CreateString((char *)config_wifi.ap.ap.ssid));
    cJSON_ReplaceItemInObject(ap, "authmode", cJSON_CreateNumber(config_wifi.ap.ap.authmode));
    cJSON_ReplaceItemInObject(ap, "maxConnections", cJSON_CreateNumber(config_wifi.ap.ap.max_connection));

    // Save WiFi passwords to NVS
    if (strlen((char *)config_wifi.sta.sta.password) > 0)
    {
        config_save_wifi_secrets((char *)config_wifi.sta.sta.password, NULL);
    }
    if (strlen((char *)config_wifi.ap.ap.password) > 0)
    {
        config_save_wifi_secrets(NULL, (char *)config_wifi.ap.ap.password);
    }

    return save_json_file(g_config);
}

// ==============================================================================
// Populate config_wifi from parsed JSON
// ==============================================================================

static void load_wifi_from_json(void)
{
    cJSON *wifi = config_get_wifi();
    if (!wifi)
        return;

    cJSON *sta = cJSON_GetObjectItem(wifi, "sta");
    if (sta)
    {
        cJSON *enabled = cJSON_GetObjectItem(sta, "enabled");
        cJSON *ssid = cJSON_GetObjectItem(sta, "ssid");

        if (enabled && cJSON_IsBool(enabled))
            config_wifi.sta_enabled = cJSON_IsTrue(enabled);

        if (ssid && cJSON_IsString(ssid))
        {
            snprintf((char *)config_wifi.sta.sta.ssid,
                     sizeof(config_wifi.sta.sta.ssid),
                     "%s", ssid->valuestring);
        }

        // Load password from NVS
        nvs_load_string(NVS_WIFI_STA_PASS,
                        (char *)config_wifi.sta.sta.password,
                        sizeof(config_wifi.sta.sta.password));
    }

    cJSON *ap = cJSON_GetObjectItem(wifi, "ap");
    if (ap)
    {
        cJSON *enabled = cJSON_GetObjectItem(ap, "enabled");
        cJSON *ssid = cJSON_GetObjectItem(ap, "ssid");
        cJSON *authmode = cJSON_GetObjectItem(ap, "authmode");
        cJSON *max_conn = cJSON_GetObjectItem(ap, "maxConnections");

        if (enabled && cJSON_IsBool(enabled))
            config_wifi.ap_enabled = cJSON_IsTrue(enabled);

        if (ssid && cJSON_IsString(ssid))
        {
            snprintf((char *)config_wifi.ap.ap.ssid,
                     sizeof(config_wifi.ap.ap.ssid),
                     "%s", ssid->valuestring);
        }

        if (authmode && cJSON_IsNumber(authmode))
            config_wifi.ap.ap.authmode = authmode->valueint;

        if (max_conn && cJSON_IsNumber(max_conn))
            config_wifi.ap.ap.max_connection = max_conn->valueint;

        // Load password from NVS
        nvs_load_string(NVS_WIFI_AP_PASS,
                        (char *)config_wifi.ap.ap.password,
                        sizeof(config_wifi.ap.ap.password));
    }
}

// ==============================================================================
// Migration from Old Format
// ==============================================================================

static esp_err_t migrate_old_config(void)
{
    ESP_LOGI(TAG, "Migrating from old configuration format");

    if (g_config)
        cJSON_Delete(g_config);

    g_config = cJSON_CreateObject();
    if (!g_config)
        return ESP_ERR_NO_MEM;

    // Migrate WiFi settings from old NVS
    nvs_handle_t h;
    esp_err_t err = nvs_open("config", NVS_READONLY, &h);
    if (err == ESP_OK)
    {
        config_wifi_t old_wifi;
        size_t size = sizeof(old_wifi);
        err = nvs_get_blob(h, "wifi", &old_wifi, &size);
        nvs_close(h);

        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "Migrating WiFi config from NVS");

            cJSON *wifi = cJSON_CreateObject();
            cJSON *sta = cJSON_CreateObject();
            cJSON *ap = cJSON_CreateObject();

            cJSON_AddBoolToObject(sta, "enabled", old_wifi.sta_enabled);
            cJSON_AddStringToObject(sta, "ssid", (char *)old_wifi.sta.sta.ssid);

            cJSON_AddBoolToObject(ap, "enabled", old_wifi.ap_enabled);
            cJSON_AddStringToObject(ap, "ssid", (char *)old_wifi.ap.ap.ssid);
            cJSON_AddNumberToObject(ap, "authmode", old_wifi.ap.ap.authmode);
            cJSON_AddNumberToObject(ap, "maxConnections", old_wifi.ap.ap.max_connection);

            cJSON_AddItemToObject(wifi, "sta", sta);
            cJSON_AddItemToObject(wifi, "ap", ap);
            cJSON_AddItemToObject(g_config, "wifi", wifi);

            // Migrate passwords
            if (strlen((char *)old_wifi.sta.sta.password) > 0)
                config_save_wifi_secrets((char *)old_wifi.sta.sta.password, NULL);
            if (strlen((char *)old_wifi.ap.ap.password) > 0)
                config_save_wifi_secrets(NULL, (char *)old_wifi.ap.ap.password);
        }
    }

    // Migrate Econet clock settings from old NVS
    err = nvs_open("config", NVS_READONLY, &h);
    if (err == ESP_OK)
    {
        config_econet_clock_t old_clock;
        size_t size = sizeof(old_clock);
        err = nvs_get_blob(h, "econet_clock", &old_clock, &size);
        nvs_close(h);

        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "Migrating Econet clock config from NVS");

            cJSON *econet = cJSON_CreateObject();
            cJSON *clock = cJSON_CreateObject();

            cJSON_AddNumberToObject(clock, "frequency", old_clock.frequency_hz);
            cJSON_AddNumberToObject(clock, "duty", old_clock.duty_pc);
            cJSON_AddStringToObject(clock, "mode",
                                    old_clock.mode == ECONET_CLOCK_INTERNAL ? "internal" : "external");
            cJSON_AddBoolToObject(clock, "invert", old_clock.invert_clock);

            cJSON_AddItemToObject(econet, "clock", clock);
            cJSON_AddItemToObject(g_config, "econet", econet);
        }
    }

    // Migrate old econet JSON file
    if (file_exists(OLD_ECONET_FILE))
    {
        ESP_LOGI(TAG, "Migrating Econet config from %s", OLD_ECONET_FILE);

        FILE *fp = fopen(OLD_ECONET_FILE, "r");
        if (fp)
        {
            fseek(fp, 0, SEEK_END);
            long size = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            char *buffer = malloc(size + 1);
            if (buffer)
            {
                fread(buffer, 1, size, fp);
                buffer[size] = '\0';
                fclose(fp);

                cJSON *old_econet = cJSON_Parse(buffer);
                free(buffer);

                if (old_econet)
                {
                    cJSON *econet = cJSON_GetObjectItem(g_config, "econet");
                    if (!econet)
                    {
                        econet = cJSON_CreateObject();
                        cJSON_AddItemToObject(g_config, "econet", econet);
                    }

                    // Copy station configs
                    cJSON *old_stations = cJSON_GetObjectItem(old_econet, "econetStations");
                    if (old_stations)
                        cJSON_AddItemToObject(econet, "localStations", cJSON_Duplicate(old_stations, 1));

                    cJSON *old_aun = cJSON_GetObjectItem(old_econet, "aunStations");
                    if (old_aun)
                        cJSON_AddItemToObject(econet, "remoteStations", cJSON_Duplicate(old_aun, 1));

                    // Migrate trunk settings
                    cJSON *old_trunk_net = cJSON_GetObjectItem(old_econet, "trunkOurNet");
                    cJSON *old_trunks = cJSON_GetObjectItem(old_econet, "trunks");

                    if (old_trunk_net || old_trunks)
                    {
                        cJSON *trunks = cJSON_CreateObject();

                        if (old_trunk_net && cJSON_IsNumber(old_trunk_net))
                            cJSON_AddNumberToObject(trunks, "ourNetwork", old_trunk_net->valueint);

                        if (old_trunks)
                        {
                            cJSON *uplinks = cJSON_CreateArray();
                            int trunk_idx = 0;

                            for (cJSON *item = old_trunks->child; item; item = item->next, trunk_idx++)
                            {
                                cJSON *uplink = cJSON_CreateObject();
                                cJSON *remote_ip = cJSON_GetObjectItem(item, "remote_ip");
                                cJSON *udp_port = cJSON_GetObjectItem(item, "udp_port");
                                cJSON *aes_key = cJSON_GetObjectItem(item, "aes_key");

                                if (remote_ip && cJSON_IsString(remote_ip))
                                    cJSON_AddStringToObject(uplink, "remoteIp", remote_ip->valuestring);
                                if (udp_port && cJSON_IsNumber(udp_port))
                                    cJSON_AddNumberToObject(uplink, "udpPort", udp_port->valueint);

                                // Migrate key to NVS
                                if (aes_key && cJSON_IsString(aes_key) && strlen(aes_key->valuestring) > 0)
                                {
                                    const char *key_str = aes_key->valuestring;
                                    size_t key_len = strlen(key_str);
                                    if (key_len > 32)
                                        key_len = 32;
                                    config_save_trunk_key(trunk_idx, (uint8_t *)key_str, key_len);
                                }

                                cJSON_AddItemToArray(uplinks, uplink);
                            }

                            cJSON_AddItemToObject(trunks, "uplinks", uplinks);
                        }

                        cJSON_AddItemToObject(g_config, "trunks", trunks);
                    }

                    cJSON_Delete(old_econet);
                }
            }
            else
            {
                fclose(fp);
            }
        }

        rename(OLD_ECONET_FILE, "/user/econet_cfg.bin.old");
    }

    esp_err_t ret = save_json_file(g_config);
    if (ret == ESP_OK)
        ESP_LOGI(TAG, "Migration completed successfully");

    return ret;
}

// ==============================================================================
// Initialization and Reload
// ==============================================================================

esp_err_t config_reload(void)
{
    // Check if migration needed
    if (!file_exists(CONFIG_FILE))
    {
        ESP_LOGI(TAG, "Config file not found, checking for old format");
        return migrate_old_config();
    }

    // Free old config
    if (g_config)
        cJSON_Delete(g_config);

    // Load new config
    g_config = load_json_file();
    if (!g_config)
    {
        ESP_LOGW(TAG, "No configuration found, using defaults");
        g_config = cJSON_CreateObject();
    }

    // Populate config_wifi from JSON
    load_wifi_from_json();

    return ESP_OK;
}

void config_init(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // Set defaults
    memset(&config_wifi, 0, sizeof(config_wifi));
    strcpy((char *)config_wifi.ap.ap.ssid, "nbreak-econet");
    config_wifi.ap_enabled = true;
    config_wifi.ap.ap.authmode = WIFI_AUTH_OPEN;
    config_wifi.ap.ap.max_connection = 3;

    // Load configuration
    config_reload();
}

// ==============================================================================
// Secret Management
// ==============================================================================

esp_err_t config_save_wifi_secrets(const char *sta_password, const char *ap_password)
{
    if (sta_password)
    {
        esp_err_t err = nvs_save_string(NVS_WIFI_STA_PASS, sta_password);
        if (err != ESP_OK)
            return err;
    }

    if (ap_password)
    {
        esp_err_t err = nvs_save_string(NVS_WIFI_AP_PASS, ap_password);
        if (err != ESP_OK)
            return err;
    }

    return ESP_OK;
}

esp_err_t config_save_trunk_key(int trunk_index, const uint8_t *key, size_t key_len)
{
    if (!key || key_len == 0 || key_len > 32)
        return ESP_ERR_INVALID_ARG;

    char nvs_key[32];
    snprintf(nvs_key, sizeof(nvs_key), "%s%d_key", NVS_TRUNK_KEY_PREFIX, trunk_index);

    uint8_t padded_key[32];
    memcpy(padded_key, key, key_len);
    if (key_len < 32)
        memset(padded_key + key_len, 0, 32 - key_len);

    return nvs_save_blob(nvs_key, padded_key, 32);
}

esp_err_t config_load_trunk_key(int trunk_index, uint8_t *key, size_t *key_len)
{
    if (!key || !key_len)
        return ESP_ERR_INVALID_ARG;

    char nvs_key[32];
    snprintf(nvs_key, sizeof(nvs_key), "%s%d_key", NVS_TRUNK_KEY_PREFIX, trunk_index);

    size_t len = 32;
    esp_err_t err = nvs_load_blob(nvs_key, key, &len);
    if (err == ESP_OK)
        *key_len = len;

    return err;
}

// ==============================================================================
// Configuration Iterators
// ==============================================================================

void config_foreach_local_station(config_local_station_iterator iter, void *ctx)
{
    if (!iter)
        return;

    cJSON *econet = config_get_econet();
    if (!econet)
        return;

    cJSON *local_stations = cJSON_GetObjectItem(econet, "localStations");
    if (!local_stations)
        return;

    for (cJSON *item = local_stations->child; item != NULL; item = item->next)
    {
        cJSON *station_id = cJSON_GetObjectItem(item, "station_id");
        cJSON *network_id = cJSON_GetObjectItem(item, "network_id");
        cJSON *udp_port = cJSON_GetObjectItem(item, "udp_port");

        if (cJSON_IsNumber(station_id) && station_id->valueint &&
            cJSON_IsNumber(udp_port) && udp_port->valueint)
        {
            config_econet_station_t station = {
                .station_id = station_id->valueint,
                .network_id = network_id && cJSON_IsNumber(network_id) ? network_id->valueint : 0,
                .local_udp_port = udp_port->valueint};
            iter(ctx, &station);
        }
    }
}

void config_foreach_remote_station(config_remote_station_iterator iter, void *ctx)
{
    if (!iter)
        return;

    cJSON *econet = config_get_econet();
    if (!econet)
        return;

    cJSON *remote_stations = cJSON_GetObjectItem(econet, "remoteStations");
    if (!remote_stations)
        return;

    for (cJSON *item = remote_stations->child; item != NULL; item = item->next)
    {
        cJSON *station_id = cJSON_GetObjectItem(item, "station_id");
        cJSON *network_id = cJSON_GetObjectItem(item, "network_id");
        cJSON *remote_ip = cJSON_GetObjectItem(item, "remote_ip");
        cJSON *udp_port = cJSON_GetObjectItem(item, "udp_port");

        if (cJSON_IsNumber(station_id) && station_id->valueint &&
            cJSON_IsString(remote_ip) &&
            cJSON_IsNumber(udp_port) && udp_port->valueint)
        {
            config_aun_station_t station = {
                .station_id = station_id->valueint,
                .network_id = network_id && cJSON_IsNumber(network_id) ? network_id->valueint : 0,
                .udp_port = udp_port->valueint};
            snprintf(station.remote_address, sizeof(station.remote_address), "%s", remote_ip->valuestring);
            iter(ctx, &station);
        }
    }
}

void config_foreach_trunk(config_trunk_iterator iter, void *ctx)
{
    if (!iter)
        return;

    cJSON *trunks_config = config_get_trunks();
    if (!trunks_config)
        return;

    cJSON *uplinks = cJSON_GetObjectItem(trunks_config, "uplinks");
    if (!uplinks)
        return;

    int trunk_idx = 0;
    for (cJSON *item = uplinks->child; item != NULL; item = item->next, trunk_idx++)
    {
        cJSON *remote_ip = cJSON_GetObjectItem(item, "remoteIp");
        cJSON *udp_port = cJSON_GetObjectItem(item, "udpPort");

        if (cJSON_IsString(remote_ip) && remote_ip->valuestring &&
            cJSON_IsNumber(udp_port) && udp_port->valueint)
        {
            config_trunk_t trunk = {
                .udp_port = udp_port->valueint,
                .key_len = 0};
            snprintf(trunk.remote_address, sizeof(trunk.remote_address), "%s", remote_ip->valuestring);

            // Load encryption key from NVS
            size_t key_len = 32;
            if (config_load_trunk_key(trunk_idx, trunk.key, &key_len) == ESP_OK)
            {
                trunk.key_len = key_len;
                iter(ctx, &trunk);
            }
            else
            {
                ESP_LOGW(TAG, "No encryption key found for trunk %d, skipping", trunk_idx);
            }
        }
    }
}

uint8_t config_get_trunk_network(void)
{
    cJSON *trunks = config_get_trunks();
    if (!trunks)
        return 0;

    cJSON *our_net = cJSON_GetObjectItem(trunks, "ourNetwork");
    if (our_net && cJSON_IsNumber(our_net))
        return (uint8_t)our_net->valueint;

    return 0;
}

// ==============================================================================
// Clock Helpers
// ==============================================================================

void config_get_econet_clock(config_econet_clock_t *clock)
{
    if (!clock)
        return;

    // Defaults
    clock->frequency_hz = 100000;
    clock->duty_pc = 50;
    clock->mode = ECONET_CLOCK_INTERNAL;
    clock->invert_clock = false;

    cJSON *econet = config_get_econet();
    if (!econet)
        return;

    cJSON *clk = cJSON_GetObjectItem(econet, "clock");
    if (!clk)
        return;

    cJSON *freq = cJSON_GetObjectItem(clk, "frequency");
    cJSON *duty = cJSON_GetObjectItem(clk, "duty");
    cJSON *mode = cJSON_GetObjectItem(clk, "mode");
    cJSON *invert = cJSON_GetObjectItem(clk, "invert");

    if (freq && cJSON_IsNumber(freq))
        clock->frequency_hz = freq->valueint;

    if (duty && cJSON_IsNumber(duty))
        clock->duty_pc = duty->valueint;

    if (mode && cJSON_IsString(mode))
    {
        if (strcmp(mode->valuestring, "external") == 0)
            clock->mode = ECONET_CLOCK_EXTERNAL;
    }

    if (invert && cJSON_IsBool(invert))
        clock->invert_clock = cJSON_IsTrue(invert);
}

void config_set_econet_clock(const config_econet_clock_t *clock)
{
    if (!clock || !g_config)
        return;

    cJSON *econet = config_get_econet();
    if (!econet)
    {
        econet = cJSON_CreateObject();
        cJSON_AddItemToObject(g_config, "econet", econet);
    }

    cJSON *clk = cJSON_CreateObject();
    cJSON_AddNumberToObject(clk, "frequency", clock->frequency_hz);
    cJSON_AddNumberToObject(clk, "duty", clock->duty_pc);
    cJSON_AddStringToObject(clk, "mode",
                            clock->mode == ECONET_CLOCK_INTERNAL ? "internal" : "external");
    cJSON_AddBoolToObject(clk, "invert", clock->invert_clock);

    cJSON_ReplaceItemInObject(econet, "clock", clk);
}

// ==============================================================================
// Web UI Helper
// ==============================================================================

esp_err_t config_save_econet(const cJSON *settings)
{
    if (!settings || !g_config)
        return ESP_ERR_INVALID_ARG;

    // Update econet section
    cJSON *econet_new = cJSON_GetObjectItem(settings, "econet");
    if (econet_new)
    {
        cJSON_ReplaceItemInObject(g_config, "econet", cJSON_Duplicate(econet_new, 1));
    }

    // Update trunks section
    cJSON *trunks_new = cJSON_GetObjectItem(settings, "trunks");
    if (trunks_new)
    {
        // Process uplinks to save encryption keys to NVS
        cJSON *uplinks = cJSON_GetObjectItem(trunks_new, "uplinks");
        if (uplinks && cJSON_IsArray(uplinks))
        {
            int trunk_idx = 0;
            for (cJSON *uplink = uplinks->child; uplink; uplink = uplink->next, trunk_idx++)
            {
                cJSON *aes_key = cJSON_GetObjectItem(uplink, "aesKey");
                if (aes_key && cJSON_IsString(aes_key) && strlen(aes_key->valuestring) > 0)
                {
                    const char *key_str = aes_key->valuestring;
                    size_t key_len = strlen(key_str);
                    if (key_len > 32)
                        key_len = 32;
                    config_save_trunk_key(trunk_idx, (uint8_t *)key_str, key_len);
                }
            }
        }

        // Store trunks config (without aesKey fields)
        cJSON *trunks_copy = cJSON_Duplicate(trunks_new, 1);
        uplinks = cJSON_GetObjectItem(trunks_copy, "uplinks");
        if (uplinks)
        {
            for (cJSON *uplink = uplinks->child; uplink; uplink = uplink->next)
            {
                cJSON_DeleteItemFromObject(uplink, "aesKey");
            }
        }
        cJSON_ReplaceItemInObject(g_config, "trunks", trunks_copy);
    }

    return config_save();
}
