/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2025 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#if CONFIG_IDF_TARGET_ESP32
// ESP32-LyraT-mini
// Sdmmc
#define ESP_GMF_SD_CLK_IO_NUM (GPIO_NUM_14)
#define ESP_GMF_SD_CMD_IO_NUM (GPIO_NUM_15)
#define ESP_GMF_SD_D0_IO_NUM  (GPIO_NUM_2)
#define ESP_GMF_SD_D1_IO_NUM  (GPIO_NUM_NC)
#define ESP_GMF_SD_D2_IO_NUM  (GPIO_NUM_NC)
#define ESP_GMF_SD_D3_IO_NUM  (GPIO_NUM_NC)
#define ESP_GMF_SD_D4_IO_NUM  (GPIO_NUM_NC)
#define ESP_GMF_SD_D5_IO_NUM  (GPIO_NUM_NC)
#define ESP_GMF_SD_D6_IO_NUM  (GPIO_NUM_NC)
#define ESP_GMF_SD_D7_IO_NUM  (GPIO_NUM_NC)
#define ESP_GMF_SD_CD_IO_NUM  (GPIO_NUM_NC)
#define ESP_GMF_SD_WP_IO_NUM  (GPIO_NUM_NC)
#define ESP_GMF_SD_WIDTH      (1)

// I2C
#define ESP_GMF_I2C_SDA_IO_NUM (GPIO_NUM_18)
#define ESP_GMF_I2C_SCL_IO_NUM (GPIO_NUM_23)

// I2S
#define ESP_GMF_I2S_DAC_MCLK_IO_NUM (GPIO_NUM_NC)
#define ESP_GMF_I2S_DAC_BCLK_IO_NUM (GPIO_NUM_5)
#define ESP_GMF_I2S_DAC_WS_IO_NUM   (GPIO_NUM_25)
#define ESP_GMF_I2S_DAC_DO_IO_NUM   (GPIO_NUM_26)
#define ESP_GMF_I2S_DAC_DI_IO_NUM   (GPIO_NUM_35)

#define ESP_GMF_I2S_ADC_MCLK_IO_NUM (GPIO_NUM_0)
#define ESP_GMF_I2S_ADC_BCLK_IO_NUM (GPIO_NUM_32)
#define ESP_GMF_I2S_ADC_WS_IO_NUM   (GPIO_NUM_33)
#define ESP_GMF_I2S_ADC_DO_IO_NUM   (GPIO_NUM_NC)
#define ESP_GMF_I2S_ADC_DI_IO_NUM   (GPIO_NUM_36)

// PA
#define ESP_GMF_AMP_IO_NUM (GPIO_NUM_21)

#elif CONFIG_IDF_TARGET_ESP32C3
// ESP32c3-Lyra
// Sdmmc
#define ESP_GMF_SD_CLK_IO_NUM       (GPIO_NUM_NC)
#define ESP_GMF_SD_CMD_IO_NUM       (GPIO_NUM_NC)
#define ESP_GMF_SD_D0_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_D1_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_D2_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_D3_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_D4_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_D5_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_D6_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_D7_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_CD_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_WP_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_WIDTH            (1)

// I2C
#define ESP_GMF_I2C_SDA_IO_NUM      (GPIO_NUM_9)
#define ESP_GMF_I2C_SCL_IO_NUM      (GPIO_NUM_8)

// I2S
#define ESP_GMF_I2S_DAC_MCLK_IO_NUM (GPIO_NUM_NC)
#define ESP_GMF_I2S_DAC_BCLK_IO_NUM (GPIO_NUM_NC)
#define ESP_GMF_I2S_DAC_WS_IO_NUM   (GPIO_NUM_NC)
#define ESP_GMF_I2S_DAC_DO_IO_NUM   (GPIO_NUM_3)
#define ESP_GMF_I2S_DAC_DI_IO_NUM   (GPIO_NUM_NC)

#define ESP_GMF_I2S_ADC_MCLK_IO_NUM (GPIO_NUM_NC)
#define ESP_GMF_I2S_ADC_BCLK_IO_NUM (GPIO_NUM_NC)
#define ESP_GMF_I2S_ADC_WS_IO_NUM   (GPIO_NUM_NC)
#define ESP_GMF_I2S_ADC_DO_IO_NUM   (GPIO_NUM_3)
#define ESP_GMF_I2S_ADC_DI_IO_NUM   (GPIO_NUM_NC)

