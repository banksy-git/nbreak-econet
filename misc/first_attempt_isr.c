#include "driver/ledc.h"
#include "driver/gpio.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_private/esp_clk.h"
#include "esp_cpu.h"   
#include "esp_log.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "esp_pm.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "soc/gpio_struct.h"
#include "soc/gpio_reg.h"

#include "driver/gpio.h"

#define CLK_PIN       18
#define DAT_IN_PIN    21
#define DATA_OUT_PIN  19
#define OE_PIN        22

#define PWM_PIN     2   // GPIO for PWM output
#define HIGH_PIN    3   // GPIO to be held high
#define PWM_FREQ_HZ 100000
#define PWM_DUTY    50  // 50%

static const char *WIFI_SSID = "iot.cairparavel";
static const char *WIFI_PASS = "yAN85KMQJswxu9fB";
static const char *TAG = "APP";
static EventGroupHandle_t s_wifi_ev;
#define WIFI_CONNECTED_BIT BIT0


static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT)
    {
        if (id == WIFI_EVENT_STA_START)
        {
            esp_wifi_connect();
        }
        else if (id == WIFI_EVENT_STA_DISCONNECTED)
        {
            ESP_LOGW(TAG, "WiFi disconnected, retrying…");
            esp_wifi_connect();
            xEventGroupClearBits(s_wifi_ev, WIFI_CONNECTED_BIT);
        }
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        xEventGroupSetBits(s_wifi_ev, WIFI_CONNECTED_BIT);
    }
}

static void wifi_start(void)
{
    s_wifi_ev = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, WIFI_SSID, sizeof(cfg.sta.ssid));
    strncpy((char *)cfg.sta.password, WIFI_PASS, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}



static DRAM_ATTR uint8_t _raw_shift_in;
static DRAM_ATTR uint8_t _recv_data_shift_in;
static DRAM_ATTR uint32_t _recv_data_bit;
static DRAM_ATTR uint32_t is_frame_active;

#define RX_FRAMES_CAP  128
static DRAM_ATTR uint16_t rx_frame_lens[RX_FRAMES_CAP];
static DRAM_ATTR volatile uint32_t rx_frame_lens_w = 0;
static DRAM_ATTR volatile uint32_t rx_frame_lens_r = 0;
static DRAM_ATTR volatile uint16_t rx_frame_len;

#define RX_BYTES_CAP  1024
static DRAM_ATTR uint8_t rx_bytes[RX_BYTES_CAP];
static DRAM_ATTR volatile uint32_t rx_bytes_w = 0;
static DRAM_ATTR uint32_t rx_bytes_w_tmp = 0;
static DRAM_ATTR volatile uint32_t rx_bytes_r = 0;

static inline void IRAM_ATTR _push_byte_isr(uint8_t b) {
    if ((rx_bytes_w_tmp - rx_bytes_r) >= (RX_BYTES_CAP - 1u)) return; // full
    rx_bytes[rx_bytes_w_tmp & (RX_BYTES_CAP - 1u)] = b;
    rx_bytes_w_tmp += 1;
    rx_frame_len += 1;
}

static inline bool _pop_byte(uint8_t *out) {
    if (rx_bytes_w == rx_bytes_r) return false;
    *out = rx_bytes[rx_bytes_r & (RX_BYTES_CAP - 1u)];
    rx_bytes_r += 1;
    return true;
}

static inline bool IRAM_ATTR _push_frame_isr() {
    if ((rx_frame_lens_w - rx_frame_lens_r) >= (RX_FRAMES_CAP - 1u)) return false;
    rx_frame_lens[rx_frame_lens_w & (RX_FRAMES_CAP - 1u)] = rx_frame_len;
    rx_frame_lens_w += 1;
    rx_bytes_w = rx_bytes_w_tmp; // Update frame position
    return true;
}

