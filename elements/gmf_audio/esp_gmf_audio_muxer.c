/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_gmf_node.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_mutex.h"
#include "esp_gmf_audio_muxer.h"
#include "esp_muxer.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_audio_element.h"
#include "esp_gmf_data_bus.h"
#include "esp_gmf_new_databus.h"
#include "gmf_audio_common.h"
#include "esp_muxer_default.h"
#include "esp_fourcc.h"
#include "esp_audio_types.h"

#define TAG                          "ESP_GMF_MUXER"
#define DEFAULT_DATABUS_SIZE         (1024)
#define DEFAULT_DATABUS_NUM          (10)
#define MUXER_DEFAULT_SLICE_DURATION (60000)
#define MUXER_MIN_PACKET_DURATION    (10)
#define MUXER_STREAM_RAM_CACHE_SIZE  (1024)
#define MUXER_FILE_RAM_CACHE_SIZE    (16 * 1024)

typedef union {
    ts_muxer_config_t   ts_cfg;
    mp4_muxer_config_t  mp4_cfg;
    flv_muxer_config_t  flv_cfg;
    wav_muxer_config_t  wav_cfg;
    caf_muxer_config_t  caf_cfg;
    ogg_muxer_config_t  ogg_cfg;
    avi_muxer_config_t  avi_cfg;
} audio_muxer_union_config_t;

/**
 * @brief  Muxer context in GMF
 */
typedef struct {
    esp_gmf_audio_element_t  parent;            /*!< The GMF muxer handle */
    esp_muxer_handle_t       muxer;             /*!< The muxer handle */
    esp_gmf_db_handle_t      out_db;            /*!< Output databus for streaming */
    int                      audio_stream_idx;  /*!< Audio stream index */
    int                      filled_size;       /*!< Filled size */
} esp_gmf_audio_muxer_t;

static void get_muxer_config(esp_muxer_type_t muxer_type, audio_muxer_union_config_t *muxer_cfg, uint32_t *muxer_cfg_size)
{
    *muxer_cfg_size = 0;
    switch (muxer_type) {
        case ESP_MUXER_TYPE_CAF:
        case ESP_MUXER_TYPE_FLV:
        case ESP_MUXER_TYPE_WAV:
            *muxer_cfg_size = sizeof(esp_muxer_config_t);
            break;
        case ESP_MUXER_TYPE_TS:
            *muxer_cfg_size = sizeof(ts_muxer_config_t);
            break;
        case ESP_MUXER_TYPE_OGG:
            *muxer_cfg_size = sizeof(ogg_muxer_config_t);
            break;
        case ESP_MUXER_TYPE_MP4:
            *muxer_cfg_size = sizeof(mp4_muxer_config_t);
            mp4_muxer_config_t *mp4_cfg = (mp4_muxer_config_t *)muxer_cfg;
            mp4_cfg->display_in_order = true;
            mp4_cfg->moov_before_mdat = true;
            break;
        case ESP_MUXER_TYPE_AVI:
            *muxer_cfg_size = sizeof(avi_muxer_config_t);
            avi_muxer_config_t *avi_cfg = (avi_muxer_config_t *)muxer_cfg;
            avi_cfg->index_type = AVI_MUXER_INDEX_AT_START;
            break;
        default:
            ESP_LOGE(TAG, "Unknown muxer type: %d", muxer_type);
            break;
    }
}

static inline esp_gmf_err_t dupl_esp_gmf_audio_muxer_cfg(esp_gmf_audio_muxer_cfg_t *config, esp_gmf_audio_muxer_cfg_t **new_config)
{
    *new_config = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_VERIFY(TAG, *new_config, return ESP_GMF_ERR_MEMORY_LACK, "muxer configuration", sizeof(*config));
    memcpy(*new_config, config, sizeof(*config));
    return ESP_GMF_ERR_OK;
}

static inline void free_esp_gmf_audio_muxer_cfg(esp_gmf_audio_muxer_cfg_t *config)
{
    if (config) {
        esp_gmf_oal_free(config);
    }
}

