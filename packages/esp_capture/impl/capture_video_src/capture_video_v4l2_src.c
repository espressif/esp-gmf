
/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */
#include <sdkconfig.h>

#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C5 || \
    CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32C61

#include "esp_capture_types.h"
#include "linux/videodev2.h"
#include "esp_video_init.h"
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <fcntl.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_capture_video_src_if.h"
#include "esp_capture_video_v4l2_src.h"
#include "capture_os.h"
#include "esp_cache.h"
#include "esp_log.h"

#define TAG "V4L2_SRC"

#define MAX_BUFS                (4)
#define MAX_SUPPORT_FORMATS_NUM (4)
#define FMT_STR(fmt)            ((uint8_t *)&fmt)[0], ((uint8_t *)&fmt)[1], ((uint8_t *)&fmt)[2], ((uint8_t *)&fmt)[3]

typedef struct {
    esp_capture_video_src_if_t  base;
    char                        dev_name[16];
    uint8_t                     buf_count;
    esp_capture_format_id_t     support_formats[MAX_SUPPORT_FORMATS_NUM];
    uint8_t                     format_count;
    int                         fd;
    uint8_t                    *fb_buffer[MAX_BUFS];
    struct v4l2_buffer          v4l2_buf[MAX_BUFS];
    bool                        fb_used[MAX_BUFS];
    esp_capture_video_info_t    nego_result;
    uint8_t                     nego_ok        : 1;
    uint8_t                     started        : 1;
    uint8_t                     use_fixed_caps : 1;
    uint8_t                     need_convert_420 : 1;
    SemaphoreHandle_t           yuv420_lock;
    uint8_t                    *yuv420_cache;
    uint8_t                    *src_buffer;
} v4l2_src_t;

static esp_capture_format_id_t get_codec_type(uint32_t fmt)
{
    switch (fmt) {
        // TODO P4 v4le only support O_UYY_E_VYY
        case V4L2_PIX_FMT_YUV420:
            return ESP_CAPTURE_FMT_ID_O_UYY_E_VYY;
        case V4L2_PIX_FMT_YUV422P:
            return ESP_CAPTURE_FMT_ID_YUV422P;
        case V4L2_PIX_FMT_MJPEG:
        case V4L2_PIX_FMT_JPEG:
            return ESP_CAPTURE_FMT_ID_MJPEG;
        case V4L2_PIX_FMT_RGB565:
            return ESP_CAPTURE_FMT_ID_RGB565;
        default:
            return ESP_CAPTURE_FMT_ID_NONE;
    }
}

static uint32_t get_v4l2_type(esp_capture_format_id_t codec)
{
    switch (codec) {
        case ESP_CAPTURE_FMT_ID_YUV420:
        case ESP_CAPTURE_FMT_ID_O_UYY_E_VYY:
            return V4L2_PIX_FMT_YUV420;
        case ESP_CAPTURE_FMT_ID_YUV422P:
            return V4L2_PIX_FMT_YUV422P;
        case ESP_CAPTURE_FMT_ID_MJPEG:
            return V4L2_PIX_FMT_MJPEG;
        case ESP_CAPTURE_FMT_ID_RGB565:
            return V4L2_PIX_FMT_RGB565;
        default:
            return 0;
    }
}

static esp_capture_err_t v4l2_open(esp_capture_video_src_if_t *src)
{
    v4l2_src_t *v4l2 = (v4l2_src_t *)src;
    do {
        struct v4l2_capability capability;
        v4l2->fd = open(v4l2->dev_name, O_RDONLY);
        if (v4l2->fd <= 0) {
            ESP_LOGE(TAG, "Fail to open device");
            return ESP_FAIL;
        }
        if (ioctl(v4l2->fd, VIDIOC_QUERYCAP, &capability)) {
            ESP_LOGE(TAG, "Fail to get capability");
            break;
        }
        if ((capability.capabilities & V4L2_CAP_VIDEO_CAPTURE) != V4L2_CAP_VIDEO_CAPTURE) {
            ESP_LOGE(TAG, "Not a capture device");
            break;
        }
        v4l2->format_count = 0;
        for (int i = 0; i < MAX_SUPPORT_FORMATS_NUM; i++) {
            struct v4l2_fmtdesc fmtdesc = {
                .index = i,
                .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            };
            if (ioctl(v4l2->fd, VIDIOC_ENUM_FMT, &fmtdesc) != 0) {
                break;
            }
            v4l2->support_formats[v4l2->format_count] = get_codec_type(fmtdesc.pixelformat);
            if (v4l2->support_formats[v4l2->format_count]) {
                ESP_LOGD(TAG, "Support Format: %c%c%c%c", FMT_STR(fmtdesc.pixelformat));
                v4l2->format_count++;
            }
        }
        if (v4l2->format_count == 0) {
            ESP_LOGE(TAG, "No support format");
            break;
        }
        ESP_LOGI(TAG, "Success to open camera");
        return 0;
    } while (0);
    if (v4l2->fd > 0) {
        close(v4l2->fd);
        v4l2->fd = 0;
    }
    return ESP_CAPTURE_ERR_NO_RESOURCES;
}

