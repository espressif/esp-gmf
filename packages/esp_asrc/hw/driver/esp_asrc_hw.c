/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "soc/soc_caps.h"
#include "hal/asrc_hal.h"
#include "hal/asrc_ll.h"
#include "asrc_caps_check.h"
#include "asrc_common.h"
#include "esp_asrc_hw.h"
#include "esp_asrc_types.h"
#include "asrc_adapter.h"

static const char *TAG = "ESP_HW_ASRC";

#define ASRC_HW_MIN(a, b)           (((a) < (b)) ? (a) : (b))
#define ASRC_NUM_BUF_COMP_SZ        (16)
#define ASRC_HW_MAX_DATA_BURST_SIZE (16)

/**
 * @brief  ASRC hardware conversion type
 */
typedef enum {
    ASRC_HW_TYPE_INVALID  = 0,  /*!< Invalid type */
    ASRC_HW_TYPE_DECIMATE = 1,  /*!< Integer down-sampling */
    ASRC_HW_TYPE_INTERP   = 2,  /*!< Integer up-sampling */
    ASRC_HW_TYPE_RESAMPLE = 3,  /*!< Non-integer resampling */
    ASRC_HW_TYPE_BYPASS   = 4,  /*!< Bypass mode (source rate equals destination rate) */
    ASRC_HW_TYPE_MAX      = 5,  /*!< Maximum enum value */
} asrc_hw_type_t;

/**
 * @brief  GDMA transfer direction
 */
typedef enum {
    ASRC_GDMA_DIR_TX,  /*!< TX direction (memory to ASRC) */
    ASRC_GDMA_DIR_RX,  /*!< RX direction (ASRC to memory) */
} asrc_gdma_dir_t;

/**
 * @brief  ASRC hardware platform context
 */
typedef struct {
    portMUX_TYPE  spinlock;      /*!< Spinlock for thread safety */
    uint8_t       ch_free_mask;  /*!< Available channel bitmask */
    uint8_t       ref_count;     /*!< Number of active handles; module is enabled iff ref_count > 0 */
} asrc_hw_platform_t;

/**
 * @brief  ASRC hardware handle structure
 */
typedef struct {
    esp_asrc_aud_info_t              src_info;          /*!< Source audio information */
    esp_asrc_aud_info_t              dest_info;         /*!< Destination audio information */
    int8_t                           asrc_idx;          /*!< ASRC channel index */
    int8_t                           asrc_num;          /*!< Number of ASRC channels */
    uint32_t                         max_tx_desc_num;   /*!< Maximum TX DMA descriptor count */
    uint32_t                         max_rx_desc_num;   /*!< Maximum RX DMA descriptor count */
    asrc_hw_type_t                   type;              /*!< ASRC hardware type */
    int32_t                          down_factor;       /*!< Down-sampling factor */
    int32_t                          up_factor;         /*!< Up-sampling factor */
    asrc_hw_gdma_link_list_handle_t  tx_desc_list;      /*!< GDMA TX link list handle */
    asrc_hw_gdma_link_list_handle_t  rx_desc_list;      /*!< GDMA RX link list handle */
    asrc_hal_context_t               asrc_hal;          /*!< ASRC HAL context */
    asrc_hw_gdma_channel_handle_t    dma_tx_chan;       /*!< GDMA TX channel handle */
    asrc_hw_gdma_channel_handle_t    dma_rx_chan;       /*!< GDMA RX channel handle */
    QueueHandle_t                    dma_rx_evt_queue;  /*!< DMA RX event queue */
    TickType_t                       timeout_tick;      /*!< Timeout in ticks */
} esp_asrc_hw_hd_t;

/**
 * @brief  Global ASRC platform instance
 */
static asrc_hw_platform_t p_asrc = {
    .spinlock     = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED,
    .ch_free_mask = (1 << ASRC_LL_STREAM_NUM) - 1,
    .ref_count    = 0,
};

