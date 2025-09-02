#include <stdio.h>
#include "esp_board_periph.h"
#include "driver/i2c_master.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/i2c_master.h"

void test_periph_i2c(void)
{
    /* Initialize I2C peripheral */
    esp_err_t ret = esp_board_periph_init("i2c_master");
    if (ret != ESP_OK) {
        printf("Failed to initialize I2C master peripheral\n");
        return;
    }

    /* Get I2C handle */
    void *i2c_handle = NULL;
    ret = esp_board_periph_get_handle("i2c_master", &i2c_handle);
    if (ret != ESP_OK || !i2c_handle) {
        printf("Failed to get I2C master handle\n");
        goto cleanup;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x30 >> 1,
        .scl_speed_hz = 400000,
    };
    i2c_master_dev_handle_t dev_handle = NULL;
    ret = i2c_master_bus_add_device(i2c_handle, &dev_config, &dev_handle);
    if (ret != ESP_OK) {
        printf("Failed to add I2C device\n");
        return;
    }

    uint8_t write_buf[2] = {0, 0x97};
    i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), 300 / portTICK_PERIOD_MS);

    uint8_t reg_addr = 0x00;
    uint8_t data[1] = {0};
    i2c_master_transmit_receive(dev_handle, &reg_addr, 1, data, sizeof(data), 300 / portTICK_PERIOD_MS);
    printf("reg: %x, data: %x\n", reg_addr, data[0]);
    if (data[0] == 0x97) {
        printf("I2C master peripheral write read success\r\n");
    } else {
        printf("I2C master peripheral write read failed\r\n");
    }
    i2c_master_bus_rm_device(dev_handle);

    /* Show peripheral information */
    esp_board_periph_show("i2c_master");

cleanup:
    /* Cleanup */
    esp_board_periph_deinit("i2c_master");
    printf("I2C master peripheral deinitialized\n");
}
