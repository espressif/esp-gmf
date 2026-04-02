/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"
#include "esp_check.h"
#include "soc/soc_caps.h"
#if SOC_ADC_SUPPORTED
#include "esp_adc/adc_continuous.h"
#endif  /* SOC_ADC_SUPPORTED */
#include "driver/i2s_types.h"
#include "driver/i2s_common.h"
#include "driver/gpio.h"
#include "dev_audio_codec.h"
#include "esp_board_periph.h"
#include "esp_board_manager_defs.h"
#include "esp_board_device.h"
#if SOC_ADC_SUPPORTED && CONFIG_ESP_BOARD_PERIPH_ADC_SUPPORT
#include "periph_adc.h"
#endif  /* SOC_ADC_SUPPORTED && CONFIG_ESP_BOARD_PERIPH_ADC_SUPPORT */
#ifdef CONFIG_ESP_BOARD_PERIPH_I2S_SUPPORT
#include "periph_i2s.h"
#endif  /* CONFIG_ESP_BOARD_PERIPH_I2S_SUPPORT */

static const char *TAG = "DEV_AUDIO_CODEC";

#ifdef CONFIG_ESP_BOARD_PERIPH_I2S_SUPPORT
extern uint8_t i2s_chan_handles[];
#endif /* CONFIG_ESP_BOARD_PERIPH_I2S_SUPPORT */

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
        return 0;                                                                                                                    \
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
        return 0;                                                                                                                    \
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
        return 0;                                                                                                                    \
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
        codec_cfg->pa_pin                          = base_cfg->pa_cfg.port;                                                          \
        codec_cfg->pa_reverted                     = base_cfg->pa_cfg.active_level == 1 ? false : true;                              \
        codec_cfg->hw_gain.pa_gain                 = base_cfg->pa_cfg.gain;                                                          \
        return 0;                                                                                                                    \
    }