static int muxer_data_cb(esp_muxer_data_info_t *data, void *ctx)
{
    esp_gmf_audio_muxer_t *muxer_el = (esp_gmf_audio_muxer_t *)ctx;
    if (muxer_el->out_db && data && data->size > 0) {
        esp_gmf_data_bus_block_t blk = {0};
        esp_gmf_err_io_t ret = esp_gmf_db_acquire_write(muxer_el->out_db, &blk, data->size, ESP_GMF_MAX_DELAY);
        if (ret == ESP_GMF_IO_OK) {
            memcpy(blk.buf, data->data, data->size);
            blk.valid_size = data->size;
            esp_gmf_db_release_write(muxer_el->out_db, &blk, ESP_GMF_MAX_DELAY);
        } else {
            ESP_LOGW(TAG, "Failed to acquire write in callback, ret: %d", ret);
        }
    }
    return 0;
}

static esp_gmf_err_t muxer_caps_iter_fun(uint32_t attr_index, esp_gmf_cap_attr_t *attr)
{
    switch (attr_index) {
        case 0: {
            const static uint32_t support_muxer_type[] = {
                ESP_FOURCC_TS,
                ESP_FOURCC_MP4,
                ESP_FOURCC_FLV,
                ESP_FOURCC_WAV,
                ESP_FOURCC_CAF,
                ESP_FOURCC_OGG,
                ESP_FOURCC_AVI,
            };
            ESP_GMF_CAP_ATTR_SET_DISCRETE(attr, ESP_FOURCC_TO_INT('T', 'Y', 'P', 'E'), (uint32_t *)&support_muxer_type,
                                          sizeof(support_muxer_type) / sizeof(uint32_t), sizeof(uint32_t));
            break;
        }
        default:
            attr->prop_type = ESP_GMF_PROP_TYPE_NONE;
            return ESP_GMF_ERR_NOT_SUPPORT;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t esp_gmf_audio_muxer_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_audio_muxer_init((esp_gmf_audio_muxer_cfg_t *)cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t esp_gmf_audio_muxer_open(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_audio_muxer_t *muxer_el = (esp_gmf_audio_muxer_t *)self;
    esp_gmf_audio_muxer_cfg_t *cfg = (esp_gmf_audio_muxer_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_JOB_ERR_FAIL);
    muxer_url_pattern_ex url_pattern = NULL;
    muxer_data_callback data_cb = NULL;
    void *ctx = NULL;
    uint32_t ram_cache_size = 0;
    esp_gmf_err_t ret;
    esp_muxer_err_t muxer_ret;
    muxer_el->filled_size = 0;
    if (cfg->output_type == ESP_GMF_AUDIO_MUXER_OUTPUT_STREAMING) {
        ret = esp_gmf_db_new_block(DEFAULT_DATABUS_NUM, DEFAULT_DATABUS_SIZE, &muxer_el->out_db);
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to create databus, ret: %d", ret);
            return ESP_GMF_JOB_ERR_FAIL;
        }
        data_cb = muxer_data_cb;
        ctx = muxer_el;
        ram_cache_size = MUXER_STREAM_RAM_CACHE_SIZE;
    } else if (cfg->output_type == ESP_GMF_AUDIO_MUXER_OUTPUT_FILE) {
        if (cfg->url_pattern == NULL) {
            ESP_LOGE(TAG, "Choose file output mode, but URL pattern is not set");
            return ESP_GMF_JOB_ERR_FAIL;
        }
        url_pattern = cfg->url_pattern;
        ctx = cfg->url_ctx;
        ram_cache_size = MUXER_FILE_RAM_CACHE_SIZE;
    } else {
        ESP_LOGE(TAG, "Invalid muxer output type: %d", cfg->output_type);
        return ESP_GMF_JOB_ERR_FAIL;
    }
    audio_muxer_union_config_t muxer_cfg = {0};
    uint32_t cfg_size = 0;
    get_muxer_config(cfg->muxer_type, &muxer_cfg, &cfg_size);
    esp_muxer_config_t *muxer_baisc_cfg = (esp_muxer_config_t *)&muxer_cfg;
    muxer_baisc_cfg->muxer_type = cfg->muxer_type;
    muxer_baisc_cfg->slice_duration = (cfg->slice_duration > 0) ? cfg->slice_duration : MUXER_DEFAULT_SLICE_DURATION;
    muxer_baisc_cfg->url_pattern_ex = url_pattern;
    muxer_baisc_cfg->url_pattern = NULL;
    muxer_baisc_cfg->data_cb = data_cb;
    muxer_baisc_cfg->ctx = ctx;
    muxer_baisc_cfg->ram_cache_size = ram_cache_size;
    muxer_el->muxer = esp_muxer_open((esp_muxer_config_t *)&muxer_cfg, cfg_size);
    if (muxer_el->muxer == NULL) {
        ESP_LOGE(TAG, "Failed to open muxer");
        return ESP_GMF_JOB_ERR_FAIL;
    }
    esp_gmf_info_sound_t snd_info = {0};
    esp_gmf_audio_el_get_snd_info(self, &snd_info);
    esp_muxer_audio_stream_info_t audio_info = {
        .codec = cfg->codec,
        .sample_rate = (uint16_t)snd_info.sample_rates,
        .bits_per_sample = (uint8_t)snd_info.bits,
        .channel = (uint8_t)snd_info.channels,
        .min_packet_duration = MUXER_MIN_PACKET_DURATION,
        .codec_spec_info = NULL,
        .spec_info_len = 0,
    };
    if (cfg->get_codec_spec_info_cb != NULL && cfg->get_codec_spec_info_ctx != NULL) {
        esp_gmf_audio_helper_spec_info_t spec_info = {0};
        esp_gmf_err_t ret = cfg->get_codec_spec_info_cb(cfg->get_codec_spec_info_ctx, &spec_info);
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to get codec specific information from audio encoder, ret: %d", ret);
            return ESP_GMF_JOB_ERR_FAIL;
        }
        audio_info.codec_spec_info = spec_info.codec_spec_info;
        audio_info.spec_info_len = spec_info.spec_info_len;
    }
    muxer_el->audio_stream_idx = -1;
    muxer_ret = esp_muxer_add_audio_stream(muxer_el->muxer, &audio_info, &muxer_el->audio_stream_idx);
    if (muxer_ret != ESP_MUXER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to add audio stream, ret: %d", muxer_ret);
        return ESP_GMF_JOB_ERR_FAIL;
    }
    ESP_LOGI(TAG, "Open muxer element, type: %d, output_type: %d sample_rate: %d, channel: %d, bits: %d",
             cfg->muxer_type, cfg->output_type, snd_info.sample_rates, snd_info.channels, snd_info.bits);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_audio_muxer_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_port_t *in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_t *out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_audio_muxer_t *muxer_el = (esp_gmf_audio_muxer_t *)self;
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    esp_gmf_err_io_t load_ret = ESP_GMF_IO_OK;
    esp_gmf_payload_t *out_load = NULL;
    esp_gmf_payload_t *in_load = NULL;
    esp_muxer_err_t muxer_ret = ESP_MUXER_ERR_OK;
    uint32_t filled_size = 0;
    if (muxer_el->filled_size == 0 && muxer_el->muxer != NULL) {
        load_ret = esp_gmf_port_acquire_in(in_port, &in_load, ESP_GMF_ELEMENT_GET(muxer_el)->in_attr.data_size,
                                           in_port->wait_ticks);
        ESP_GMF_PORT_ACQUIRE_IN_CHECK(TAG, load_ret, out_len, goto __muxer_proc_release;);
        ESP_LOGV(TAG, "Acquire in load, load: %p, buf: %p, valid size: %d, buf length: %d, done: %d, pts: %lld",
                 in_load, in_load->buf, in_load->valid_size, in_load->buf_length, in_load->is_done, in_load->pts);
        if (in_load->valid_size == 0) {
            if (in_load->is_done) {
                out_len = ESP_GMF_JOB_ERR_DONE;
            } else {
                ESP_LOGE(TAG, "Invalid in load size %d", in_load->valid_size);
                out_len = ESP_GMF_JOB_ERR_FAIL;
            }
            goto __muxer_proc_release;
        }
        esp_muxer_audio_packet_t audio_packet = {
            .pts = in_load->pts,
            .data = in_load->buf,
            .len = in_load->valid_size,
        };
        muxer_ret = esp_muxer_add_audio_packet(muxer_el->muxer, muxer_el->audio_stream_idx, &audio_packet);
        if (muxer_ret != ESP_MUXER_ERR_OK) {
            ESP_LOGE(TAG, "Fail to add audio packet to muxer, ret: %d", muxer_ret);
            out_len = ESP_GMF_JOB_ERR_FAIL;
            goto __muxer_proc_release;
        }
    }
    if (muxer_el->out_db) {
        esp_gmf_db_get_filled_size(muxer_el->out_db, &filled_size);
        if (filled_size > 0) {
            load_ret = esp_gmf_port_acquire_out(out_port, &out_load, filled_size, ESP_GMF_MAX_DELAY);
            ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, load_ret, out_len, goto __muxer_proc_release;);
            esp_gmf_data_bus_block_t blk = {0};
            load_ret = esp_gmf_db_acquire_read(muxer_el->out_db, &blk, filled_size, ESP_GMF_MAX_DELAY);
            if (load_ret == ESP_GMF_IO_OK) {
                if (blk.valid_size > 0) {
                    memcpy(out_load->buf, blk.buf, blk.valid_size);
                    out_load->valid_size = blk.valid_size;
                    out_load->is_done = (in_load == NULL ? true : in_load->is_done);
                } else {
                    out_load->valid_size = 0;
                }
                esp_gmf_db_release_read(muxer_el->out_db, &blk, ESP_GMF_MAX_DELAY);
            } else {
                ESP_LOGE(TAG, "Failed to acquire read from databus, ret: %d", load_ret);
                out_len = ESP_GMF_JOB_ERR_FAIL;
                goto __muxer_proc_release;
            }
            if (out_load->is_done) {
                out_len = ESP_GMF_JOB_ERR_DONE;
            }
            ESP_LOGV(TAG, "Acquire out load, load: %p, buf: %p, valid size: %d, buf length: %d, done: %d, pts: %lld",
                     out_load, out_load->buf, out_load->valid_size, out_load->buf_length, out_load->is_done, out_load->pts);
        } else {
            out_len = ESP_GMF_JOB_ERR_CONTINUE;
        }
    } else {
        out_len = ESP_GMF_JOB_ERR_OK;
    }
__muxer_proc_release:
    if (out_len == ESP_GMF_JOB_ERR_DONE && muxer_el->muxer != NULL) {
        muxer_ret = esp_muxer_close(muxer_el->muxer);
        if (muxer_ret != ESP_MUXER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to close muxer, ret: %d", muxer_ret);
            out_len = ESP_GMF_JOB_ERR_FAIL;
        }
        muxer_el->muxer = NULL;
        if (muxer_el->out_db) {
            esp_gmf_db_get_filled_size(muxer_el->out_db, &filled_size);
            if (filled_size > 0) {
                out_len = ESP_GMF_JOB_ERR_TRUNCATE;
            }
        }
    }
    if (out_load != NULL) {
        load_ret = esp_gmf_port_release_out(out_port, out_load, out_port->wait_ticks);
        if ((load_ret < ESP_GMF_IO_OK) && (load_ret != ESP_GMF_IO_ABORT)) {
            ESP_LOGE(TAG, "OUT port release error, ret:%d", load_ret);
            out_len = ESP_GMF_JOB_ERR_FAIL;
        }
    }
    if (in_load != NULL) {
        load_ret = esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY);
        if ((load_ret < ESP_GMF_IO_OK) && (load_ret != ESP_GMF_IO_ABORT)) {
            ESP_LOGE(TAG, "IN port release error, ret:%d", load_ret);
            out_len = ESP_GMF_JOB_ERR_FAIL;
        }
    }
    return out_len;
}

