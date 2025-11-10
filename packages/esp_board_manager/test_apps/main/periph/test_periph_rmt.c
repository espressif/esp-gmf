/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

/**
 * @brief  Periph RMT test
 *
 *         This test example can be used to test the rmt peripheral that has been initialized through the board manager.
 *         Connect a WS2812 LED strip to the RMT TX GPIO you configured, change the LED number by EXAMPLE_LED_NUMBERS.
 *         You should see a rainbow chasing demonstration pattern for
 */

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_board_periph.h"
#include "periph_rmt.h"

#define EXAMPLE_LED_NUMBERS       28
#define EXAMPLE_FRAME_DURATION_MS 10
#define EXAMPLE_ANGLE_INC_FRAME   0.02
#define EXAMPLE_ANGLE_INC_LED     0.3
#define EXAMPLE_TEST_SECONDS      5

static const char *TAG = "TEST_RMT";

static uint32_t channel_resolution_hz = 0;
static uint8_t  led_strip_pixels[EXAMPLE_LED_NUMBERS * 3];

static rmt_symbol_word_t ws2812_zero = {
    .level0 = 1,
    .level1 = 0,
};

static rmt_symbol_word_t ws2812_one = {
    .level0 = 1,
    .level1 = 0,
};

// reset defaults to 50uS
static rmt_symbol_word_t ws2812_reset = {
    .level0 = 0,
    .level1 = 0,
};

static size_t encoder_callback(const void *data, size_t data_size,
                               size_t symbols_written, size_t symbols_free,
                               rmt_symbol_word_t *symbols, bool *done, void *arg)
{
    // We need a minimum of 8 symbol spaces to encode a byte. We only
    // need one to encode a reset, but it's simpler to simply demand that
    // there are 8 symbol spaces free to write anything.
    if (symbols_free < 8) {
        return 0;
    }

    // We can calculate where in the data we are from the symbol pos.
    // Alternatively, we could use some counter referenced by the arg
    // parameter to keep track of this.
    size_t data_pos = symbols_written / 8;
    uint8_t *data_bytes = (uint8_t *)data;
    if (data_pos < data_size) {
        // Encode a byte
        size_t symbol_pos = 0;
        for (int bitmask = 0x80; bitmask != 0; bitmask >>= 1) {
            if (data_bytes[data_pos] & bitmask) {
                symbols[symbol_pos++] = ws2812_one;
            } else {
                symbols[symbol_pos++] = ws2812_zero;
            }
        }
        // We're done; we should have written 8 symbols.
        return symbol_pos;
    } else {
        // All bytes already are encoded.
        // Encode the reset, and we're done.
        symbols[0] = ws2812_reset;
        *done = 1;  // Indicate end of the transaction.
        return 1;   // we only wrote one symbol
    }
}

void test_periph_rmt()
{
    periph_rmt_handle_t *rmt_handle = NULL;
    esp_err_t ret = esp_board_periph_get_handle("rmt_tx", (void **)&rmt_handle);
    if (ret != ESP_OK || rmt_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get RMT handle");
        return;
    }

    periph_rmt_config_t *rmt_cfg = {0};
    ret = esp_board_periph_get_config("rmt_tx", (void *)&rmt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get RMT config %s", esp_err_to_name(ret));
        return;
    }

    rmt_channel_handle_t led_chan = rmt_handle->channel;
    channel_resolution_hz = rmt_cfg->channel_config.tx.resolution_hz;

    ws2812_zero.duration0 = 0.3 * channel_resolution_hz / 1000000;  // T0H=0.3us
    ws2812_zero.duration1 = 0.9 * channel_resolution_hz / 1000000;  // T0L=0.9us

    ws2812_one.duration0 = 0.9 * channel_resolution_hz / 1000000;  // T1H=0.9us
    ws2812_one.duration1 = 0.3 * channel_resolution_hz / 1000000;  // T1L=0.3us

    ws2812_reset.duration0 = channel_resolution_hz / 1000000 * 50 / 2;
    ws2812_reset.duration1 = channel_resolution_hz / 1000000 * 50 / 2;

    ESP_LOGI(TAG, "Create simple callback-based encoder");
    rmt_encoder_handle_t simple_encoder = NULL;
    const rmt_simple_encoder_config_t simple_encoder_cfg = {
        .callback = encoder_callback
        // Note we don't set min_chunk_size here as the default of 64 is good enough.
    };
    ESP_ERROR_CHECK(rmt_new_simple_encoder(&simple_encoder_cfg, &simple_encoder));

    ESP_LOGI(TAG, "Enable RMT TX channel");
    ESP_ERROR_CHECK(rmt_enable(led_chan));

    ESP_LOGI(TAG, "Start LED rainbow chase");
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,  // no transfer loop
    };
    float offset = 0;
    const float color_off = (M_PI * 2) / 3;

    int test_cnt = 1000 / EXAMPLE_FRAME_DURATION_MS * EXAMPLE_TEST_SECONDS;
    while (test_cnt--) {
        for (int led = 0; led < EXAMPLE_LED_NUMBERS; led++) {
            // Build RGB pixels. Each color is an offset sine, which gives a
            // hue-like effect.
            float angle = offset + (led * EXAMPLE_ANGLE_INC_LED);
            led_strip_pixels[led * 3 + 0] = sin(angle + color_off * 0) * 127 + 128;
            led_strip_pixels[led * 3 + 1] = sin(angle + color_off * 1) * 127 + 128;
            led_strip_pixels[led * 3 + 2] = sin(angle + color_off * 2) * 117 + 128;
            ;
        }
        // Flush RGB values to LEDs
        ESP_ERROR_CHECK(rmt_transmit(led_chan, simple_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
        vTaskDelay(pdMS_TO_TICKS(EXAMPLE_FRAME_DURATION_MS));
        // Increase offset to shift pattern
        offset += EXAMPLE_ANGLE_INC_FRAME;
        if (offset > 2 * M_PI) {
            offset -= 2 * M_PI;
        }
    }

    ESP_ERROR_CHECK(rmt_disable(led_chan));
}
