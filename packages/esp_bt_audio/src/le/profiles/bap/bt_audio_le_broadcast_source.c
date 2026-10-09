/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <stdbool.h>

#include "bt_audio_host_ops.h"
#include "bt_audio_le_broadcast_source.h"
#include "bt_audio_le_stream.h"
#include "bt_audio_le_tx_group.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"

#include "esp_ble_audio_bap_api.h"
#include "esp_ble_audio_codec_api.h"
#include "esp_ble_audio_defs.h"
#include "esp_ble_iso_common_api.h"
#include "bt_audio_le_lc3_preset.h"

/* net_buf_simple types are provided by esp_ble_iso through the Zephyr compatibility layer. */

#define BT_AUDIO_LE_BSRC_CC_LTV_MAX          8

/**
 * @brief  Runtime context for the broadcast source profile.
 */
typedef struct {
    esp_ble_audio_bap_broadcast_source_t *source;                                               /*!< BLE Audio broadcast source instance */
    bt_audio_le_stream_t                **streams;                                              /*!< Source stream wrappers */
    bt_audio_le_tx_group_t               *tx_group;                                             /*!< Shared BIG transmit pacer */
    esp_ble_audio_bap_lc3_preset_t        preset;                                               /*!< Active LC3 preset */
    uint8_t                               codec_data[CONFIG_BT_AUDIO_CODEC_CFG_MAX_DATA_SIZE];  /*!< Codec specific data buffer */
    uint8_t                               codec_meta[CONFIG_BT_AUDIO_CODEC_CFG_MAX_METADATA_SIZE]; /*!< Codec metadata buffer */
    uint8_t                               stream_count;                                         /*!< Number of source streams */
    bool                                  big_adv_added;                                        /*!< BIG has been associated with extended advertising */
} bt_audio_le_broadcast_source_ctx_t;

typedef struct {
    esp_bt_audio_le_bsrc_lc3_preset_t     id;      /*!< Public preset identifier */
    const esp_ble_audio_bap_lc3_preset_t *preset;  /*!< Official BAP LC3 preset template */
} bt_audio_le_bsrc_preset_entry_t;

static const char *TAG = "BT_AUD_LE_BSRC";
static bt_audio_le_broadcast_source_ctx_t *s_bsrc;

