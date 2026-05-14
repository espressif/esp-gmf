/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp_ble_audio_aics_api.h"
#include "esp_ble_audio_defs.h"
#include "esp_ble_audio_vcp_api.h"
#include "esp_ble_audio_vocs_api.h"

#include "bt_audio_evt_dispatcher.h"
#include "bt_audio_le_vcp.h"
#include "bt_audio_ops.h"

static const char *TAG = "BT_AUD_LE_VCP";

static esp_ble_audio_vocs_register_param_t *s_vocs_param;
static char (*s_vocs_desc)[32];
static esp_ble_audio_aics_register_param_t *s_aics_param;
static char (*s_aics_desc)[32];

static esp_err_t bt_audio_le_vcp_set_absolute(uint32_t vol)
{
    if (vol > 100) {
        vol = 100;
    }
    esp_err_t ret = esp_ble_audio_vcp_vol_rend_set_vol((uint8_t)((vol * 255) / 100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set absolute volume failed: vol %lu, err %s", (unsigned long)vol, esp_err_to_name(ret));
    }
    return ret;
}

static void bt_audio_le_vcp_state_cb(struct bt_conn *conn, int err, uint8_t volume, uint8_t mute)
{
    (void)conn;
    if (err) {
        ESP_LOGW(TAG, "VCP state callback failed: %d", err);
        return;
    }

    esp_bt_audio_event_vol_absolute_t event = {
        .context = ESP_BT_AUDIO_STREAM_CONTEXT_MEDIA,
        .mute = mute != 0,
        .vol = (uint8_t)((volume * 100 + 127) / 255),
    };
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_VOL_ABSOLUTE, &event);
}

static void bt_audio_le_vcp_flags_cb(struct bt_conn *conn, int err, uint8_t flags)
{
    (void)conn;
    if (err == 0) {
        ESP_LOGD(TAG, "VCP flags 0x%02x", flags);
    }
}

static void bt_audio_le_vocs_state_cb(esp_ble_audio_vocs_t *inst, int err, int16_t offset)
{
    (void)inst;
    (void)offset;
    if (err) {
        ESP_LOGD(TAG, "VOCS state callback failed: %d", err);
    }
}

static void bt_audio_le_vocs_location_cb(esp_ble_audio_vocs_t *inst, int err, uint32_t location)
{
    (void)inst;
    (void)location;
    if (err) {
        ESP_LOGD(TAG, "VOCS location callback failed: %d", err);
    }
}

static void bt_audio_le_vocs_description_cb(esp_ble_audio_vocs_t *inst, int err, char *description)
{
    (void)inst;
    (void)description;
    if (err) {
        ESP_LOGD(TAG, "VOCS description callback failed: %d", err);
    }
}

static void bt_audio_le_aics_state_cb(esp_ble_audio_aics_t *inst, int err, int8_t gain, uint8_t mute, uint8_t mode)
{
    (void)inst;
    (void)gain;
    (void)mute;
    (void)mode;
    if (err) {
        ESP_LOGD(TAG, "AICS state callback failed: %d", err);
    }
}

static void bt_audio_le_aics_gain_setting_cb(esp_ble_audio_aics_t *inst, int err, uint8_t units,
                                             int8_t minimum, int8_t maximum)
{
    (void)inst;
    (void)units;
    (void)minimum;
    (void)maximum;
    if (err) {
        ESP_LOGD(TAG, "AICS gain callback failed: %d", err);
    }
}

static void bt_audio_le_aics_type_cb(esp_ble_audio_aics_t *inst, int err, uint8_t input_type)
{
    (void)inst;
    (void)input_type;
    if (err) {
        ESP_LOGD(TAG, "AICS type callback failed: %d", err);
    }
}

static void bt_audio_le_aics_status_cb(esp_ble_audio_aics_t *inst, int err, bool active)
{
    (void)inst;
    (void)active;
    if (err) {
        ESP_LOGD(TAG, "AICS status callback failed: %d", err);
    }
}

static void bt_audio_le_aics_description_cb(esp_ble_audio_aics_t *inst, int err, char *description)
{
    (void)inst;
    (void)description;
    if (err) {
        ESP_LOGD(TAG, "AICS description callback failed: %d", err);
    }
}

static esp_ble_audio_vcp_vol_rend_cb_t s_vcp_cbs = {
    .state = bt_audio_le_vcp_state_cb,
    .flags = bt_audio_le_vcp_flags_cb,
};

static esp_ble_audio_vocs_cb_t s_vocs_cbs = {
    .state       = bt_audio_le_vocs_state_cb,
    .location    = bt_audio_le_vocs_location_cb,
    .description = bt_audio_le_vocs_description_cb,
};

static esp_ble_audio_aics_cb_t s_aics_cbs = {
    .state        = bt_audio_le_aics_state_cb,
    .gain_setting = bt_audio_le_aics_gain_setting_cb,
    .type         = bt_audio_le_aics_type_cb,
    .status       = bt_audio_le_aics_status_cb,
    .description  = bt_audio_le_aics_description_cb,
};