// Dummy codec configuration setup function template (for internal PA-only path)
#define DEFINE_DUMMY_CODEC_CONFIG_SETUP(codec_name)                                                                                   \
    static int codec_name##_config_setup(dev_audio_codec_config_t *base_cfg, dev_audio_codec_handles_t *handles, void *specific_cfg)  \
    {                                                                                                                                 \
        CODEC_CONFIG_STRUCT(codec_name) *codec_cfg = (CODEC_CONFIG_STRUCT(codec_name) *)specific_cfg;                                 \
        if (handles->gpio_if == NULL) {                                                                                               \
            handles->gpio_if = (audio_codec_gpio_if_t *)audio_codec_new_gpio();                                                       \
            if (handles->gpio_if == NULL) {                                                                                           \
                ESP_LOGE(TAG, "Failed to create gpio interface for dummy codec");                                                     \
                return -1;                                                                                                            \
            }                                                                                                                         \
        }                                                                                                                             \
        codec_cfg->gpio_if = handles->gpio_if;                                                                                        \
        codec_cfg->pa_pin = base_cfg->pa_cfg.port;                                                                                    \
        codec_cfg->pa_reverted = base_cfg->pa_cfg.active_level == 1 ? false : true;                                                   \
        return 0;                                                                                                                     \
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

#ifdef CONFIG_CODEC_DUMMY_SUPPORT
DEFINE_CODEC_FACTORY(dummy)
DEFINE_DUMMY_CODEC_CONFIG_SETUP(dummy)
#endif  /* CONFIG_CODEC_DUMMY_SUPPORT */

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
#ifdef CONFIG_CODEC_DUMMY_SUPPORT
    {"dummy", dummy_factory, dummy_config_setup, sizeof(dummy_codec_cfg_t)},
#endif  /* CONFIG_CODEC_DUMMY_SUPPORT */
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

#ifdef CONFIG_ESP_BOARD_PERIPH_I2S_SUPPORT
static bool is_i2s_tx_aux_output_conflicting(const periph_i2s_config_t *i2s_periph_cfg, gpio_num_t aux_io)
{
    // When the auxiliary output IO is configured, check if it conflicts with any of the active I2S TX pins (WS, BCLK, DOUT, and PDM TX pins if supported)
    if (i2s_periph_cfg == NULL) {
        return false;
    }

    if (i2s_periph_cfg->mode == I2S_COMM_MODE_STD) {
        return aux_io == i2s_periph_cfg->i2s_cfg.std.gpio_cfg.ws ||
               aux_io == i2s_periph_cfg->i2s_cfg.std.gpio_cfg.bclk ||
               aux_io == i2s_periph_cfg->i2s_cfg.std.gpio_cfg.dout;
    }
#if CONFIG_SOC_I2S_SUPPORTS_TDM
    if (i2s_periph_cfg->mode == I2S_COMM_MODE_TDM) {
        return aux_io == i2s_periph_cfg->i2s_cfg.tdm.gpio_cfg.ws ||
               aux_io == i2s_periph_cfg->i2s_cfg.tdm.gpio_cfg.bclk ||
               aux_io == i2s_periph_cfg->i2s_cfg.tdm.gpio_cfg.dout;
    }
#endif  /* CONFIG_SOC_I2S_SUPPORTS_TDM */
#if CONFIG_SOC_I2S_SUPPORTS_PDM_TX
    if (i2s_periph_cfg->mode == I2S_COMM_MODE_PDM) {
        return aux_io == i2s_periph_cfg->i2s_cfg.pdm_tx.gpio_cfg.clk ||
               aux_io == i2s_periph_cfg->i2s_cfg.pdm_tx.gpio_cfg.dout ||
#if SOC_I2S_PDM_MAX_TX_LINES > 1
               aux_io == i2s_periph_cfg->i2s_cfg.pdm_tx.gpio_cfg.dout2 ||
#endif  /* SOC_I2S_PDM_MAX_TX_LINES > 1 */
               false;
    }
#endif  /* CONFIG_SOC_I2S_SUPPORTS_PDM_TX */
    return false;
}

static esp_err_t configure_i2s_tx_aux_output(const dev_audio_codec_config_t *codec_cfg, dev_audio_codec_handles_t *codec_handles)
{
    if (codec_cfg->i2s_cfg.tx_aux_out_io < 0) {
        return ESP_OK;
    }
    if (codec_cfg->i2s_cfg.name == NULL || codec_cfg->i2s_cfg.name[0] == '\0') {
        ESP_LOGE(TAG, "tx_aux_out_io requires valid i2s peripheral name");
        return ESP_ERR_INVALID_ARG;
    }

    periph_i2s_config_t *i2s_periph_cfg = NULL;
    esp_err_t ret = esp_board_periph_get_config(codec_cfg->i2s_cfg.name, (void **)&i2s_periph_cfg);
    if (ret != ESP_OK || i2s_periph_cfg == NULL) {
        ESP_LOGE(TAG, "Get I2S peripheral config failed, name:%s err:%s", codec_cfg->i2s_cfg.name, esp_err_to_name(ret));
        return ESP_ERR_NOT_FOUND;
    }
    if ((i2s_periph_cfg->direction & I2S_DIR_TX) == 0) {
        ESP_LOGE(TAG, "tx_aux_out_io only supports I2S TX direction");
        return ESP_ERR_INVALID_STATE;
    }

    gpio_num_t aux_io = (gpio_num_t)codec_cfg->i2s_cfg.tx_aux_out_io;
    if (is_i2s_tx_aux_output_conflicting(i2s_periph_cfg, aux_io)) {
        ESP_LOGE(TAG, "tx_aux_out_io conflicts with active I2S TX pins");
        return ESP_ERR_INVALID_ARG;
    }

    int out_sig = -1;
    ret = periph_i2s_get_data_out_signal(codec_cfg->i2s_cfg.name, codec_cfg->i2s_cfg.tx_aux_out_line, &out_sig);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Get I2S data output signal failed, name:%s line:%d err:%s",
                 codec_cfg->i2s_cfg.name, codec_cfg->i2s_cfg.tx_aux_out_line, esp_err_to_name(ret));
        return ret;
    }

    ESP_RETURN_ON_ERROR(gpio_reset_pin(aux_io), TAG, "reset gpio failed");
    ESP_RETURN_ON_ERROR(gpio_set_direction(aux_io, GPIO_MODE_OUTPUT), TAG, "set direction failed");
    esp_rom_gpio_connect_out_signal(aux_io, out_sig, codec_cfg->i2s_cfg.tx_aux_out_invert ? 1 : 0, 0);
    ESP_RETURN_ON_ERROR(gpio_set_drive_capability(aux_io, GPIO_DRIVE_CAP_0), TAG, "set drive capability failed");
    codec_handles->tx_aux_out_io = aux_io;
    ESP_LOGI(TAG, "Configured tx_aux_out_io:%d from i2s name:%s port:%d line:%u invert:%u out_sig:%d",
             aux_io, codec_cfg->i2s_cfg.name, i2s_periph_cfg->port, codec_cfg->i2s_cfg.tx_aux_out_line,
             codec_cfg->i2s_cfg.tx_aux_out_invert, out_sig);
    return ESP_OK;
}
#endif  /* CONFIG_ESP_BOARD_PERIPH_I2S_SUPPORT */

