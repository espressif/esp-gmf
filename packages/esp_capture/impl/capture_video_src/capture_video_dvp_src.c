/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_capture_defaults.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "DVP_SRC"

typedef struct {
    esp_capture_video_src_if_t       base;
    esp_capture_video_dvp_src_cfg_t  cfg;
    esp_capture_video_info_t         vid_info;
    bool                             need_convert_420;
    camera_fb_t                     *pic[2];
    uint8_t                         *yuv420_cache;
    SemaphoreHandle_t                yuv420_lock;
    uint8_t                          cur_pic;
    uint8_t                          dvp_inited     : 1;
    uint8_t                          started        : 1;
    uint8_t                          use_fixed_caps : 1;
} dvp_src_t;

static esp_capture_err_t dvp_src_open(esp_capture_video_src_if_t *src)
{
    return ESP_CAPTURE_ERR_OK;
}

static framesize_t get_video_quality(int width, int height)
{
    // TODO add other resolution support
    if (width == 320 && height == 240) {
        return FRAMESIZE_QVGA;
    }
    if (width == 480 && height == 320) {
        return FRAMESIZE_HVGA;
    }
    if (width == 640 && height == 480) {
        return FRAMESIZE_VGA;
    }
    if (width == 1024 && height == 768) {
        return FRAMESIZE_XGA;
    }
    if (width == 1280 && height == 720) {
        return FRAMESIZE_HD;
    }
    if (width == 1920 && height == 1080) {
        return FRAMESIZE_FHD;
    }
    return FRAMESIZE_QVGA;
}

static esp_capture_err_t dvp_src_get_support_codecs(esp_capture_video_src_if_t *src, const esp_capture_format_id_t **codecs, uint8_t *num)
{
    static esp_capture_format_id_t dvp_codecs[] = {
        ESP_CAPTURE_FMT_ID_MJPEG,
        ESP_CAPTURE_FMT_ID_YUV422,
        ESP_CAPTURE_FMT_ID_YUV420,
        ESP_CAPTURE_FMT_ID_RGB565_BE,
    };
    *codecs = dvp_codecs;
    *num = sizeof(dvp_codecs) / sizeof(dvp_codecs[0]);
    return ESP_CAPTURE_ERR_OK;
}

static bool dvp_src_codec_supported(esp_capture_video_src_if_t *src, esp_capture_format_id_t codec)
{
    uint8_t n = 0;
    const esp_capture_format_id_t *codecs;
    dvp_src_get_support_codecs(src, &codecs, &n);
    for (uint8_t i = 0; i < n; i++) {
        if (codecs[i] == codec) {
            return true;
        }
    }
    return false;
}

