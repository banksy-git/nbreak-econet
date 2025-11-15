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
#include "driver/ledc.h"
#include "driver/gpio.h"

#include "config.h"
#define ECONET_PRIVATE_API
#include "econet.h"

econet_config_t econet_cfg;
econet_stats_t econet_stats;
uint8_t DRAM_ATTR econet_station_id = 254;

void econet_clock_setup(void)
{
    if (econet_cfg.clk_oe_pin != -1)
    {
        ledc_timer_config_t ledc_timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .timer_num = LEDC_TIMER_0,
            .duty_resolution = LEDC_TIMER_8_BIT,
            .freq_hz = econet_cfg.clk_freq_hz,
            .clk_cfg = LEDC_AUTO_CLK};
        ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

        ledc_channel_config_t ledc_channel = {
            .gpio_num = econet_cfg.clk_output_pin,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0,
            .timer_sel = LEDC_TIMER_0,
            .duty = (255 * 50) / 100, // 50% duty
            .hpoint = 0,
            .flags.output_invert = 0};
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    }

    if (econet_cfg.clk_oe_pin != -1)
    {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << econet_cfg.clk_oe_pin) | (1ULL << 12),
            .mode = GPIO_MODE_OUTPUT,
            .pull_down_en = 0,
            .pull_up_en = 0,
            .intr_type = GPIO_INTR_DISABLE};
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        gpio_set_level(econet_cfg.clk_oe_pin, econet_cfg.clk_oe_pin != -1 ? 1 : 0);
    }
}

void econet_clock_start(void)
{
    if (econet_cfg.clk_oe_pin != -1)
    {
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    }
}

void econet_setup(const econet_config_t *config)
{
    econet_cfg = *config;

    if (econet_cfg.clk_freq_hz == 0)
    {
        econet_cfg.clk_freq_hz = 100000;
    }

    econet_clock_setup();
    econet_rx_setup();
    econet_tx_setup();
}

void econet_start(void)
{
    ESP_LOGI(TAG, "Starting ADLC transciever");
    econet_reconfigure();
    econet_clock_start();
    econet_rx_start();
    econet_tx_start();
}

void econet_reconfigure(void)
{
    econet_station_id = config_econet.this_station_id;
}