/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_check.h"

#include "dev_audio_codec.h"
#include "esp_board_periph.h"
#include "esp_board_manager_defs.h"
#include "esp_board_device.h"

static const char *TAG = "DEV_AUDIO_CODEC";

// Macro to generate codec function name
#define CODEC_NEW_FUNC(codec_name) codec_name##_codec_new

// Macro to generate codec config struct name
#define CODEC_CONFIG_STRUCT(codec_name) codec_name##_codec_cfg_t

// Generic codec factory function template
#define DEFINE_CODEC_FACTORY(codec_name)                                                     \
    static audio_codec_if_t *codec_name##_factory(void *cfg)                                 \
    {                                                                                        \
        CODEC_CONFIG_STRUCT(codec_name) *codec_cfg = (CODEC_CONFIG_STRUCT(codec_name) *)cfg; \
        const audio_codec_if_t *result             = CODEC_NEW_FUNC(codec_name)(codec_cfg);  \
        return (audio_codec_if_t *)result; \
    }

// ADC and DAC codec configuration setup function template (for ES8311, ES8388, ES8389, etc.)
#define DEFINE_AUDIO_CODEC_CONFIG_SETUP(codec_name)                                                                                  \
    static int codec_name##_config_setup(dev_audio_codec_config_t *base_cfg, dev_audio_codec_handles_t *handles, void *specific_cfg) \
    {                                                                                                                                \
        CODEC_CONFIG_STRUCT(codec_name) *codec_cfg = (CODEC_CONFIG_STRUCT(codec_name) *)specific_cfg;                                \
        codec_cfg->ctrl_if                         = handles->ctrl_if;                                                               \
        codec_cfg->gpio_if                         = handles->gpio_if;                                                               \
        codec_cfg->use_mclk                        = base_cfg->mclk_enabled;                                                         \
        codec_cfg->pa_pin                          = base_cfg->pa_cfg.port;                                                          \
        codec_cfg->hw_gain.pa_gain                 = base_cfg->pa_cfg.gain;                                                          \
        codec_cfg->pa_reverted                     = base_cfg->pa_cfg.active_level == 1 ? false : true;                              \
        if (base_cfg->dac_enabled) {                                                                                                 \
            codec_cfg->codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;                                                                     \
        }                                                                                                                            \
        if (base_cfg->adc_enabled) {                                                                                                 \
            codec_cfg->codec_mode |= ESP_CODEC_DEV_WORK_MODE_ADC;                                                                    \
        }                                                                                                                            \
        return 0; \
    }

#define DEFINE_AUDIO_CODEC_CONFIG_SETUP_NO_MCLK(codec_name)                                                                          \
    static int codec_name##_config_setup(dev_audio_codec_config_t *base_cfg, dev_audio_codec_handles_t *handles, void *specific_cfg) \
    {                                                                                                                                \
        CODEC_CONFIG_STRUCT(codec_name) *codec_cfg = (CODEC_CONFIG_STRUCT(codec_name) *)specific_cfg;                                \
        codec_cfg->ctrl_if                         = handles->ctrl_if;                                                               \
        codec_cfg->gpio_if                         = handles->gpio_if;                                                               \
        codec_cfg->pa_pin                          = base_cfg->pa_cfg.port;                                                          \
        codec_cfg->hw_gain.pa_gain                 = base_cfg->pa_cfg.gain;                                                          \
        codec_cfg->pa_reverted                     = base_cfg->pa_cfg.active_level == 1 ? false : true;                              \
        if (base_cfg->dac_enabled) {                                                                                                 \
            codec_cfg->codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;                                                                     \
        }                                                                                                                            \
        if (base_cfg->adc_enabled) {                                                                                                 \
            codec_cfg->codec_mode |= ESP_CODEC_DEV_WORK_MODE_ADC;                                                                    \
        }                                                                                                                            \
        return 0; \
    }

