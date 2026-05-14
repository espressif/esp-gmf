/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "host/ble_gap.h"
#include "host/ble_hs_adv.h"
#include "os/os_mbuf.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"

#include "esp_ble_audio_bap_api.h"
#include "esp_ble_audio_bap_lc3_preset_defs.h"
#include "esp_ble_audio_defs.h"
#include "esp_ble_iso_common_api.h"

#include "bt_audio_le_broadcast_source.h"
#include "bt_audio_le_stream.h"

/* Broadcast Audio Announcement: Broadcast_Name AD type (assigned number 0x30) */
#define BT_AUDIO_LE_ADV_TYPE_BROADCAST_NAME  0x30U

/**
 * @brief  Runtime context for the broadcast source profile.
 */
typedef struct {
    esp_ble_audio_bap_broadcast_source_t *source;        /*!< BLE Audio broadcast source instance */
    bt_audio_le_stream_t                **streams;       /*!< Source stream wrappers */
    uint8_t                               stream_count;  /*!< Number of source streams */
} bt_audio_le_broadcast_source_ctx_t;

static const char *TAG = "BT_AUD_LE_BSRC";
static bt_audio_le_broadcast_source_ctx_t *s_bsrc;

NET_BUF_SIMPLE_DEFINE(s_base_buf, 128);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_48_2_1_DEFINE(s_preset, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);

