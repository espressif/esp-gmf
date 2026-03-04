/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_adc/adc_continuous.h"

#define BOARD_CODEC_ADC_PATT_LEN_MAX  16

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define DEV_AUDIO_CODEC_DATA_IF_TYPE_I2S  0
#define DEV_AUDIO_CODEC_DATA_IF_TYPE_ADC  1

/**
 * @brief  Audio codec device handles structure
 *
 *         This structure contains all the handles needed to interact with an audio codec device,
 *         including the codec device handle, data interface, control interface, GPIO interface,
 *         and codec interface.
 */
typedef struct {
    esp_codec_dev_handle_t       codec_dev;      /*!< Codec device handle */
    const audio_codec_data_if_t *data_if;        /*!< Data interface handle */
    const audio_codec_ctrl_if_t *ctrl_if;        /*!< Control interface handle */
    const audio_codec_gpio_if_t *gpio_if;        /*!< GPIO interface handle */
    const audio_codec_if_t      *codec_if;       /*!< Codec interface handle */
    int16_t                      tx_aux_out_io;  /*!< Optional mirrored/inverted I2S TX auxiliary output IO */
} dev_audio_codec_handles_t;

/**
 * @brief  Board codec power amplifier configuration structure
 */
typedef struct {
    const char *name;          /*!< Power amplifier name */
    int16_t     port;          /*!< GPIO port number */
    float       gain;          /*!< Amplifier gain value */
    int16_t     active_level;  /*!< Active level (high/low) */
} board_codec_pa_t;

/**
 * @brief  Board codec I2C configuration structure
 */
typedef struct {
    const char *name;       /*!< I2C device name */
    int8_t      port;       /*!< I2C port number */
    int16_t     address;    /*!< I2C device address */
    int32_t     frequency;  /*!< I2C clock frequency in Hz */
} board_codec_i2c_t;

/**
 * @brief  Board codec I2S configuration structure
 */
typedef struct {
    const char *name;               /*!< I2S device name */
    int8_t      port;               /*!< I2S port number */
    int         clk_src;            /*!< I2S clock source, need converted from `i2s_clock_src_t`. If set to 0 will use default clock source */
    int16_t     tx_aux_out_io;      /*!< Optional mirrored/inverted I2S TX auxiliary output IO, -1 means disabled */
    uint8_t     tx_aux_out_line;    /*!< Optional TX data line index used for auxiliary output routing */
    uint8_t     tx_aux_out_invert;  /*!< Optional invert flag for auxiliary output routing */
} board_codec_i2s_t;

/**
 * @brief  Board codec ADC configuration mode enumeration
 */
typedef enum {
    BOARD_CODEC_ADC_CFG_MODE_SINGLE_UNIT = 0,  /*!< Single unit + channel list */
    BOARD_CODEC_ADC_CFG_MODE_PATTERN     = 1,  /*!< Full adc_digi_pattern_config_t list */
} board_codec_adc_cfg_mode_t;

/**
 * @brief  Board codec ADC pattern configuration structure
 */
typedef struct {
    int  unit;       /*!< ADC unit id (1 or 2) */
    int  channel;    /*!< ADC channel id */
    int  atten;      /*!< ADC attenuation */
    int  bit_width;  /*!< ADC bit width */
} board_codec_adc_pattern_t;

/**
 * @brief  Board codec ADC configuration structure
 */
typedef struct {
    const char                 *periph_name;         /*!< ADC peripheral name for reuse path */
    uint32_t                    sample_rate_hz;      /*!< ADC sample rate in Hz */
    uint32_t                    max_store_buf_size;  /*!< ADC conversion pool size in bytes */
    uint32_t                    conv_frame_size;     /*!< ADC conversion frame size in bytes */
    int                         conv_mode;           /*!< Fallback field for non-ADC targets */
    int                         format;              /*!< Fallback field for non-ADC targets */
    uint8_t                     pattern_num;         /*!< Number of configured ADC pattern entries */
    board_codec_adc_cfg_mode_t  cfg_mode;            /*!< ADC cfg mode */
    union {
        struct {
            int      unit_id;                                   /*!< Single-unit mode ADC unit */
            int      atten;                                     /*!< Single-unit mode attenuation */
            int      bit_width;                                 /*!< Single-unit mode bit width */
            uint8_t  channel_id[BOARD_CODEC_ADC_PATT_LEN_MAX];  /*!< Single-unit channel list */
        } single_unit;
        board_codec_adc_pattern_t  patterns[BOARD_CODEC_ADC_PATT_LEN_MAX];  /*!< Pattern mode list */
    } cfg;
} board_codec_adc_t;

