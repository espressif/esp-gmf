/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2024 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "unity.h"
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_gmf_oal_mem.h"
#include "driver/sdmmc_host.h"
#include "vfs_fat_internal.h"

static const char *TAG = "ESP_GMF_UT_COM";

#define ESP_SD_PIN_CLK GPIO_NUM_15
#define ESP_SD_PIN_CMD GPIO_NUM_7
#define ESP_SD_PIN_D0  GPIO_NUM_4
#define ESP_SD_PIN_D1  -1
#define ESP_SD_PIN_D2  -1
#define ESP_SD_PIN_D3  -1
#define ESP_SD_PIN_D4  -1
#define ESP_SD_PIN_D5  -1
#define ESP_SD_PIN_D6  -1
#define ESP_SD_PIN_D7  -1
#define ESP_SD_PIN_CD  -1
#define ESP_SD_PIN_WP  -1

int verify_two_files(const char *src_path, const char *dest_path)
{
    int len = 4096;
    struct stat src_siz = {0};
    stat(src_path, &src_siz);
    FILE *src_file = fopen(src_path, "rb");
    TEST_ASSERT_NOT_NULL(src_file);
    char *src_buf = esp_gmf_oal_calloc(1, len);
    TEST_ASSERT_NOT_NULL(src_buf);
    ESP_LOGI(TAG, "The source file size is %ld, path:%s", src_siz.st_size, src_path);

    FILE *dest_file = fopen(dest_path, "rb");
    TEST_ASSERT_NOT_NULL(dest_file);
    struct stat dest_siz = {0};
    char *dest_buf = esp_gmf_oal_calloc(1, len);
    TEST_ASSERT_NOT_NULL(dest_buf);
    stat(dest_path, &dest_siz);
    ESP_LOGI(TAG, "The destination file size is %ld, path:%s", dest_siz.st_size, dest_path);

    TEST_ASSERT_EQUAL(src_siz.st_size, dest_siz.st_size);
    int result = ESP_OK;
    uint32_t pos = 0;
    uint32_t max_len = (dest_siz.st_size > src_siz.st_size ? dest_siz.st_size : src_siz.st_size);
    bool run = true;
    while (run) {
        fread(src_buf, 1, len, src_file);
        fread(dest_buf, 1, len, dest_file);
        for (int i = 0; i < len; ++i) {
            if (src_buf[i] != dest_buf[i]) {
                ESP_LOGE(TAG, "Unexepect data at:%ld, src:%x, dest:%x, max_len:%ld", pos, src_buf[i], dest_buf[i], max_len);
                result = ESP_FAIL;
                break;
            }
            pos++;
            if (pos == max_len) {
                run = false;
                break;
            }
        }
    }
    fclose(src_file);
    fclose(dest_file);
    esp_gmf_oal_free(src_buf);
    esp_gmf_oal_free(dest_buf);
    return result;
}

void esp_gmf_ut_setup_sdmmc(sdmmc_card_t **out_card)
{
    sdmmc_card_t *card = NULL;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;  // 1-bits
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024};
#if SOC_SDMMC_USE_GPIO_MATRIX
    slot_config.clk = ESP_SD_PIN_CLK;
    slot_config.cmd = ESP_SD_PIN_CMD;
    slot_config.d0 = ESP_SD_PIN_D0;
    slot_config.d1 = ESP_SD_PIN_D1;
    slot_config.d2 = ESP_SD_PIN_D2;
    slot_config.d3 = ESP_SD_PIN_D3;
    slot_config.d4 = ESP_SD_PIN_D4;
    slot_config.d5 = ESP_SD_PIN_D5;
    slot_config.d6 = ESP_SD_PIN_D6;
    slot_config.d7 = ESP_SD_PIN_D7;
    slot_config.cd = ESP_SD_PIN_CD;
    slot_config.wp = ESP_SD_PIN_WP;
#endif  /* SOC_SDMMC_USE_GPIO_MATRIX */
    TEST_ESP_OK(esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card));
    *out_card = card;
}

void esp_gmf_ut_teardown_sdmmc(sdmmc_card_t *card)
{
    TEST_ESP_OK(esp_vfs_fat_sdcard_unmount("/sdcard", card));
}