static esp_err_t bt_audio_le_broadcast_source_set_periodic_data(uint8_t adv_handle)
{
    struct os_mbuf *per_adv_data = NULL;
    uint8_t per_len;
    uint8_t type = BLE_HS_ADV_TYPE_SVC_DATA_UUID16;
    int err;

    net_buf_simple_reset(&s_base_buf);
    ESP_RETURN_ON_ERROR(esp_ble_audio_bap_broadcast_source_get_base(s_bsrc->source, &s_base_buf),
                        TAG, "Failed to get broadcast BASE");

    per_len = s_base_buf.len + 1;
    per_adv_data = os_msys_get_pkthdr(sizeof(per_len) + sizeof(type) + s_base_buf.len, 0);
    ESP_RETURN_ON_FALSE(per_adv_data, ESP_ERR_NO_MEM, TAG, "Failed to allocate periodic adv data");

    err = os_mbuf_append(per_adv_data, &per_len, sizeof(per_len));
    err |= os_mbuf_append(per_adv_data, &type, sizeof(type));
    err |= os_mbuf_append(per_adv_data, s_base_buf.data, s_base_buf.len);
    if (err) {
        ESP_LOGE(TAG, "Set periodic adv data failed: mbuf append error");
        os_mbuf_free_chain(per_adv_data);
        return ESP_FAIL;
    }

    err = ble_gap_periodic_adv_set_data(adv_handle, per_adv_data);
    if (err) {
        ESP_LOGE(TAG, "Set periodic adv data failed: ble_gap_periodic_adv_set_data err %d", err);
        os_mbuf_free_chain(per_adv_data);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t bt_audio_le_broadcast_source_start_periodic_adv(uint8_t adv_handle)
{
    struct ble_gap_periodic_adv_params params = {0};
    int err;

    ESP_RETURN_ON_FALSE(s_bsrc && s_bsrc->source, ESP_ERR_INVALID_STATE, TAG, "Broadcast source not initialized");

    params.include_tx_power = 0;
    params.itvl_min = BLE_GAP_ADV_ITVL_MS(100);
    params.itvl_max = BLE_GAP_ADV_ITVL_MS(100);
    err = ble_gap_periodic_adv_configure(adv_handle, &params);
    ESP_RETURN_ON_FALSE(err == 0, ESP_FAIL, TAG, "Failed to configure periodic adv: %d", err);

    ESP_RETURN_ON_ERROR(bt_audio_le_broadcast_source_set_periodic_data(adv_handle), TAG,
                        "Failed to set periodic adv data");
    err = ble_gap_periodic_adv_start(adv_handle);
    if (err != 0) {
        ESP_LOGE(TAG, "Start periodic adv failed: GAP error %d", err);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t bt_audio_le_broadcast_source_start(uint8_t adv_handle)
{
    esp_ble_iso_ext_adv_info_t info = {
        .adv_handle = adv_handle,
    };

    ESP_RETURN_ON_FALSE(s_bsrc && s_bsrc->source, ESP_ERR_INVALID_STATE, TAG, "Broadcast source not initialized");
    ESP_RETURN_ON_ERROR(esp_ble_iso_big_ext_adv_add(&info), TAG, "Failed to add BIG ext adv");
    esp_err_t ret = esp_ble_audio_bap_broadcast_source_start(s_bsrc->source, adv_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start broadcast source failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t bt_audio_le_broadcast_source_init(const esp_bt_audio_le_cfg_t *cfg, bt_audio_le_adv_builder_t adv_builder)
{
    esp_err_t ret = ESP_OK;
    uint8_t mono[] = {
        ESP_BLE_AUDIO_CODEC_DATA(ESP_BLE_AUDIO_CODEC_CFG_CHAN_ALLOC,
                                 BT_BYTES_LIST_LE32(ESP_BLE_AUDIO_LOCATION_MONO_AUDIO))};

    ESP_RETURN_ON_FALSE(cfg && adv_builder, ESP_ERR_INVALID_ARG, TAG, "Invalid broadcast source args");
    ESP_RETURN_ON_FALSE(!s_bsrc, ESP_ERR_INVALID_STATE, TAG, "Broadcast source already initialized");

    uint8_t stream_count = cfg->bsrc.stream_num ? cfg->bsrc.stream_num : CONFIG_BT_BAP_BROADCAST_SRC_STREAM_COUNT;
    if (stream_count > CONFIG_BT_BAP_BROADCAST_SRC_STREAM_COUNT) {
        stream_count = CONFIG_BT_BAP_BROADCAST_SRC_STREAM_COUNT;
    }

    s_bsrc = heap_caps_calloc_prefer(1, sizeof(*s_bsrc), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(s_bsrc, ESP_ERR_NO_MEM, TAG, "No memory for broadcast source");
    s_bsrc->stream_count = stream_count;
    s_bsrc->streams = heap_caps_calloc_prefer(stream_count, sizeof(*s_bsrc->streams), 2,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_GOTO_ON_FALSE(s_bsrc->streams, ESP_ERR_NO_MEM, fail, TAG, "No memory for broadcast streams");

    esp_ble_audio_bap_broadcast_source_stream_param_t stream_params[CONFIG_BT_BAP_BROADCAST_SRC_STREAM_COUNT];
    memset(stream_params, 0, sizeof(stream_params));
    for (uint8_t i = 0; i < stream_count; i++) {
        ESP_GOTO_ON_ERROR(bt_audio_le_stream_create(&s_bsrc->streams[i]), fail, TAG,
                          "Failed to create broadcast stream");
        s_bsrc->streams[i]->base.profile = ESP_BT_AUDIO_STREAM_PROFILE_LE_BROADCAST;
        s_bsrc->streams[i]->base.direction = ESP_BT_AUDIO_STREAM_DIR_SOURCE;
        stream_params[i].stream = &s_bsrc->streams[i]->bap_stream;
        stream_params[i].data = mono;
        stream_params[i].data_len = sizeof(mono);
    }

    esp_ble_audio_bap_broadcast_source_subgroup_param_t subgroup = {
        .params_count = stream_count,
        .params = stream_params,
        .codec_cfg = &s_preset.codec_cfg,
    };
    esp_ble_audio_bap_broadcast_source_param_t create_param = {
        .params_count = 1,
        .params = &subgroup,
        .qos = &s_preset.qos,
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
        ESP_GOTO_ON_ERROR(bt_audio_le_adv_builder_add_field(adv_builder, BT_AUDIO_LE_ADV_TYPE_BROADCAST_NAME,
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
    for (uint8_t i = 0; i < s_bsrc->stream_count; i++) {
        bt_audio_le_stream_destroy(s_bsrc->streams ? s_bsrc->streams[i] : NULL);
    }
    heap_caps_free(s_bsrc->streams);
    s_bsrc->streams = NULL;
    heap_caps_free(s_bsrc);
    s_bsrc = NULL;
}