// PA
#define ESP_GMF_AMP_IO_NUM          (GPIO_NUM_1)

#elif CONFIG_IDF_TARGET_ESP32S3
// ESP32s3_Korvo_2
// Sdmmc
#define ESP_GMF_SD_CLK_IO_NUM       (GPIO_NUM_15)
#define ESP_GMF_SD_CMD_IO_NUM       (GPIO_NUM_7)
#define ESP_GMF_SD_D0_IO_NUM        (GPIO_NUM_4)
#define ESP_GMF_SD_D1_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_D2_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_D3_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_D4_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_D5_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_D6_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_D7_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_CD_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_WP_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_WIDTH            (1)

// I2C
#define ESP_GMF_I2C_SDA_IO_NUM      (GPIO_NUM_17)
#define ESP_GMF_I2C_SCL_IO_NUM      (GPIO_NUM_18)

// I2S
#define ESP_GMF_I2S_DAC_MCLK_IO_NUM (GPIO_NUM_16)
#define ESP_GMF_I2S_DAC_BCLK_IO_NUM (GPIO_NUM_9)
#define ESP_GMF_I2S_DAC_WS_IO_NUM   (GPIO_NUM_45)
#define ESP_GMF_I2S_DAC_DO_IO_NUM   (GPIO_NUM_8)
#define ESP_GMF_I2S_DAC_DI_IO_NUM   (GPIO_NUM_10)

#define ESP_GMF_I2S_ADC_MCLK_IO_NUM (GPIO_NUM_16)
#define ESP_GMF_I2S_ADC_BCLK_IO_NUM (GPIO_NUM_9)
#define ESP_GMF_I2S_ADC_WS_IO_NUM   (GPIO_NUM_45)
#define ESP_GMF_I2S_ADC_DO_IO_NUM   (GPIO_NUM_8)
#define ESP_GMF_I2S_ADC_DI_IO_NUM   (GPIO_NUM_10)
// PA
#define ESP_GMF_AMP_IO_NUM          (GPIO_NUM_48)

#elif CONFIG_IDF_TARGET_ESP32P4
// ESP32p4_Function_EV_Board
// Sdmmc
#define ESP_GMF_SD_CLK_IO_NUM       (GPIO_NUM_43)
#define ESP_GMF_SD_CMD_IO_NUM       (GPIO_NUM_44)
#define ESP_GMF_SD_D0_IO_NUM        (GPIO_NUM_39)
#define ESP_GMF_SD_D1_IO_NUM        (GPIO_NUM_40)
#define ESP_GMF_SD_D2_IO_NUM        (GPIO_NUM_41)
#define ESP_GMF_SD_D3_IO_NUM        (GPIO_NUM_42)
#define ESP_GMF_SD_D4_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_D5_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_D6_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_D7_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_CD_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_WP_IO_NUM        (GPIO_NUM_NC)
#define ESP_GMF_SD_WIDTH            (4)

// I2C
#define ESP_GMF_I2C_SDA_IO_NUM      (GPIO_NUM_7)
#define ESP_GMF_I2C_SCL_IO_NUM      (GPIO_NUM_8)

// I2S
#define ESP_GMF_I2S_DAC_MCLK_IO_NUM (GPIO_NUM_13)
#define ESP_GMF_I2S_DAC_BCLK_IO_NUM (GPIO_NUM_12)
#define ESP_GMF_I2S_DAC_WS_IO_NUM   (GPIO_NUM_10)
#define ESP_GMF_I2S_DAC_DO_IO_NUM   (GPIO_NUM_9)
#define ESP_GMF_I2S_DAC_DI_IO_NUM   (GPIO_NUM_11)

#define ESP_GMF_I2S_ADC_MCLK_IO_NUM (GPIO_NUM_13)
#define ESP_GMF_I2S_ADC_BCLK_IO_NUM (GPIO_NUM_12)
#define ESP_GMF_I2S_ADC_WS_IO_NUM   (GPIO_NUM_10)
#define ESP_GMF_I2S_ADC_DO_IO_NUM   (GPIO_NUM_9)
#define ESP_GMF_I2S_ADC_DI_IO_NUM   (GPIO_NUM_11)
// PA
#define ESP_GMF_AMP_IO_NUM          (GPIO_NUM_53)

#endif /* CONFIG_IDF_TARGET_ESP32 */

#ifdef __cplusplus
}
#endif /* __cplusplus */