static esp_capture_err_t dvp_src_init_camera(dvp_src_t *dvp_src, esp_capture_video_info_t *vid_info)
{
    if (dvp_src->dvp_inited) {
        if (vid_info->format_id == dvp_src->vid_info.format_id && vid_info->width == dvp_src->vid_info.width && vid_info->height == dvp_src->vid_info.height) {
            return ESP_CAPTURE_ERR_OK;
        }
        esp_camera_deinit();
        dvp_src->dvp_inited = false;
    }
    camera_config_t camera_config = {
        .pin_pwdn = dvp_src->cfg.pwr_pin,
        .pin_reset = dvp_src->cfg.reset_pin,
        .pin_xclk = dvp_src->cfg.xclk_pin,
        .pin_sccb_sda = -1,
        .pin_sccb_scl = -1,
        .pin_d7 = dvp_src->cfg.data[7],
        .pin_d6 = dvp_src->cfg.data[6],
        .pin_d5 = dvp_src->cfg.data[5],
        .pin_d4 = dvp_src->cfg.data[4],
        .pin_d3 = dvp_src->cfg.data[3],
        .pin_d2 = dvp_src->cfg.data[2],
        .pin_d1 = dvp_src->cfg.data[1],
        .pin_d0 = dvp_src->cfg.data[0],
        .pin_vsync = dvp_src->cfg.vsync_pin,
        .pin_href = dvp_src->cfg.href_pin,
        .pin_pclk = dvp_src->cfg.pclk_pin,
        .xclk_freq_hz = dvp_src->cfg.xclk_freq,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .jpeg_quality = 12,  // 0-63 lower number means higher quality
        .fb_count = dvp_src->cfg.buf_count,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
        .sccb_i2c_port = dvp_src->cfg.i2c_port,
    };
    dvp_src->need_convert_420 = false;
    if (camera_config.xclk_freq_hz == 0) {
        camera_config.xclk_freq_hz = 20000000;
    }
    if (vid_info->format_id == ESP_CAPTURE_FMT_ID_MJPEG) {
        camera_config.pixel_format = PIXFORMAT_JPEG;
    } else if (vid_info->format_id == ESP_CAPTURE_FMT_ID_RGB565_BE) {
        camera_config.pixel_format = PIXFORMAT_RGB565;
    } else if (vid_info->format_id == ESP_CAPTURE_FMT_ID_YUV422 || vid_info->format_id == ESP_CAPTURE_FMT_ID_YUV420) {
        camera_config.pixel_format = PIXFORMAT_YUV422;
        if (vid_info->format_id == ESP_CAPTURE_FMT_ID_YUV420) {
            dvp_src->need_convert_420 = true;
        }
    } else {
        ESP_LOGE(TAG, "Format not supported %x", vid_info->format_id);
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    camera_config.frame_size = get_video_quality(vid_info->width, vid_info->height);
    esp_err_t err = esp_camera_init(&camera_config);
    if (err == ESP_OK) {
        if (vid_info != &dvp_src->vid_info) {
            dvp_src->vid_info = *vid_info;
        }
        dvp_src->dvp_inited = true;
        return ESP_CAPTURE_ERR_OK;
    }
    ESP_LOGE(TAG, "Failed to init camera error 0x%x", err);
    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
}

static esp_capture_err_t dvp_src_set_fixed_caps(esp_capture_video_src_if_t *src, const esp_capture_video_info_t *fixed_caps)
{
    if (src == NULL || fixed_caps == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    dvp_src_t *dvp_src = (dvp_src_t *)src;
    if (dvp_src->started) {
        return ESP_CAPTURE_ERR_INVALID_STATE;
    }
    dvp_src->use_fixed_caps = (fixed_caps->format_id != ESP_CAPTURE_FMT_ID_NONE);
    if (dvp_src->use_fixed_caps) {
        int ret = dvp_src_init_camera(dvp_src, (esp_capture_video_info_t *)fixed_caps);
        // Clear fixed caps flag if failed
        if (ret != ESP_CAPTURE_ERR_OK) {
            dvp_src->use_fixed_caps = false;
        }
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t dvp_src_negotiate_caps(esp_capture_video_src_if_t *src, esp_capture_video_info_t *in_cap, esp_capture_video_info_t *out_caps)
{
    dvp_src_t *dvp_src = (dvp_src_t *)src;
    // Process fixed capability
    if (dvp_src->use_fixed_caps) {
        if (in_cap->format_id == ESP_CAPTURE_FMT_ID_NONE || in_cap->format_id == ESP_CAPTURE_FMT_ID_ANY) {
            *out_caps = dvp_src->vid_info;
            return ESP_CAPTURE_ERR_OK;
        }
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    // Process negotiate with any format
    if (in_cap->format_id == ESP_CAPTURE_FMT_ID_ANY) {
        // TODO here just return for yuv422
        *out_caps = *in_cap;
        out_caps->format_id = ESP_CAPTURE_FMT_ID_YUV422;
        return ESP_CAPTURE_ERR_OK;
    }
    if (dvp_src_codec_supported(src, in_cap->format_id) == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    *out_caps = *in_cap;
    dvp_src->vid_info = *in_cap;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t dvp_src_start(esp_capture_video_src_if_t *src)
{
    dvp_src_t *dvp_src = (dvp_src_t *)src;
    if (dvp_src->dvp_inited == false) {
        int ret = dvp_src_init_camera(dvp_src, &dvp_src->vid_info);
        if (ret != ESP_CAPTURE_ERR_OK) {
            return ret;
        }
    }
    if (dvp_src->need_convert_420) {
        dvp_src->yuv420_cache = malloc(dvp_src->vid_info.width * dvp_src->vid_info.height * 3 / 2);
        if (dvp_src->yuv420_cache == NULL) {
            return ESP_CAPTURE_ERR_NO_MEM;
        }
        dvp_src->yuv420_lock = xSemaphoreCreateCounting(1, 1);
        if (dvp_src->yuv420_lock == NULL) {
            return ESP_CAPTURE_ERR_NO_MEM;
        }
    }
    dvp_src->started = true;
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

static esp_capture_err_t dvp_src_acquire_frame(esp_capture_video_src_if_t *src, esp_capture_stream_frame_t *frame)
{
    dvp_src_t *dvp_src = (dvp_src_t *)src;
    if (dvp_src->started == false) {
        return ESP_CAPTURE_ERR_INVALID_STATE;
    }
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL) {
        ESP_LOGE(TAG, "Camera capture failed");
        return ESP_CAPTURE_ERR_NO_MEM;
    }
    for (int i = 0; i < dvp_src->cfg.buf_count; i++) {
        if (dvp_src->pic[i] == NULL) {
            dvp_src->pic[i] = fb;
            if (dvp_src->need_convert_420) {
                xSemaphoreTake(dvp_src->yuv420_lock, portMAX_DELAY);
                dvp_src->cur_pic = i;
                convert_yuv420(dvp_src->vid_info.width, dvp_src->vid_info.height,
                               dvp_src->pic[i]->buf, dvp_src->yuv420_cache);
                frame->data = dvp_src->yuv420_cache;
                frame->size = dvp_src->pic[i]->len * 3 / 4;
            } else {
                frame->data = dvp_src->pic[i]->buf;
                frame->size = dvp_src->pic[i]->len;
            }
            return ESP_CAPTURE_ERR_OK;
        }
    }
    // User not consumed, should not happen
    esp_camera_fb_return(fb);
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t dvp_src_release_frame(esp_capture_video_src_if_t *src, esp_capture_stream_frame_t *frame)
{
    dvp_src_t *dvp_src = (dvp_src_t *)src;
    if (dvp_src->started == false) {
        return ESP_CAPTURE_ERR_INVALID_STATE;
    }
    int sel = -1;
    if (dvp_src->need_convert_420) {
        sel = dvp_src->cur_pic;
    } else {
        for (int i = 0; i < dvp_src->cfg.buf_count; i++) {
            if (dvp_src->pic[i] && frame->data == dvp_src->pic[i]->buf) {
                sel = i;
                break;
            }
        }
    }
    if (sel >= 0 && dvp_src->pic[sel]) {
        if (dvp_src->need_convert_420) {
            xSemaphoreGive(dvp_src->yuv420_lock);
        }
        esp_camera_fb_return(dvp_src->pic[sel]);
        dvp_src->pic[sel] = NULL;
        return ESP_CAPTURE_ERR_OK;
    }
    return ESP_CAPTURE_ERR_NOT_FOUND;
}

static esp_capture_err_t dvp_src_stop(esp_capture_video_src_if_t *src)
{
    dvp_src_t *dvp_src = (dvp_src_t *)src;
    if (dvp_src->dvp_inited) {
        dvp_src->pic[0] = dvp_src->pic[1] = NULL;
        if (dvp_src->yuv420_lock) {
            vSemaphoreDelete(dvp_src->yuv420_lock);
            dvp_src->yuv420_lock = NULL;
        }
        if (dvp_src->use_fixed_caps == false) {
            esp_camera_deinit();
            dvp_src->dvp_inited = false;
        }
    }
    if (dvp_src->yuv420_cache) {
        free(dvp_src->yuv420_cache);
        dvp_src->yuv420_cache = NULL;
    }
    dvp_src->started = false;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t dvp_src_close(esp_capture_video_src_if_t *src)
{
    dvp_src_t *dvp_src = (dvp_src_t *)src;
    dvp_src->use_fixed_caps = false;
    return dvp_src_stop(src);
}

esp_capture_video_src_if_t *esp_capture_new_video_dvp_src(esp_capture_video_dvp_src_cfg_t *cfg)
{
    dvp_src_t *dvp = calloc(1, sizeof(dvp_src_t));
    if (dvp == NULL) {
        return NULL;
    }
    dvp->base.open = dvp_src_open;
    dvp->base.get_support_codecs = dvp_src_get_support_codecs;
    dvp->base.set_fixed_caps = dvp_src_set_fixed_caps;
    dvp->base.negotiate_caps = dvp_src_negotiate_caps;
    dvp->base.start = dvp_src_start;
    dvp->base.acquire_frame = dvp_src_acquire_frame;
    dvp->base.release_frame = dvp_src_release_frame;
    dvp->base.stop = dvp_src_stop;
    dvp->base.close = dvp_src_close;
    dvp->cfg = *cfg;
    if (cfg->buf_count == 0) {
        dvp->cfg.buf_count = 1;
    }
    return &dvp->base;
}

#endif  /* CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3 */
