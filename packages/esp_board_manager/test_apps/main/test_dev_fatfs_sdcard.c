#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include "esp_log.h"
#include "esp_board_device.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#ifdef CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SUPPORT
#include "dev_fatfs_sdcard.h"
#endif  /* CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SUPPORT */
#ifdef CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SPI_SUPPORT
#include "dev_fatfs_sdcard_spi.h"
#endif  /* CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SPI_SUPPORT */

static const char *TAG = "TEST_SDCARD";

void test_sdcard(void)
{
    const char *test_str = "Hello SD Card!\n";
    char read_buf[64] = {0};

    /* Initialize SD card device */
    esp_err_t ret = esp_board_device_init("fs_sdcard");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SD card");
        return;
    }

    /* Test SD card functionality */
    ESP_LOGI(TAG, "SD card initialized successfully");

    /* Show device information */
    esp_board_device_show("fs_sdcard");

    /* Test file write operation */
    FILE *f = fopen("/sdcard/test.txt", "w");
    if (f) {
        fprintf(f, "%s", test_str);
        fclose(f);
        ESP_LOGI(TAG, "Test file written successfully");
    } else {
        ESP_LOGE(TAG, "Failed to create test file: %s", strerror(errno));
        goto cleanup;
    }

    /* Test file read operation */
    f = fopen("/sdcard/test.txt", "r");
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

#ifdef CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SUPPORT
    dev_fatfs_sdcard_handle_t *fs_sdcard_handle = NULL;
#endif
#ifdef CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SPI_SUPPORT
    dev_fatfs_sdcard_spi_handle_t *fs_sdcard_handle = NULL;
#endif
    ret = esp_board_device_get_handle("fs_sdcard", (void **)&fs_sdcard_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SD card handle");
        return;
    }
    /* Print SD card information */
    sdmmc_card_print_info(stdout, fs_sdcard_handle->card);

    /* List directory contents */
    ESP_LOGI(TAG, "SD card root directory contents:");
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
    esp_board_device_deinit("fs_sdcard");
    ESP_LOGI(TAG, "SD card deinitialized");
}