/**
 * @brief  Audio codec configuration structure
 *
 *         This structure contains all the configuration parameters needed to initialize
 *         an audio codec device, including ADC/DAC settings, power amplifier configuration,
 *         I2C/I2S interface settings, and feature enable flags.
 */
typedef struct {
    const char *name;                /*!< Codec device name */
    const char *chip;                /*!< Codec chip type */
    const char *type;                /*!< Codec type */
    uint8_t     data_if_type;        /*!< Auto-selected data interface type: DEV_AUDIO_CODEC_DATA_IF_TYPE_* */
    /* ADC Configuration */
    bool        adc_enabled;         /*!< Enable ADC functionality */
    uint8_t     adc_max_channel;     /*!< Maximum number of ADC channels */
    uint32_t    adc_channel_mask;    /*!< ADC channel mask string */
    const char *adc_channel_labels;  /*!< ADC channel labels,
                                        # - FC: Front Center
                                        # - RE: Reference
                                        # - FL/FR: Front Left/Right
                                        # - SL/SR: Side Left/Right
                                        # - BL/BR: Back Left/Right
                                        # - NA: Not Available/Not Enable
                                        */
    int         adc_init_gain;       /*!< ADC initial gain in dB */

    /* DAC Configuration */
    bool      dac_enabled;       /*!< Enable DAC functionality */
    uint8_t   dac_max_channel;   /*!< Maximum number of DAC channels */
    uint32_t  dac_channel_mask;  /*!< DAC channel mask string */
    int       dac_init_gain;     /*!< DAC initial gain in dB */

    /* Interface Configuration */
    board_codec_pa_t   pa_cfg;   /*!< Power amplifier configuration */
    board_codec_i2c_t  i2c_cfg;  /*!< I2C interface configuration */
    board_codec_i2s_t  i2s_cfg;  /*!< I2S interface configuration */
    board_codec_adc_t  adc_cfg;  /*!< ADC data-if configuration */

    /* Metadata */
    uint8_t *metadata;       /*!< Metadata buffer */
    int      metadata_size;  /*!< Metadata buffer size */

    /* Feature Flags */
    uint8_t  mclk_enabled : 1;  /*!< Enable MCLK output */
    uint8_t  aec_enabled  : 1;  /*!< Enable Acoustic Echo Cancellation */
    uint8_t  eq_enabled   : 1;  /*!< Enable Equalizer */
    uint8_t  alc_enabled  : 1;  /*!< Enable Automatic Level Control */
} dev_audio_codec_config_t;

/**
 * @brief  Initialize the audio codec device with the given configuration
 *
 *         This function initializes an audio codec device using the provided configuration structure.
 *         It sets up the necessary hardware interfaces (I2C, I2S, GPIO, etc.) and allocates resources
 *         for the codec. The resulting device handle can be used for further audio operations.
 *
 * @param[in]   cfg            Pointer to the audio codec configuration structure
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  device_handle  Pointer to a variable to receive the dev_audio_codec_handles_t handle
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_audio_codec_init(void *cfg, int cfg_size, void **device_handle);

/**
 * @brief  Deinitialize the audio codec device
 *
 *         This function deinitializes the audio codec device and frees the allocated resources.
 *         It should be called when the device is no longer needed to prevent memory leaks.
 *
 * @param[in]  device_handle  Pointer to the device handle to be deinitialized
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_audio_codec_deinit(void *device_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