#define DEFINE_AUDIO_CODEC_CONFIG_SETUP_NO_MCLK_NO_HW_GAIN(codec_name)                                                               \
    static int codec_name##_config_setup(dev_audio_codec_config_t *base_cfg, dev_audio_codec_handles_t *handles, void *specific_cfg) \
    {                                                                                                                                \
        CODEC_CONFIG_STRUCT(codec_name) *codec_cfg = (CODEC_CONFIG_STRUCT(codec_name) *)specific_cfg;                                \
        codec_cfg->ctrl_if                         = handles->ctrl_if;                                                               \
        codec_cfg->gpio_if                         = handles->gpio_if;                                                               \
        codec_cfg->pa_pin                          = base_cfg->pa_cfg.port;                                                          \
        codec_cfg->pa_reverted                     = base_cfg->pa_cfg.active_level == 1 ? false : true;                              \
        if (base_cfg->dac_enabled) {                                                                                                 \
            codec_cfg->codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;                                                                     \
        }                                                                                                                            \
        if (base_cfg->adc_enabled) {                                                                                                 \
            codec_cfg->codec_mode |= ESP_CODEC_DEV_WORK_MODE_ADC;                                                                    \
        }                                                                                                                            \
        return 0; \
    }

// DAC codec configuration setup function template (for AW88298, TAS5805M, etc.)
#define DEFINE_AUDIO_DAC_CONFIG_SETUP(codec_name)                                                                                    \
    static int codec_name##_config_setup(dev_audio_codec_config_t *base_cfg, dev_audio_codec_handles_t *handles, void *specific_cfg) \
    {                                                                                                                                \
        CODEC_CONFIG_STRUCT(codec_name) *codec_cfg = (CODEC_CONFIG_STRUCT(codec_name) *)specific_cfg;                                \
        codec_cfg->ctrl_if                         = handles->ctrl_if;                                                               \
        codec_cfg->gpio_if                         = handles->gpio_if;                                                               \
        codec_cfg->hw_gain.pa_gain                 = base_cfg->pa_cfg.gain;                                                          \
        return 0;                                                                                                                    \
    }

// DAC codec configuration setup function template with pa_reverted configuration (for ES8156, etc.)
#define DEFINE_AUDIO_DAC_CONFIG_SETUP_WITH_PA_REVERTED(codec_name)                                                                   \
    static int codec_name##_config_setup(dev_audio_codec_config_t *base_cfg, dev_audio_codec_handles_t *handles, void *specific_cfg) \
    {                                                                                                                                \
        CODEC_CONFIG_STRUCT(codec_name) *codec_cfg = (CODEC_CONFIG_STRUCT(codec_name) *)specific_cfg;                                \
        codec_cfg->ctrl_if                         = handles->ctrl_if;                                                               \
        codec_cfg->gpio_if                         = handles->gpio_if;                                                               \
        codec_cfg->pa_reverted                     = base_cfg->pa_cfg.active_level == 1 ? false : true;                              \
        codec_cfg->hw_gain.pa_gain                 = base_cfg->pa_cfg.gain;                                                          \
        return 0;                                                                                                                    \
    }

// ADC codec configuration setup function template (for ES7210)
#define DEFINE_AUDIO_ADC_ES7210_CONFIG_SETUP(codec_name)                                                                             \
    static int codec_name##_config_setup(dev_audio_codec_config_t *base_cfg, dev_audio_codec_handles_t *handles, void *specific_cfg) \
    {                                                                                                                                \
        CODEC_CONFIG_STRUCT(codec_name) *codec_cfg = (CODEC_CONFIG_STRUCT(codec_name) *)specific_cfg;                                \
        codec_cfg->ctrl_if                         = handles->ctrl_if;                                                               \
        codec_cfg->mic_selected                    = base_cfg->adc_channel_mask;                                                     \
        return 0;                                                                                                                    \
    }

// Simple ADC codec configuration setup function template (for ES7243, ES7243E)
#define DEFINE_AUDIO_ADC_SIMPLE_CONFIG_SETUP(codec_name)                                                                             \
    static int codec_name##_config_setup(dev_audio_codec_config_t *base_cfg, dev_audio_codec_handles_t *handles, void *specific_cfg) \
    {                                                                                                                                \
        CODEC_CONFIG_STRUCT(codec_name) *codec_cfg = (CODEC_CONFIG_STRUCT(codec_name) *)specific_cfg;                                \
        codec_cfg->ctrl_if                         = handles->ctrl_if;                                                               \
        return 0;                                                                                                                    \
    }

