/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */
#include <sdkconfig.h>

#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C5 || \
    CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32C61 || CONFIG_IDF_TARGET_ESP32S31

#include "esp_capture_types.h"
#include "linux/videodev2.h"
#include "esp_video_init.h"
#include "esp_video_ioctl.h"
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_capture_video_src_if.h"
#include "esp_capture_video_v4l2_src.h"
#include "capture_os.h"
#include "esp_cache.h"
#include "esp_log.h"

#define TAG "V4L2_SRC"

#define MAX_BUFS                (4)
#define MAX_SUPPORT_FORMATS_NUM (8)
#define MAX_QUERY_SIZES         (48)
#define FMT_STR(fmt)            ((uint8_t *)&fmt)[0], ((uint8_t *)&fmt)[1], ((uint8_t *)&fmt)[2], ((uint8_t *)&fmt)[3]
#define MAX_DQBUF_TIMEOUT       (1000)

typedef struct {
    esp_capture_video_src_if_t  base;
    char                        dev_name[16];
    uint8_t                     buf_count;
    esp_capture_format_id_t     support_formats[MAX_SUPPORT_FORMATS_NUM];
    uint8_t                     format_count;
    bool                        sensor_fmt_supported;
    bool                        selected_sensor_fmt_valid;
    esp_cam_sensor_format_t     selected_sensor_fmt;
    int                         fd;
    uint8_t                    *fb_buffer[MAX_BUFS];
    size_t                      fb_length[MAX_BUFS];
    struct v4l2_buffer          v4l2_buf[MAX_BUFS];
    bool                        fb_used[MAX_BUFS];
    bool                        buffers_mapped;
    esp_capture_video_info_t    nego_result;
    esp_capture_video_info_t    fixed_info;
    uint8_t                     nego_ok             : 1;
    uint8_t                     started             : 1;
    uint8_t                     use_fixed_caps      : 1;
    uint8_t                     need_convert_420    : 1;
    uint8_t                     need_rgb565_convert : 1;
    SemaphoreHandle_t           yuv420_lock;
    uint8_t                    *yuv420_cache;
    uint8_t                    *src_buffer;
} v4l2_src_t;

static esp_capture_err_t v4l2_close(esp_capture_video_src_if_t *src);
static esp_capture_err_t v4l2_open(esp_capture_video_src_if_t *src);

static esp_capture_format_id_t get_codec_type(uint32_t fmt)
{
    switch (fmt) {
        // TODO P4 v4l2 only support O_UYY_E_VYY
        case V4L2_PIX_FMT_YUV420:
            return ESP_CAPTURE_FMT_ID_O_UYY_E_VYY;
        case V4L2_PIX_FMT_MJPEG:
        case V4L2_PIX_FMT_JPEG:
            return ESP_CAPTURE_FMT_ID_MJPEG;
        case V4L2_PIX_FMT_H264:
            return ESP_CAPTURE_FMT_ID_H264;
        case V4L2_PIX_FMT_RGB565:
            return ESP_CAPTURE_FMT_ID_RGB565;
        case V4L2_PIX_FMT_RGB565X:
            return ESP_CAPTURE_FMT_ID_RGB565_BE;
        case V4L2_PIX_FMT_YUYV:
            return ESP_CAPTURE_FMT_ID_YUV422;
        case V4L2_PIX_FMT_UYVY:
            return ESP_CAPTURE_FMT_ID_UYVY422;
        default:
            return ESP_CAPTURE_FMT_ID_NONE;
    }
}

static esp_capture_format_id_t get_sensor_codec_type(esp_cam_sensor_output_format_t fmt)
{
    switch (fmt) {
        case ESP_CAM_SENSOR_PIXFORMAT_RGB565_LE:
            return ESP_CAPTURE_FMT_ID_RGB565;
        case ESP_CAM_SENSOR_PIXFORMAT_RGB565_BE:
            return ESP_CAPTURE_FMT_ID_RGB565_BE;
        case ESP_CAM_SENSOR_PIXFORMAT_YUV422_UYVY:
            return ESP_CAPTURE_FMT_ID_UYVY422;
        case ESP_CAM_SENSOR_PIXFORMAT_YUV422_YUYV:
            return ESP_CAPTURE_FMT_ID_YUV422;
        case ESP_CAM_SENSOR_PIXFORMAT_YUV420:
            return ESP_CAPTURE_FMT_ID_O_UYY_E_VYY;
        case ESP_CAM_SENSOR_PIXFORMAT_JPEG:
            return ESP_CAPTURE_FMT_ID_MJPEG;
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
        case ESP_CAPTURE_FMT_ID_MJPEG:
            return V4L2_PIX_FMT_JPEG;
        case ESP_CAPTURE_FMT_ID_H264:
            return V4L2_PIX_FMT_H264;
        case ESP_CAPTURE_FMT_ID_RGB565:
            return V4L2_PIX_FMT_RGB565;
        case ESP_CAPTURE_FMT_ID_RGB565_BE:
            return V4L2_PIX_FMT_RGB565X;
        case ESP_CAPTURE_FMT_ID_YUV422:
            return V4L2_PIX_FMT_YUYV;
        case ESP_CAPTURE_FMT_ID_UYVY422:
            return V4L2_PIX_FMT_UYVY;
        default:
            return 0;
    }
}

