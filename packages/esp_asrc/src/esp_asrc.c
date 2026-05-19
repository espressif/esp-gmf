/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_asrc.h"
#include "asrc_common.h"
#include "asrc_utils.h"
#include "esp_asrc_types.h"
#include "asrc_sw_bit_cvt_ops.h"
#include "asrc_sw_ch_cvt_ops.h"
#include "asrc_sw_rate_cvt_ops.h"
#include "asrc_hw_ops.h"

#define ASRC_MAX_MODULES      4
#define ASRC_LOAD_STORE_COUNT 2

static const char *TAG = "ESP_ASRC";

/**
 * @brief  ASRC module handle structure
 */
typedef struct {
    void                 *module;            /*!< Module handle pointer */
    const esp_asrc_ops_t *asrc_ops;          /*!< ASRC operations interface */
    uint16_t              out_sample_bytes;  /*!< Output sample size in bytes */
} asrc_module_t;

/**
 * @brief  ASRC buffer structure
 */
typedef struct {
    void     *buf;          /*!< Buffer pointer */
    uint32_t  buf_len;      /*!< Buffer length in bytes */
    bool      is_occupied;  /*!< Buffer occupation flag */
} asrc_buf_t;

/**
 * @brief  ASRC main handle structure
 */
typedef struct {
    asrc_module_t  asrc_el[ASRC_MAX_MODULES];          /*!< ASRC module handles array */
    asrc_buf_t     load_store[ASRC_LOAD_STORE_COUNT];  /*!< Load/store buffer array */
    uint16_t       in_sample_bytes;                    /*!< Input sample size in bytes */
    uint8_t        module_cnt;                         /*!< Number of active modules */
} esp_asrc_hd_t;

static inline bool asrc_bypass_check(const esp_asrc_cfg_t *cfg)
{
    return (cfg->src_info.sample_rate == cfg->dest_info.sample_rate) &&
           (cfg->src_info.channel == cfg->dest_info.channel) &&
           (cfg->src_info.bits_per_sample == cfg->dest_info.bits_per_sample);
}

static inline esp_asrc_err_t asrc_bypass_process(esp_asrc_handle_t handle, uint8_t *in_samples, uint32_t in_samples_num,
                                                 uint8_t *out_samples, uint32_t *out_samples_num)
{
    ASRC_NULL_CHECK(TAG, in_samples, "in samples", return ESP_ASRC_ERR_INVALID_PARAMETER);
    ASRC_NULL_CHECK(TAG, out_samples, "out samples", return ESP_ASRC_ERR_INVALID_PARAMETER);
    if (*out_samples_num < in_samples_num) {
        ESP_LOGE(TAG, "Sample num(%" PRIu32 ") for output less than needed(%" PRIu32 ") in bypass process",
                 *out_samples_num, in_samples_num);
        return ESP_ASRC_ERR_INVALID_PARAMETER;
    }
    esp_asrc_hd_t *asrc_hd = (esp_asrc_hd_t *)handle;
    if (in_samples != out_samples) {
        memcpy(out_samples, in_samples, in_samples_num * asrc_hd->in_sample_bytes);
    }
    *out_samples_num = in_samples_num;
    return ESP_ASRC_ERR_OK;
}

static inline esp_asrc_err_t asrc_bypass_get_out_sample_num(esp_asrc_handle_t handle, uint32_t in_samples_num, uint32_t *out_samples_num)
{
    *out_samples_num = in_samples_num;
    return ESP_ASRC_ERR_OK;
}

const esp_asrc_ops_t *asrc_bypass_ops()
{
    static const esp_asrc_ops_t asrc_ops = {
        .open = NULL,
        .get_out_sample_num = asrc_bypass_get_out_sample_num,
        .process = asrc_bypass_process,
        .close = NULL,
    };
    return &asrc_ops;
}