static void asrc_hw_get_up_down_factor(esp_asrc_hw_hd_t *hd)
{
    int32_t src_rate = hd->src_info.sample_rate;
    int32_t dest_rate = hd->dest_info.sample_rate;
    int32_t downfactor = src_rate;
    int32_t upfactor = dest_rate;
    for (int32_t fact = 2; fact <= ASRC_HW_MIN(downfactor, upfactor); fact++) {
        while ((downfactor % fact == 0) && (upfactor % fact == 0)) {
            downfactor /= fact;
            upfactor /= fact;
        }
    }
    hd->down_factor = downfactor;
    hd->up_factor = upfactor;
    if (downfactor == 1 && upfactor == 1) {
        hd->type = ASRC_HW_TYPE_BYPASS;
    } else if ((downfactor == 1) && (upfactor == 2 || upfactor == 4)) {
        hd->type = ASRC_HW_TYPE_INTERP;
    } else if ((upfactor == 1) && (downfactor == 2 || downfactor == 4)) {
        hd->type = ASRC_HW_TYPE_DECIMATE;
    } else {
        hd->type = ASRC_HW_TYPE_RESAMPLE;
    }
}

static void asrc_hw_get_available_asrc_idx(esp_asrc_hw_hd_t *hd)
{
    int8_t asrc_idx = -1;
    uint8_t min_ch = ASRC_HW_MIN(hd->src_info.channel, hd->dest_info.channel);
    bool need_enable = false;
    portENTER_CRITICAL(&p_asrc.spinlock);
    if (p_asrc.ref_count == 0) {
        need_enable = true;
    }
    p_asrc.ref_count++;
    for (int i = 0; i <= ASRC_LL_STREAM_NUM - min_ch; i++) {
        bool all_free = true;
        for (int j = 0; j < min_ch; j++) {
            if (((p_asrc.ch_free_mask >> (i + j)) & 0x1) == 0) {
                all_free = false;
                break;
            }
        }
        if (all_free) {
            asrc_idx = i;
            for (int j = 0; j < min_ch; j++) {
                p_asrc.ch_free_mask &= ~(1 << (i + j));
            }
            break;
        }
    }
    portEXIT_CRITICAL(&p_asrc.spinlock);
    if (need_enable) {
        asrc_hal_enable_asrc_module(&hd->asrc_hal, true);
    }
    hd->asrc_idx = asrc_idx;
    hd->asrc_num = (asrc_idx == -1) ? (0) : (min_ch);
}

static void asrc_hw_free_available_asrc_idx(esp_asrc_hw_hd_t *hd)
{
    int8_t asrc_idx = hd->asrc_idx;
    bool need_disable = false;
    portENTER_CRITICAL(&p_asrc.spinlock);
    if (asrc_idx != -1) {
        for (int i = 0; i < hd->asrc_num; i++) {
            p_asrc.ch_free_mask |= (1 << (asrc_idx + i));
        }
    }
    p_asrc.ref_count--;
    if (p_asrc.ref_count == 0) {
        need_disable = true;
    }
    portEXIT_CRITICAL(&p_asrc.spinlock);
    if (need_disable) {
        asrc_hal_enable_asrc_module(&hd->asrc_hal, false);
    }
}

static void asrc_hw_module_init(esp_asrc_hw_hd_t *hd, asrc_hal_config_t *asrc_cfg)
{
    for (uint8_t i = 0; i < hd->asrc_num; i++) {
        asrc_hal_init_stream(&hd->asrc_hal, asrc_cfg, hd->asrc_idx + i);
    }
}

static void asrc_hw_module_deinit(esp_asrc_hw_hd_t *hd)
{
    for (uint8_t i = 0; i < hd->asrc_num; i++) {
        asrc_hal_deinit_stream(&hd->asrc_hal, hd->asrc_idx + i);
    }
}

static void asrc_hw_set_out_bytes_num(asrc_hal_context_t *hal, uint8_t asrc_num, uint8_t asrc_idx, uint32_t out_bytes_num)
{
    for (uint8_t i = 0; i < asrc_num; i++) {
        asrc_hal_set_out_bytes_num(hal, asrc_idx + i, out_bytes_num / asrc_num);
    }
}

