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

#define ECONET_CLK_TMR_CHANNEL LEDC_TIMER_0
#define ECONET_CLK_PWM_CHANNEL LEDC_CHANNEL_0

econet_config_t econet_cfg;
econet_stats_t econet_stats;

void econet_clock_setup(void)
{

    // Clock OE and debug pin 18
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << econet_cfg.clk_oe_pin) | (1ULL << 18) | (1ULL << 19),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // PWM generation (LED module)
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = ECONET_CLK_TMR_CHANNEL,
        .duty_resolution = LEDC_TIMER_6_BIT,
        .freq_hz = econet_cfg.clk_freq_hz,
        .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .gpio_num = econet_cfg.clk_output_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = ECONET_CLK_PWM_CHANNEL,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

void econet_clock_reconfigure(void)
{
    config_econet_clock_t clock_cfg;
    config_load_econet_clock(&clock_cfg);

    if (clock_cfg.mode == ECONET_CLOCK_INTERNAL)
    {
        gpio_set_level(econet_cfg.clk_oe_pin, 1);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, ECONET_CLK_PWM_CHANNEL,
                      (64 * clock_cfg.duty_pc) / 100);
        ledc_set_freq(LEDC_LOW_SPEED_MODE, ECONET_CLK_TMR_CHANNEL,
                      clock_cfg.frequency_hz);
    }
    else if (clock_cfg.mode == ECONET_CLOCK_EXTERNAL)
    {
        gpio_set_level(econet_cfg.clk_oe_pin, 0);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, ECONET_CLK_PWM_CHANNEL, 0);
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << econet_cfg.clk_output_pin),
            .mode = GPIO_MODE_INPUT,
            .pull_down_en = 0,
            .pull_up_en = 0,
            .intr_type = GPIO_INTR_DISABLE};
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }

    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, ECONET_CLK_PWM_CHANNEL));
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
    econet_clock_reconfigure();
    econet_rx_start();
    econet_tx_start();
}

void econet_rx_shutdown(void)
{
    econet_rx_clear_bitmaps();
    econet_rx_packet_t shutdown_cmd = {.type = 'S'};
    xQueueSend(econet_rx_packet_queue, &shutdown_cmd, portMAX_DELAY);
}