static inline bool asrc_hw_pre_check(esp_asrc_cfg_t *cfg, const asrc_hw_caps_t *hw_caps)
{
    if ((cfg->src_info.sample_rate == cfg->dest_info.sample_rate) || (hw_caps == NULL) || (cfg->perf_type != ESP_ASRC_PERF_TYPE_AUTO)) {
        return false;
    }
    esp_asrc_cfg_t asrc_cfg = {0};
    memcpy(&asrc_cfg, cfg, sizeof(esp_asrc_cfg_t));
    asrc_cfg.src_info.bits_per_sample = 16;
    if (!hw_caps->rate_cvt_caps(&asrc_cfg.src_info, cfg->dest_info.sample_rate)) {
        return false;
    }
    return true;
}

static inline esp_asrc_err_t asrc_create_module(esp_asrc_cfg_t *cfg, asrc_module_t *asrc_el)
{
    esp_asrc_err_t ret = asrc_el->asrc_ops->open(cfg, &asrc_el->module);
    ASRC_RET_CHECK(ret, return ret);
    asrc_el->out_sample_bytes = (cfg->dest_info.bits_per_sample >> 3) * cfg->dest_info.channel;
    memcpy(&cfg->src_info, &cfg->dest_info, sizeof(esp_asrc_aud_info_t));
    return ESP_ASRC_ERR_OK;
}

static inline esp_asrc_err_t asrc_optimize_module_order(esp_asrc_cfg_t asrc_cfg, esp_asrc_aud_info_t *dest_info,
                                                        asrc_module_t *asrc_el, uint8_t *module_cnt)
{
    esp_asrc_err_t ret = ESP_ASRC_ERR_OK;
    const asrc_hw_caps_t *hw_caps = asrc_hw_get_caps();
    bool use_hw = asrc_hw_pre_check(&asrc_cfg, hw_caps);
__retry:
    int idx = 0;
    *module_cnt = 0;
    memcpy(&asrc_cfg.dest_info, &asrc_cfg.src_info, sizeof(esp_asrc_aud_info_t));
    bool bpc_first = use_hw ? (asrc_cfg.src_info.bits_per_sample != 16) : (asrc_cfg.src_info.bits_per_sample == 8);
    bool bpc_last = use_hw ? (dest_info->bits_per_sample != 16) : (dest_info->bits_per_sample == 8);
    // If bpc last, then bpc last
    if (bpc_first) {
        asrc_cfg.dest_info.bits_per_sample = (bpc_last || use_hw) ? 16 : dest_info->bits_per_sample;
        asrc_el[idx].asrc_ops = asrc_sw_get_bit_cvt_ops();
        ret = asrc_create_module(&asrc_cfg, &asrc_el[idx++]);
        ASRC_RET_CHECK(ret, return ret);
    }
    if (use_hw) {
        asrc_cfg.dest_info.channel = hw_caps->ch_cvt_caps(&asrc_cfg.src_info, dest_info->channel, asrc_cfg.weight, asrc_cfg.weight_len) ?
                                     dest_info->channel : asrc_cfg.src_info.channel;
        asrc_cfg.dest_info.sample_rate = dest_info->sample_rate;
        asrc_el[idx].asrc_ops = asrc_hw_get_ops();
        ret = asrc_create_module(&asrc_cfg, &asrc_el[idx]);
        if (ret != ESP_ASRC_ERR_OK) {
            if (idx > 0) {
                asrc_el[0].asrc_ops->close(asrc_el[0].module);
            }
            use_hw = false;
            goto __retry;
        }
        idx++;
    }
    // If module is decrease data, then process sequence is chc -> bpc -> src
    if (asrc_cfg.src_info.channel > dest_info->channel) {
        asrc_cfg.dest_info.channel = dest_info->channel;
        asrc_el[idx].asrc_ops = asrc_sw_get_ch_cvt_ops();
        ret = asrc_create_module(&asrc_cfg, &asrc_el[idx++]);
        ASRC_RET_CHECK(ret, return ret);
    }
    if (asrc_cfg.src_info.bits_per_sample > dest_info->bits_per_sample && !bpc_last && !bpc_first) {
        asrc_cfg.dest_info.bits_per_sample = dest_info->bits_per_sample;
        asrc_el[idx].asrc_ops = asrc_sw_get_bit_cvt_ops();
        ret = asrc_create_module(&asrc_cfg, &asrc_el[idx++]);
        ASRC_RET_CHECK(ret, return ret);
    }
    // If module is increase data, then process sequence is src -> bpc -> chc
    if (dest_info->sample_rate != asrc_cfg.src_info.sample_rate) {
        asrc_cfg.dest_info.sample_rate = dest_info->sample_rate;
        asrc_el[idx].asrc_ops = asrc_sw_get_rate_cvt_ops();
        ret = asrc_create_module(&asrc_cfg, &asrc_el[idx++]);
        ASRC_RET_CHECK(ret, return ret);
    }
    if (dest_info->bits_per_sample > asrc_cfg.src_info.bits_per_sample && !bpc_last && !bpc_first) {
        asrc_cfg.dest_info.bits_per_sample = dest_info->bits_per_sample;
        asrc_el[idx].asrc_ops = asrc_sw_get_bit_cvt_ops();
        ret = asrc_create_module(&asrc_cfg, &asrc_el[idx++]);
        ASRC_RET_CHECK(ret, return ret);
    }
    if (dest_info->channel > asrc_cfg.src_info.channel) {
        asrc_cfg.dest_info.channel = dest_info->channel;
        asrc_el[idx].asrc_ops = asrc_sw_get_ch_cvt_ops();
        ret = asrc_create_module(&asrc_cfg, &asrc_el[idx++]);
        ASRC_RET_CHECK(ret, return ret);
    }
    // If bpc last, then bpc last
    if (bpc_last) {
        asrc_cfg.dest_info.bits_per_sample = dest_info->bits_per_sample;
        asrc_el[idx].asrc_ops = asrc_sw_get_bit_cvt_ops();
        ret = asrc_create_module(&asrc_cfg, &asrc_el[idx++]);
        ASRC_RET_CHECK(ret, return ret);
    }
    *module_cnt = idx;
    return ESP_ASRC_ERR_OK;
}

static inline asrc_buf_t *asrc_get_avail_load_buf(asrc_buf_t *load_store, uint32_t acquire_len)
{
    for (int i = 0; i < ASRC_LOAD_STORE_COUNT; i++) {
        if (load_store[i].is_occupied == false) {
            if (load_store[i].buf_len < acquire_len) {
                if (load_store[i].buf != NULL) {
                    asrc_free(load_store[i].buf);
                    load_store[i].buf = NULL;
                }
                uint32_t allocated_size = 0;
                esp_asrc_buffer_alignment_t al = {0};
                esp_asrc_get_buffer_alignment(&al);
                load_store[i].buf = esp_asrc_align_alloc(acquire_len, al.outbuf_addr_align, al.outbuf_size_align,
                                                         &allocated_size);
                ASRC_MEM_CHECK(TAG, load_store[i].buf, "load temp buf", allocated_size, return NULL);
                load_store[i].buf_len = allocated_size;
            }
            load_store[i].is_occupied = true;
            return &load_store[i];
        }
    }
    return NULL;
}

static inline void asrc_release_load_buf(asrc_buf_t *load_store)
{
    for (int i = 0; i < ASRC_LOAD_STORE_COUNT; i++) {
        if (load_store[i].buf != NULL) {
            asrc_free(load_store[i].buf);
            load_store[i].buf = NULL;
        }
    }
}

esp_asrc_err_t esp_asrc_open(esp_asrc_cfg_t *cfg, esp_asrc_handle_t *handle)
{
    ASRC_NULL_CHECK(TAG, handle, "asrc handle pointer", return ESP_ASRC_ERR_INVALID_PARAMETER);
    *handle = NULL;
    ASRC_NULL_CHECK(TAG, cfg, "asrc config", return ESP_ASRC_ERR_INVALID_PARAMETER);
    if (cfg->perf_type < ESP_ASRC_PERF_TYPE_AUTO || cfg->perf_type > ESP_ASRC_PERF_TYPE_SW_SPEED) {
        ESP_LOGE(TAG, "Invalid perf type");
        return ESP_ASRC_ERR_INVALID_PARAMETER;
    }
    esp_asrc_err_t ret = ESP_ASRC_ERR_OK;
    // Create handle
    esp_asrc_hd_t *asrc_hd = asrc_calloc(1, sizeof(esp_asrc_hd_t));
    ASRC_MEM_CHECK(TAG, asrc_hd, "asrc handle", (int)sizeof(esp_asrc_hd_t), {return ESP_ASRC_ERR_MEM_LACK;});
    asrc_hd->in_sample_bytes = (cfg->src_info.bits_per_sample >> 3) * cfg->src_info.channel;
    if (!asrc_bypass_check(cfg)) {
        if (cfg->perf_type == ESP_ASRC_PERF_TYPE_HW_ONLY) {
            const esp_asrc_ops_t *hw_ops = asrc_hw_get_ops();
            if (hw_ops) {
                ret = hw_ops->open(cfg, &asrc_hd->asrc_el[0].module);
                ASRC_RET_CHECK(ret, goto _exit);
                asrc_hd->asrc_el[0].out_sample_bytes = (cfg->dest_info.bits_per_sample >> 3) * cfg->dest_info.channel;
                asrc_hd->asrc_el[0].asrc_ops = hw_ops;
                asrc_hd->module_cnt = 1;
            } else {
                ESP_LOGE(TAG, "HW is not supported");
                ret = ESP_ASRC_ERR_NOT_SUPPORT;
                goto _exit;
            }
        } else {
            ret = asrc_optimize_module_order(*cfg, &cfg->dest_info, asrc_hd->asrc_el, &asrc_hd->module_cnt);
            ASRC_RET_CHECK(ret, goto _exit);
        }
    } else {
        asrc_hd->asrc_el[0].asrc_ops = asrc_bypass_ops();
        asrc_hd->asrc_el[0].out_sample_bytes = (cfg->dest_info.bits_per_sample >> 3) * cfg->dest_info.channel;
        asrc_hd->asrc_el[0].module = asrc_hd;
        asrc_hd->module_cnt = 1;
    }
    *handle = asrc_hd;
    return ESP_ASRC_ERR_OK;
_exit:
    esp_asrc_close(asrc_hd);
    return ret;
}

esp_asrc_err_t esp_asrc_get_out_sample_num(esp_asrc_handle_t handle, uint32_t in_samples_cnt, uint32_t *out_samples_cnt)
{
    ASRC_NULL_CHECK(TAG, handle, "asrc handle", return ESP_ASRC_ERR_INVALID_PARAMETER);
    ASRC_NULL_CHECK(TAG, out_samples_cnt, "out samples cnt", return ESP_ASRC_ERR_INVALID_PARAMETER);
    esp_asrc_hd_t *asrc_hd = (esp_asrc_hd_t *)handle;
    esp_asrc_err_t ret = ESP_ASRC_ERR_OK;
    for (uint8_t i = 0; i < asrc_hd->module_cnt; i++) {
        ret = asrc_hd->asrc_el[i].asrc_ops->get_out_sample_num(asrc_hd->asrc_el[i].module, in_samples_cnt, out_samples_cnt);
        ASRC_RET_CHECK(ret, return ret);
        in_samples_cnt = *out_samples_cnt;
    }
    return ret;
}