static esp_gmf_job_err_t esp_gmf_audio_muxer_close(esp_gmf_element_handle_t self, void *para)
{
    ESP_LOGD(TAG, "Close muxer element");
    esp_gmf_audio_muxer_t *muxer_el = (esp_gmf_audio_muxer_t *)self;
    if (muxer_el->muxer) {
        esp_muxer_close(muxer_el->muxer);
        muxer_el->muxer = NULL;
    }
    if (muxer_el->out_db) {
        esp_gmf_db_deinit(muxer_el->out_db);
        muxer_el->out_db = NULL;
    }
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_err_t esp_gmf_audio_muxer_destroy(esp_gmf_element_handle_t self)
{
    ESP_LOGD(TAG, "Destroy muxer element");
    esp_gmf_audio_muxer_cfg_t *cfg = (esp_gmf_audio_muxer_cfg_t *)OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(self);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_muxer_caps_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t muxer_caps = {0};
    muxer_caps.cap_eightcc = ESP_GMF_CAPS_AUDIO_MUXER;
    muxer_caps.attr_fun = muxer_caps_iter_fun;
    int ret = esp_gmf_cap_append(&caps, &muxer_caps);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to create capability");

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->caps = caps;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t muxer_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
{
    ESP_GMF_NULL_CHECK(TAG, ctx, return ESP_GMF_ERR_INVALID_ARG;);
    ESP_GMF_NULL_CHECK(TAG, evt, return ESP_GMF_ERR_INVALID_ARG;);
    if ((evt->type != ESP_GMF_EVT_TYPE_REPORT_INFO)
        || (evt->sub != ESP_GMF_INFO_SOUND)
        || (evt->payload == NULL)) {
        return ESP_GMF_ERR_OK;
    }
    esp_gmf_element_handle_t self = (esp_gmf_element_handle_t)ctx;
    esp_gmf_element_handle_t el = evt->from;
    esp_gmf_event_state_t state = ESP_GMF_EVENT_STATE_NONE;
    esp_gmf_element_get_state(self, &state);
    if (state < ESP_GMF_EVENT_STATE_OPENING) {
        esp_gmf_audio_muxer_cfg_t *muxer_cfg = (esp_gmf_audio_muxer_cfg_t *)OBJ_GET_CFG(self);
        ESP_GMF_NULL_CHECK(TAG, muxer_cfg, return ESP_GMF_ERR_FAIL);
        esp_gmf_info_sound_t *info = (esp_gmf_info_sound_t *)evt->payload;
        esp_gmf_audio_el_set_snd_info(self, info);
        ESP_LOGD(TAG, "RECV info, from: %s-%p, next: %p, self: %s-%p, type: %x, state: %s, rate: %d, ch: %d, bits: %d",
                 OBJ_GET_TAG(el), el, esp_gmf_node_for_next((esp_gmf_node_t *)el), OBJ_GET_TAG(self), self, evt->type,
                 esp_gmf_event_get_state_str(state), info->sample_rates, info->channels, info->bits);
        if (state == ESP_GMF_EVENT_STATE_NONE) {
            esp_gmf_element_set_state(self, ESP_GMF_EVENT_STATE_INITIALIZED);
        }
    } else {
        ESP_LOGW(TAG, "Not support to handle event, type: %x, sub: %x", evt->type, evt->sub);
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_audio_muxer_init(esp_gmf_audio_muxer_cfg_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_INVALID_ARG;);
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_audio_muxer_t *muxer_el = esp_gmf_oal_calloc(1, sizeof(esp_gmf_audio_muxer_t));
    ESP_GMF_MEM_VERIFY(TAG, muxer_el, return ESP_GMF_ERR_MEMORY_LACK, "muxer element", sizeof(esp_gmf_audio_muxer_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)muxer_el;
    obj->new_obj = esp_gmf_audio_muxer_new;
    obj->del_obj = esp_gmf_audio_muxer_destroy;

    esp_gmf_audio_muxer_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(esp_gmf_audio_muxer_cfg_t));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto ES_MUXER_FAIL;}, "muxer configuration", sizeof(esp_gmf_audio_muxer_cfg_t));
    memcpy(cfg, config, sizeof(esp_gmf_audio_muxer_cfg_t));
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_gmf_audio_muxer_cfg_t));

    ret = esp_gmf_obj_set_tag(obj, "aud_muxer");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto ES_MUXER_FAIL, "Failed to set obj tag");

    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                      ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, DEFAULT_DATABUS_SIZE);
    el_cfg.dependency = true;  // Muxer depends on audio information

    ret = esp_gmf_audio_el_init(muxer_el, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto ES_MUXER_FAIL, "Failed to initialize muxer element");

    ESP_GMF_ELEMENT_GET(muxer_el)->ops.open = esp_gmf_audio_muxer_open;
    ESP_GMF_ELEMENT_GET(muxer_el)->ops.process = esp_gmf_audio_muxer_process;
    ESP_GMF_ELEMENT_GET(muxer_el)->ops.close = esp_gmf_audio_muxer_close;
    ESP_GMF_ELEMENT_GET(muxer_el)->ops.event_receiver = muxer_received_event_handler;
    ESP_GMF_ELEMENT_GET(muxer_el)->ops.load_caps = _load_muxer_caps_func;
    ESP_GMF_ELEMENT_GET(muxer_el)->ops.load_methods = NULL;

    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;

ES_MUXER_FAIL:
    esp_gmf_audio_muxer_destroy(obj);
    return ret;
}
