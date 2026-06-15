/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <errno.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp_ble_audio_bap_api.h"
#include "esp_ble_audio_common_api.h"
#include "esp_ble_audio_defs.h"

#include "bt_audio_le_stream.h"
#include "bt_audio_le_unicast_server.h"

#define BT_AUDIO_LE_UNFRAMED_SUPPORTED  true
#define BT_AUDIO_LE_PREF_PHY            ESP_BLE_ISO_PHY_2M
#define BT_AUDIO_LE_PREF_RTN            2
#define BT_AUDIO_LE_PREF_LATENCY_MS     10

/**
 * @brief  Runtime context for the BAP unicast server.
 */
typedef struct {
    bt_audio_le_stream_t **sink_streams;    /*!< Allocated sink ASE stream wrappers */
    bt_audio_le_stream_t **source_streams;  /*!< Allocated source ASE stream wrappers */
    uint8_t                sink_count;      /*!< Number of sink streams */
    uint8_t                source_count;    /*!< Number of source streams */
} bt_audio_le_unicast_server_ctx_t;

static const char *TAG = "BT_AUD_LE_US";
static bt_audio_le_unicast_server_ctx_t *s_us;

static const esp_ble_audio_bap_qos_cfg_pref_t s_qos_pref = ESP_BLE_AUDIO_BAP_QOS_CFG_PREF(
    BT_AUDIO_LE_UNFRAMED_SUPPORTED,
    BT_AUDIO_LE_PREF_PHY,
    BT_AUDIO_LE_PREF_RTN,
    BT_AUDIO_LE_PREF_LATENCY_MS,
    20000,
    40000,
    20000,
    40000);

static inline bt_audio_le_stream_t *bt_audio_le_unicast_server_find_free(esp_ble_audio_dir_t dir)
{
    bt_audio_le_stream_t **streams = (dir == ESP_BLE_AUDIO_DIR_SINK) ? s_us->sink_streams : s_us->source_streams;
    uint8_t count = (dir == ESP_BLE_AUDIO_DIR_SINK) ? s_us->sink_count : s_us->source_count;

    for (uint8_t i = 0; i < count; i++) {
        if (streams[i] && streams[i]->bap_stream.conn == NULL) {
            return streams[i];
        }
    }
    return NULL;
}

static int bt_audio_le_unicast_server_config_cb(esp_ble_conn_t *conn,
                                                const esp_ble_audio_bap_ep_t *ep,
                                                esp_ble_audio_dir_t dir,
                                                const esp_ble_audio_codec_cfg_t *codec_cfg,
                                                esp_ble_audio_bap_stream_t **stream,
                                                esp_ble_audio_bap_qos_cfg_pref_t *const pref,
                                                esp_ble_audio_bap_ascs_rsp_t *rsp)
{
    (void)conn;
    (void)ep;
    (void)codec_cfg;
    bt_audio_le_stream_t *le_stream = bt_audio_le_unicast_server_find_free(dir);
    if (!le_stream) {
        *rsp = ESP_BLE_AUDIO_BAP_ASCS_RSP(ESP_BLE_AUDIO_BAP_ASCS_RSP_CODE_NO_MEM,
                                          ESP_BLE_AUDIO_BAP_ASCS_REASON_NONE);
        ESP_LOGW(TAG, "Configure unicast stream: no free stream for dir %u", dir);
        return -ENOMEM;
    }

    le_stream->base.profile = ESP_BT_AUDIO_STREAM_PROFILE_LE_UNICAST;
    le_stream->base.direction = (dir == ESP_BLE_AUDIO_DIR_SINK) ? ESP_BT_AUDIO_STREAM_DIR_SINK : ESP_BT_AUDIO_STREAM_DIR_SOURCE;
    *stream = &le_stream->bap_stream;
    *pref = s_qos_pref;
    ESP_LOGD(TAG, "Configured LE unicast stream dir %u", dir);
    return 0;
}

