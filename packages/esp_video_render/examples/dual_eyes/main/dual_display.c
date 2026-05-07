/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "dual_display.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_gc9a01.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

static const char *TAG = "DUAL_DISPLAY";

#define BREAK_ON_ERR(err, fmt, ...)  if ((err) != ESP_OK) {  \
    ESP_LOGE(TAG, fmt, ##__VA_ARGS__);                       \
    break;                                                   \
}
#define MOUNT_PATH  "/sdcard"

static dual_display_t display[2];
static sdmmc_card_t *g_card = NULL;

esp_err_t dual_display_brightness_init(void)
{
    if (BSP_LCD_POWER != GPIO_NUM_NC) {
        gpio_reset_pin(BSP_LCD_POWER);
        gpio_set_direction(BSP_LCD_POWER, GPIO_MODE_OUTPUT);
        gpio_set_level(BSP_LCD_POWER, 1);
    }

    // Setup LEDC peripheral for PWM backlight control
    const ledc_channel_config_t LCD_backlight_channel = {
        .gpio_num = BSP_LCD_BL,
        .speed_mode = LCD_LEDC_SPEED_MODE,
        .channel = LCD_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = 1,
        .duty = 0,
        .hpoint = 0};
    const ledc_timer_config_t LCD_backlight_timer = {
        .speed_mode = LCD_LEDC_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = 1,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK};

    ESP_RETURN_ON_ERROR(ledc_timer_config(&LCD_backlight_timer), TAG, "LEDC timer config failed");
    ESP_RETURN_ON_ERROR(ledc_channel_config(&LCD_backlight_channel), TAG, "LEDC channel config failed");

    return ESP_OK;
}

esp_err_t dual_display_brightness_set(int brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = 100;
    }
    if (brightness_percent < 0) {
        brightness_percent = 0;
    }
    ESP_LOGI(TAG, "Setting LCD backlight: %d%%", brightness_percent);
    uint32_t duty_cycle = (1023 * brightness_percent) / 100;  // LEDC resolution set to 10bits, thus: 100% = 1023
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH, duty_cycle), TAG, "LEDC set duty failed");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH), TAG, "LEDC update duty failed");
    return ESP_OK;
}

esp_err_t dual_display_sdcard_init(void)
{
    esp_err_t ret;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 32 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = BSP_SDCARD_CLK;
    slot_config.cmd = BSP_SDCARD_CMD;
    slot_config.d0 = BSP_SDCARD_D0;
#if (BSP_SDCARD_D1 != -1)
    slot_config.width = 4;
    slot_config.d1 = BSP_SDCARD_D1;
    slot_config.d2 = BSP_SDCARD_D2;
    slot_config.d3 = BSP_SDCARD_D3;
#else
    slot_config.width = 1;
#endif  /* (BSP_SDCARD_D1 != -1) */

    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    ret = esp_vfs_fat_sdmmc_mount(MOUNT_PATH, &host, &slot_config, &mount_config, &g_card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                          "Make sure SD card lines have pull-up resistors in place.",
                     esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Filesystem mounted");
    return ESP_OK;
}

void dual_display_sdcard_deinit(void)
{
    if (g_card) {
        esp_vfs_fat_sdcard_unmount(MOUNT_PATH, g_card);
        g_card = NULL;
    }
}

esp_err_t dual_display_init(dual_display_t *out_disp)
{
    esp_err_t ret = ESP_OK;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = BSP_LCD_DC,
        .cs_gpio_num = BSP_LCD_CS_0,
        .pclk_hz = BSP_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 1,  // Just one transaction in queue, this will save memory for dram, but draw_bitmap will cost more time
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BSP_LCD_RST,
        .flags.reset_active_high = false,
        .color_space = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = NULL,  // If you want to use vendor specific init commands, please set it to the pointer of gc9107_vendor_config
    };
#if BSP_LCD_BIGENDIAN
    panel_cfg.color_space = LCD_RGB_ELEMENT_ORDER_BGR;
#endif  /* BSP_LCD_BIGENDIAN */
    do {
        if (display[0].lcd_handle) {
            ESP_LOGW(TAG, "Display already initialized");
            ret = ESP_FAIL;
            break;
        }
        if (dual_display_sdcard_init() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to mount sdcard");
            ret = ESP_FAIL;
            break;
        }
        const spi_bus_config_t buscfg = {
            .sclk_io_num = BSP_LCD_PCLK,
            .mosi_io_num = BSP_LCD_DATA0,
            .miso_io_num = GPIO_NUM_NC,
            .quadwp_io_num = GPIO_NUM_NC,
            .quadhd_io_num = GPIO_NUM_NC,
            .max_transfer_sz = TRANS_SIZE * sizeof(uint16_t),
            .isr_cpu_id = SPI_ISR_CPU_ID + 1,
        };
        ret = spi_bus_initialize(BSP_LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO);
        BREAK_ON_ERR(ret, "SPI init failed");

        ret = dual_display_brightness_init();
        BREAK_ON_ERR(ret, "Brightness init failed");
        dual_display_brightness_set(100);

        int16_t cs_gpio[] = {BSP_LCD_CS_0, BSP_LCD_CS_1};
        for (int i = 0; i < 2; i++) {
            io_cfg.cs_gpio_num = cs_gpio[i];
            ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM, &io_cfg, &display[i].io_handle);
            BREAK_ON_ERR(ret, "Fail to new panel IO for %d", i);
            ret = esp_lcd_new_panel_gc9a01(display[i].io_handle, &panel_cfg, &display[i].lcd_handle);
            BREAK_ON_ERR(ret, "Fail to new LCD for %d", i);
            esp_lcd_panel_reset(display[i].lcd_handle);
        }
        for (int i = 0; i < 2; i++) {
            ESP_ERROR_CHECK(esp_lcd_panel_init(display[i].lcd_handle));
            ESP_ERROR_CHECK(esp_lcd_panel_invert_color(display[i].lcd_handle, true));
            ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(display[i].lcd_handle, true));
        }
    } while (0);
    if (ret == ESP_OK) {
        out_disp[0] = display[0];
        out_disp[1] = display[1];
        return ESP_OK;
    }
    dual_display_deinit();
    return ret;
}

void dual_display_deinit(void)
{
    for (int i = 0; i < 2; i++) {
        if (display[i].lcd_handle) {
            ESP_ERROR_CHECK(esp_lcd_panel_del(display[i].lcd_handle));
            display[i].lcd_handle = NULL;
        }
        if (display[i].io_handle) {
            ESP_ERROR_CHECK(esp_lcd_panel_io_del(display[i].io_handle));
            display[i].io_handle = NULL;
        }
    }
    // Deinitialize brightness control
    ledc_stop(LCD_LEDC_SPEED_MODE, LCD_LEDC_CH, 0);
    // Free SPI bus
    spi_bus_free(BSP_LCD_SPI_NUM);
    dual_display_sdcard_deinit();
    ESP_LOGI(TAG, "Dual display deinitialize");
}
