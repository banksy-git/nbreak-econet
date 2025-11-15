
#include "adlc.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/message_buffer.h"

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

adlc_stats_t adlc_stats;

static i2s_chan_handle_t rx_handle;
static i2s_chan_handle_t tx_handle;

// Callbacks
static adlc_config_t _cfg;


// Receiver bit state
static uint8_t _raw_shift_in;
static uint8_t _recv_data_shift_in;
static uint32_t _recv_data_bit;
static uint32_t is_frame_active;
static uint8_t rx_bytes[512];
static uint16_t rx_frame_len;

// Transmit state
static MessageBufferHandle_t tx_frame_buffer;
static uint8_t tx_frame[1024];
static uint8_t tx_bits[2048];

static inline uint16_t crc16_x25(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
        {
            crc = (crc & 0x0001) ? (uint16_t)((crc >> 1) ^ 0x8408)
                                 : (uint16_t)(crc >> 1);
        }
    }
    return (uint16_t)(crc ^ 0xFFFF);
}

//
// Transmitter
//

#define NEXT_BYTE_OR_RETURN               \
    do                                    \
    {                                     \
        q += 1;                           \
        frame_length++;                   \
        if (frame_length == out_capacity) \
            return false;                 \
        *q = 0;                           \
    } while (0)

#define NEXT_BIT_OR_RETURN       \
    do                           \
    {                            \
        out_bit_pos += 1;        \
        if (out_bit_pos == 8)    \
        {                        \
            NEXT_BYTE_OR_RETURN; \
            out_bit_pos = 0;     \
        }                        \
    } while (0)

bool _generate_frame_bits(uint8_t *out, size_t out_capacity, size_t *out_length, const uint8_t *payload, size_t payload_length)
{

    if (out_capacity < 2 || payload_length == 0 || out_length == NULL)
    {
        return false;
    }

    size_t frame_length = 0;
    uint8_t *q = out;

    // SOF FLAG
    *q = 0x7E;
    NEXT_BYTE_OR_RETURN;

    // Create bit-stuffed payload for transmission
    int out_bit_pos = 0;
    int one_count = 0;
    for (int i = 0; i < payload_length; i++)
    {

        uint8_t c = payload[i];
        for (int j = 0; j < 8; j++)
        {

            // Add bit to output
            // uint8_t bit = (c & 1) >> 7;
            // *q = (*q << 1) | bit;
            // c <<= 1;
            uint8_t bit = (c & 1);
            *q = (*q << 1) | bit;
            c >>= 1;
            if (bit != 0)
            {
                one_count += 1;
            }
            else
            {
                one_count = 0;
            }

            NEXT_BIT_OR_RETURN;

            // Bit stuffing
            if (one_count == 5)
            {
                one_count = 0;
                *q = (*q << 1) | 0;
                NEXT_BIT_OR_RETURN;
            }
        }
    }

    // Compute CRC over unstuffed payload bytes
    uint16_t fcs = crc16_x25(payload, payload_length);

    // Emit CRC (16 bits)
    uint8_t fcs_bytes[2] = {(uint8_t)(fcs & 0xFF), (uint8_t)(fcs >> 8)};
    for (int idx = 0; idx < 2; idx++)
    {
        uint8_t b = fcs_bytes[idx];
        for (int k = 0; k < 8; k++)
        { 

            uint8_t bit = (b >> k) & 1;
            *q = (uint8_t)((*q << 1) | bit);
            if (bit)
                one_count++;
            else
                one_count = 0;
            NEXT_BIT_OR_RETURN;
            if (one_count == 5)
            {
                one_count = 0;
                *q = (uint8_t)((*q << 1) | 0);
                NEXT_BIT_OR_RETURN;
            }
        }
    }

    // Add end flag without stuffing
    uint8_t c = 0x7e;
    for (int j = 0; j < 8; j++)
    {
        // Add bit to output
        *q = (*q << 1) | ((c & 0x80) >> 7);
        c <<= 1;
        NEXT_BIT_OR_RETURN;
    }

    // If last byte not complete, pad with ones
    if (out_bit_pos > 0)
    {
        while (out_bit_pos != 8)
        {
            *q = (*q << 1) | 1;
            out_bit_pos += 1;
        }
        NEXT_BYTE_OR_RETURN;
    }

    *out_length = frame_length;
    return true;
}


volatile DRAM_ATTR size_t tx_outstanding;
volatile DRAM_ATTR bool tx_arm;
bool IRAM_ATTR adlc_on_packet_sent(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx)
{
    if (tx_arm) {
        gpio_set_level(_cfg.data_driver_en_pin, 1);
        tx_arm = false;
    }
    tx_outstanding -= event->size/2;
    if (tx_outstanding==0) {
        gpio_set_level(_cfg.data_driver_en_pin, 0);
    }

    return false;
}