static int bt_audio_le_unicast_server_reconfig_cb(esp_ble_audio_bap_stream_t *stream,
                                                  esp_ble_audio_dir_t dir,
                                                  const esp_ble_audio_codec_cfg_t *codec_cfg,
                                                  esp_ble_audio_bap_qos_cfg_pref_t *const pref,
                                                  esp_ble_audio_bap_ascs_rsp_t *rsp)
{
    (void)stream;
    (void)dir;
    (void)codec_cfg;
    *pref = s_qos_pref;
    *rsp = ESP_BLE_AUDIO_BAP_ASCS_RSP(ESP_BLE_AUDIO_BAP_ASCS_RSP_CODE_SUCCESS,
                                      ESP_BLE_AUDIO_BAP_ASCS_REASON_NONE);
    return 0;
}

static int bt_audio_le_unicast_server_qos_cb(esp_ble_audio_bap_stream_t *stream,
                                             const esp_ble_audio_bap_qos_cfg_t *qos,
                                             esp_ble_audio_bap_ascs_rsp_t *rsp)
{
    bt_audio_le_stream_t *le_stream = NULL;
    if (bt_audio_le_stream_find_by_bap_stream(stream, &le_stream) != ESP_OK) {
        *rsp = ESP_BLE_AUDIO_BAP_ASCS_RSP(ESP_BLE_AUDIO_BAP_ASCS_RSP_CODE_INVALID_ASE_STATE,
                                          ESP_BLE_AUDIO_BAP_ASCS_REASON_NONE);
        ESP_LOGW(TAG, "QoS config: BAP stream is invalid");
        return -EINVAL;
    }
    ESP_LOGI(TAG, "QoS config: presentation delay %u, max SDU %u", qos->pd, qos->sdu);
    le_stream->presentation_delay = qos->pd;
    le_stream->max_sdu = qos->sdu;
    *rsp = ESP_BLE_AUDIO_BAP_ASCS_RSP(ESP_BLE_AUDIO_BAP_ASCS_RSP_CODE_SUCCESS,
                                      ESP_BLE_AUDIO_BAP_ASCS_REASON_NONE);
    return 0;
}

static int bt_audio_le_unicast_server_enable_cb(esp_ble_audio_bap_stream_t *stream,
                                                const uint8_t meta[],
                                                size_t meta_len,
                                                esp_ble_audio_bap_ascs_rsp_t *rsp)
{
    (void)stream;
    (void)meta;
    (void)meta_len;
    *rsp = ESP_BLE_AUDIO_BAP_ASCS_RSP(ESP_BLE_AUDIO_BAP_ASCS_RSP_CODE_SUCCESS,
                                      ESP_BLE_AUDIO_BAP_ASCS_REASON_NONE);
    return 0;
}

static int bt_audio_le_unicast_server_start_cb(esp_ble_audio_bap_stream_t *stream,
                                               esp_ble_audio_bap_ascs_rsp_t *rsp)
{
    (void)stream;
    *rsp = ESP_BLE_AUDIO_BAP_ASCS_RSP(ESP_BLE_AUDIO_BAP_ASCS_RSP_CODE_SUCCESS,
                                      ESP_BLE_AUDIO_BAP_ASCS_REASON_NONE);
    return 0;
}

/**
 * @brief  Temporary state used while validating ASE metadata.
 */
typedef struct {
    esp_ble_audio_bap_ascs_rsp_t *rsp;                     /*!< ASCS response to update on validation errors */
    bool                          stream_context_present;  /*!< Stream context metadata was present */
    bool                          rejected;                /*!< Metadata has been rejected */
} bt_audio_le_metadata_parse_t;

static inline bool bt_audio_le_metadata_validate(uint8_t type, const uint8_t *data, uint8_t data_len, void *user_data)
{
    (void)data;
    bt_audio_le_metadata_parse_t *parse = user_data;

    if (!ESP_BLE_AUDIO_METADATA_TYPE_IS_KNOWN(type)) {
        ESP_LOGE(TAG, "Invalid metadata type %u or length %u", type, data_len);
        *parse->rsp = ESP_BLE_AUDIO_BAP_ASCS_RSP(ESP_BLE_AUDIO_BAP_ASCS_RSP_CODE_METADATA_REJECTED, type);
        parse->rejected = true;
        return false;
    }
    if (type == ESP_BLE_AUDIO_METADATA_TYPE_STREAM_CONTEXT) {
        parse->stream_context_present = true;
    }
    return true;
}

