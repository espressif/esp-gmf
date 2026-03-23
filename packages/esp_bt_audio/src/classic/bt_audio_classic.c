/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_err.h"
#include "esp_check.h"

#include "esp_sbc_dec.h"
#include "esp_sbc_enc.h"

#include "bt_audio_classic.h"
#include "bt_audio_a2dp.h"
#include "bt_audio_hfp.h"
#include "bt_audio_avrcp.h"
#include "bt_audio_pbac.h"

static bool is_inited = false;
static const char *TAG = "BT_AUD_CLASSIC";

esp_err_t bt_audio_classic_init(esp_bt_audio_classic_cfg_t *classic_config)
{
    if (is_inited) {
        ESP_LOGE(TAG, "Classic audio already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (classic_config == NULL) {
        ESP_LOGE(TAG, "Classic audio config is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    esp_sbc_dec_register();
    esp_sbc_enc_register();

#ifdef CONFIG_BT_AVRCP_ENABLED
    if (classic_config->roles & ESP_BT_AUDIO_CLASSIC_ROLE_AVRC_CT) {
        ESP_RETURN_ON_ERROR(bt_audio_avrcp_ct_init(), TAG, "Failed to init AVRC CT");
    }
    if (classic_config->roles & ESP_BT_AUDIO_CLASSIC_ROLE_AVRC_TG) {
        ESP_RETURN_ON_ERROR(bt_audio_avrcp_tg_init(), TAG, "Failed to init AVRC TG");
    }
#endif  /* CONFIG_BT_AVRCP_ENABLED */
#ifdef CONFIG_BT_A2DP_ENABLE
    if (classic_config->roles & ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC) {
        ESP_RETURN_ON_ERROR(bt_audio_a2dp_src_init(classic_config), TAG, "Failed to init A2DP SRC");
    }
    if (classic_config->roles & ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SNK) {
        ESP_RETURN_ON_ERROR(bt_audio_a2dp_sink_init(), TAG, "Failed to init A2DP SNK");
    }
#endif  /* CONFIG_BT_A2DP_ENABLE */
#ifdef CONFIG_BT_HFP_ENABLE
    if (classic_config->roles & ESP_BT_AUDIO_CLASSIC_ROLE_HFP_HF) {
        ESP_RETURN_ON_ERROR(bt_audio_hfp_hf_init(), TAG, "Failed to init HFP HF");
    }
#endif  /* CONFIG_BT_HFP_ENABLE */
#ifdef CONFIG_BT_PBAC_ENABLED
    if (classic_config->roles & ESP_BT_AUDIO_CLASSIC_ROLE_PBAP_PCE) {
        ESP_RETURN_ON_ERROR(bt_audio_pbac_init(), TAG, "Failed to init PBAC");
    }
#endif  /* CONFIG_BT_PBAC_ENABLED */
    is_inited = true;

    return ESP_OK;
}

esp_err_t bt_audio_classic_deinit()
{
    if (!is_inited) {
        return ESP_OK;
    }

#ifdef CONFIG_BT_A2DP_ENABLE
    bt_audio_a2dp_src_deinit();
    bt_audio_a2dp_sink_deinit();
#endif  /* CONFIG_BT_A2DP_ENABLE */
#ifdef CONFIG_BT_AVRCP_ENABLED
    bt_audio_avrcp_ct_deinit();
    bt_audio_avrcp_tg_deinit();
#endif  /* CONFIG_BT_AVRCP_ENABLED */
#ifdef CONFIG_BT_HFP_ENABLE
    bt_audio_hfp_hf_deinit();
#endif  /* CONFIG_BT_HFP_ENABLE */
#ifdef CONFIG_BT_PBAC_ENABLED
    bt_audio_pbac_deinit();
#endif  /* CONFIG_BT_PBAC_ENABLED */

    is_inited = false;
    return ESP_OK;
}