static void v4l2_extra_setting(v4l2_src_t *v4l2)
{
    struct timeval timeout;
    memset(&timeout, 0, sizeof(timeout));
    timeout.tv_sec = MAX_DQBUF_TIMEOUT / 1000;
    timeout.tv_usec = (MAX_DQBUF_TIMEOUT % 1000) * 1000;
    // Setting Dequeue buffer timeout to avoid block forever when read
    ioctl(v4l2->fd, VIDIOC_S_DQBUF_TIMEOUT, &timeout);
}

static bool v4l2_sensor_fmt_equal(const esp_cam_sensor_format_t *a, const esp_cam_sensor_format_t *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    return (a->format == b->format && a->width == b->width && a->height == b->height &&
            a->fps == b->fps && a->regs == b->regs);
}

static bool v4l2_add_support_format(v4l2_src_t *v4l2, esp_capture_format_id_t format)
{
    for (uint8_t i = 0; i < v4l2->format_count; i++) {
        if (v4l2->support_formats[i] == format) {
            return true;
        }
    }
    if (v4l2->format_count >= MAX_SUPPORT_FORMATS_NUM) {
        return false;
    }
    v4l2->support_formats[v4l2->format_count++] = format;
    return true;
}

static bool v4l2_output_format_supported(v4l2_src_t *v4l2, esp_capture_format_id_t format_id)
{
    for (int i = 0; ; i++) {
        struct v4l2_fmtdesc fmtdesc = {
            .index = i,
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        };
        if (ioctl(v4l2->fd, VIDIOC_ENUM_FMT, &fmtdesc) != 0) {
            break;
        }
        if (get_codec_type(fmtdesc.pixelformat) == format_id) {
            return true;
        }
    }
    return false;
}

static uint32_t v4l2_get_device_pixel_format(v4l2_src_t *v4l2, esp_capture_format_id_t format_id)
{
    for (uint32_t i = 0; ; i++) {
        struct v4l2_fmtdesc fmtdesc = {
            .index = i,
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        };
        if (ioctl(v4l2->fd, VIDIOC_ENUM_FMT, &fmtdesc) != 0) {
            break;
        }
        if (get_codec_type(fmtdesc.pixelformat) == format_id) {
            return fmtdesc.pixelformat;
        }
    }
    return get_v4l2_type(format_id);
}

static void v4l2_discover_standard_caps(v4l2_src_t *v4l2)
{
    for (int i = 0; ; i++) {
        struct v4l2_fmtdesc fmtdesc = {
            .index = i,
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        };
        if (ioctl(v4l2->fd, VIDIOC_ENUM_FMT, &fmtdesc) != 0) {
            break;
        }
        esp_capture_format_id_t format = get_codec_type(fmtdesc.pixelformat);
        if (format == ESP_CAPTURE_FMT_ID_NONE) {
            continue;
        }
        ESP_LOGD(TAG, "Support format %c%c%c%c", FMT_STR(fmtdesc.pixelformat));
        v4l2_add_support_format(v4l2, format);
    }
}

static void v4l2_discover_sensor_caps(v4l2_src_t *v4l2)
{
    for (uint32_t i = 0; ; i++) {
        struct v4l2_sensor_format_enum sensor_enum = {
            .index = i,
        };
        if (ioctl(v4l2->fd, VIDIOC_ENUM_SENSOR_FMT, &sensor_enum) != 0) {
            break;
        }
        esp_capture_format_id_t format = get_sensor_codec_type(sensor_enum.format.format);
        if (format != ESP_CAPTURE_FMT_ID_NONE) {
            v4l2_add_support_format(v4l2, format);
        }
    }
}

