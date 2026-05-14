/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_fourcc.h"
#include "gmf_video_common.h"
#include "esp_gmf_video_element.h"
#include "esp_log.h"

#define TAG "VIDEO_COMM"

const char *esp_gmf_video_get_format_string(uint32_t codec)
{
    switch (codec) {
        case ESP_FOURCC_H264:
            return "h264";
        case ESP_FOURCC_MJPG:
            return "mjpeg";
        case ESP_FOURCC_RGB16:
            return "rgb565";
        case ESP_FOURCC_RGB16_BE:
            return "rgb565_be";
        case ESP_FOURCC_RGB24:
            return "rgb888";
        case ESP_FOURCC_BGR24:
            return "bgr888";
        case ESP_FOURCC_YUV420P:
            return "yuv420p";
        case ESP_FOURCC_YUV422P:
            return "yuv422p";
        case ESP_FOURCC_YUYV:
            return "yuv422";
        case ESP_FOURCC_YVYU:
            return "yvyu";
        case ESP_FOURCC_UYVY:
            return "uyvy";
        case ESP_FOURCC_VYUY:
            return "vyuy";
        case ESP_FOURCC_OUYY_EVYY:
            return "o_uyy_e_vyy";
        default:
            return "none";
    }
}

esp_gmf_err_t esp_gmf_video_handle_events(esp_gmf_event_pkt_t *evt, void *ctx)
{
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_element_handle_t self = (esp_gmf_element_handle_t) ctx;
    // Only process report info currently
    if ((evt->type != ESP_GMF_EVT_TYPE_REPORT_INFO)
        || (evt->sub != ESP_GMF_INFO_VIDEO)
        || (evt->payload == NULL)) {
        return ESP_GMF_ERR_OK;
    }
    esp_gmf_event_state_t state = -1;
    esp_gmf_element_get_state(self, &state);
    // State must matched
    esp_gmf_info_video_t *vid_info = evt->payload;
    esp_gmf_video_element_t *video_el = (esp_gmf_video_element_t *)self;
    video_el->src_info = *vid_info;
    ESP_LOGI(TAG, "Video info for %s-%p format:%s %dx%d %dfps",
                OBJ_GET_TAG(self), self,
                esp_gmf_video_get_format_string(vid_info->format_id),
                (int)vid_info->width, (int)vid_info->height, (int)vid_info->fps);
    if (state == ESP_GMF_EVENT_STATE_NONE) {
        esp_gmf_element_set_state(self, ESP_GMF_EVENT_STATE_INITIALIZED);
    }
    return ret;
}