/* Official BAP broadcast presets. Templates are const; the active mutable copy is stored in the PSRAM context. */
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_8_1_1_DEFINE(s_preset_8_1_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                    ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_8_1_2_DEFINE(s_preset_8_1_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                    ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_8_2_1_DEFINE(s_preset_8_2_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                    ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_8_2_2_DEFINE(s_preset_8_2_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                    ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_16_1_1_DEFINE(s_preset_16_1_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_16_1_2_DEFINE(s_preset_16_1_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_16_2_1_DEFINE(s_preset_16_2_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_16_2_2_DEFINE(s_preset_16_2_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_24_1_1_DEFINE(s_preset_24_1_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_24_1_2_DEFINE(s_preset_24_1_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_24_2_1_DEFINE(s_preset_24_2_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_24_2_2_DEFINE(s_preset_24_2_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_32_1_1_DEFINE(s_preset_32_1_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_32_1_2_DEFINE(s_preset_32_1_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_32_2_1_DEFINE(s_preset_32_2_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_32_2_2_DEFINE(s_preset_32_2_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_441_1_1_DEFINE(s_preset_441_1_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                      ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_441_1_2_DEFINE(s_preset_441_1_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                      ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_441_2_1_DEFINE(s_preset_441_2_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                      ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_441_2_2_DEFINE(s_preset_441_2_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                      ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_48_1_1_DEFINE(s_preset_48_1_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_48_1_2_DEFINE(s_preset_48_1_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_48_3_1_DEFINE(s_preset_48_3_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_48_3_2_DEFINE(s_preset_48_3_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_48_5_1_DEFINE(s_preset_48_5_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_48_5_2_DEFINE(s_preset_48_5_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_48_2_1_DEFINE(s_preset_48_2_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_48_2_2_DEFINE(s_preset_48_2_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_48_4_1_DEFINE(s_preset_48_4_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_48_4_2_DEFINE(s_preset_48_4_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_48_6_1_DEFINE(s_preset_48_6_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_48_6_2_DEFINE(s_preset_48_6_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);

static const bt_audio_le_bsrc_preset_entry_t s_preset_table[] = {
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_8KHZ_7_5MS_26B_HQ, &s_preset_8_1_1 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_8KHZ_7_5MS_26B_HR, &s_preset_8_1_2 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_8KHZ_10MS_30B_HQ, &s_preset_8_2_1 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_8KHZ_10MS_30B_HR, &s_preset_8_2_2 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_16KHZ_7_5MS_30B_HQ, &s_preset_16_1_1 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_16KHZ_7_5MS_30B_HR, &s_preset_16_1_2 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_16KHZ_10MS_40B_HQ, &s_preset_16_2_1 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_16KHZ_10MS_40B_HR, &s_preset_16_2_2 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_24KHZ_7_5MS_45B_HQ, &s_preset_24_1_1 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_24KHZ_7_5MS_45B_HR, &s_preset_24_1_2 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_24KHZ_10MS_60B_HQ, &s_preset_24_2_1 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_24KHZ_10MS_60B_HR, &s_preset_24_2_2 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_32KHZ_7_5MS_60B_HQ, &s_preset_32_1_1 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_32KHZ_7_5MS_60B_HR, &s_preset_32_1_2 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_32KHZ_10MS_80B_HQ, &s_preset_32_2_1 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_32KHZ_10MS_80B_HR, &s_preset_32_2_2 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_44_1KHZ_7_5MS_97B_HQ, &s_preset_441_1_1 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_44_1KHZ_7_5MS_97B_HR, &s_preset_441_1_2 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_44_1KHZ_10MS_130B_HQ, &s_preset_441_2_1 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_44_1KHZ_10MS_130B_HR, &s_preset_441_2_2 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_7_5MS_75B_HQ, &s_preset_48_1_1 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_7_5MS_75B_HR, &s_preset_48_1_2 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_7_5MS_90B_HQ, &s_preset_48_3_1 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_7_5MS_90B_HR, &s_preset_48_3_2 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_7_5MS_117B_HQ, &s_preset_48_5_1 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_7_5MS_117B_HR, &s_preset_48_5_2 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_10MS_100B_HQ, &s_preset_48_2_1 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_10MS_100B_HR, &s_preset_48_2_2 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_10MS_120B_HQ, &s_preset_48_4_1 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_10MS_120B_HR, &s_preset_48_4_2 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_10MS_155B_HQ, &s_preset_48_6_1 },
    { ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_10MS_155B_HR, &s_preset_48_6_2 },
};

NET_BUF_SIMPLE_DEFINE(s_base_buf, 128);

static esp_err_t bt_audio_le_broadcast_source_set_periodic_data(uint8_t adv_handle)
{
    uint8_t per_adv_data[256];
    size_t pos = 0;
    uint8_t type = BT_AUDIO_AD_TYPE_SVC_DATA_UUID16;

    /* Retrieve BASE from the BAP broadcast source. */
    net_buf_simple_reset(&s_base_buf);
    ESP_RETURN_ON_ERROR(esp_ble_audio_bap_broadcast_source_get_base(s_bsrc->source, &s_base_buf),
                        TAG, "Failed to get broadcast BASE");

    /* Build periodic advertising data: [length][AD type][BASE data]. */
    uint8_t per_len = s_base_buf.len + 1;
    if (pos + 1 + 1 + s_base_buf.len > sizeof(per_adv_data)) {
        ESP_LOGE(TAG, "Periodic adv data too large");
        return ESP_ERR_NO_MEM;
    }
    per_adv_data[pos++] = per_len;
    per_adv_data[pos++] = type;
    memcpy(per_adv_data + pos, s_base_buf.data, s_base_buf.len);
    pos += s_base_buf.len;

    esp_err_t ret = bt_audio_host_periodic_adv_set_data(adv_handle, per_adv_data, pos);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set periodic adv data failed");
    }
    return ret;
}

esp_err_t bt_audio_le_broadcast_source_start_periodic_adv(uint8_t adv_handle)
{
    bt_audio_periodic_adv_params_t params = {0};

    ESP_RETURN_ON_FALSE(s_bsrc && s_bsrc->source, ESP_ERR_INVALID_STATE, TAG, "Broadcast source not initialized");

    params.include_tx_power = false;
    params.itvl_min = BT_AUDIO_PERIODIC_ADV_ITVL_MS(100);
    params.itvl_max = BT_AUDIO_PERIODIC_ADV_ITVL_MS(100);
    esp_err_t ret = bt_audio_host_periodic_adv_configure(adv_handle, &params);
    ESP_RETURN_ON_FALSE(ret == ESP_OK, ESP_FAIL, TAG, "Failed to configure periodic adv");

    ESP_RETURN_ON_ERROR(bt_audio_le_broadcast_source_set_periodic_data(adv_handle), TAG,
                        "Failed to set periodic adv data");
    ret = bt_audio_host_periodic_adv_start(adv_handle);
    ESP_RETURN_ON_FALSE(ret == ESP_OK, ESP_FAIL, TAG, "Failed to start periodic adv");
    return ESP_OK;
}

esp_err_t bt_audio_le_broadcast_source_start(uint8_t adv_handle)
{
    esp_ble_iso_ext_adv_info_t info = {
        .adv_handle = adv_handle,
    };

    ESP_RETURN_ON_FALSE(s_bsrc && s_bsrc->source, ESP_ERR_INVALID_STATE, TAG, "Broadcast source not initialized");
    for (uint8_t i = 0; i < s_bsrc->stream_count; i++) {
        bt_audio_le_stream_dispatch_allocated(s_bsrc->streams[i]);
    }
    if (!s_bsrc->big_adv_added) {
        ESP_RETURN_ON_ERROR(esp_ble_iso_big_ext_adv_add(&info), TAG, "Failed to add BIG ext adv");
        s_bsrc->big_adv_added = true;
    }
    esp_err_t ret = esp_ble_audio_bap_broadcast_source_start(s_bsrc->source, adv_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start broadcast source failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t bt_audio_le_broadcast_source_stop(void)
{
    ESP_RETURN_ON_FALSE(s_bsrc && s_bsrc->source, ESP_ERR_INVALID_STATE, TAG, "Broadcast source not initialized");
    esp_err_t ret = esp_ble_audio_bap_broadcast_source_stop(s_bsrc->source);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Stop broadcast source failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static const esp_ble_audio_bap_lc3_preset_t *bt_audio_le_bsrc_select_preset(esp_bt_audio_le_bsrc_lc3_preset_t preset)
{
    if (preset == ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_DEFAULT) {
        preset = ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_10MS_100B_HQ;
    }
    for (size_t i = 0; i < sizeof(s_preset_table) / sizeof(s_preset_table[0]); i++) {
        if (s_preset_table[i].id == preset) {
            return s_preset_table[i].preset;
        }
    }
    return NULL;
}

static esp_err_t bt_audio_le_bsrc_init_preset(bt_audio_le_broadcast_source_ctx_t *ctx,
                                              const esp_ble_audio_bap_lc3_preset_t *src)
{
    ESP_RETURN_ON_FALSE(src && src->codec_cfg.data && src->codec_cfg.meta, ESP_ERR_INVALID_ARG, TAG,
                        "Invalid LC3 preset template");
    ESP_RETURN_ON_FALSE(src->codec_cfg.data_len <= sizeof(ctx->codec_data), ESP_ERR_INVALID_SIZE, TAG,
                        "LC3 codec data too large");
    ESP_RETURN_ON_FALSE(src->codec_cfg.meta_len <= sizeof(ctx->codec_meta), ESP_ERR_INVALID_SIZE, TAG,
                        "LC3 codec metadata too large");

    memcpy(ctx->codec_data, src->codec_cfg.data, src->codec_cfg.data_len);
    memcpy(ctx->codec_meta, src->codec_cfg.meta, src->codec_cfg.meta_len);
    ctx->preset = *src;
    ctx->preset.codec_cfg.data = ctx->codec_data;
    ctx->preset.codec_cfg.meta = ctx->codec_meta;
    return ESP_OK;
}

esp_err_t bt_audio_le_broadcast_source_init(const esp_bt_audio_le_cfg_t *cfg, bt_audio_le_adv_builder_t adv_builder)
{
    esp_err_t ret = ESP_OK;
    uint8_t stream_cc[CONFIG_BT_BAP_BROADCAST_SRC_STREAM_COUNT][BT_AUDIO_LE_BSRC_CC_LTV_MAX];

    ESP_RETURN_ON_FALSE(cfg && adv_builder, ESP_ERR_INVALID_ARG, TAG, "Invalid broadcast source args");
    ESP_RETURN_ON_FALSE(!s_bsrc, ESP_ERR_INVALID_STATE, TAG, "Broadcast source already initialized");
    ESP_RETURN_ON_FALSE(cfg->bsrc.stream_num > 0, ESP_ERR_INVALID_ARG, TAG, "stream_num must be at least 1");
    ESP_RETURN_ON_FALSE(cfg->bsrc.stream_num <= CONFIG_BT_BAP_BROADCAST_SRC_STREAM_COUNT, ESP_ERR_INVALID_ARG, TAG,
                        "stream_num %u exceeds CONFIG_BT_BAP_BROADCAST_SRC_STREAM_COUNT %u",
                        cfg->bsrc.stream_num, (unsigned)CONFIG_BT_BAP_BROADCAST_SRC_STREAM_COUNT);

    const esp_ble_audio_bap_lc3_preset_t *preset_tmpl = bt_audio_le_bsrc_select_preset(cfg->bsrc.lc3_preset);
    ESP_RETURN_ON_FALSE(preset_tmpl, ESP_ERR_INVALID_ARG, TAG, "Unsupported LC3 broadcast preset %d",
                        (int)cfg->bsrc.lc3_preset);

    uint8_t stream_count = cfg->bsrc.stream_num;

    const uint32_t supported_locations = ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT |
                                         ESP_BT_AUDIO_AUDIO_LOC_FRONT_RIGHT;
    uint32_t configured_locations = 0;
    for (uint8_t i = 0; i < stream_count; i++) {
        uint32_t loc = (i < ESP_BT_AUDIO_LE_BSRC_STREAM_MAX) ? cfg->bsrc.stream_locations[i] : 0;
        ESP_RETURN_ON_FALSE((loc & ~supported_locations) == 0, ESP_ERR_INVALID_ARG, TAG,
                            "Unsupported location 0x%08lx for stream %u", loc, i);
        ESP_RETURN_ON_FALSE(loc == 0 || (loc & (loc - 1)) == 0, ESP_ERR_INVALID_ARG, TAG,
                            "Stream %u must contain at most one audio location", i);
        ESP_RETURN_ON_FALSE(loc == 0 || (configured_locations & loc) == 0, ESP_ERR_INVALID_ARG, TAG,
                            "Audio location 0x%08lx is assigned more than once", loc);
        configured_locations |= loc;
    }

    s_bsrc = heap_caps_calloc_prefer(1, sizeof(*s_bsrc), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(s_bsrc, ESP_ERR_NO_MEM, TAG, "No memory for broadcast source");
    ESP_GOTO_ON_ERROR(bt_audio_le_bsrc_init_preset(s_bsrc, preset_tmpl), fail, TAG,
                      "Failed to initialize LC3 broadcast preset");
    s_bsrc->stream_count = stream_count;
    s_bsrc->streams = heap_caps_calloc_prefer(stream_count, sizeof(*s_bsrc->streams), 2,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_GOTO_ON_FALSE(s_bsrc->streams, ESP_ERR_NO_MEM, fail, TAG, "No memory for broadcast streams");
    ESP_GOTO_ON_ERROR(bt_audio_le_tx_group_create("bsrc_tx", &s_bsrc->tx_group), fail, TAG,
                      "Failed to create broadcast TX group");
    ESP_GOTO_ON_ERROR(bt_audio_le_tx_group_set_task_cfg(s_bsrc->tx_group,
                                                        cfg->src_send_task_core_id,
                                                        cfg->src_send_task_prio,
                                                        cfg->src_send_task_stack_size),
                      fail, TAG, "Invalid LE source send task configuration");

    esp_ble_audio_bap_broadcast_source_stream_param_t stream_params[CONFIG_BT_BAP_BROADCAST_SRC_STREAM_COUNT];
    memset(stream_params, 0, sizeof(stream_params));
    for (uint8_t i = 0; i < stream_count; i++) {
        uint32_t loc = (i < ESP_BT_AUDIO_LE_BSRC_STREAM_MAX) ? cfg->bsrc.stream_locations[i] : 0;
        if (loc == 0) {
            loc = ESP_BLE_AUDIO_LOCATION_MONO_AUDIO;
        }
        uint8_t loc_ltv[] = {
            ESP_BLE_AUDIO_CODEC_DATA(ESP_BLE_AUDIO_CODEC_CFG_CHAN_ALLOC, BT_BYTES_LIST_LE32(loc))
        };
        memcpy(stream_cc[i], loc_ltv, sizeof(loc_ltv));

        ESP_GOTO_ON_ERROR(bt_audio_le_stream_create(&s_bsrc->streams[i]), fail, TAG,
                          "Failed to create broadcast stream");
        s_bsrc->streams[i]->base.profile = ESP_BT_AUDIO_STREAM_PROFILE_LE_BROADCAST;
        s_bsrc->streams[i]->base.direction = ESP_BT_AUDIO_STREAM_DIR_SOURCE;
        s_bsrc->streams[i]->base.context = ESP_BT_AUDIO_STREAM_CONTEXT_MEDIA;
        ESP_GOTO_ON_ERROR(bt_audio_le_tx_group_add(s_bsrc->tx_group, s_bsrc->streams[i]),
                          fail, TAG, "Failed to add BIS stream to broadcast TX group");
        stream_params[i].stream = &s_bsrc->streams[i]->bap_stream;
        stream_params[i].data = stream_cc[i];
        stream_params[i].data_len = sizeof(loc_ltv);
    }

    uint32_t subgroup_loc = configured_locations ? configured_locations : ESP_BLE_AUDIO_LOCATION_MONO_AUDIO;
    ESP_GOTO_ON_ERROR(esp_ble_audio_codec_cfg_set_chan_allocation(&s_bsrc->preset.codec_cfg, subgroup_loc),
                      fail, TAG, "Failed to set subgroup channel allocation");

    esp_ble_audio_bap_broadcast_source_subgroup_param_t subgroup = {
        .params_count = stream_count,
        .params = stream_params,
        .codec_cfg = &s_bsrc->preset.codec_cfg,
    };
    esp_ble_audio_bap_broadcast_source_param_t create_param = {
        .params_count = 1,
        .params = &subgroup,
        .qos = &s_bsrc->preset.qos,
        .packing = ESP_BLE_ISO_PACKING_SEQUENTIAL,
        .encryption = cfg->bsrc.broadcast_code[0] != 0,
    };
    if (create_param.encryption) {
        memcpy(create_param.broadcast_code, cfg->bsrc.broadcast_code, ESP_BLE_ISO_BROADCAST_CODE_SIZE);
    }

    ESP_GOTO_ON_ERROR(esp_ble_audio_bap_broadcast_source_create(&create_param, &s_bsrc->source),
                      fail, TAG, "Failed to create broadcast source");

    uint32_t broadcast_id = esp_random() & 0xFFFFFFU;
    uint8_t svc_data[] = {
        (uint8_t)(ESP_BLE_AUDIO_UUID_BROADCAST_AUDIO_VAL & 0xFF),
        (uint8_t)((ESP_BLE_AUDIO_UUID_BROADCAST_AUDIO_VAL >> 8) & 0xFF),
        (uint8_t)(broadcast_id & 0xFF),
        (uint8_t)((broadcast_id >> 8) & 0xFF),
        (uint8_t)((broadcast_id >> 16) & 0xFF),
    };
    ESP_GOTO_ON_ERROR(bt_audio_le_adv_builder_add_service_data(adv_builder, svc_data, sizeof(svc_data)),
                      fail, TAG, "Failed to add broadcast service data");
    if (cfg->bsrc.broadcast_name[0]) {
        size_t name_len = strnlen((const char *)cfg->bsrc.broadcast_name, sizeof(cfg->bsrc.broadcast_name));
        ESP_GOTO_ON_ERROR(bt_audio_le_adv_builder_add_field(adv_builder, BT_AUDIO_AD_TYPE_BROADCAST_NAME,
                                                            cfg->bsrc.broadcast_name, name_len),
                          fail, TAG, "Failed to add broadcast name");
    }

    return ESP_OK;

fail:
    bt_audio_le_broadcast_source_deinit();
    ESP_LOGE(TAG, "Init broadcast source failed: %s", esp_err_to_name(ret));
    return ret;
}

void bt_audio_le_broadcast_source_deinit(void)
{
    if (!s_bsrc) {
        return;
    }
    if (s_bsrc->source) {
        esp_ble_audio_bap_broadcast_source_stop(s_bsrc->source);
        esp_ble_audio_bap_broadcast_source_delete(s_bsrc->source);
    }
    bt_audio_le_tx_group_destroy(s_bsrc->tx_group);
    s_bsrc->tx_group = NULL;
    for (uint8_t i = 0; i < s_bsrc->stream_count; i++) {
        bt_audio_le_stream_destroy(s_bsrc->streams ? s_bsrc->streams[i] : NULL);
    }
    heap_caps_free(s_bsrc->streams);
    s_bsrc->streams = NULL;
    heap_caps_free(s_bsrc);
    s_bsrc = NULL;
}