static esp_capture_err_t v4l2_discover_caps(v4l2_src_t *v4l2)
{
    v4l2->format_count = 0;
    v4l2->sensor_fmt_supported = false;

    struct v4l2_sensor_format_enum probe = {
        .index = 0,
    };
    v4l2_discover_standard_caps(v4l2);
    if (ioctl(v4l2->fd, VIDIOC_ENUM_SENSOR_FMT, &probe) == 0) {
        v4l2->sensor_fmt_supported = true;
        v4l2_discover_sensor_caps(v4l2);
    }

    if (v4l2->format_count == 0) {
        /* Fallback: at least current G_FMT when enumeration produced nothing useful. */
        struct v4l2_format init_format = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        };
        if (ioctl(v4l2->fd, VIDIOC_G_FMT, &init_format) == 0) {
            esp_capture_format_id_t format = get_codec_type(init_format.fmt.pix.pixelformat);
            if (format != ESP_CAPTURE_FMT_ID_NONE) {
                v4l2_add_support_format(v4l2, format);
            }
        }
    }
    if (v4l2->format_count == 0) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    ESP_LOGI(TAG, "Discovered %u formats (sensor=%d, no sensor reconfiguration)",
             v4l2->format_count, (int)v4l2->sensor_fmt_supported);
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t v4l2_ensure_open(v4l2_src_t *v4l2, bool *need_close)
{
    if (need_close) {
        *need_close = false;
    }
    if (v4l2->fd >= 0) {
        return ESP_CAPTURE_ERR_OK;
    }
    esp_capture_err_t ret = v4l2_open(&v4l2->base);
    if (ret == ESP_CAPTURE_ERR_OK && need_close) {
        *need_close = true;
    }
    return ret;
}

static esp_capture_err_t v4l2_open(esp_capture_video_src_if_t *src)
{
    v4l2_src_t *v4l2 = (v4l2_src_t *)src;
    do {
        if (v4l2->fd >= 0) {
            // Allow pre-open when negotiate
            return ESP_CAPTURE_ERR_OK;
        }
        struct v4l2_capability capability;
        v4l2->fd = open(v4l2->dev_name, O_RDONLY);
        if (v4l2->fd < 0) {
            ESP_LOGE(TAG, "Fail to open device");
            return ESP_CAPTURE_ERR_NO_RESOURCES;
        }
        if (ioctl(v4l2->fd, VIDIOC_QUERYCAP, &capability)) {
            ESP_LOGE(TAG, "Fail to get capability");
            break;
        }
        if ((capability.capabilities & V4L2_CAP_VIDEO_CAPTURE) != V4L2_CAP_VIDEO_CAPTURE) {
            ESP_LOGE(TAG, "Not a capture device");
            break;
        }
        if (v4l2_discover_caps(v4l2) != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "No supported format");
            break;
        }
        v4l2_extra_setting(v4l2);
        ESP_LOGI(TAG, "Success to open camera");
        return ESP_CAPTURE_ERR_OK;
    } while (0);
    if (v4l2->fd >= 0) {
        close(v4l2->fd);
        v4l2->fd = -1;
    }
    return ESP_CAPTURE_ERR_NO_RESOURCES;
}

static esp_capture_err_t v4l2_get_support_codecs(esp_capture_video_src_if_t *src, const esp_capture_format_id_t **codecs,
                                                 uint8_t *num)
{
    v4l2_src_t *v4l2 = (v4l2_src_t *)src;
    *codecs = v4l2->support_formats;
    *num = v4l2->format_count;
    return ESP_CAPTURE_ERR_OK;
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

static inline uint8_t v4l2_fract_to_fps(const struct v4l2_fract *fract)
{
    if (fract->numerator == 0) {
        return 0;
    }
    uint32_t fps = (fract->denominator + (fract->numerator / 2)) / fract->numerator;
    if (fps == 0) {
        return 1;
    }
    return (uint8_t)fps;
}

static uint8_t v4l2_query_default_fps(v4l2_src_t *v4l2, uint32_t pixel_fmt, uint32_t width, uint32_t height)
{
    /* Current / initial interval: CSI/DVP via common get_parm, UVC via uvc_video_get_parm. */
    struct v4l2_streamparm parm = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    };
    if (ioctl(v4l2->fd, VIDIOC_G_PARM, &parm) == 0) {
        uint8_t fps = v4l2_fract_to_fps(&parm.parm.capture.timeperframe);
        if (fps) {
            return fps;
        }
    }
    struct v4l2_frmivalenum frmival = {
        .index = 0,
        .pixel_format = pixel_fmt,
        .width = width,
        .height = height,
    };
    if (ioctl(v4l2->fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) != 0) {
        return 0;
    }
    if (frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
        return v4l2_fract_to_fps(&frmival.discrete);
    }
    if (frmival.type == V4L2_FRMIVAL_TYPE_CONTINUOUS || frmival.type == V4L2_FRMIVAL_TYPE_STEPWISE) {
        return v4l2_fract_to_fps(&frmival.stepwise.min);
    }
    return 0;
}