static int bt_audio_le_unicast_server_metadata_cb(esp_ble_audio_bap_stream_t *stream,
                                                  const uint8_t meta[],
                                                  size_t meta_len,
                                                  esp_ble_audio_bap_ascs_rsp_t *rsp)
{
    (void)stream;
    bt_audio_le_metadata_parse_t parse = {
        .rsp = rsp,
    };
    esp_err_t err = esp_ble_audio_data_parse(meta, meta_len, bt_audio_le_metadata_validate, &parse);
    if (err) {
        ESP_LOGE(TAG, "Metadata config failed: parse error %s", esp_err_to_name(err));
        return -EIO;
    }
    if (parse.rejected) {
        ESP_LOGE(TAG, "Metadata config failed: metadata rejected");
        return -EINVAL;
    }
    if (!parse.stream_context_present) {
        ESP_LOGE(TAG, "Stream audio context not present on peer");
        *rsp = ESP_BLE_AUDIO_BAP_ASCS_RSP(ESP_BLE_AUDIO_BAP_ASCS_RSP_CODE_METADATA_REJECTED,
                                          ESP_BLE_AUDIO_BAP_ASCS_REASON_NONE);
        return -EINVAL;
    }
    return 0;
}

static int bt_audio_le_unicast_server_disable_cb(esp_ble_audio_bap_stream_t *stream,
                                                 esp_ble_audio_bap_ascs_rsp_t *rsp)
{
    bt_audio_le_stream_t *le_stream = NULL;
    bt_audio_le_stream_find_by_bap_stream(stream, &le_stream);
    if (le_stream) {
        le_stream->started = false;
    }
    *rsp = ESP_BLE_AUDIO_BAP_ASCS_RSP(ESP_BLE_AUDIO_BAP_ASCS_RSP_CODE_SUCCESS,
                                      ESP_BLE_AUDIO_BAP_ASCS_REASON_NONE);
    return 0;
}

static int bt_audio_le_unicast_server_stop_cb(esp_ble_audio_bap_stream_t *stream,
                                              esp_ble_audio_bap_ascs_rsp_t *rsp)
{
    bt_audio_le_stream_t *le_stream = NULL;
    bt_audio_le_stream_find_by_bap_stream(stream, &le_stream);
    if (le_stream) {
        le_stream->started = false;
    }
    *rsp = ESP_BLE_AUDIO_BAP_ASCS_RSP(ESP_BLE_AUDIO_BAP_ASCS_RSP_CODE_SUCCESS,
                                      ESP_BLE_AUDIO_BAP_ASCS_REASON_NONE);
    return 0;
}

static int bt_audio_le_unicast_server_release_cb(esp_ble_audio_bap_stream_t *stream,
                                                 esp_ble_audio_bap_ascs_rsp_t *rsp)
{
    bt_audio_le_stream_t *le_stream = NULL;
    bt_audio_le_stream_find_by_bap_stream(stream, &le_stream);
    if (le_stream) {
        le_stream->started = false;
    }
    *rsp = ESP_BLE_AUDIO_BAP_ASCS_RSP(ESP_BLE_AUDIO_BAP_ASCS_RSP_CODE_SUCCESS,
                                      ESP_BLE_AUDIO_BAP_ASCS_REASON_NONE);
    return 0;
}

static esp_ble_audio_bap_unicast_server_cb_t s_unicast_server_cb = {
    .config   = bt_audio_le_unicast_server_config_cb,
    .reconfig = bt_audio_le_unicast_server_reconfig_cb,
    .qos      = bt_audio_le_unicast_server_qos_cb,
    .enable   = bt_audio_le_unicast_server_enable_cb,
    .start    = bt_audio_le_unicast_server_start_cb,
    .metadata = bt_audio_le_unicast_server_metadata_cb,
    .disable  = bt_audio_le_unicast_server_disable_cb,
    .stop     = bt_audio_le_unicast_server_stop_cb,
    .release  = bt_audio_le_unicast_server_release_cb,
};