// Function pointer types
typedef audio_codec_if_t *(*codec_factory_func_t)(void *cfg);
typedef int (*codec_config_setup_func_t)(dev_audio_codec_config_t *base_cfg, dev_audio_codec_handles_t *handles, void *specific_cfg);

// Codec registry entry
typedef struct {
    const char                *name;
    codec_factory_func_t       factory_func;
    codec_config_setup_func_t  config_setup_func;
    size_t                     cfg_size;
} codec_registry_entry_t;

// Define codecs based on configuration
#ifdef CONFIG_CODEC_ES8311_SUPPORT
DEFINE_CODEC_FACTORY(es8311)
DEFINE_AUDIO_CODEC_CONFIG_SETUP(es8311)
#endif  /* CONFIG_CODEC_ES8311_SUPPORT */

#ifdef CONFIG_CODEC_ES7210_SUPPORT
DEFINE_CODEC_FACTORY(es7210)
DEFINE_AUDIO_ADC_ES7210_CONFIG_SETUP(es7210)
#endif  /* CONFIG_CODEC_ES7210_SUPPORT */

#ifdef CONFIG_CODEC_ES7243_SUPPORT
DEFINE_CODEC_FACTORY(es7243)
DEFINE_AUDIO_ADC_SIMPLE_CONFIG_SETUP(es7243)
#endif  /* CONFIG_CODEC_ES7243_SUPPORT */

#ifdef CONFIG_CODEC_ES7243E_SUPPORT
DEFINE_CODEC_FACTORY(es7243e)
DEFINE_AUDIO_ADC_SIMPLE_CONFIG_SETUP(es7243e)
#endif  /* CONFIG_CODEC_ES7243E_SUPPORT */

#ifdef CONFIG_CODEC_ES8156_SUPPORT
DEFINE_CODEC_FACTORY(es8156)
DEFINE_AUDIO_DAC_CONFIG_SETUP_WITH_PA_REVERTED(es8156)
#endif  /* CONFIG_CODEC_ES8156_SUPPORT */

#ifdef CONFIG_CODEC_ES8374_SUPPORT
DEFINE_CODEC_FACTORY(es8374)
DEFINE_AUDIO_CODEC_CONFIG_SETUP_NO_MCLK_NO_HW_GAIN(es8374)
#endif  /* CONFIG_CODEC_ES8374_SUPPORT */

#ifdef CONFIG_CODEC_ES8388_SUPPORT
DEFINE_CODEC_FACTORY(es8388)
DEFINE_AUDIO_CODEC_CONFIG_SETUP_NO_MCLK(es8388)
#endif  /* CONFIG_CODEC_ES8388_SUPPORT */

#ifdef CONFIG_CODEC_ES8389_SUPPORT
DEFINE_CODEC_FACTORY(es8389)
DEFINE_AUDIO_CODEC_CONFIG_SETUP(es8389)
#endif  /* CONFIG_CODEC_ES8389_SUPPORT */

#ifdef CONFIG_CODEC_TAS5805M_SUPPORT
DEFINE_CODEC_FACTORY(tas5805m)
DEFINE_AUDIO_DAC_CONFIG_SETUP(tas5805m)
#endif  /* CONFIG_CODEC_TAS5805M_SUPPORT */

#ifdef CONFIG_CODEC_ZL38063_SUPPORT
DEFINE_CODEC_FACTORY(zl38063)
DEFINE_AUDIO_CODEC_CONFIG_SETUP(zl38063)
#endif  /* CONFIG_CODEC_ZL38063_SUPPORT */

#ifdef CONFIG_CODEC_AW88298_SUPPORT
DEFINE_CODEC_FACTORY(aw88298)
DEFINE_AUDIO_DAC_CONFIG_SETUP(aw88298)
#endif  /* CONFIG_CODEC_AW88298_SUPPORT */