static esp_capture_err_t v4l2_get_support_codecs(esp_capture_video_src_if_t *src, const esp_capture_format_id_t **codecs,
                                                 uint8_t *num)
{
    v4l2_src_t *v4l2 = (v4l2_src_t *)src;
    *codecs = v4l2->support_formats;
    *num = v4l2->format_count;
    return 0;
}

static bool v4l2_is_input_supported(v4l2_src_t *v4l2, esp_capture_format_id_t in_codec)
{
    for (uint8_t i = 0; i < v4l2->format_count; i++) {
        if (v4l2->support_formats[i] == in_codec) {
            return true;
        }
    }
    return false;
}

static esp_capture_err_t v4l2_set_fixed_caps(esp_capture_video_src_if_t *src, const esp_capture_video_info_t *fixed_caps)
{
    if (src == NULL || fixed_caps == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    v4l2_src_t *v4l2 = (v4l2_src_t *)src;
    v4l2->use_fixed_caps = (fixed_caps->format_id != ESP_CAPTURE_FMT_ID_NONE);
    if (v4l2->use_fixed_caps) {
        v4l2->nego_result = *fixed_caps;
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t v4l2_match_resolution(v4l2_src_t *v4l2, uint32_t pixel_fmt, esp_capture_video_info_t *wanted,
                                               esp_capture_video_info_t *actual)
{
    struct v4l2_format init_format;
    memset(&init_format, 0, sizeof(init_format));
    init_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(v4l2->fd, VIDIOC_G_FMT, &init_format) != 0) {
        ESP_LOGE(TAG, "Failed to get init format");
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    actual->width = init_format.fmt.pix.width;
    actual->height = init_format.fmt.pix.height;
    actual->format_id = get_codec_type(pixel_fmt);
    actual->fps = wanted->fps;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t v4l2_negotiate_format(v4l2_src_t *v4l2, esp_capture_video_info_t *vid_info)
{
    // Check format supported or not
    if (v4l2_is_input_supported(v4l2, vid_info->format_id) == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    esp_capture_video_info_t match_info = *vid_info;
    esp_capture_err_t ret = v4l2_match_resolution(v4l2, get_v4l2_type(vid_info->format_id), vid_info, &match_info);
    if (ret != ESP_CAPTURE_ERR_OK) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    v4l2->nego_result = match_info;
    v4l2->nego_ok = true;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t v4l2_negotiate_caps(esp_capture_video_src_if_t *src, esp_capture_video_info_t *in_cap,
                                             esp_capture_video_info_t *out_caps)
{
    v4l2_src_t *v4l2 = (v4l2_src_t *)src;
    v4l2->nego_ok = false;
    v4l2->need_convert_420 = false;
    do {
        if (v4l2->use_fixed_caps) {
            if (in_cap->format_id != v4l2->nego_result.format_id && (in_cap->format_id != ESP_CAPTURE_FMT_ID_ANY)) {
                return ESP_CAPTURE_ERR_NOT_SUPPORTED;
            }
            v4l2_negotiate_format(v4l2, &v4l2->nego_result);
            break;
        }
        if (in_cap->format_id == ESP_CAPTURE_FMT_ID_YUV420) {
            // Try to use YUV422 mode instead
            esp_capture_video_info_t prefer_info = *in_cap;
            prefer_info.format_id = ESP_CAPTURE_FMT_ID_YUV422P;
            v4l2_negotiate_format(v4l2, &prefer_info);
            if (v4l2->nego_ok) {
                // Convert to 420 internally
                v4l2->need_convert_420 = true;
                *out_caps = v4l2->nego_result;
                out_caps->format_id = ESP_CAPTURE_FMT_ID_YUV420;
                return ESP_CAPTURE_ERR_OK;
            }
        }
        if (v4l2->format_count && (in_cap->format_id == ESP_CAPTURE_FMT_ID_ANY)) {
            esp_capture_video_info_t prefer_info = *in_cap;
            prefer_info.format_id = v4l2->support_formats[0];
            v4l2_negotiate_format(v4l2, &prefer_info);
            break;
        }
        v4l2_negotiate_format(v4l2, in_cap);
    } while (0);
    if (v4l2->nego_ok) {
        *out_caps = v4l2->nego_result;
        return ESP_CAPTURE_ERR_OK;
    }
    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
}

static esp_capture_err_t v4l2_alloc_buffer(v4l2_src_t *v4l2, esp_capture_video_info_t *vid_info)
{
    do {
        struct v4l2_requestbuffers req = {0};
        struct v4l2_format format = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .fmt.pix.width = vid_info->width,
            .fmt.pix.height = vid_info->height,
        };
        format.fmt.pix.pixelformat = get_v4l2_type(vid_info->format_id);
        int ret;
        if ((ret = ioctl(v4l2->fd, VIDIOC_S_FMT, &format)) != 0) {
            ESP_LOGE(TAG, "Failed to set format codec %x %x ret %d", (int)vid_info->format_id, (int)format.fmt.pix.pixelformat, ret);
            break;
        }
        req.count = v4l2->buf_count;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (ioctl(v4l2->fd, VIDIOC_REQBUFS, &req) != 0) {
            ESP_LOGE(TAG, "Failed to require buffer");
            break;
        }
        for (int i = 0; i < v4l2->buf_count; i++) {
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            if (ioctl(v4l2->fd, VIDIOC_QUERYBUF, &buf) != 0) {
                break;
            }
            v4l2->fb_buffer[i] = (uint8_t *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, v4l2->fd, buf.m.offset);
            if (!v4l2->fb_buffer[i]) {
                ESP_LOGE(TAG, "Failed to map buffer");
                break;
            }
            if (ioctl(v4l2->fd, VIDIOC_QBUF, &buf) != 0) {
                ESP_LOGE(TAG, "Failed to queue video frame");
                break;
            }
        }
        return ESP_CAPTURE_ERR_OK;
    } while (0);
    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
}

static esp_capture_err_t v4l2_start(esp_capture_video_src_if_t *src)
{
    v4l2_src_t *v4l2 = (v4l2_src_t *)src;
    if (v4l2 == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    if (v4l2->nego_ok == false) {
        ESP_LOGE(TAG, "Negotiate not OK yet");
        return ESP_CAPTURE_ERR_INVALID_STATE;
    }
    int ret = v4l2_alloc_buffer(v4l2, &v4l2->nego_result);
    if (ret != ESP_CAPTURE_ERR_OK) {
        return ret;
    }
    uint32_t type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(v4l2->fd, VIDIOC_STREAMON, &type);
    if (v4l2->need_convert_420) {
        v4l2->yuv420_cache = malloc(v4l2->nego_result.width * v4l2->nego_result.height * 3 / 2);
        if (v4l2->yuv420_cache == NULL) {
            return ESP_CAPTURE_ERR_NO_MEM;
        }
        v4l2->yuv420_lock = xSemaphoreCreateCounting(1, 1);
        if (v4l2->yuv420_lock == NULL) {
            return ESP_CAPTURE_ERR_NO_MEM;
        }
    }
    v4l2->started = true;
    return ESP_CAPTURE_ERR_OK;
}

static void convert_yuv420(uint32_t w, uint32_t h, uint8_t *src, uint8_t *dst)
{
    uint32_t bytes = w * h;
    uint8_t *y = dst;
    uint8_t *u = dst + bytes;
    uint8_t *v = u + (bytes >> 2);
    w >>= 1;
    h >>= 1;
    for (int i = 0; i < h; i++) {
        for (int i = 0; i < w; i++) {
            *(y++) = *(src++);
            *(u++) = *(src++);
            *(y++) = *(src++);
            *(v++) = *(src++);
        }
        for (int i = 0; i < w; i++) {
            *(y++) = *(src);
            src += 2;
            *(y++) = *(src);
            src += 2;
        }
    }
}

static esp_capture_err_t v4l2_acquire_frame(esp_capture_video_src_if_t *src, esp_capture_stream_frame_t *frame)
{
    v4l2_src_t *v4l2 = (v4l2_src_t *)src;
    if (v4l2->started == false) {
        return ESP_CAPTURE_ERR_INVALID_STATE;
    }
    struct v4l2_buffer buf = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };
    int ret = ioctl(v4l2->fd, VIDIOC_DQBUF, &buf);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to receive video frame ret %d", ret);
        return ESP_CAPTURE_ERR_INTERNAL;
    }
    v4l2->fb_used[buf.index] = true;
    frame->data = v4l2->fb_buffer[buf.index];
    frame->size = buf.bytesused;
    v4l2->v4l2_buf[buf.index] = buf;
    if (v4l2->need_convert_420) {
        xSemaphoreTake(v4l2->yuv420_lock, portMAX_DELAY);
        convert_yuv420(v4l2->nego_result.width, v4l2->nego_result.height,
                        frame->data, v4l2->yuv420_cache);
        v4l2->src_buffer = frame->data;
        frame->data = v4l2->yuv420_cache;
        frame->size = frame->size * 3 / 4;
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t v4l2_release_frame(esp_capture_video_src_if_t *src, esp_capture_stream_frame_t *frame)
{
    v4l2_src_t *v4l2 = (v4l2_src_t *)src;
    if (v4l2 == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    if (v4l2->started == false) {
        return ESP_CAPTURE_ERR_INVALID_STATE;
    }
    uint8_t *frame_data = frame->data;
    if (v4l2->need_convert_420) {
        frame_data = v4l2->src_buffer;
        xSemaphoreGive(v4l2->yuv420_lock);
    }
    for (int i = 0; i < v4l2->buf_count; i++) {
        struct v4l2_buffer *buf = &v4l2->v4l2_buf[i];
        if (v4l2->fb_used[i] && v4l2->fb_buffer[i] == frame_data) {
            v4l2->fb_used[i] = 0;
            ioctl(v4l2->fd, VIDIOC_QBUF, buf);
            return 0;
        }
    }
    ESP_LOGW(TAG, "not found frame %p", frame->data);
    return ESP_CAPTURE_ERR_NOT_FOUND;
}

static esp_capture_err_t v4l2_stop(esp_capture_video_src_if_t *src)
{
    v4l2_src_t *v4l2 = (v4l2_src_t *)src;
    if (v4l2 == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    uint32_t type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(v4l2->fd, VIDIOC_STREAMOFF, &type);
    if (v4l2->yuv420_lock) {
        vSemaphoreDelete(v4l2->yuv420_lock);
        v4l2->yuv420_lock = NULL;
    }
    if (v4l2->yuv420_cache) {
        free(v4l2->yuv420_cache);
        v4l2->yuv420_cache = NULL;
    }
    v4l2->need_convert_420 = false;
    v4l2->nego_ok = false;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t v4l2_close(esp_capture_video_src_if_t *src)
{
    v4l2_src_t *v4l2 = (v4l2_src_t *)src;
    if (v4l2 == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    if (v4l2->fd > 0) {
        close(v4l2->fd);
    }
    v4l2->fd = 0;
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_video_src_if_t *esp_capture_new_video_v4l2_src(esp_capture_video_v4l2_src_cfg_t *cfg)
{
    if (cfg == NULL || cfg->buf_count == 0) {
        return NULL;
    }
    v4l2_src_t *v4l2 = capture_calloc(1, sizeof(v4l2_src_t));
    if (v4l2 == NULL) {
        return NULL;
    }
    v4l2->base.open = v4l2_open;
    v4l2->base.get_support_codecs = v4l2_get_support_codecs;
    v4l2->base.set_fixed_caps = v4l2_set_fixed_caps;
    v4l2->base.negotiate_caps = v4l2_negotiate_caps;
    v4l2->base.start = v4l2_start;
    v4l2->base.acquire_frame = v4l2_acquire_frame;
    v4l2->base.release_frame = v4l2_release_frame;
    v4l2->base.stop = v4l2_stop;
    v4l2->base.close = v4l2_close;
    strncpy(v4l2->dev_name, cfg->dev_name, sizeof(v4l2->dev_name));
    v4l2->buf_count = (cfg->buf_count > MAX_BUFS ? MAX_BUFS : cfg->buf_count);
    return &v4l2->base;
}

#endif  // CONFIG_IDF_TARGET_ESP32P4