static inline bool _pop_frame(uint16_t *out) {
    if (rx_frame_lens_w == rx_frame_lens_r) return false;
    *out = rx_frame_lens[rx_frame_lens_r & (RX_FRAMES_CAP - 1u)];
    rx_frame_lens_r += 1;
    return true;
}


volatile uint32_t last_isr_cycles;

int64_t isr_t0;

static void IRAM_ATTR clk_data_in(void*){

    uint8_t c = (GPIO.in.val >> DAT_IN_PIN) & 1;
    _raw_shift_in = (_raw_shift_in << 1) | c;

    // Search for flag
    if (_raw_shift_in==0x7e) {
        if (is_frame_active==0) {
            is_frame_active = 1;
            _recv_data_bit = 0;
            rx_bytes_w_tmp = rx_bytes_w;
            rx_frame_len = 0;
            isr_t0 = esp_cpu_get_cycle_count();
        } else {
            is_frame_active = 0;
            if (rx_frame_len>=4) {
                _push_frame_isr();
                int64_t t1 = esp_cpu_get_cycle_count();
                last_isr_cycles = (int32_t)(t1 - isr_t0);
            }
        }
        return;
    }

    if (is_frame_active==0) {
        return;
    }

    // Search for ABORT
    if ((_raw_shift_in & 0x7f) == 0x7f) {
        is_frame_active = 0;
        return;
    }

    // Bit stuffing
    if ((_raw_shift_in&0x3f)==0x3e) {
        return;
    }
    
    // Data
    _recv_data_shift_in = (_recv_data_shift_in>>1) | (c<<7); // Data is LSB first
    _recv_data_bit += 1;
    if (_recv_data_bit==8) {
        _push_byte_isr(_recv_data_shift_in);
        _recv_data_bit = 0;

        // Long frame, probably something is wrong here
        if (rx_frame_len>512) {
            is_frame_active = 0;
            return;
        }
    }
    
   
    
}

void rx_reader_task(void *arg)
{
    uint8_t byte = 0;
    int bit_count = 0;

    uint8_t row[32];
    char line[128];
    int row_idx = 0;

    while (1) {

        uint16_t len;
        if (!_pop_frame(&len)) {
            vTaskDelay(pdMS_TO_TICKS(1));  // no new bits yet
            continue;
        }

        double isr_us = (double)last_isr_cycles * 1e6 / esp_clk_cpu_freq(); // freq in Hz

        char *p = line;
        p += sprintf(line, "%d bytes %.2f:", len, isr_us);
        for (int i=0;i<len && i<32;i++) {
            uint8_t b;
            _pop_byte(&b);
            p += sprintf(p, "%02X", b);
        }

        ESP_LOGI(TAG, "%s", line);
    }
}

void app_main(void)
{

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    
    wifi_start();


    //
    // Start Econet Clock
    //
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_8_BIT, // 8-bit resolution
        .freq_hz          = PWM_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .gpio_num       = PWM_PIN,
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .duty           = (255 * PWM_DUTY) / 100, // 50% duty
        .hpoint         = 0,
        .flags.output_invert = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // --- Configure GPIO3 as output and set high ---
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << HIGH_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    gpio_set_level(HIGH_PIN, 1);

    // --- Start PWM ---
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));


    // Pins
    gpio_config_t io = {
        .pin_bit_mask = (1ULL<<CLK_PIN) | (1ULL<<DAT_IN_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = 0, .pull_up_en = 0,
        .intr_type = GPIO_INTR_POSEDGE
    };
    gpio_config(&io);

    gpio_config_t out = {
        .pin_bit_mask = (1ULL<<DATA_OUT_PIN) | (1ULL<<OE_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0, .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&out);

    gpio_set_level(OE_PIN, 0);
    gpio_set_level(DATA_OUT_PIN, 1);

    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    gpio_isr_handler_add(CLK_PIN, clk_data_in, NULL);

    xTaskCreatePinnedToCore(rx_reader_task, "rx_reader", 4096, NULL, 4, NULL, 0);

}
