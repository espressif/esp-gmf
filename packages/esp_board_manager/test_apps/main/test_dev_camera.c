/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_board_device.h"
#include "dev_camera.h"
#include "test_dev_camera.h"
#include "esp_jpeg_enc.h"

#define BUFFER_COUNT     2
#define CAPTURE_SECONDS  3
#define MEMORY_TYPE      V4L2_MEMORY_MMAP

static const char *TAG = "TEST_DEV_CAMERA";

static esp_err_t camera_capture_stream_by_format(int fd, int type, uint32_t v4l2_format, uint32_t width, uint32_t height)
{
    uint8_t *buffer[BUFFER_COUNT];
    uint32_t frame_size;
    uint32_t frame_count;
    struct v4l2_buffer buf;
    struct v4l2_requestbuffers req;
    struct v4l2_format format = {
        .type = type,
        .fmt.pix.width = width,
        .fmt.pix.height = height,
        .fmt.pix.pixelformat = v4l2_format,
    };

    if (ioctl(fd, VIDIOC_S_FMT, &format) != 0) {
        return ESP_FAIL;
    }

    memset(&req, 0, sizeof(req));
    req.count = BUFFER_COUNT;
    req.type = type;
    req.memory = MEMORY_TYPE;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(TAG, "failed to require buffer");
        return ESP_FAIL;
    }

    for (int i = 0; i < BUFFER_COUNT; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = type;
        buf.memory = MEMORY_TYPE;
        buf.index = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGE(TAG, "failed to query buffer");
            return ESP_FAIL;
        }

        buffer[i] = (uint8_t *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                    MAP_SHARED, fd, buf.m.offset);

        if (!buffer[i]) {
            ESP_LOGE(TAG, "failed to map buffer");
            return ESP_FAIL;
        }

        if (ioctl(fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "failed to queue video frame");
            return ESP_FAIL;
        }
    }

    if (ioctl(fd, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGE(TAG, "failed to start stream");
        return ESP_FAIL;
    }

    frame_count = 0;
    frame_size = 0;
    int64_t start_time_us = esp_timer_get_time();
    while (esp_timer_get_time() - start_time_us < (CAPTURE_SECONDS * 1000 * 1000)) {
        memset(&buf, 0, sizeof(buf));
        buf.type = type;
        buf.memory = MEMORY_TYPE;
        if (ioctl(fd, VIDIOC_DQBUF, &buf) != 0) {
            ESP_LOGE(TAG, "failed to receive video frame");
            return ESP_FAIL;
        }
        /**
         * If no error, the flags has V4L2_BUF_FLAG_DONE. If error, the flags has V4L2_BUF_FLAG_ERROR.
         * We need to skip these frames, but we also need queue the buffer.
         */
        if (buf.flags & V4L2_BUF_FLAG_DONE) {
            frame_size += buf.bytesused;
            frame_count++;
        }

        if (ioctl(fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "failed to queue video frame");
            return ESP_FAIL;
        }
    }

    if (ioctl(fd, VIDIOC_STREAMOFF, &type) != 0) {
        ESP_LOGE(TAG, "failed to stop stream");
        return ESP_FAIL;
    }

    if (frame_count > 0) {
        ESP_LOGI(TAG, "\twidth:  %" PRIu32, format.fmt.pix.width);
        ESP_LOGI(TAG, "\theight: %" PRIu32, format.fmt.pix.height);
        ESP_LOGI(TAG, "\tsize:   %" PRIu32, frame_size / frame_count);
        ESP_LOGI(TAG, "\tFPS:    %" PRIu32, frame_count / CAPTURE_SECONDS);
    } else {
        ESP_LOGW(TAG, "No frame captured");
    }

#ifdef CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SUPPORT
    ESP_LOGI(TAG, "Save JPEG file to sd card...");
    jpeg_enc_config_t jpeg_enc_cfg = DEFAULT_JPEG_ENC_CONFIG();
    jpeg_enc_cfg.width = format.fmt.pix.width;
    jpeg_enc_cfg.height = format.fmt.pix.height;
    int input_src_size = 0;
    switch (format.fmt.pix.pixelformat) {
        case V4L2_PIX_FMT_SBGGR8:
        case V4L2_PIX_FMT_GREY:
            jpeg_enc_cfg.src_type = JPEG_PIXEL_FORMAT_GRAY;
            jpeg_enc_cfg.subsampling = JPEG_SUBSAMPLE_GRAY;
            input_src_size = format.fmt.pix.width * format.fmt.pix.height;
            break;
        case V4L2_PIX_FMT_YUV422P:
            jpeg_enc_cfg.src_type = JPEG_PIXEL_FORMAT_YCbYCr;
            jpeg_enc_cfg.subsampling = JPEG_SUBSAMPLE_422;
            input_src_size = format.fmt.pix.width * format.fmt.pix.height * 2;
            break;
        default:
            ESP_LOGE(TAG, "Unsupported format");
            return ESP_ERR_NOT_SUPPORTED;
    }

    jpeg_enc_handle_t jpeg_enc = NULL;

    if (jpeg_enc_open(&jpeg_enc_cfg, &jpeg_enc) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Jpeg encoder open failed");
        return ESP_FAIL;
    }

    int outbuf_size = 100 * 1024;
    int out_len = 0;
    uint8_t *outbuf = NULL;
    outbuf = (uint8_t *)calloc(1, outbuf_size);
    if (outbuf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate output buffer");
        goto jpeg_enc_exit;
    }

    if (jpeg_enc_process(jpeg_enc, buffer[buf.index], input_src_size, outbuf, outbuf_size, &out_len) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Jpeg encoder process failed");
        goto jpeg_enc_exit;
    }

    FILE *out = NULL;
    out = fopen("/sdcard/esp_jpeg_encode_one_picture.jpg", "wb");
    if (out == NULL) {
        ESP_LOGE(TAG, "Failed to open output file");
        goto jpeg_enc_exit;
    }
    fwrite(outbuf, 1, out_len, out);
    fclose(out);

jpeg_enc_exit:
    jpeg_enc_close(jpeg_enc);
    if (outbuf) {
        free(outbuf);
    }
#endif  // CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SUPPORT

    return ESP_OK;
}