// Codec registry - add new codecs here
static const codec_registry_entry_t codec_registry[] = {
#ifdef CONFIG_CODEC_ES8311_SUPPORT
    {"es8311", es8311_factory, es8311_config_setup, sizeof(es8311_codec_cfg_t)},
#endif  /* CONFIG_CODEC_ES8311_SUPPORT */
#ifdef CONFIG_CODEC_ES7210_SUPPORT
    {"es7210", es7210_factory, es7210_config_setup, sizeof(es7210_codec_cfg_t)},
#endif  /* CONFIG_CODEC_ES7210_SUPPORT */
#ifdef CONFIG_CODEC_ES7243_SUPPORT
    {"es7243", es7243_factory, es7243_config_setup, sizeof(es7243_codec_cfg_t)},
#endif  /* CONFIG_CODEC_ES7243_SUPPORT */
#ifdef CONFIG_CODEC_ES7243E_SUPPORT
    {"es7243e", es7243e_factory, es7243e_config_setup, sizeof(es7243e_codec_cfg_t)},
#endif  /* CONFIG_CODEC_ES7243E_SUPPORT */
#ifdef CONFIG_CODEC_ES8156_SUPPORT
    {"es8156", es8156_factory, es8156_config_setup, sizeof(es8156_codec_cfg_t)},
#endif  /* CONFIG_CODEC_ES8156_SUPPORT */
#ifdef CONFIG_CODEC_ES8374_SUPPORT
    {"es8374", es8374_factory, es8374_config_setup, sizeof(es8374_codec_cfg_t)},
#endif  /* CONFIG_CODEC_ES8374_SUPPORT */
#ifdef CONFIG_CODEC_ES8388_SUPPORT
    {"es8388", es8388_factory, es8388_config_setup, sizeof(es8388_codec_cfg_t)},
#endif  /* CONFIG_CODEC_ES8388_SUPPORT */
#ifdef CONFIG_CODEC_ES8389_SUPPORT
    {"es8389", es8389_factory, es8389_config_setup, sizeof(es8389_codec_cfg_t)},
#endif  /* CONFIG_CODEC_ES8389_SUPPORT */
#ifdef CONFIG_CODEC_TAS5805M_SUPPORT
    {"tas5805m", tas5805m_factory, tas5805m_config_setup, sizeof(tas5805m_codec_cfg_t)},
#endif  /* CONFIG_CODEC_TAS5805M_SUPPORT */
#ifdef CONFIG_CODEC_ZL38063_SUPPORT
    {"zl38063", zl38063_factory, zl38063_config_setup, sizeof(zl38063_codec_cfg_t)},
#endif  /* CONFIG_CODEC_ZL38063_SUPPORT */
#ifdef CONFIG_CODEC_AW88298_SUPPORT
    {"aw88298", aw88298_factory, aw88298_config_setup, sizeof(aw88298_codec_cfg_t)},
#endif  /* CONFIG_CODEC_AW88298_SUPPORT */
    // Add more codecs here as needed
    {NULL, NULL, NULL, 0}  // End marker
};

// Find codec in registry
static const codec_registry_entry_t *find_codec(const char *name)
{
    for (int i = 0; codec_registry[i].name != NULL; i++) {
        ESP_LOGD(TAG, "codec_registry[%d].name: %s,wanted: %s", i, codec_registry[i].name, name);
        if (strcmp(codec_registry[i].name, name) == 0) {
            return &codec_registry[i];
        }
    }
    return NULL;
}

esp_err_t __attribute__((weak)) codec_factory_entry_t(dev_audio_codec_config_t *cfg, dev_audio_codec_handles_t *handles)
{
    ESP_LOGW(TAG, "codec_factory_entry_t is not implemented");
    return ESP_OK;
}