static esp_capture_err_t v4l2_get_default_format(esp_capture_video_src_if_t *src, esp_capture_video_info_t *default_format)
{
    v4l2_src_t *v4l2 = (v4l2_src_t *)src;
    if (src == NULL || default_format == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    bool need_close = false;
    if (v4l2->fd < 0) {
        esp_capture_err_t ret = v4l2_open(src);
        if (ret != ESP_CAPTURE_ERR_OK) {
            return ret;
        }
        need_close = true;
    }
    struct v4l2_format init_format = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    };
    esp_capture_err_t ret = ESP_CAPTURE_ERR_OK;
    if (ioctl(v4l2->fd, VIDIOC_G_FMT, &init_format) != 0) {
        ESP_LOGE(TAG, "Failed to get init format");
        ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
    } else {
        memset(default_format, 0, sizeof(*default_format));
        default_format->format_id = get_codec_type(init_format.fmt.pix.pixelformat);
        default_format->width = init_format.fmt.pix.width;
        default_format->height = init_format.fmt.pix.height;
        default_format->fps = v4l2_query_default_fps(v4l2, init_format.fmt.pix.pixelformat,
                                                    init_format.fmt.pix.width, init_format.fmt.pix.height);
    }
    if (need_close) {
        v4l2_close(src);
    }
    return ret;
}

