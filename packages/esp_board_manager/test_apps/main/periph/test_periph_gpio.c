#include <stdio.h>
#include "esp_board_periph.h"
#include "driver/gpio.h"

void test_periph_gpio(void)
{
    /* Initialize GPIO peripherals */
    esp_err_t ret = esp_board_periph_init("gpio_power_amp");
    if (ret != ESP_OK) {
        printf("Failed to initialize GPIO power amp\n");
        return;
    }
    /* Get GPIO handles */
    void *gpio_power_amp_handle = NULL;
    ret = esp_board_periph_get_handle("gpio_power_amp", &gpio_power_amp_handle);
    if (ret != ESP_OK || !gpio_power_amp_handle) {
        printf("Failed to get GPIO power amp handle\n");
        goto cleanup_power_amp;
    }
    printf("GPIO power amp handle: %p\n", gpio_power_amp_handle);

    /* Show peripheral information */
    esp_board_periph_show("gpio_power_amp");

cleanup_power_amp:
    esp_board_periph_deinit("gpio_power_amp");
    printf("GPIO power amp deinitialized\n");
}