#if SOC_ADC_SUPPORTED && CONFIG_CODEC_DATA_ADC_SUPPORT
static void fill_audio_adc_continuous_cfg_from_board(audio_codec_adc_continuous_cfg_t *out_cfg,
                                                     const board_codec_adc_t *in_cfg)
{
    memset(out_cfg, 0, sizeof(*out_cfg));
    out_cfg->max_store_buf_size = in_cfg->max_store_buf_size;
    out_cfg->conv_frame_size = in_cfg->conv_frame_size;
    out_cfg->sample_freq_hz = in_cfg->sample_rate_hz;
    out_cfg->conv_mode = in_cfg->conv_mode;
    out_cfg->format = in_cfg->format;
    out_cfg->pattern_num = in_cfg->pattern_num;
    out_cfg->cfg_mode = (in_cfg->cfg_mode == BOARD_CODEC_ADC_CFG_MODE_PATTERN) ? AUDIO_CODEC_ADC_CFG_MODE_PATTERN : AUDIO_CODEC_ADC_CFG_MODE_SINGLE_UNIT;

    if (in_cfg->cfg_mode == BOARD_CODEC_ADC_CFG_MODE_PATTERN) {
        for (int i = 0; i < in_cfg->pattern_num; i++) {
            out_cfg->cfg.patterns[i].unit = (adc_unit_t)in_cfg->cfg.patterns[i].unit;
            out_cfg->cfg.patterns[i].channel = in_cfg->cfg.patterns[i].channel;
            out_cfg->cfg.patterns[i].atten = (adc_atten_t)in_cfg->cfg.patterns[i].atten;
            out_cfg->cfg.patterns[i].bit_width = (adc_bitwidth_t)in_cfg->cfg.patterns[i].bit_width;
        }
    } else {
        out_cfg->cfg.single_unit.unit_id = (adc_unit_t)in_cfg->cfg.single_unit.unit_id;
        out_cfg->cfg.single_unit.atten = (adc_atten_t)in_cfg->cfg.single_unit.atten;
        out_cfg->cfg.single_unit.bit_width = (adc_bitwidth_t)in_cfg->cfg.single_unit.bit_width;
        memcpy(out_cfg->cfg.single_unit.channel_id, in_cfg->cfg.single_unit.channel_id, in_cfg->pattern_num);
    }
}
#endif  /* SOC_ADC_SUPPORTED && CONFIG_CODEC_DATA_ADC_SUPPORT */