static void camera_capability_print(const struct v4l2_capability *cap)
{
    struct v4l2_capability capability = *cap;
    ESP_LOGI(TAG, "version: %d.%d.%d", (uint16_t)(capability.version >> 16),
             (uint8_t)(capability.version >> 8),
             (uint8_t)capability.version);
    ESP_LOGI(TAG, "driver:  %s", capability.driver);
    ESP_LOGI(TAG, "card:    %s", capability.card);
    ESP_LOGI(TAG, "bus:     %s", capability.bus_info);
    ESP_LOGI(TAG, "capabilities:");
    if (capability.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
        ESP_LOGI(TAG, "\tVIDEO_CAPTURE");
    }
    if (capability.capabilities & V4L2_CAP_READWRITE) {
        ESP_LOGI(TAG, "\tREADWRITE");
    }
    if (capability.capabilities & V4L2_CAP_ASYNCIO) {
        ESP_LOGI(TAG, "\tASYNCIO");
    }
    if (capability.capabilities & V4L2_CAP_STREAMING) {
        ESP_LOGI(TAG, "\tSTREAMING");
    }
    if (capability.capabilities & V4L2_CAP_META_OUTPUT) {
        ESP_LOGI(TAG, "\tMETA_OUTPUT");
    }
    if (capability.capabilities & V4L2_CAP_TIMEPERFRAME) {
        ESP_LOGI(TAG, "\tTIMEPERFRAME");
    }
    if (capability.capabilities & V4L2_CAP_DEVICE_CAPS) {
        ESP_LOGI(TAG, "device capabilities:");
        if (capability.device_caps & V4L2_CAP_VIDEO_CAPTURE) {
            ESP_LOGI(TAG, "\tVIDEO_CAPTURE");
        }
        if (capability.device_caps & V4L2_CAP_READWRITE) {
            ESP_LOGI(TAG, "\tREADWRITE");
        }
        if (capability.device_caps & V4L2_CAP_ASYNCIO) {
            ESP_LOGI(TAG, "\tASYNCIO");
        }
        if (capability.device_caps & V4L2_CAP_STREAMING) {
            ESP_LOGI(TAG, "\tSTREAMING");
        }
        if (capability.device_caps & V4L2_CAP_META_OUTPUT) {
            ESP_LOGI(TAG, "\tMETA_OUTPUT");
        }
        if (capability.device_caps & V4L2_CAP_TIMEPERFRAME) {
            ESP_LOGI(TAG, "\tTIMEPERFRAME");
        }
    }
}

static esp_err_t camera_capture_stream(const char *cam_dev_path)
{
    int fd;
    esp_err_t ret;
    struct v4l2_capability capability;
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    fd = open(cam_dev_path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "failed to open device");
        return ESP_FAIL;
    }

    if (ioctl(fd, VIDIOC_QUERYCAP, &capability)) {
        ESP_LOGE(TAG, "failed to get capability");
        ret = ESP_FAIL;
        goto exit_0;
    }

    camera_capability_print(&capability);

    for (int fmt_index = 0;; fmt_index++) {
        struct v4l2_fmtdesc fmtdesc = {
            .index = fmt_index,
            .type = type,
        };

        // Trying to get all supported formats
        if (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) != 0) {
            break;
        }

        struct v4l2_format format = {
            .type = type,
        };
        struct v4l2_streamparm sparm = {
            .type = type,
            .parm.capture.capability = V4L2_CAP_TIMEPERFRAME,
        };
        struct v4l2_captureparm *cparam = &sparm.parm.capture;

        if (ioctl(fd, VIDIOC_G_PARM, &sparm) != 0) {
            ESP_LOGE(TAG, "failed to get stream parameter");
            ret = ESP_FAIL;
            goto exit_0;
        }

        if (ioctl(fd, VIDIOC_G_FMT, &format) != 0) {
            ESP_LOGE(TAG, "failed to get format");
            ret = ESP_FAIL;
            goto exit_0;
        }

        ESP_LOGI(TAG, "Capture format: %s, frame size: %" PRIu32 "x%" PRIu32 ", FPS: %0.1f, for %d seconds:",
                 (char *)fmtdesc.description, format.fmt.pix.width, format.fmt.pix.height,
                 (double)cparam->timeperframe.denominator / (double)cparam->timeperframe.numerator,
                 CAPTURE_SECONDS);

        if (camera_capture_stream_by_format(fd, type, fmtdesc.pixelformat, format.fmt.pix.width,
                                            format.fmt.pix.height) != ESP_OK) {
            break;
        }
    }

    ret = ESP_OK;

exit_0:
    close(fd);
    return ret;
}

esp_err_t test_dev_camera()
{
    dev_camera_handle_t *camera_handle = NULL;
    esp_err_t ret = esp_board_device_get_handle("camera_sensor", (void **)&camera_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get camera device");
        return ret;
    }

    ret = camera_capture_stream(camera_handle->dev_path);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to capture stream");
        return ret;
    }

    return ESP_OK;
}