static void asrc_hw_start(asrc_hal_context_t *hal, uint8_t asrc_num, uint8_t asrc_idx)
{
    for (uint8_t i = 0; i < asrc_num; i++) {
        asrc_hal_start(hal, asrc_idx + i);
    }
}

static esp_asrc_err_t asrc_hw_check_caps(esp_asrc_hw_cfg_t *cfg)
{
    if (cfg->timeout_ms == 0) {
        ESP_LOGE(TAG, "Invalid timeout_ms: 0 is not allowed (use > 0 for a finite timeout, or -1 for portMAX_DELAY)");
        return ESP_ASRC_ERR_INVALID_PARAMETER;
    }
    uint8_t ret = asrc_hw_rate_cvt_caps(&cfg->src_info, cfg->dest_info.sample_rate) &&
                  asrc_hw_ch_cvt_caps(&cfg->src_info, cfg->dest_info.channel, cfg->weight, cfg->weight_len) &&
                  asrc_hw_bit_cvt_caps(&cfg->src_info, cfg->dest_info.bits_per_sample);
    return ret == 1 ? ESP_ASRC_ERR_OK : ESP_ASRC_ERR_NOT_SUPPORT;
}

static void asrc_hw_get_out_samples(esp_asrc_hw_hd_t *hd, uint32_t in_samples_cnt, uint32_t *out_samples_cnt)
{
    switch (hd->type) {
        case ASRC_HW_TYPE_BYPASS:
            *out_samples_cnt = in_samples_cnt;
            break;
        case ASRC_HW_TYPE_DECIMATE:
            *out_samples_cnt = in_samples_cnt / hd->down_factor;
            break;
        case ASRC_HW_TYPE_INTERP:
            *out_samples_cnt = in_samples_cnt * hd->up_factor;
            break;
        case ASRC_HW_TYPE_RESAMPLE:
            *out_samples_cnt = (int32_t)((float)in_samples_cnt * (float)hd->dest_info.sample_rate / (float)hd->src_info.sample_rate);
            break;
        default:
            break;
    }
}

