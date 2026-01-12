/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include "esp_log.h"
#include "esp_board_device.h"
#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#include "dev_fs_fat.h"
#ifdef CONFIG_ESP_BOARD_DEV_FS_FAT_SUB_SDMMC_SUPPORT
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#endif  /* CONFIG_ESP_BOARD_DEV_FS_FAT_SUB_SDMMC_SUPPORT */

static const char *TAG = "TEST_FS_FAT";

void test_fs_fat_device(void)
{
    const char *test_str = "Hello FS_FAT!\n";
    char read_buf[64] = {0};
    const char *test_filename = "/sdcard/test_fs_fat.txt";
    char *device_name = NULL;
    ESP_LOGI(TAG, "=== Starting FS_FAT Device Tests ===");
    ESP_LOGI(TAG, "=== Testing FS_FAT Device ===");

    /* Initialize FS_FAT device */
    esp_err_t ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_FS_FAT);
    if (ret != ESP_OK) {
        ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize FS_FAT device");
            return;
        } else {
            device_name = ESP_BOARD_DEVICE_NAME_FS_SDCARD;  // Use the main device name
        }
    } else {
        device_name = ESP_BOARD_DEVICE_NAME_FS_FAT;  // Use the main device name
    }

    /* Test FS_FAT functionality */
    ESP_LOGI(TAG, "FS_FAT device initialized successfully");

    /* Get device handle and show device information */
    void *device_handle = NULL;
    ret = esp_board_manager_get_device_handle(device_name, &device_handle);
    if (ret == ESP_OK && device_handle) {
        ESP_LOGI(TAG, "Device handle obtained successfully");
        sdmmc_card_print_info(stdout, ((dev_fs_fat_handle_t *)device_handle)->card);
    } else {
        ESP_LOGE(TAG, "Failed to get device handle");
    }

    /* Show device information */
    esp_board_device_show(device_name);

    /* Test file write operation */
    FILE *f = fopen(test_filename, "w");
    if (f) {
        fprintf(f, "%s", test_str);
        fclose(f);
        ESP_LOGI(TAG, "Test file written successfully");
    } else {
        ESP_LOGE(TAG, "Failed to create test file: %s", strerror(errno));
        goto cleanup;
    }

    /* Test file read operation */
    f = fopen(test_filename, "r");
    if (f) {
        size_t bytes_read = fread(read_buf, 1, sizeof(read_buf) - 1, f);
        read_buf[bytes_read] = '\0';  // Ensure null termination
        fclose(f);
        ESP_LOGI(TAG, "Read from file: %s", read_buf);

        /* Compare written and read data */
        if (strcmp(test_str, read_buf) == 0) {
            ESP_LOGI(TAG, "File content verification passed");
        } else {
            ESP_LOGE(TAG, "File content verification failed!");
            ESP_LOGE(TAG, "Expected: %s", test_str);
            ESP_LOGE(TAG, "Got: %s", read_buf);
        }
    } else {
        ESP_LOGE(TAG, "Failed to read test file: %s", strerror(errno));
    }

    /* List directory contents */
    ESP_LOGI(TAG, "FS_FAT root directory contents:");
    DIR *dir = opendir("/sdcard");
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            struct stat st;
            char fullpath[300];
            snprintf(fullpath, sizeof(fullpath), "/sdcard/%s", entry->d_name);

            if (stat(fullpath, &st) == 0) {
                ESP_LOGI(TAG, "%s - %ld bytes", entry->d_name, st.st_size);
            } else {
                ESP_LOGI(TAG, "%s", entry->d_name);
            }
        }
        closedir(dir);
    } else {
        ESP_LOGE(TAG, "Failed to open directory: %s", strerror(errno));
    }

cleanup:
    /* Cleanup */
    esp_board_device_deinit(device_name);
    ESP_LOGI(TAG, "FS_FAT device deinitialized");
    ESP_LOGI(TAG, "=== FS_FAT Device Tests Complete ===");
}