esp_err_t bt_audio_le_vcp_init(const esp_bt_audio_le_vcp_cfg_t *cfg, bt_audio_le_adv_builder_t adv_builder)
{
    esp_err_t ret = ESP_OK;
    esp_ble_audio_vcp_vol_rend_register_param_t param = {0};

#if CONFIG_BT_VCP_VOL_REND_VOCS_INSTANCE_COUNT > 0
    s_vocs_param = heap_caps_calloc_prefer(CONFIG_BT_VCP_VOL_REND_VOCS_INSTANCE_COUNT,
                                           sizeof(*s_vocs_param), 2,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                           MALLOC_CAP_DEFAULT);
    ESP_GOTO_ON_FALSE(s_vocs_param, ESP_ERR_NO_MEM, fail, TAG, "No memory for VOCS params");
    s_vocs_desc = heap_caps_calloc_prefer(CONFIG_BT_VCP_VOL_REND_VOCS_INSTANCE_COUNT, sizeof(*s_vocs_desc), 2,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                          MALLOC_CAP_DEFAULT);
    ESP_GOTO_ON_FALSE(s_vocs_desc, ESP_ERR_NO_MEM, fail, TAG, "No memory for VOCS descriptions");
    for (size_t i = 0; i < CONFIG_BT_VCP_VOL_REND_VOCS_INSTANCE_COUNT; i++) {
        s_vocs_param[i].location_writable = true;
        s_vocs_param[i].desc_writable = true;
        snprintf(s_vocs_desc[i], sizeof(s_vocs_desc[i]), "Output %u", (unsigned)(i + 1));
        s_vocs_param[i].output_desc = s_vocs_desc[i];
        s_vocs_param[i].cb = &s_vocs_cbs;
    }
    param.vocs_param = s_vocs_param;
#endif  /* CONFIG_BT_VCP_VOL_REND_VOCS_INSTANCE_COUNT > 0 */

#if CONFIG_BT_VCP_VOL_REND_AICS_INSTANCE_COUNT > 0
    s_aics_param = heap_caps_calloc_prefer(CONFIG_BT_VCP_VOL_REND_AICS_INSTANCE_COUNT,
                                           sizeof(*s_aics_param), 2,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                           MALLOC_CAP_DEFAULT);
    ESP_GOTO_ON_FALSE(s_aics_param, ESP_ERR_NO_MEM, fail, TAG, "No memory for AICS params");
    s_aics_desc = heap_caps_calloc_prefer(CONFIG_BT_VCP_VOL_REND_AICS_INSTANCE_COUNT, sizeof(*s_aics_desc), 2,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                          MALLOC_CAP_DEFAULT);
    ESP_GOTO_ON_FALSE(s_aics_desc, ESP_ERR_NO_MEM, fail, TAG, "No memory for AICS descriptions");
    for (size_t i = 0; i < CONFIG_BT_VCP_VOL_REND_AICS_INSTANCE_COUNT; i++) {
        s_aics_param[i].gain_mode = ESP_BLE_AUDIO_AICS_MODE_MANUAL;
        s_aics_param[i].units = 1;
        s_aics_param[i].min_gain = -100;
        s_aics_param[i].max_gain = 100;
        s_aics_param[i].type = ESP_BLE_AUDIO_AICS_INPUT_TYPE_UNSPECIFIED;
        s_aics_param[i].status = true;
        s_aics_param[i].desc_writable = true;
        snprintf(s_aics_desc[i], sizeof(s_aics_desc[i]), "Input %u", (unsigned)(i + 1));
        s_aics_param[i].description = s_aics_desc[i];
        s_aics_param[i].cb = &s_aics_cbs;
    }
    param.aics_param = s_aics_param;
#endif  /* CONFIG_BT_VCP_VOL_REND_AICS_INSTANCE_COUNT > 0 */

    param.step = cfg ? cfg->step : 1;
    param.mute = cfg ? cfg->mute : ESP_BLE_AUDIO_VCP_STATE_UNMUTED;
    param.volume = cfg ? cfg->volume : 10;
    param.cb = &s_vcp_cbs;

    if (adv_builder) {
        ESP_GOTO_ON_ERROR(bt_audio_le_adv_builder_add_service_uuid16(adv_builder, ESP_BLE_AUDIO_UUID_VCS_VAL),
                          fail, TAG, "Failed to add VCS UUID");
#if CONFIG_BT_VCP_VOL_REND_AICS_INSTANCE_COUNT > 0
        ESP_GOTO_ON_ERROR(bt_audio_le_adv_builder_add_service_uuid16(adv_builder, ESP_BLE_AUDIO_UUID_AICS_VAL),
                          fail, TAG, "Failed to add AICS UUID");
#endif  /* CONFIG_BT_VCP_VOL_REND_AICS_INSTANCE_COUNT > 0 */
#if CONFIG_BT_VCP_VOL_REND_VOCS_INSTANCE_COUNT > 0
        ESP_GOTO_ON_ERROR(bt_audio_le_adv_builder_add_service_uuid16(adv_builder, ESP_BLE_AUDIO_UUID_VOCS_VAL),
                          fail, TAG, "Failed to add VOCS UUID");
#endif  /* CONFIG_BT_VCP_VOL_REND_VOCS_INSTANCE_COUNT > 0 */
    }

    ESP_GOTO_ON_ERROR(esp_ble_audio_vcp_vol_rend_register(&param), fail, TAG, "Failed to register VCP renderer");

    esp_bt_audio_vol_ops_t vol_ops = {0};
    ESP_GOTO_ON_ERROR(bt_audio_ops_get_vol(&vol_ops), fail, TAG, "Failed to get volume ops");
    vol_ops.set_absolute = bt_audio_le_vcp_set_absolute;
    ESP_GOTO_ON_ERROR(bt_audio_ops_set_vol(&vol_ops), fail, TAG, "Failed to set volume ops");

    return ESP_OK;

fail:
    bt_audio_le_vcp_deinit();
    ESP_LOGE(TAG, "Init VCP failed: %s", esp_err_to_name(ret));
    return ret;
}

void bt_audio_le_vcp_deinit(void)
{
    heap_caps_free(s_vocs_param);
    s_vocs_param = NULL;
    heap_caps_free(s_vocs_desc);
    s_vocs_desc = NULL;
    heap_caps_free(s_aics_param);
    s_aics_param = NULL;
    heap_caps_free(s_aics_desc);
    s_aics_desc = NULL;
}