esp_asrc_err_t esp_asrc_hw_open(esp_asrc_hw_cfg_t *cfg, esp_asrc_hw_handle_t *handle)
{
    *handle = NULL;
    esp_asrc_err_t ret = asrc_hw_check_caps(cfg);
    ASRC_RET_CHECK(ret, return ret);
    esp_asrc_hw_hd_t *asrc_hd = asrc_calloc(1, sizeof(esp_asrc_hw_hd_t));
    ASRC_MEM_CHECK(TAG, asrc_hd, "asrc hw handle", (int)sizeof(esp_asrc_hw_hd_t), return ESP_ASRC_ERR_MEM_LACK);
    memcpy(&asrc_hd->src_info, &cfg->src_info, sizeof(esp_asrc_aud_info_t));
    memcpy(&asrc_hd->dest_info, &cfg->dest_info, sizeof(esp_asrc_aud_info_t));
    asrc_hd->timeout_tick = (cfg->timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(cfg->timeout_ms);
    asrc_hal_init(&asrc_hd->asrc_hal);
    asrc_hw_get_available_asrc_idx(asrc_hd);
    if (asrc_hd->asrc_idx == -1) {
        ESP_LOGE(TAG, "There is no free hardware samplerate convert channel");
        ret = ESP_ASRC_ERR_FAIL;
        goto _exit;
    }
    asrc_hw_get_up_down_factor(asrc_hd);
    asrc_hal_config_t asrc_cfg = {
        .src_info = {
            .sample_rate = asrc_hd->src_info.sample_rate,
            .channel = asrc_hd->src_info.channel,
            .bits_per_sample = asrc_hd->src_info.bits_per_sample,
        },
        .dest_info = {
            .sample_rate = asrc_hd->dest_info.sample_rate,
            .channel = asrc_hd->dest_info.channel,
            .bits_per_sample = asrc_hd->dest_info.bits_per_sample,
        },
        .weight = cfg->weight,
        .weight_len = cfg->weight_len,
    };
    asrc_hw_module_init(asrc_hd, &asrc_cfg);
    asrc_hd->dma_rx_evt_queue = xQueueCreateWithCaps(1, sizeof(asrc_hw_gdma_evt_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ASRC_MEM_CHECK(TAG, asrc_hd->dma_rx_evt_queue, "rx event queue", (int)sizeof(asrc_hw_gdma_evt_t),
                   {ret = ESP_ASRC_ERR_MEM_LACK; goto _exit;});
    esp_err_t gdma_ret = asrc_hw_gdma_create_channel(asrc_hd->asrc_idx, asrc_hd->dma_rx_evt_queue, ASRC_HW_MAX_DATA_BURST_SIZE,
                                                     &asrc_hd->dma_tx_chan, &asrc_hd->dma_rx_chan);
    ASRC_RET_CHECK(gdma_ret, {ret = ESP_ASRC_ERR_FAIL; goto _exit;});
    asrc_hw_start(&asrc_hd->asrc_hal, asrc_hd->asrc_num, asrc_hd->asrc_idx);
    *handle = asrc_hd;
    return ESP_ASRC_ERR_OK;
_exit:
    esp_asrc_hw_close(asrc_hd);
    return ret;
}

esp_asrc_err_t esp_asrc_hw_get_out_sample_num(esp_asrc_hw_handle_t handle, uint32_t in_samples_cnt, uint32_t *out_samples_cnt)
{
    esp_asrc_hw_hd_t *hd = (esp_asrc_hw_hd_t *)handle;
    if (hd->type == ASRC_HW_TYPE_BYPASS) {
        *out_samples_cnt = in_samples_cnt;
    } else {
        if (in_samples_cnt < (int32_t)((hd->src_info.sample_rate + hd->dest_info.sample_rate - 1) / hd->dest_info.sample_rate)) {
            ESP_LOGE(TAG, "Invalid in_samples_num %" PRIu32 " %" PRId32, in_samples_cnt,
                     (int32_t)((hd->src_info.sample_rate + hd->dest_info.sample_rate - 1) / hd->dest_info.sample_rate));
            return ESP_ASRC_ERR_FAIL;
        }
        asrc_hw_get_out_samples(hd, in_samples_cnt, out_samples_cnt);
        *out_samples_cnt = *out_samples_cnt + ASRC_NUM_BUF_COMP_SZ;
    }
    return ESP_ASRC_ERR_OK;
}

esp_asrc_err_t esp_asrc_hw_process(esp_asrc_hw_handle_t handle, uint8_t *in_samples, uint32_t in_samples_num,
                                   uint8_t *out_samples, uint32_t *out_sample_num)
{
    ASRC_NULL_CHECK(TAG, handle, "asrc handle", return ESP_ASRC_ERR_INVALID_PARAMETER);
    ASRC_NULL_CHECK(TAG, in_samples, "in samples", return ESP_ASRC_ERR_INVALID_PARAMETER);
    ASRC_NULL_CHECK(TAG, out_samples, "out samples", return ESP_ASRC_ERR_INVALID_PARAMETER);
    ASRC_NULL_CHECK(TAG, out_sample_num, "out sample num", return ESP_ASRC_ERR_INVALID_PARAMETER);
    esp_asrc_hw_hd_t *hd = (esp_asrc_hw_hd_t *)handle;
    // Outbuf cache line aligned check
    uint32_t outbuf_sz = *out_sample_num * hd->dest_info.channel * (hd->dest_info.bits_per_sample >> 3);
    esp_err_t hw_ret = asrc_hw_check_buffer_alignment(out_samples, outbuf_sz);
    ASRC_RET_CHECK(hw_ret, return ESP_ASRC_ERR_INVALID_ALIGNMENT);
    uint32_t in_byte_cnt = in_samples_num * hd->src_info.channel * (hd->src_info.bits_per_sample >> 3);
    // The in_buffer cache write back
    hw_ret = asrc_hw_cache_msync_c2m(in_samples, in_byte_cnt);
    ASRC_RET_CHECK(hw_ret, return ESP_ASRC_ERR_FAIL);
    // Reset queue
    xQueueReset(hd->dma_rx_evt_queue);
    // Get round out samples number by in_samples_num, used to config gdma rx desc and set asrc out bytes num
    uint32_t out_num = 0;
    asrc_hw_get_out_samples(hd, in_samples_num, &out_num);
    asrc_hw_set_out_bytes_num(&hd->asrc_hal, hd->asrc_num, hd->asrc_idx,
                              out_num * (hd->dest_info.bits_per_sample >> 3) * hd->dest_info.channel);
    // Config gdma tx desc
    hw_ret = asrc_hw_gdma_create_link_list(in_byte_cnt, &hd->tx_desc_list, &hd->max_tx_desc_num);
    ASRC_RET_CHECK(hw_ret, return ESP_ASRC_ERR_FAIL);
    hw_ret = asrc_hw_gdma_mount_link_list(hd->tx_desc_list, hd->max_tx_desc_num, in_samples, in_byte_cnt);
    ASRC_RET_CHECK(hw_ret, return ESP_ASRC_ERR_FAIL);
    asrc_hw_gdma_start_channel(hd->dma_tx_chan, hd->tx_desc_list);
    // Config gdma rx desc
    uint32_t out_byte_cnt = (out_num + ASRC_NUM_BUF_COMP_SZ) * hd->dest_info.channel * (hd->dest_info.bits_per_sample >> 3);
    hw_ret = asrc_hw_gdma_create_link_list(out_byte_cnt, &hd->rx_desc_list, &hd->max_rx_desc_num);
    ASRC_RET_CHECK(hw_ret, return ESP_ASRC_ERR_FAIL);
    hw_ret = asrc_hw_gdma_mount_link_list(hd->rx_desc_list, hd->max_rx_desc_num, out_samples, out_byte_cnt);
    ASRC_RET_CHECK(hw_ret, return ESP_ASRC_ERR_FAIL);
    asrc_hw_gdma_start_channel(hd->dma_rx_chan, hd->rx_desc_list);
    while (1) {
        asrc_hw_gdma_evt_t asrc_evt;
        BaseType_t ret_val = xQueueReceive(hd->dma_rx_evt_queue, &asrc_evt, hd->timeout_tick);
        if (ret_val != pdTRUE) {
            ESP_LOGE(TAG, "HW ASRC process timeout");
            return ESP_ASRC_ERR_TIMEOUT;
        }
        if (asrc_evt.gdma_evt & ASRC_HW_GDMA_RECV_FRAME_DONE) {
            uint32_t out_sz = asrc_hal_get_out_data_bytes(&hd->asrc_hal, hd->asrc_idx) * hd->asrc_num;
            *out_sample_num = out_sz / (hd->dest_info.channel * hd->dest_info.bits_per_sample >> 3);
            hw_ret = asrc_hw_cache_msync_m2c(out_samples, outbuf_sz);
            ASRC_RET_CHECK(hw_ret, return ESP_ASRC_ERR_FAIL);
            break;
        }
    }
    return ESP_ASRC_ERR_OK;
}

void esp_asrc_hw_close(esp_asrc_hw_handle_t handle)
{
    esp_asrc_hw_hd_t *hd = (esp_asrc_hw_hd_t *)handle;
    if (hd != NULL) {
        asrc_hw_gdma_free_link_list(hd->tx_desc_list);
        asrc_hw_gdma_free_link_list(hd->rx_desc_list);
        asrc_hw_gdma_destroy_channel(hd->dma_tx_chan, hd->dma_rx_chan);
        if (hd->dma_rx_evt_queue) {
            vQueueDeleteWithCaps(hd->dma_rx_evt_queue);
            hd->dma_rx_evt_queue = NULL;
        }
        asrc_hw_module_deinit(hd);
        asrc_hw_free_available_asrc_idx(hd);
        asrc_hal_deinit(&hd->asrc_hal);
        asrc_free(hd);
    }
}