int dev_audio_codec_init(void *cfg, int cfg_size, void **device_handle)
{
    dev_audio_codec_config_t *codec_cfg = (dev_audio_codec_config_t *)cfg;

    dev_audio_codec_handles_t *codec_handles = (dev_audio_codec_handles_t *)malloc(sizeof(dev_audio_codec_handles_t));
    if (codec_handles == NULL) {
        ESP_LOGE(TAG, "Failed to allocate codec handles");
        return -1;
    }
    memset(codec_handles, 0, sizeof(dev_audio_codec_handles_t));

    esp_codec_dev_type_t dev_type = ESP_CODEC_DEV_TYPE_NONE;
    audio_codec_i2s_cfg_t i2s_cfg = {0};
    if (strcmp(codec_cfg->name, ESP_BOARD_DEVICE_NAME_AUDIO_ADC) == 0) {
        ESP_LOGI(TAG, "ADC is ENABLED");
        esp_board_periph_ref_handle(codec_cfg->i2s_cfg.name, &i2s_cfg.rx_handle);

        dev_type |= ESP_CODEC_DEV_TYPE_IN;
    } else if (strcmp(codec_cfg->name, ESP_BOARD_DEVICE_NAME_AUDIO_DAC) == 0) {
        codec_handles->gpio_if = (audio_codec_gpio_if_t *)audio_codec_new_gpio();
        esp_board_periph_ref_handle(codec_cfg->i2s_cfg.name, &i2s_cfg.tx_handle);
        ESP_LOGI(TAG, "DAC is ENABLED");
        dev_type |= ESP_CODEC_DEV_TYPE_OUT;
    }
    i2s_cfg.port = codec_cfg->i2s_cfg.port;
    codec_handles->data_if = (audio_codec_data_if_t *)audio_codec_new_i2s_data(&i2s_cfg);
    ESP_LOGI(TAG, "Init %s, i2s_name: %s, i2s_rx_handle:%p, i2s_tx_handle:%p, data_if: %p",
             codec_cfg->name, codec_cfg->i2s_cfg.name, i2s_cfg.rx_handle, i2s_cfg.tx_handle, codec_handles->data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {0};
    esp_board_periph_ref_handle(codec_cfg->i2c_cfg.name, &i2c_cfg.bus_handle);
    i2c_cfg.addr = codec_cfg->i2c_cfg.address;
    codec_handles->ctrl_if = (audio_codec_ctrl_if_t *)audio_codec_new_i2c_ctrl(&i2c_cfg);

    // Dynamic codec selection based on name
    const codec_registry_entry_t *codec_entry = find_codec(codec_cfg->chip);
    if (codec_entry == NULL) {
        ESP_LOGE(TAG, "Unsupported codec: %s", codec_cfg->chip);
        return -1;
    }

    // Allocate codec-specific configuration
    void *codec_specific_cfg = malloc(codec_entry->cfg_size);
    if (codec_specific_cfg == NULL) {
        ESP_LOGE(TAG, "Failed to allocate codec configuration");
        return -1;
    }
    memset(codec_specific_cfg, 0, codec_entry->cfg_size);

    // Setup codec-specific configuration
    if (codec_entry->config_setup_func != NULL) {
        codec_entry->config_setup_func(codec_cfg, codec_handles, codec_specific_cfg);
    }

    // Create codec using factory function
    codec_handles->codec_if = codec_entry->factory_func(codec_specific_cfg);
    if (codec_handles->codec_if == NULL) {
        ESP_LOGE(TAG, "Failed to create codec: %s", codec_cfg->name);
        free(codec_specific_cfg);
        return -1;
    }

    ESP_LOGI(TAG, "Successfully initialized codec: %s", codec_cfg->name);
    free(codec_specific_cfg);

    // New output codec device
    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = codec_handles->codec_if,
        .data_if = codec_handles->data_if,
        .dev_type = dev_type,
    };
    codec_handles->codec_dev = esp_codec_dev_new(&dev_cfg);
    ESP_LOGI(TAG, "Create esp_codec_dev success, dev:%p, chip:%s", codec_handles->codec_dev, codec_cfg->chip);
    *device_handle = codec_handles;
    return 0;
}

int dev_audio_codec_deinit(void *device_handle)
{
    dev_audio_codec_handles_t *codec_handles = (dev_audio_codec_handles_t *)device_handle;

    esp_codec_dev_delete(codec_handles->codec_dev);
    codec_handles->codec_dev = NULL;
    audio_codec_delete_codec_if(codec_handles->codec_if);
    codec_handles->codec_if = NULL;
    audio_codec_delete_ctrl_if(codec_handles->ctrl_if);
    codec_handles->ctrl_if = NULL;
    if (codec_handles->gpio_if) {
        audio_codec_delete_gpio_if(codec_handles->gpio_if);
        codec_handles->gpio_if = NULL;
    }
    audio_codec_delete_data_if(codec_handles->data_if);

    dev_audio_codec_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (cfg) {
        esp_board_periph_unref_handle(cfg->i2s_cfg.name);
        esp_board_periph_unref_handle(cfg->i2c_cfg.name);
    }

    codec_handles->data_if = NULL;
    free(codec_handles);
    return 0;
}
