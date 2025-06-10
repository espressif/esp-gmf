/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdatomic.h>
#include "esp_gmf_event.h"
#include "esp_gmf_video_element.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Atomic declare and operations
 */
#define ATOM_VAR                    atomic_int
#define GET_ATOM(v)                 atomic_load(&v);
#define SET_ATOM(atom, v)           atomic_store(&atom, v)
#define ATOM_SET_BITS(atom, bits)   atomic_fetch_or(&atom, bits)
#define ATOM_CLEAR_BITS(atom, bits) atomic_fetch_and(&atom, ~bits)

#define GMF_VIDEO_BREAK_ON_FAIL(ret) if (ret != ESP_GMF_ERR_OK) {  \
    break;                                                         \
}

#define GMF_VIDEO_ALIGN_UP(size, align) (((size) + ((align) - 1)) & ~((align) - 1))

#define GMF_VIDEO_UPDATE_CONFIG(config, info, need_recfg) do {    \
    (need_recfg) = ((config)->in_pixel_fmt != (info)->format_id)  \
        || ((config)->in_res.width != (info)->width)              \
        || ((config)->in_res.height != (info)->height);           \
    (config)->in_res.width  = (info)->width;                      \
    (config)->in_res.height = (info)->height;                     \
    (config)->in_pixel_fmt  = (info)->format_id;                  \
} while (0);

/**
 * @brief  Get video format string representation
 *
 * @param[in]  format  Video format FourCC representation
 *
 * @return
 *       - "none"  Not valid codec type
 *       - Others  String representation of codec
 */
const char *esp_gmf_video_get_format_string(uint32_t format);

/**
 * @brief  Common video event handler
 *
 * @note  This API implement basic handler of GMF report info event, element should derive from `esp_gmf_video_element_t`
 *
 * @param[in]  evt  GMT event packet
 * @param[in]  ctx  Event context
 *
 * @return
 *       - ESP_GMF_ERR_OK  On success
 */
esp_gmf_err_t esp_gmf_video_handle_events(esp_gmf_event_pkt_t *evt, void *ctx);

/**
 * @brief  Update basic information of video element
 *         This function updates the width, height, and pixel format of a video element.
 *         Typically called when video stream configuration changes (e.g., resolution adjustment
 *         or format switching) to synchronize the latest parameters
 *
 * @param[in]  self       Video element handle pointing to the target video element object.
 *                        Must be initialized and non-NULL.
 * @param[in]  width      New video width in pixels
 * @param[in]  height     New video height in pixels
 * @param[in]  pixel_fmt  Pixel format
 *
 */
static inline void gmf_video_update_info(esp_gmf_video_element_handle_t self, uint16_t width, uint16_t height, uint32_t pixel_fmt)
{
    esp_gmf_info_video_t vid_info = {0};
    esp_gmf_video_el_get_src_info(self, &vid_info);
    vid_info.height = height;
    vid_info.width = width;
    vid_info.format_id = pixel_fmt;
    esp_gmf_element_notify_vid_info(self, &vid_info);
}

/**
 * @brief  Acquire input/output payload for processing
 *
 * @param[in]   in_port          Handle to the input port
 * @param[in]   out_port         Handle to the output port
 * @param[out]  in_load          Double pointer to receive the acquired input payload buffer
 *                               The buffer will be allocated/reserved according to in_wanted_size
 * @param[out]  out_load         Double pointer to receive the acquired output payload buffer
 *                               The buffer will be allocated/reserved according to out_wanted_size
 * @param[in]   in_wanted_size   Desired minimum size for input payload buffer (in bytes)
 * @param[in]   out_wanted_size  Desired minimum size for output payload buffer (in bytes)
 * @param[in]   is_bypass        Bypass flag indicating whether to use zero-copy optimization (true)
 *                               or allow data modification (false)
 *
 * @return
 *       - ESP_GMF_JOB_ERR_OK    On success
 *       - ESP_GMF_JOB_ERR_FAIL  On failure
 *
 */
static inline esp_gmf_job_err_t video_el_acquire_payload(esp_gmf_port_handle_t in_port, esp_gmf_port_handle_t out_port, esp_gmf_payload_t **in_load,
                                                         esp_gmf_payload_t **out_load, uint32_t in_wanted_size, uint32_t out_wanted_size, bool is_bypass)
{
    esp_gmf_err_io_t load_ret = ESP_GMF_IO_OK;
    load_ret = esp_gmf_port_acquire_in(in_port, in_load, in_wanted_size, ESP_GMF_MAX_DELAY);
    if (load_ret < ESP_GMF_IO_OK) {
        if (load_ret == ESP_GMF_IO_ABORT) {
            return ESP_GMF_JOB_ERR_OK;
        }
        ESP_LOGE("Imgfx_el", "Acquire size: %ld on in port, ret: %d", in_wanted_size, load_ret);
        return ESP_GMF_JOB_ERR_FAIL;
    }
    if (((*in_load)->valid_size) < (in_wanted_size)) {
        ESP_LOGE("Imgfx_el", "Acquire size %ld-%d on in port is not enough", in_wanted_size, (*in_load)->valid_size);
        return ESP_GMF_JOB_ERR_FAIL;
    }
    if ((in_port->is_shared == 1)
        && (is_bypass)) {
        /* If it is share buffer and is_bypass is true, just copy it */
        *out_load = (*in_load);
    }
    load_ret = esp_gmf_port_acquire_out(out_port, out_load, out_wanted_size, ESP_GMF_MAX_DELAY);
    if (load_ret < ESP_GMF_IO_OK) {
        if (load_ret == ESP_GMF_IO_ABORT) {
            return ESP_GMF_JOB_ERR_OK;
        }
        ESP_LOGE("Imgfx_el", "Acquire size: %ld on out port, ret: %d", out_wanted_size, load_ret);
        return ESP_GMF_JOB_ERR_FAIL;
    }
    if ((*out_load)->buf_length < out_wanted_size) {
        ESP_LOGE("Imgfx_el", "Acquire size %ld-%d on out port is not enough", out_wanted_size, (*out_load)->buf_length);
        return ESP_GMF_JOB_ERR_FAIL;
    }
    return ESP_GMF_JOB_ERR_OK;
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */
