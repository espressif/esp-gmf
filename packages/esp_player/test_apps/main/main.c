/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_board_manager_includes.h"
#include "esp_extractor_defaults.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_video_dec_default.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_app_unit_test.h"
#include "media_lib_adapter.h"

#include "render_common.h"

#define ESP_PLAYER_TASK_MAIN_PRIO  5

void *sdcard_handle = NULL;

void app_main(void)
{
    vTaskPrioritySet(NULL, ESP_PLAYER_TASK_MAIN_PRIO);
    media_lib_add_default_adapter();

    esp_extractor_register_default();
    esp_audio_dec_register_default();
    esp_audio_simple_dec_register_default();
    esp_video_dec_register_default();

    esp_gmf_app_setup_codec_dev(NULL);
    esp_gmf_app_setup_sdcard(&sdcard_handle);
#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT
    video_render_reconfig_lcd();
    esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD);
    if (video_render_test_app_setup() != ESP_PLAYER_ERR_OK) {
        ESP_LOGW("ESP_PLAYER_UT", "video_render_test_app_setup failed; video cases may skip or leak-check may fail");
    }
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT */
    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_gmf_app_test_main();
}