#if SOC_ADC_SUPPORTED && CONFIG_ESP_BOARD_PERIPH_ADC_SUPPORT && CONFIG_CODEC_DATA_ADC_SUPPORT
static esp_err_t fill_adc_data_cfg_from_periph(dev_audio_codec_config_t *codec_cfg, periph_adc_handle_t *adc_periph_handle,
                                               audio_codec_adc_cfg_t *out_cfg)
{
    if (adc_periph_handle == NULL || out_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    periph_adc_config_t *adc_periph_cfg = NULL;
    esp_err_t ret = esp_board_periph_get_config(codec_cfg->adc_cfg.periph_name, (void **)&adc_periph_cfg);
    if (ret != ESP_OK || adc_periph_cfg == NULL) {
        ESP_LOGE(TAG, "Get ADC peripheral config failed, name:%s err:%s", codec_cfg->adc_cfg.periph_name, esp_err_to_name(ret));
        return ESP_ERR_NOT_FOUND;
    }

    if (adc_periph_cfg->role != ESP_BOARD_PERIPH_ROLE_CONTINUOUS) {
        ESP_LOGE(TAG, "ADC peripheral %s must be continuous mode", codec_cfg->adc_cfg.periph_name);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (adc_periph_cfg->cfg.continuous.pattern_num == 0 ||
        adc_periph_cfg->cfg.continuous.pattern_num > SOC_ADC_PATT_LEN_MAX) {
        ESP_LOGE(TAG, "Invalid ADC pattern_num:%" PRIu32, adc_periph_cfg->cfg.continuous.pattern_num);
        return ESP_ERR_INVALID_ARG;
    }

    out_cfg->handle = adc_periph_handle->continuous;
    out_cfg->continuous_cfg.max_store_buf_size = adc_periph_cfg->cfg.continuous.handle_cfg.max_store_buf_size;
    out_cfg->continuous_cfg.conv_frame_size = adc_periph_cfg->cfg.continuous.handle_cfg.conv_frame_size;
    out_cfg->continuous_cfg.sample_freq_hz = adc_periph_cfg->cfg.continuous.sample_freq_hz;
    out_cfg->continuous_cfg.conv_mode = adc_periph_cfg->cfg.continuous.conv_mode;
    out_cfg->continuous_cfg.format = adc_periph_cfg->cfg.continuous.format;
    out_cfg->continuous_cfg.pattern_num = adc_periph_cfg->cfg.continuous.pattern_num;
    out_cfg->continuous_cfg.cfg_mode = (adc_periph_cfg->cfg.continuous.cfg_mode == PERIPH_ADC_CONTINUOUS_CFG_MODE_PATTERN) ? AUDIO_CODEC_ADC_CFG_MODE_PATTERN : AUDIO_CODEC_ADC_CFG_MODE_SINGLE_UNIT;
    if (adc_periph_cfg->cfg.continuous.cfg_mode == PERIPH_ADC_CONTINUOUS_CFG_MODE_PATTERN) {
        memcpy(out_cfg->continuous_cfg.cfg.patterns, adc_periph_cfg->cfg.continuous.cfg.patterns,
               sizeof(adc_digi_pattern_config_t) * adc_periph_cfg->cfg.continuous.pattern_num);
    } else {
        out_cfg->continuous_cfg.cfg.single_unit.unit_id = (adc_unit_t)adc_periph_cfg->cfg.continuous.cfg.single_unit.unit_id;
        out_cfg->continuous_cfg.cfg.single_unit.atten = (adc_atten_t)adc_periph_cfg->cfg.continuous.cfg.single_unit.atten;
        out_cfg->continuous_cfg.cfg.single_unit.bit_width = (adc_bitwidth_t)adc_periph_cfg->cfg.continuous.cfg.single_unit.bit_width;
        memcpy(out_cfg->continuous_cfg.cfg.single_unit.channel_id, adc_periph_cfg->cfg.continuous.cfg.single_unit.channel_id,
               adc_periph_cfg->cfg.continuous.pattern_num);
    }
    return ESP_OK;
}
#endif  /* SOC_ADC_SUPPORTED && CONFIG_ESP_BOARD_PERIPH_ADC_SUPPORT && CONFIG_CODEC_DATA_ADC_SUPPORT */

int dev_audio_codec_init(void *cfg, int cfg_size, void **device_handle)
{
    if (cfg == NULL || cfg_size != sizeof(dev_audio_codec_config_t) || device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid init argument cfg:%p size:%d device_handle:%p", cfg, cfg_size, device_handle);
        return -1;
    }
    dev_audio_codec_config_t *codec_cfg = (dev_audio_codec_config_t *)cfg;

    dev_audio_codec_handles_t *codec_handles = (dev_audio_codec_handles_t *)calloc(1, sizeof(dev_audio_codec_handles_t));
    if (codec_handles == NULL) {
        ESP_LOGE(TAG, "Failed to allocate codec handles");
        return -1;
    }
    codec_handles->tx_aux_out_io = -1;

    esp_codec_dev_type_t dev_type = ESP_CODEC_DEV_TYPE_NONE;
    audio_codec_i2s_cfg_t i2s_cfg = {0};
    /* Balance esp_board_periph_ref_handle / unref: track successful refs only (see init_err). */
    int i2s_periph_ref_count = 0;
    bool adc_periph_refd = false;
    bool i2c_periph_refd = false;

    if (!codec_cfg->adc_enabled && !codec_cfg->dac_enabled) {
        ESP_LOGE(TAG, "Neither ADC nor DAC enabled");
        goto init_err;
    }

    if (codec_cfg->data_if_type == DEV_AUDIO_CODEC_DATA_IF_TYPE_I2S) {
        if (codec_cfg->adc_enabled) {
            esp_err_t ret_i2s = esp_board_periph_ref_handle(codec_cfg->i2s_cfg.name, &i2s_cfg.rx_handle);
            if (ret_i2s != ESP_OK) {
                ESP_LOGE(TAG, "Failed to ref I2S peripheral %s for RX, err:%s",
                         codec_cfg->i2s_cfg.name ? codec_cfg->i2s_cfg.name : "(null)", esp_err_to_name(ret_i2s));
                goto init_err;
            }
            i2s_periph_ref_count++;
            // I2S Clock is generated by TX, when creating codec_dev_data_if, a TX handle needs to be provided
            if (i2s_cfg.rx_handle != NULL) {
                i2s_chan_info_t channel_info = {0};
                i2s_channel_get_info(i2s_cfg.rx_handle, &channel_info);
                i2s_cfg.tx_handle = channel_info.pair_chan;
            }
            ESP_LOGI(TAG, "ADC over I2S is enabled");
            dev_type |= ESP_CODEC_DEV_TYPE_IN;
        }
        if (codec_cfg->dac_enabled) {
            codec_handles->gpio_if = (audio_codec_gpio_if_t *)audio_codec_new_gpio();
            esp_err_t ret_i2s = esp_board_periph_ref_handle(codec_cfg->i2s_cfg.name, &i2s_cfg.tx_handle);
            if (ret_i2s != ESP_OK) {
                ESP_LOGE(TAG, "Failed to ref I2S peripheral %s for TX, err:%s",
                         codec_cfg->i2s_cfg.name ? codec_cfg->i2s_cfg.name : "(null)", esp_err_to_name(ret_i2s));
                goto init_err;
            }
            i2s_periph_ref_count++;
            ESP_LOGI(TAG, "DAC is ENABLED");
            dev_type |= ESP_CODEC_DEV_TYPE_OUT;
        }
        i2s_cfg.port = codec_cfg->i2s_cfg.port;
        i2s_cfg.clk_src = codec_cfg->i2s_cfg.clk_src;
        codec_handles->data_if = (audio_codec_data_if_t *)audio_codec_new_i2s_data(&i2s_cfg);
    }
    else if (codec_cfg->data_if_type == DEV_AUDIO_CODEC_DATA_IF_TYPE_ADC) {
#if SOC_ADC_SUPPORTED && CONFIG_CODEC_DATA_ADC_SUPPORT
        if (codec_cfg->dac_enabled) {
            ESP_LOGE(TAG, "ADC data_if only supports input, but dac_enabled is true in config");
            goto init_err;
        }

        audio_codec_adc_cfg_t adc_data_cfg = {0};
        if (codec_cfg->adc_cfg.periph_name != NULL && codec_cfg->adc_cfg.periph_name[0] != '\0') {
#ifdef CONFIG_ESP_BOARD_PERIPH_ADC_SUPPORT
            periph_adc_handle_t *adc_periph_handle = NULL;
            esp_err_t ret = esp_board_periph_ref_handle(codec_cfg->adc_cfg.periph_name, (void **)&adc_periph_handle);
            if (ret != ESP_OK || adc_periph_handle == NULL) {
                ESP_LOGE(TAG, "Failed to ref adc peripheral %s, err:%s", codec_cfg->adc_cfg.periph_name, esp_err_to_name(ret));
                goto init_err;
            }
            adc_periph_refd = true;
            ret = fill_adc_data_cfg_from_periph(codec_cfg, adc_periph_handle, &adc_data_cfg);
            if (ret != ESP_OK) {
                goto init_err;
            }
            ESP_LOGI(TAG, "Using reused ADC peripheral %s", codec_cfg->adc_cfg.periph_name);
#else
            ESP_LOGE(TAG, "No available ADC peripherals");
            goto init_err;
#endif  /* CONFIG_ESP_BOARD_PERIPH_ADC_SUPPORT */
        } else if (codec_cfg->adc_cfg.pattern_num > 0) {
            adc_data_cfg.handle = NULL;
            fill_audio_adc_continuous_cfg_from_board(&adc_data_cfg.continuous_cfg, &codec_cfg->adc_cfg);
            ESP_LOGI(TAG, "Using local ADC config");
        } else {
            ESP_LOGE(TAG, "ADC data_if requires available ADC peripherals or valid adc local config");
            goto init_err;
        }
        codec_handles->data_if = audio_codec_new_adc_data(&adc_data_cfg);
        dev_type |= ESP_CODEC_DEV_TYPE_IN;
#else
        ESP_LOGE(TAG, "ADC data_if is not supported on this chip");
        goto init_err;
#endif  /* SOC_ADC_SUPPORTED && CONFIG_CODEC_DATA_ADC_SUPPORT */
    } else {
        ESP_LOGE(TAG, "Invalid data_if_type: %u", codec_cfg->data_if_type);
        goto init_err;
    }

    if (codec_handles->data_if == NULL) {
        ESP_LOGE(TAG, "Failed to create data interface");
        goto init_err;
    }

#ifdef CONFIG_ESP_BOARD_PERIPH_I2S_SUPPORT
    if (codec_cfg->data_if_type == DEV_AUDIO_CODEC_DATA_IF_TYPE_I2S && codec_cfg->i2s_cfg.tx_aux_out_io >= 0) {
        if (configure_i2s_tx_aux_output(codec_cfg, codec_handles) != ESP_OK) {
            goto init_err;
        }
    }
#endif  /* CONFIG_ESP_BOARD_PERIPH_I2S_SUPPORT */

    const char *codec_name = codec_cfg->chip;
    bool use_codec_if = true;
    bool use_dummy_codec = false;
    if (strcmp(codec_cfg->chip, "internal") == 0) {
        if (codec_cfg->dac_enabled) {
            codec_name = "dummy";
            use_dummy_codec = true;
            ESP_LOGI(TAG, "Using internal chip with dummy codec for PA control");
        } else {
            use_codec_if = false;
            ESP_LOGI(TAG, "Using internal chip input-only, codec_if is NULL");
        }
    }

    if (use_codec_if) {
        audio_codec_i2c_cfg_t i2c_cfg = {0};
        if (!use_dummy_codec) {
            if (codec_cfg->i2c_cfg.name == NULL || codec_cfg->i2c_cfg.name[0] == '\0') {
                ESP_LOGE(TAG, "External codec requires valid i2c peripheral");
                goto init_err;
            }
            if (esp_board_periph_ref_handle(codec_cfg->i2c_cfg.name, &i2c_cfg.bus_handle) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to ref i2c peripheral %s", codec_cfg->i2c_cfg.name);
                goto init_err;
            }
            i2c_periph_refd = true;
            i2c_cfg.addr = codec_cfg->i2c_cfg.address;
            codec_handles->ctrl_if = (audio_codec_ctrl_if_t *)audio_codec_new_i2c_ctrl(&i2c_cfg);
            if (codec_handles->ctrl_if == NULL) {
                ESP_LOGE(TAG, "Failed to create i2c ctrl interface");
                goto init_err;
            }
        }

        // Dynamic codec selection based on name
        const codec_registry_entry_t *codec_entry = find_codec(codec_name);
        if (codec_entry == NULL) {
            ESP_LOGE(TAG, "Unsupported codec: %s", codec_name);
            goto init_err;
        }

        // Allocate codec-specific configuration
        void *codec_specific_cfg = malloc(codec_entry->cfg_size);
        if (codec_specific_cfg == NULL) {
            ESP_LOGE(TAG, "Failed to allocate codec configuration");
            goto init_err;
        }
        memset(codec_specific_cfg, 0, codec_entry->cfg_size);

        // Setup codec-specific configuration
        if (codec_entry->config_setup_func != NULL) {
            if (codec_entry->config_setup_func(codec_cfg, codec_handles, codec_specific_cfg) != 0) {
                ESP_LOGE(TAG, "Setup codec config failed: %s", codec_name);
                free(codec_specific_cfg);
                goto init_err;
            }
        }

        // Create codec using factory function
        codec_handles->codec_if = codec_entry->factory_func(codec_specific_cfg);
        if (codec_handles->codec_if == NULL) {
            ESP_LOGE(TAG, "Failed to create codec: %s", codec_cfg->name);
            free(codec_specific_cfg);
            goto init_err;
        }

        ESP_LOGI(TAG, "Successfully initialized codec: %s", codec_cfg->name);
        free(codec_specific_cfg);
    }

    // New output codec device
    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = codec_handles->codec_if,
        .data_if = codec_handles->data_if,
        .dev_type = dev_type,
    };
    codec_handles->codec_dev = esp_codec_dev_new(&dev_cfg);
    if (codec_handles->codec_dev == NULL) {
        ESP_LOGE(TAG, "Create esp_codec_dev failed");
        goto init_err;
    }
    ESP_LOGI(TAG, "Create esp_codec_dev success, dev:%p, chip:%s", codec_handles->codec_dev, codec_cfg->chip);
    *device_handle = codec_handles;
    return 0;

init_err:
    if (codec_handles) {
        if (codec_handles->codec_if != NULL) {
            audio_codec_delete_codec_if(codec_handles->codec_if);
            codec_handles->codec_if = NULL;
        }
        if (codec_handles->ctrl_if != NULL) {
            audio_codec_delete_ctrl_if(codec_handles->ctrl_if);
            codec_handles->ctrl_if = NULL;
        }
        if (codec_handles->gpio_if != NULL) {
            audio_codec_delete_gpio_if(codec_handles->gpio_if);
            codec_handles->gpio_if = NULL;
        }
        if (codec_handles->data_if != NULL) {
            audio_codec_delete_data_if(codec_handles->data_if);
            codec_handles->data_if = NULL;
        }
        if (codec_handles->tx_aux_out_io >= 0) {
            gpio_reset_pin((gpio_num_t)codec_handles->tx_aux_out_io);
            codec_handles->tx_aux_out_io = -1;
        }
        if (adc_periph_refd) {
            esp_board_periph_unref_handle(codec_cfg->adc_cfg.periph_name);
        }
        if (i2c_periph_refd) {
            esp_board_periph_unref_handle(codec_cfg->i2c_cfg.name);
        }
        if (codec_cfg->data_if_type == DEV_AUDIO_CODEC_DATA_IF_TYPE_I2S &&
            codec_cfg->i2s_cfg.name != NULL && codec_cfg->i2s_cfg.name[0] != '\0') {
            for (int ur = 0; ur < i2s_periph_ref_count; ur++) {
                esp_board_periph_unref_handle(codec_cfg->i2s_cfg.name);
            }
        }
        free(codec_handles);
    }
    return -1;
}

int dev_audio_codec_deinit(void *device_handle)
{
    if (device_handle == NULL) {
        return -1;
    }
    dev_audio_codec_handles_t *codec_handles = (dev_audio_codec_handles_t *)device_handle;

    if (codec_handles->codec_dev) {
        esp_codec_dev_delete(codec_handles->codec_dev);
        codec_handles->codec_dev = NULL;
    }
    if (codec_handles->codec_if) {
        audio_codec_delete_codec_if(codec_handles->codec_if);
        codec_handles->codec_if = NULL;
    }
    if (codec_handles->ctrl_if) {
        audio_codec_delete_ctrl_if(codec_handles->ctrl_if);
        codec_handles->ctrl_if = NULL;
    }
    if (codec_handles->gpio_if) {
        audio_codec_delete_gpio_if(codec_handles->gpio_if);
        codec_handles->gpio_if = NULL;
    }
    if (codec_handles->data_if) {
        audio_codec_delete_data_if(codec_handles->data_if);
    }
    if (codec_handles->tx_aux_out_io >= 0) {
        gpio_reset_pin((gpio_num_t)codec_handles->tx_aux_out_io);
        codec_handles->tx_aux_out_io = -1;
    }

    dev_audio_codec_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (cfg) {
        if (cfg->data_if_type == DEV_AUDIO_CODEC_DATA_IF_TYPE_I2S &&
            cfg->i2s_cfg.name != NULL && cfg->i2s_cfg.name[0] != '\0') {
            int n = (cfg->adc_enabled ? 1 : 0) + (cfg->dac_enabled ? 1 : 0);
            for (int ur = 0; ur < n; ur++) {
                esp_board_periph_unref_handle(cfg->i2s_cfg.name);
            }
        }
        if (cfg->data_if_type == DEV_AUDIO_CODEC_DATA_IF_TYPE_ADC &&
            cfg->adc_cfg.periph_name != NULL && cfg->adc_cfg.periph_name[0] != '\0') {
            esp_board_periph_unref_handle(cfg->adc_cfg.periph_name);
        }
        if (strcmp(cfg->chip, "internal") != 0 &&
            cfg->i2c_cfg.name != NULL && cfg->i2c_cfg.name[0] != '\0') {
            esp_board_periph_unref_handle(cfg->i2c_cfg.name);
        }
    }

    codec_handles->data_if = NULL;
    free(codec_handles);
    return 0;
}
