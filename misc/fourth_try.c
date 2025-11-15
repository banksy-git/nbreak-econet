
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
#include "driver/parlio_tx.h"

adlc_stats_t adlc_stats;

static i2s_chan_handle_t rx_handle;
static parlio_tx_unit_handle_t tx_unit = NULL;

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
static uint8_t tx_bits[8192];
static uint32_t tx_write_byte_pos;
static uint32_t tx_write_bit_pos;
static uint8_t tx_write_one_count;

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

static inline void _add_raw_bit(uint8_t b)
{
    tx_bits[tx_write_byte_pos] = (tx_bits[tx_write_byte_pos] << 2) | b;
    tx_write_bit_pos += 2;
    if (tx_write_bit_pos >= 8)
    {
        tx_write_bit_pos = 0;
        tx_write_byte_pos++;
    }
}

static inline void _add_bit(uint8_t bit)
{
    _add_raw_bit((bit ? 1 : 0) | 2);
}

static inline void _add_byte_unstuffed(uint8_t c)
{
    for (int j = 0; j < 8; j++)
    {
        // _add_bit((c&0x80)>>7);
        // c <<= 1;
        _add_bit(c & 1);
        c >>= 1;
    }
}

static inline void _add_byte_stuffed(uint8_t c)
{

    for (int j = 0; j < 8; j++)
    {
        // Add bit to output
        // uint8_t bit = (c&0x80)>>7;
        // c <<= 1;
        uint8_t bit = (c & 1);
        c >>= 1;

        _add_bit(bit);
        if (bit != 0)
        {
            tx_write_one_count += 1;
        }
        else
        {
            tx_write_one_count = 0;
        }

        // Bit stuffing
        if (tx_write_one_count == 5)
        {
            _add_bit(0);
            tx_write_one_count = 0;
        }
    }
}

void _generate_frame_bits(const uint8_t *payload, size_t payload_length)
{
    tx_write_byte_pos = 0;
    tx_write_bit_pos = 0;
    tx_write_one_count = 0;

    _add_byte_unstuffed(0x7e);

    for (int i = 0; i < payload_length; i++)
    {
        _add_byte_stuffed(payload[i]);
    }

    // Compute CRC over unstuffed payload bytes
    uint16_t fcs = crc16_x25(payload, payload_length);

    // Emit CRC (16 bits)
    uint8_t fcs_bytes[2] = {(uint8_t)(fcs & 0xFF), (uint8_t)(fcs >> 8)};
    for (int i = 0; i < 2; i++)
    {
        _add_byte_stuffed(fcs_bytes[i]);
    }

    _add_byte_unstuffed(0x7e);

    // Complete the byte
    if (tx_write_bit_pos > 0)
    {
        _add_raw_bit(0);
    }

    // Tail with zero
    for (int j = 0; j < 8; j++)
    {
        _add_raw_bit(0);
    }
}

volatile DRAM_ATTR size_t tx_outstanding;
volatile DRAM_ATTR bool tx_arm;
bool IRAM_ATTR adlc_on_packet_sent(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx)
{
    if (tx_arm)
    {
        gpio_set_level(_cfg.data_driver_en_pin, 1);
        tx_arm = false;
    }
    tx_outstanding -= event->size / 2;
    if (tx_outstanding == 0)
    {
        gpio_set_level(_cfg.data_driver_en_pin, 0);
    }

    return false;
}

static void _tx_task(void *params)
{

    for (;;)
    {

        size_t tx_frame_length = xMessageBufferReceive(tx_frame_buffer, tx_frame, sizeof(tx_frame), portMAX_DELAY);

        // Convert to HDLC bitstream
        _generate_frame_bits(tx_frame, tx_frame_length);

        // ESP_LOG_BUFFER_HEXDUMP("ADLC", tx_bits, tx_write_byte_pos, ESP_LOG_INFO);
        // ESP_LOGI("ADLC", "Send txlen %d", tx_write_byte_pos);

        // Configure TX unit transmission parameters
        parlio_transmit_config_t transmit_config = {
            .idle_value = 0x0,
        };

        // Start Totransmission transaction
        ESP_ERROR_CHECK(parlio_tx_unit_transmit(tx_unit, tx_bits, tx_write_byte_pos * 16, &transmit_config));
        parlio_tx_unit_wait_all_done(tx_unit, -1);

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

        uint8_t buffer[10];
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

    // RX - Uses I2S peripheral
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_SLAVE);
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 8;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(200000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_8BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = config->clk_pin,
            .ws = config->gnd_pin,
            .dout = I2S_GPIO_UNUSED,
            .din = config->data_in_pin,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));

    // TX - PAR-IO peripheral
    parlio_tx_unit_config_t tx_config = {
        .clk_src = PARLIO_CLK_SRC_EXTERNAL, // Select external clock source
        .data_width = 2,
        .clk_in_gpio_num = config->clk_pin, // Set external clock source input pin
        .input_clk_src_freq_hz = 200000,    // External clock source frequen
        .valid_gpio_num = -1,
        .clk_out_gpio_num = -1,
        .data_gpio_nums = {
            config->data_out_pin,
            config->data_driver_en_pin,
        },
        .output_clk_freq_hz = 200000,
        .trans_queue_depth = 8,
        .max_transfer_size = 128,
        .sample_edge = PARLIO_SAMPLE_EDGE_POS,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_MSB,

    };
    // Create TX unit instance
    ESP_ERROR_CHECK(parlio_new_tx_unit(&tx_config, &tx_unit));
    ESP_ERROR_CHECK(parlio_tx_unit_enable(tx_unit));

    tx_frame_buffer = xMessageBufferCreate(4096);
}

void adlc_start()
{

    ESP_LOGW("ADLC", "Starting ADLC transciever");
    xTaskCreate(_rx_task, "adlc_rx", 2048, NULL, 1, NULL);
    xTaskCreate(_tx_task, "adlc_tx", 2048, NULL, 1, NULL);
}