static inline esp_err_t bt_audio_le_unicast_server_create_streams(bt_audio_le_stream_t ***streams, uint8_t count)
{
    if (count == 0) {
        *streams = NULL;
        return ESP_OK;
    }

    *streams = heap_caps_calloc_prefer(count, sizeof(bt_audio_le_stream_t *), 2,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                       MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(*streams, ESP_ERR_NO_MEM, TAG, "No memory for stream list");

    for (uint8_t i = 0; i < count; i++) {
        ESP_RETURN_ON_ERROR(bt_audio_le_stream_create(&(*streams)[i]), TAG, "Failed to create LE stream");
    }
    return ESP_OK;
}

esp_err_t bt_audio_le_unicast_server_init(const esp_bt_audio_le_cfg_t *cfg, bt_audio_le_adv_builder_t adv_builder)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "LE config is NULL");
    ESP_RETURN_ON_FALSE(!s_us, ESP_ERR_INVALID_STATE, TAG, "Unicast server already initialized");

    s_us = heap_caps_calloc_prefer(1, sizeof(*s_us), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(s_us, ESP_ERR_NO_MEM, TAG, "No memory for unicast server");

    s_us->sink_count = cfg->snk_cnt ? cfg->snk_cnt : CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT;
    s_us->source_count = cfg->src_cnt ? cfg->src_cnt : CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT;
    if (s_us->sink_count > CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT) {
        s_us->sink_count = CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT;
    }
    if (s_us->source_count > CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT) {
        s_us->source_count = CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT;
    }

    esp_ble_audio_bap_unicast_server_register_param_t param = {
        .snk_cnt = s_us->sink_count,
        .src_cnt = s_us->source_count,
    };
    ESP_GOTO_ON_ERROR(esp_ble_audio_bap_unicast_server_register(&param), fail, TAG,
                      "Failed to register BAP unicast server");
    ESP_GOTO_ON_ERROR(esp_ble_audio_bap_unicast_server_register_cb(&s_unicast_server_cb), fail, TAG,
                      "Failed to register BAP unicast callbacks");
    ESP_GOTO_ON_ERROR(bt_audio_le_unicast_server_create_streams(&s_us->sink_streams, s_us->sink_count), fail, TAG,
                      "Failed to create sink streams");
    ESP_GOTO_ON_ERROR(bt_audio_le_unicast_server_create_streams(&s_us->source_streams, s_us->source_count), fail, TAG,
                      "Failed to create source streams");

    if (adv_builder) {
        bt_audio_le_adv_builder_add_service_uuid16(adv_builder, ESP_BLE_AUDIO_UUID_ASCS_VAL);
        uint8_t svc_data[] = {
            (uint8_t)(ESP_BLE_AUDIO_UUID_ASCS_VAL & 0xFF),
            (uint8_t)((ESP_BLE_AUDIO_UUID_ASCS_VAL >> 8) & 0xFF),
            ESP_BLE_AUDIO_UNICAST_ANNOUNCEMENT_TARGETED,
            (uint8_t)(cfg->pacs.sink_context_mask & 0xFF),
            (uint8_t)((cfg->pacs.sink_context_mask >> 8) & 0xFF),
            (uint8_t)(cfg->pacs.source_context_mask & 0xFF),
            (uint8_t)((cfg->pacs.source_context_mask >> 8) & 0xFF),
            0x00,
        };
        bt_audio_le_adv_builder_add_service_data(adv_builder, svc_data, sizeof(svc_data));
    }

    return ESP_OK;

fail:
    bt_audio_le_unicast_server_deinit();
    ESP_LOGE(TAG, "Init unicast server failed: %s", esp_err_to_name(ret));
    return ret;
}

void bt_audio_le_unicast_server_deinit(void)
{
    if (!s_us) {
        return;
    }

    for (uint8_t i = 0; i < s_us->sink_count; i++) {
        bt_audio_le_stream_destroy(s_us->sink_streams ? s_us->sink_streams[i] : NULL);
    }
    for (uint8_t i = 0; i < s_us->source_count; i++) {
        bt_audio_le_stream_destroy(s_us->source_streams ? s_us->source_streams[i] : NULL);
    }
    heap_caps_free(s_us->sink_streams);
    s_us->sink_streams = NULL;
    heap_caps_free(s_us->source_streams);
    s_us->source_streams = NULL;
    heap_caps_free(s_us);
    s_us = NULL;
    esp_ble_audio_bap_unicast_server_unregister();
}