esp_asrc_err_t esp_asrc_get_bytes_per_sample(esp_asrc_handle_t handle, uint16_t *in_sample_bytes, uint16_t *out_sample_bytes)
{
    ASRC_NULL_CHECK(TAG, handle, "asrc handle", return ESP_ASRC_ERR_INVALID_PARAMETER);
    esp_asrc_hd_t *asrc_hd = (esp_asrc_hd_t *)handle;
    if (in_sample_bytes != NULL) {
        *in_sample_bytes = asrc_hd->in_sample_bytes;
    }
    if (out_sample_bytes != NULL) {
        *out_sample_bytes = asrc_hd->module_cnt == 0 ? asrc_hd->in_sample_bytes :
                                                       asrc_hd->asrc_el[asrc_hd->module_cnt - 1].out_sample_bytes;
    }
    return ESP_ASRC_ERR_OK;
}

esp_asrc_err_t esp_asrc_process(esp_asrc_handle_t handle, uint8_t *in_samples, uint32_t in_samples_num,
                                uint8_t *out_samples, uint32_t *out_sample_num)
{
    ASRC_NULL_CHECK(TAG, handle, "asrc handle", return ESP_ASRC_ERR_INVALID_PARAMETER);
    ASRC_NULL_CHECK(TAG, out_sample_num, "out sample num", return ESP_ASRC_ERR_INVALID_PARAMETER);
    esp_asrc_err_t ret = ESP_ASRC_ERR_OK;
    esp_asrc_hd_t *asrc_hd = (esp_asrc_hd_t *)handle;
    asrc_buf_t in_load_first = {
        .buf = in_samples,
        .buf_len = in_samples_num * asrc_hd->in_sample_bytes,
        .is_occupied = true,
    };
    asrc_buf_t out_load_last = {
        .buf = out_samples,
        .buf_len = *out_sample_num * asrc_hd->asrc_el[asrc_hd->module_cnt - 1].out_sample_bytes,
        .is_occupied = true,
    };
    asrc_buf_t *in_load = &in_load_first;
    asrc_buf_t *out_load = NULL;
    uint32_t max_out_samples = 0;
    for (uint8_t i = 0; i < asrc_hd->module_cnt; i++) {
        if (i == asrc_hd->module_cnt - 1) {
            out_load = &out_load_last;
            max_out_samples = *out_sample_num;
        } else {
            asrc_hd->asrc_el[i].asrc_ops->get_out_sample_num(asrc_hd->asrc_el[i].module, in_samples_num, &max_out_samples);
            out_load = asrc_get_avail_load_buf(asrc_hd->load_store, max_out_samples * asrc_hd->asrc_el[i].out_sample_bytes);
            ASRC_NULL_CHECK(TAG, out_load, "load temp buf", return ESP_ASRC_ERR_MEM_LACK);
        }
        max_out_samples = out_load->buf_len / asrc_hd->asrc_el[i].out_sample_bytes;
        ret = asrc_hd->asrc_el[i].asrc_ops->process(asrc_hd->asrc_el[i].module, in_load->buf, in_samples_num,
                                                    out_load->buf, &max_out_samples);
        ASRC_RET_CHECK(ret, return ret);
        in_samples_num = max_out_samples;
        in_load->is_occupied = false;
        in_load = out_load;
    }
    *out_sample_num = max_out_samples;
    return ESP_ASRC_ERR_OK;
}

void esp_asrc_close(esp_asrc_handle_t handle)
{
    esp_asrc_hd_t *asrc_hd = (esp_asrc_hd_t *)handle;
    if (asrc_hd != NULL) {
        for (uint8_t i = 0; i < asrc_hd->module_cnt; i++) {
            if (asrc_hd->asrc_el[i].module != NULL) {
                if (asrc_hd->asrc_el[i].asrc_ops->close != NULL) {
                    asrc_hd->asrc_el[i].asrc_ops->close(asrc_hd->asrc_el[i].module);
                }
                asrc_hd->asrc_el[i].module = NULL;
            }
        }
        asrc_release_load_buf(asrc_hd->load_store);
        asrc_free(asrc_hd);
    }
}
