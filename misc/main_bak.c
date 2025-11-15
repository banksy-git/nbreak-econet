
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

#include "soc/gpio_sig_map.h"
#include "soc/gpio_struct.h"
#include "soc/gpio_reg.h"

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"


#define CLK_PIN       18
#define DATA_IN_PIN   21
#define DATA_OUT_PIN  19
#define OE_PIN        22

#define PWM_PIN     2   // GPIO for PWM output
#define HIGH_PIN    3   // GPIO to be held high
#define PWM_FREQ_HZ 200000
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



static uint8_t _raw_shift_in;
static uint8_t _recv_data_shift_in;
static uint32_t _recv_data_bit;
static uint32_t is_frame_active;

#define RX_FRAMES_CAP  128
static uint16_t rx_frame_lens[RX_FRAMES_CAP];
static volatile uint32_t rx_frame_lens_w = 0;
static volatile uint32_t rx_frame_lens_r = 0;
static volatile uint16_t rx_frame_len;

#define RX_BYTES_CAP  1024
static uint8_t rx_bytes[RX_BYTES_CAP];
static volatile uint32_t rx_bytes_w = 0;
static uint32_t rx_bytes_w_tmp = 0;
static volatile uint32_t rx_bytes_r = 0;

static inline void _push_byte(uint8_t b) {
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

static inline bool _push_frame() {
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



static inline void clk_bit(uint8_t c){

    _raw_shift_in = (_raw_shift_in << 1) | c;

    // Search for flag
    if (_raw_shift_in==0x7e) {
        if (is_frame_active==0) {
            is_frame_active = 1;
            _recv_data_bit = 0;
            rx_bytes_w_tmp = rx_bytes_w;
            rx_frame_len = 0;
        } else {
            is_frame_active = 0;
            if (rx_frame_len>=4) {
                _push_frame();
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
        printf("ABRT\n");
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
        _push_byte(_recv_data_shift_in);
        _recv_data_bit = 0;

        // Long frame, probably something is wrong here
        if (rx_frame_len>512) {
            is_frame_active = 0;
            return;
        }
    }
    
}

i2s_chan_handle_t rx_handle;
i2s_chan_handle_t tx_handle;

void rx_reader()
{
    uint8_t byte = 0;
    int bit_count = 0;

    uint8_t row[32];
    char line[128];
    int row_idx = 0;
    uint16_t len;

    while(_pop_frame(&len)) {


        char *p = line;
        p += sprintf(line, "%d bytes:", len);
        for (int i=0;i<len && i<32;i++) {
            uint8_t b;
            _pop_byte(&b);
            p += sprintf(p, "%02X", b);
        }

        uint8_t tx_buffer[8] = {0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
        i2s_channel_write(tx_handle, tx_buffer, sizeof(tx_buffer), 
            NULL, 0);


        ESP_LOGI(TAG, "%s", line);
    }

}


bool IRAM_ATTR on_packet_sent(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx)
{
    return false;
}

void app_main(void)
{

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    
    //wifi_start();

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

    // 
    // Econet transciever (Abusing I2S for the win...)
    //
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_SLAVE);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 8;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(100000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_8BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CLK_PIN,
            .ws = 23,
            .dout = DATA_OUT_PIN,
            .din = DATA_IN_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));

    i2s_event_callbacks_t callbacks = {
        .on_sent = on_packet_sent
    };
    i2s_channel_register_event_callback(tx_handle, &callbacks, NULL);


    xTaskCreate(econet_rx_task, "econet_rx", 256, NULL, 1, NULL);

    while(1) {

        uint8_t buffer[8];
        size_t bytes_read = 0;
        ret = i2s_channel_read(rx_handle, buffer, sizeof(buffer), 
            &bytes_read, portMAX_DELAY);

        
        if (ret == ESP_OK) {

            for (int i=0; i<bytes_read;i++) {
                uint8_t bits = buffer[i];
                for (int j=0; j<8; j++) {
                    clk_bit((bits&0x80)>>7);
                    bits<<=1;
                }
            }
        }

        rx_reader();

        
     
    }

        

}