static void _tx_task(void *params)
{

    for(;;) {

        size_t tx_frame_length = xMessageBufferReceive(tx_frame_buffer, tx_frame, sizeof(tx_frame), portMAX_DELAY);

        // Convert to HDLC frame

        uint8_t* p = tx_bits;
        size_t capacity = sizeof(tx_bits);

    #define PADLEN 8

        for (int i=0; i<PADLEN; i++) {
            *p = 0x0F;
            capacity--;
            p++;
        }

        size_t txlen;
        if (!_generate_frame_bits(p, capacity, &txlen, tx_frame, tx_frame_length)) {
            ESP_LOGE("ADLC", "Failed to generate frame");
            continue;
        }
        p += txlen;

 
        // Pad frame to DMA size
        int pad_length = 8 - (txlen % 8);
        if (pad_length != 8) {
            while (pad_length--) {
                *p = 0xFF;
                p++;
                txlen++;
            }
        }

        for (int i=0; i<8; i++) {
            *p = 0xFF;
            p++;
            txlen++;
        }

        
        // Send data
        tx_outstanding = txlen+PADLEN-8;
        tx_arm = true;
        ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
        ESP_ERROR_CHECK(i2s_channel_write(tx_handle, tx_bits, txlen+PADLEN, NULL, portMAX_DELAY));

        while(tx_outstanding);
        ESP_ERROR_CHECK(i2s_channel_disable(tx_handle));

        adlc_stats.tx_frame_count++;


    }
}

void adlc_send(uint8_t *data, uint16_t length)
{
    xMessageBufferSend(tx_frame_buffer, data, length, portMAX_DELAY);
}

//
// Receiver
//
static inline void _push_frame()
{

    if (rx_frame_len < 6)
    {
        adlc_stats.rx_short_frame_count++;
        return;
    }

    uint32_t data_len = rx_frame_len - 2;

    // Check CRC
    uint16_t fcs = crc16_x25(rx_bytes, data_len);
    uint8_t fcs_bytes[2] = {(uint8_t)(fcs & 0xFF), (uint8_t)(fcs >> 8)};
    if (memcmp(rx_bytes + data_len, fcs_bytes, 2))
    {
        adlc_stats.rx_crc_fail_count++;
        return;
    }

    adlc_stats.rx_frame_count++;

    if (_cfg.on_frame_rx != NULL)
    {
        _cfg.on_frame_rx(rx_bytes, data_len, _cfg.user_ctx);
    }
}

static inline void _clk_bit(uint8_t c)
{

    _raw_shift_in = (_raw_shift_in << 1) | c;

    // Search for flag
    if (_raw_shift_in == 0x7e)
    {
        if (is_frame_active == 0)
        {
            is_frame_active = 1;
            _recv_data_bit = 0;
            rx_frame_len = 0;
        }
        else
        {
            is_frame_active = 0;
            _push_frame();
        }
        return;
    }

    if (is_frame_active == 0)
    {
        return;
    }

    // Search for ABORT
    if ((_raw_shift_in & 0x7f) == 0x7f)
    {
        is_frame_active = 0;
        adlc_stats.rx_abort_count++;
        return;
    }

    // Bit stuffing
    if ((_raw_shift_in & 0x3f) == 0x3e)
    {
        return;
    }

    // Data
    _recv_data_shift_in = (_recv_data_shift_in >> 1) | (c << 7); // Data is LSB first
    _recv_data_bit += 1;
    if (_recv_data_bit == 8)
    {

        rx_bytes[rx_frame_len] = _recv_data_shift_in;
        rx_frame_len += 1;
        if (rx_frame_len == sizeof(rx_bytes))
        {
            is_frame_active = 0;
            adlc_stats.rx_oversize_count++;
            return;
        }

        _recv_data_bit = 0;
    }
}

static void _rx_task(void *params)
{

    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

    while (1)
    {

        uint8_t buffer[8];
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(rx_handle, buffer, sizeof(buffer),
                                         &bytes_read, portMAX_DELAY);
        if (ret == ESP_OK)
        {
            for (int i = 0; i < bytes_read; i++)
            {
                uint8_t bits = buffer[i];
                for (int j = 0; j < 8; j++)
                {
                    _clk_bit((bits & 0x80) >> 7);
                    bits <<= 1;
                }
            }
        }
    }
}

void adlc_setup(adlc_config_t *config)
{
    _cfg = *config;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << config->data_driver_en_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = 0 ,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(config->data_driver_en_pin, 0);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_SLAVE);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 8;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(200000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_8BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = config->clk_pin,
            .ws = config->gnd_pin,
            .dout = config->data_out_pin,
            .din = config->data_in_pin,
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
        .on_sent = adlc_on_packet_sent
    };
    i2s_channel_register_event_callback(tx_handle, &callbacks, NULL);

    tx_frame_buffer = xMessageBufferCreate(4096);
}

void adlc_start()
{

    ESP_LOGW("ADLC", "Starting ADLC transciever");
    xTaskCreate(_rx_task, "adlc_rx", 2048, NULL, 1, NULL);
    xTaskCreate(_tx_task, "adlc_tx", 2048, NULL, 1, NULL);
}