static esp_capture_err_t v4l2_set_fixed_caps(esp_capture_video_src_if_t *src, const esp_capture_video_info_t *fixed_caps)
{
    if (src == NULL || fixed_caps == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    v4l2_src_t *v4l2 = (v4l2_src_t *)src;
    v4l2->use_fixed_caps = (fixed_caps->format_id != ESP_CAPTURE_FMT_ID_NONE);
    if (v4l2->use_fixed_caps) {
        v4l2->fixed_info = *fixed_caps;
    }
    return ESP_CAPTURE_ERR_OK;
}

static bool v4l2_sensor_supports_format(const esp_cam_sensor_format_t *candidate,
                                        const esp_cam_sensor_format_t *current,
                                        esp_capture_format_id_t format_id,
                                        bool output_format_supported)
{
    if (get_sensor_codec_type(candidate->format) == format_id) {
        return true;
    }
    if (output_format_supported == false) {
        return false;
    }
    /*
     * CSI output formats are controller/ISP capabilities and can be used with
     * all enumerated sensor resolutions. DVP/SPI extra output formats depend
     * on the sensor pixel format, so only reuse them for an equivalent mode.
     */
    if (candidate->port == ESP_CAM_SENSOR_MIPI_CSI) {
        return true;
    }
    return current && candidate->port == current->port && candidate->format == current->format;
}

static esp_capture_err_t v4l2_match_sensor_resolution(v4l2_src_t *v4l2, esp_capture_format_id_t format_id,
                                                      const esp_capture_video_info_t *wanted,
                                                      esp_capture_video_info_t *actual)
{
    esp_cam_sensor_format_t current = {0};
    bool have_current = (ioctl(v4l2->fd, VIDIOC_G_SENSOR_FMT, &current) == 0);
    bool output_format_supported = v4l2_output_format_supported(v4l2, format_id);

    esp_cam_sensor_format_t exact = {0};
    esp_cam_sensor_format_t best_ge = {0};
    esp_cam_sensor_format_t best_lt = {0};
    bool have_exact = false;
    bool have_best_ge = false;
    bool have_best_lt = false;
    uint32_t best_ge_w = UINT32_MAX;
    uint32_t best_lt_w = 0;

    for (uint32_t i = 0; ; i++) {
        struct v4l2_sensor_format_enum sensor_enum = {
            .index = i,
        };
        if (ioctl(v4l2->fd, VIDIOC_ENUM_SENSOR_FMT, &sensor_enum) != 0) {
            break;
        }
        if (v4l2_sensor_supports_format(&sensor_enum.format, have_current ? &current : NULL,
                                        format_id, output_format_supported) == false) {
            continue;
        }
        esp_cam_sensor_format_t *candidate = &sensor_enum.format;
        if (candidate->width == wanted->width && candidate->height == wanted->height) {
            if (have_exact == false || (wanted->fps && candidate->fps == wanted->fps)) {
                exact = *candidate;
                have_exact = true;
            }
            if (wanted->fps == 0 || candidate->fps == wanted->fps) {
                break;
            }
            continue;
        }
        if (candidate->width >= wanted->width) {
            if (have_best_ge == false || (uint32_t)candidate->width < best_ge_w ||
                    ((uint32_t)candidate->width == best_ge_w && candidate->height < best_ge.height)) {
                best_ge_w = candidate->width;
                best_ge = *candidate;
                have_best_ge = true;
            }
        } else if (have_best_lt == false || (uint32_t)candidate->width >= best_lt_w) {
            best_lt_w = candidate->width;
            best_lt = *candidate;
            have_best_lt = true;
        }
    }

    const esp_cam_sensor_format_t *selected = have_exact ? &exact :
                                              (have_best_ge ? &best_ge :
                                               (have_best_lt ? &best_lt : NULL));
    if (selected == NULL) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    v4l2->selected_sensor_fmt = *selected;
    v4l2->selected_sensor_fmt_valid = true;
    actual->format_id = format_id;
    actual->width = selected->width;
    actual->height = selected->height;
    actual->fps = wanted->fps ? wanted->fps : selected->fps;
    ESP_LOGI(TAG, "Best sensor match %dx%d@%d fmt=%x", actual->width, actual->height,
             actual->fps, (unsigned)actual->format_id);
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t v4l2_match_resolution(v4l2_src_t *v4l2, esp_capture_format_id_t format_id,
                                               esp_capture_video_info_t *wanted, esp_capture_video_info_t *actual)
{
    v4l2->selected_sensor_fmt_valid = false;
    if (v4l2->sensor_fmt_supported) {
        return v4l2_match_sensor_resolution(v4l2, format_id, wanted, actual);
    }

    /* Standard V4L2 path (for example UVC): enumerate sizes directly. */
    uint32_t pixel_fmt = v4l2_get_device_pixel_format(v4l2, format_id);
    struct v4l2_frmsizeenum frame_size = {0};
    uint32_t min_width = 0;
    uint32_t min_height = 0;
    uint32_t max_width = 0;
    uint32_t max_height = 0;
    frame_size.pixel_format = pixel_fmt;
    frame_size.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (int i = 0; ; i++) {
        frame_size.index = i;
        int ret = ioctl(v4l2->fd, VIDIOC_ENUM_FRAMESIZES, &frame_size);
        if (ret != 0) {
            if (i == 0) {
                struct v4l2_format init_format;
                memset(&init_format, 0, sizeof(init_format));
                init_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (ioctl(v4l2->fd, VIDIOC_G_FMT, &init_format) != 0) {
                    ESP_LOGE(TAG, "Failed to get init format");
                    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
                }
                min_width = init_format.fmt.pix.width;
                min_height = init_format.fmt.pix.height;
            }
            break;
        }
        if (frame_size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            if (frame_size.discrete.width == wanted->width && frame_size.discrete.height == wanted->height) {
                min_width = frame_size.discrete.width;
                min_height = frame_size.discrete.height;
                break;
            }
            if (frame_size.discrete.width >= wanted->width) {
                if (min_width == 0 || frame_size.discrete.width < min_width) {
                    min_width = frame_size.discrete.width;
                    min_height = frame_size.discrete.height;
                }
            } else if (frame_size.discrete.width >= max_width) {
                max_width = frame_size.discrete.width;
                max_height = frame_size.discrete.height;
            }
        }
    }
    if ((min_width > 0 && min_height > 0) || (max_width > 0 && max_height > 0)) {
        actual->width = min_width ? min_width : max_width;
        actual->height = min_height ? min_height : max_height;
        ESP_LOGI(TAG, "Best match %dx%d", actual->width, actual->height);
        actual->format_id = get_codec_type(pixel_fmt);
        actual->fps = wanted->fps;
        return ESP_CAPTURE_ERR_OK;
    }
    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
}

static esp_capture_err_t v4l2_negotiate_format(v4l2_src_t *v4l2, esp_capture_video_info_t *vid_info)
{
    if (v4l2_is_input_supported(v4l2, vid_info->format_id) == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    esp_capture_video_info_t match_info = *vid_info;
    esp_capture_err_t ret = v4l2_match_resolution(v4l2, vid_info->format_id, vid_info, &match_info);
    if (ret != ESP_CAPTURE_ERR_OK) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    v4l2->nego_result = match_info;
    v4l2->nego_ok = true;
    return ESP_CAPTURE_ERR_OK;
}

static void v4l2_try_negotiate(v4l2_src_t *v4l2, esp_capture_video_info_t *in_cap, esp_capture_video_info_t *out_caps)
{
    v4l2_negotiate_format(v4l2, in_cap);
    if (v4l2->nego_ok) {
        *out_caps = v4l2->nego_result;
        return;
    }
    esp_capture_video_info_t prefer_info = *in_cap;
    if (in_cap->format_id == ESP_CAPTURE_FMT_ID_YUV420) {
        prefer_info.format_id = ESP_CAPTURE_FMT_ID_YUV422;
        v4l2_negotiate_format(v4l2, &prefer_info);
        if (v4l2->nego_ok) {
            v4l2->need_convert_420 = true;
        }
    } else if (in_cap->format_id == ESP_CAPTURE_FMT_ID_RGB565 ||
               in_cap->format_id == ESP_CAPTURE_FMT_ID_RGB565_BE) {
        prefer_info.format_id = (in_cap->format_id == ESP_CAPTURE_FMT_ID_RGB565) ?
                                ESP_CAPTURE_FMT_ID_RGB565_BE : ESP_CAPTURE_FMT_ID_RGB565;
        v4l2_negotiate_format(v4l2, &prefer_info);
        if (v4l2->nego_ok) {
            v4l2->need_rgb565_convert = true;
        }
    }
    if (v4l2->nego_ok) {
        *out_caps = v4l2->nego_result;
        out_caps->format_id = in_cap->format_id;
    }
}

static esp_capture_err_t v4l2_negotiate_caps(esp_capture_video_src_if_t *src, esp_capture_video_info_t *in_cap,
                                             esp_capture_video_info_t *out_caps)
{
    v4l2_src_t *v4l2 = (v4l2_src_t *)src;
    v4l2->nego_ok = false;
    v4l2->need_convert_420 = false;
    v4l2->need_rgb565_convert = false;
    v4l2->selected_sensor_fmt_valid = false;
    do {
        if (v4l2->use_fixed_caps) {
            if (in_cap->format_id != v4l2->fixed_info.format_id && (in_cap->format_id != ESP_CAPTURE_FMT_ID_ANY)) {
                return ESP_CAPTURE_ERR_NOT_SUPPORTED;
            }
            v4l2_try_negotiate(v4l2, &v4l2->fixed_info, out_caps);
            break;
        }
        if (v4l2->format_count && (in_cap->format_id == ESP_CAPTURE_FMT_ID_ANY)) {
            esp_capture_video_info_t prefer_info = *in_cap;
            prefer_info.format_id = v4l2->support_formats[0];
            v4l2_try_negotiate(v4l2, &prefer_info, out_caps);
            break;
        }
        v4l2_try_negotiate(v4l2, in_cap, out_caps);
    } while (0);
    if (v4l2->nego_ok) {
        return ESP_CAPTURE_ERR_OK;
    }
    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
}

static void v4l2_free_buffers(v4l2_src_t *v4l2)
{
    if (v4l2->fd < 0) {
        return;
    }
    for (int i = 0; i < v4l2->buf_count; i++) {
        if (v4l2->fb_buffer[i]) {
            munmap(v4l2->fb_buffer[i], v4l2->fb_length[i] ? v4l2->fb_length[i] : 1);
            v4l2->fb_buffer[i] = NULL;
            v4l2->fb_length[i] = 0;
            v4l2->fb_used[i] = false;
        }
    }
    if (v4l2->buffers_mapped) {
        struct v4l2_requestbuffers req = {
            .count = 0,
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
        };
        ioctl(v4l2->fd, VIDIOC_REQBUFS, &req);
        v4l2->buffers_mapped = false;
    }
}

static esp_capture_err_t v4l2_apply_sensor_format(v4l2_src_t *v4l2)
{
    if (v4l2->selected_sensor_fmt_valid == false || v4l2->sensor_fmt_supported == false) {
        return ESP_CAPTURE_ERR_OK;
    }
    esp_cam_sensor_format_t cur = {0};
    if (ioctl(v4l2->fd, VIDIOC_G_SENSOR_FMT, &cur) == 0 &&
            v4l2_sensor_fmt_equal(&cur, &v4l2->selected_sensor_fmt)) {
        return ESP_CAPTURE_ERR_OK;
    }
    if (ioctl(v4l2->fd, VIDIOC_S_SENSOR_FMT, &v4l2->selected_sensor_fmt) != 0) {
        ESP_LOGE(TAG, "Failed to set sensor format %ux%u",
                 v4l2->selected_sensor_fmt.width, v4l2->selected_sensor_fmt.height);
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t v4l2_alloc_buffer(v4l2_src_t *v4l2, esp_capture_video_info_t *vid_info)
{
    do {
        v4l2_free_buffers(v4l2);
        if (v4l2_apply_sensor_format(v4l2) != ESP_CAPTURE_ERR_OK) {
            break;
        }
        struct v4l2_requestbuffers req = {0};
        struct v4l2_format format = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .fmt.pix.width = vid_info->width,
            .fmt.pix.height = vid_info->height,
        };
        format.fmt.pix.pixelformat = v4l2_get_device_pixel_format(v4l2, vid_info->format_id);
        int ret;
        if ((ret = ioctl(v4l2->fd, VIDIOC_S_FMT, &format)) != 0) {
            ESP_LOGE(TAG, "Failed to set format codec %x %x ret %d", (int)vid_info->format_id,
                     (int)format.fmt.pix.pixelformat, ret);
            break;
        }
        req.count = v4l2->buf_count;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (ioctl(v4l2->fd, VIDIOC_REQBUFS, &req) != 0) {
            ESP_LOGE(TAG, "Failed to require buffer");
            break;
        }
        bool map_ok = true;
        for (int i = 0; i < v4l2->buf_count; i++) {
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            if (ioctl(v4l2->fd, VIDIOC_QUERYBUF, &buf) != 0) {
                map_ok = false;
                break;
            }
            v4l2->fb_buffer[i] = (uint8_t *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, v4l2->fd, buf.m.offset);
            if (!v4l2->fb_buffer[i] || v4l2->fb_buffer[i] == MAP_FAILED) {
                ESP_LOGE(TAG, "Failed to map buffer");
                v4l2->fb_buffer[i] = NULL;
                map_ok = false;
                break;
            }
            v4l2->fb_length[i] = buf.length;
            if (ioctl(v4l2->fd, VIDIOC_QBUF, &buf) != 0) {
                ESP_LOGE(TAG, "Failed to queue video frame");
                map_ok = false;
                break;
            }
        }
        if (map_ok == false) {
            break;
        }
        v4l2->buffers_mapped = true;
        return ESP_CAPTURE_ERR_OK;
    } while (0);
    v4l2_free_buffers(v4l2);
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

static void convert_rgb565(uint32_t w, uint32_t h, uint8_t *src)
{
    int pixels = w * h;
    uint16_t *src16 = (uint16_t *)src;
    int i = 0;
    while (i < pixels) {
        src16[i] = (src16[i] << 8) | (src16[i] >> 8);
        i++;
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
        int err = errno;
        if (err == ETIMEDOUT) {
            return ESP_CAPTURE_ERR_TIMEOUT;
        }
        ESP_LOGE(TAG, "Failed to receive video frame ret %d errno %d (%s)", ret, err, strerror(err));
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
    } else if (v4l2->need_rgb565_convert) {
        convert_rgb565(v4l2->nego_result.width, v4l2->nego_result.height, frame->data);
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
            return ESP_CAPTURE_ERR_OK;
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
    if (v4l2->fd >= 0) {
        ioctl(v4l2->fd, VIDIOC_STREAMOFF, &type);
    }
    v4l2_free_buffers(v4l2);
    if (v4l2->yuv420_lock) {
        vSemaphoreDelete(v4l2->yuv420_lock);
        v4l2->yuv420_lock = NULL;
    }
    if (v4l2->yuv420_cache) {
        free(v4l2->yuv420_cache);
        v4l2->yuv420_cache = NULL;
    }
    v4l2->need_convert_420 = false;
    v4l2->need_rgb565_convert = false;
    v4l2->nego_ok = false;
    v4l2->started = false;
    v4l2->selected_sensor_fmt_valid = false;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t v4l2_close(esp_capture_video_src_if_t *src)
{
    v4l2_src_t *v4l2 = (v4l2_src_t *)src;
    if (v4l2 == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    v4l2_free_buffers(v4l2);
    if (v4l2->fd >= 0) {
        close(v4l2->fd);
    }
    v4l2->fd = -1;
    v4l2->format_count = 0;
    v4l2->selected_sensor_fmt_valid = false;
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t esp_capture_v4l2_get_formats(esp_capture_video_src_if_t *src,
                                               esp_capture_format_id_t *formats,
                                               uint8_t *count)
{
    if (src == NULL || count == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    v4l2_src_t *v4l2 = (v4l2_src_t *)src;
    bool need_close = false;
    esp_capture_err_t ret = v4l2_ensure_open(v4l2, &need_close);
    if (ret != ESP_CAPTURE_ERR_OK) {
        return ret;
    }
    uint8_t needed = v4l2->format_count;
    if (formats == NULL) {
        *count = needed;
        ret = ESP_CAPTURE_ERR_OK;
    } else if (*count < needed) {
        *count = needed;
        ret = ESP_CAPTURE_ERR_NOT_ENOUGH;
    } else {
        memcpy(formats, v4l2->support_formats, needed * sizeof(esp_capture_format_id_t));
        *count = needed;
        ret = ESP_CAPTURE_ERR_OK;
    }
    if (need_close) {
        v4l2_close(src);
    }
    return ret;
}

esp_capture_err_t esp_capture_v4l2_get_frame_sizes(esp_capture_video_src_if_t *src,
                                                   esp_capture_format_id_t format_id,
                                                   esp_capture_v4l2_frame_size_t *sizes,
                                                   uint8_t *count)
{
    if (src == NULL || count == NULL || format_id == ESP_CAPTURE_FMT_ID_NONE) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    v4l2_src_t *v4l2 = (v4l2_src_t *)src;
    bool need_close = false;
    esp_capture_err_t ret = v4l2_ensure_open(v4l2, &need_close);
    if (ret != ESP_CAPTURE_ERR_OK) {
        return ret;
    }

    esp_capture_v4l2_frame_size_t tmp[MAX_QUERY_SIZES];
    uint8_t needed = 0;
    if (v4l2->sensor_fmt_supported) {
        esp_cam_sensor_format_t current = {0};
        bool have_current = (ioctl(v4l2->fd, VIDIOC_G_SENSOR_FMT, &current) == 0);
        bool output_format_supported = v4l2_output_format_supported(v4l2, format_id);

        for (uint32_t i = 0; ; i++) {
            struct v4l2_sensor_format_enum sensor_enum = {
                .index = i,
            };
            if (ioctl(v4l2->fd, VIDIOC_ENUM_SENSOR_FMT, &sensor_enum) != 0) {
                break;
            }
            if (v4l2_sensor_supports_format(&sensor_enum.format, have_current ? &current : NULL,
                                            format_id, output_format_supported) == false) {
                continue;
            }
            bool found = false;
            for (uint8_t j = 0; j < needed; j++) {
                if (tmp[j].width == sensor_enum.format.width && tmp[j].height == sensor_enum.format.height) {
                    found = true;
                    break;
                }
            }
            if (found == false && needed < MAX_QUERY_SIZES) {
                tmp[needed].width = sensor_enum.format.width;
                tmp[needed].height = sensor_enum.format.height;
                needed++;
            }
        }
    } else {
        uint32_t pixel_fmt = v4l2_get_device_pixel_format(v4l2, format_id);
        for (uint32_t i = 0; i < MAX_QUERY_SIZES; i++) {
            struct v4l2_frmsizeenum frame_size = {
                .index = i,
                .pixel_format = pixel_fmt,
                .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            };
            if (ioctl(v4l2->fd, VIDIOC_ENUM_FRAMESIZES, &frame_size) != 0) {
                break;
            }
            if (frame_size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                tmp[needed].width = frame_size.discrete.width;
                tmp[needed].height = frame_size.discrete.height;
                needed++;
            }
        }
        if (needed == 0) {
            struct v4l2_format current = {
                .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            };
            if (ioctl(v4l2->fd, VIDIOC_G_FMT, &current) == 0 &&
                    get_codec_type(current.fmt.pix.pixelformat) == format_id) {
                tmp[0].width = current.fmt.pix.width;
                tmp[0].height = current.fmt.pix.height;
                needed = 1;
            }
        }
    }

    if (needed == 0) {
        ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
    } else if (sizes == NULL) {
        *count = needed;
        ret = ESP_CAPTURE_ERR_OK;
    } else if (*count < needed) {
        *count = needed;
        ret = ESP_CAPTURE_ERR_NOT_ENOUGH;
    } else {
        memcpy(sizes, tmp, needed * sizeof(esp_capture_v4l2_frame_size_t));
        *count = needed;
        ret = ESP_CAPTURE_ERR_OK;
    }
    if (need_close) {
        v4l2_close(src);
    }
    return ret;
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
    v4l2->base.get_default_format = v4l2_get_default_format;
    v4l2->base.negotiate_caps = v4l2_negotiate_caps;
    v4l2->base.start = v4l2_start;
    v4l2->base.acquire_frame = v4l2_acquire_frame;
    v4l2->base.release_frame = v4l2_release_frame;
    v4l2->base.stop = v4l2_stop;
    v4l2->base.close = v4l2_close;
    strncpy(v4l2->dev_name, cfg->dev_name, sizeof(v4l2->dev_name));
    v4l2->dev_name[sizeof(v4l2->dev_name) - 1] = '\0';
    v4l2->buf_count = (cfg->buf_count > MAX_BUFS ? MAX_BUFS : cfg->buf_count);
    v4l2->fd = -1;
    return &v4l2->base;
}

#endif  // CONFIG_IDF_TARGET_ESP32P4
