#include "esp_log.h"
#include "esp_check.h"
#include "esp_board_device.h"
#include "dev_gpio_expander.h"
#include "freertos/FreeRTOS.h"
#include "esp_io_expander.h"

#define GPIO_EXPANDER_TEST IO_EXPANDER_PIN_NUM_1

static const char *TAG = "TEST_GPIO_EXPANDER";

void test_dev_gpio_expander(void)
{
    void *dev_cfg = NULL;
    esp_err_t ret = esp_board_device_get_config("gpio_expander", &dev_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get gpio_expander device config: %s", esp_err_to_name(ret));
        return;
    }

    void *dev_handle = NULL;
    ret = esp_board_device_get_handle("gpio_expander", &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get gpio_expander device handle: %s", esp_err_to_name(ret));
        return;
    }

    dev_io_expander_config_t *gpio_config = (dev_io_expander_config_t *)dev_cfg;
    esp_io_expander_handle_t *gpio_expander = (esp_io_expander_handle_t *)dev_handle;

    uint32_t pin_mask = 0xFFFF;
    for (uint32_t i = 0; i < gpio_config->max_pins; i++) {
        pin_mask = (1 << i);
        if (gpio_config->output_io_mask & pin_mask) {
            ESP_LOGI(TAG, "Pin %d is configured as output, set this io as TEST_IO", i);
            break;
        }
    }
    if (pin_mask == 0xFFFF) {
        ESP_LOGW(TAG, "No output io found, test pass");
        return;
    }

    uint32_t level_mask = 0;
    ESP_GOTO_ON_ERROR(esp_io_expander_get_level(*gpio_expander, pin_mask, &level_mask), err, TAG, "Failed to get io level");
    uint32_t initial_level = (level_mask & pin_mask) ? 1 : 0;
    ESP_LOGI(TAG, "TEST_IO initial io level: %d", (level_mask & pin_mask) ? 1 : 0);

    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "Trying to set TEST_IO to low");
    ESP_GOTO_ON_ERROR(esp_io_expander_set_level(*gpio_expander, pin_mask, 0), err, TAG, "Failed to set io level");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_GOTO_ON_ERROR(esp_io_expander_get_level(*gpio_expander, pin_mask, &level_mask), err, TAG, "Failed to get io level");
    ESP_LOGI(TAG, "TEST_IO level: %d", (level_mask & pin_mask) ? 1 : 0);

    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "Trying to set TEST_IO to high");
    ESP_GOTO_ON_ERROR(esp_io_expander_set_level(*gpio_expander, pin_mask, 1), err, TAG, "Failed to set io level");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_GOTO_ON_ERROR(esp_io_expander_get_level(*gpio_expander, pin_mask, &level_mask), err, TAG, "Failed to get io level");
    ESP_LOGI(TAG, "TEST_IO level: %d", (level_mask & pin_mask) ? 1 : 0);

    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "Trying to reset TEST_IO to initial level: %d", initial_level);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_GOTO_ON_ERROR(esp_io_expander_set_level(*gpio_expander, pin_mask, initial_level), err, TAG, "Failed to set io level");
    esp_io_expander_get_level(*gpio_expander, pin_mask, &level_mask);
    ESP_LOGI(TAG, "TEST_IO restored level: %d", (level_mask & pin_mask) ? 1 : 0);

    return;
err:
    ESP_LOGE(TAG, "Failed to test gpio_expander: %s", esp_err_to_name(ret));
    return;
}
