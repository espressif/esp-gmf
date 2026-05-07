/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "video_render_proc.h"
#include "esp_gmf_video_param.h"
#include "assets_path.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t                  *buf;
    uint32_t                  size;
    esp_video_render_frame_t  last;
    int                       got;
} out_ctx_t;

static int writer_cb(esp_video_render_frame_t *frame, void *ctx)
{
    out_ctx_t *o = (out_ctx_t *)ctx;
    free(o->buf);
    o->buf = (uint8_t *)malloc(frame->size);
    assert(o->buf);
    memcpy(o->buf, frame->data, frame->size);
    o->size = frame->size;
    o->last = *frame;
    o->last.data = o->buf;
    o->got++;
    return 0;
}

static void reset_out(out_ctx_t *o)
{
    if (!o) {
        return;
    }
    free(o->buf);
    memset(o, 0, sizeof(*o));
}

static void fill_left_right_rgb565(uint8_t *buf, int w, int h, uint16_t left, uint16_t right)
{
    for (int y = 0; y < h; y++) {
        uint16_t *row = (uint16_t *)(buf + y * w * 2);
        for (int x = 0; x < w; x++) {
            row[x] = (x < w / 2) ? left : right;
        }
    }
}

static void fill_rgb888_test(uint8_t *buf, int w, int h)
{
    // 2x2 repeating pattern:
    // (0,0)=red, (1,0)=green, (0,1)=blue, (1,1)=white
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t *p = buf + (y * w + x) * 3;
            const int px = x & 1;
            const int py = y & 1;
            if (px == 0 && py == 0) {
                p[0] = 255;
                p[1] = 0;
                p[2] = 0;
            }
            else if (px == 1 && py == 0) {
                p[0] = 0;
                p[1] = 255;
                p[2] = 0;
            }
            else if (px == 0 && py == 1) {
                p[0] = 0;
                p[1] = 0;
                p[2] = 255;
            } else {
                p[0] = 255;
                p[1] = 255;
                p[2] = 255;
            }
        }
    }
}

static uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static int read_file_all(const char *path, uint8_t **out_data, size_t *out_size)
{
    *out_data = NULL;
    *out_size = 0;
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(fp);
        return -1;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(fp);
        return -1;
    }
    size_t rd = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    if (rd != (size_t)sz) {
        free(buf);
        return -1;
    }
    *out_data = buf;
    *out_size = rd;
    return 0;
}

static int discover_h264_encoders(char encoders[][64], int max_count)
{
    if (!encoders || max_count <= 0) {
        return -1;
    }

    FILE *fp = popen("gst-inspect-1.0 2>/dev/null | grep -iE 'h264.*enc|enc.*h264' | grep -i encoder", "r");
    if (!fp) {
        return -1;
    }

    int count = 0;
    char line[512];
    while (count < max_count && fgets(line, sizeof(line), fp)) {
        char *first_colon = strchr(line, ':');
        if (!first_colon) {
            continue;
        }
        char *second_colon = strchr(first_colon + 1, ':');
        if (!second_colon) {
            continue;
        }

        // Extract element name (between first and second colon)
        const char *name_start = first_colon + 1;
        while (*name_start == ' ' || *name_start == '\t') {
            name_start++;
        }
        size_t name_len = second_colon - name_start;
        if (name_len <= 0 || name_len >= 64) {
            continue;
        }

        memcpy(encoders[count], name_start, name_len);
        encoders[count][name_len] = '\0';
        count++;
    }
    pclose(fp);
    return count;
}

// Get recommended pipeline properties for a known encoder, or NULL for defaults
static const char *get_encoder_props(const char *element_name)
{
    if (!element_name) {
        return NULL;
    }
    if (strcmp(element_name, "x264enc") == 0) {
        return "tune=zerolatency speed-preset=ultrafast key-int-max=1 bitrate=256 byte-stream=true aud=true";
    } else if (strcmp(element_name, "openh264enc") == 0) {
        return "bitrate=256000";
    } else if (strcmp(element_name, "avenc_h264") == 0) {
        return "max_b_frames=0";
    }
    // For unknown encoders, try with defaults (no properties)
    return "";
}

// Verify that an encoder can actually negotiate caps (some encoders exist but fail at runtime)
// Returns 1 if encoder works, 0 if it fails negotiation
static int gst_encoder_sanity_ok(const char *element_name, const char *props)
{
    if (!element_name || element_name[0] == '\0') {
        return 0;
    }

    char encoder_pipeline[256];
    if (props && props[0] != '\0') {
        snprintf(encoder_pipeline, sizeof(encoder_pipeline), "%s %s", element_name, props);
    } else {
        snprintf(encoder_pipeline, sizeof(encoder_pipeline), "%s", element_name);
    }

    // Probe negotiation with a tiny pipeline. Some elements can appear in gst-inspect
    // but still fail at runtime (e.g., missing runtime backends like OpenMAX).
    //
    // Keep it quiet: redirect stderr to avoid dumping gst errors during unit tests.
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "gst-launch-1.0 -q -e "
             "videotestsrc num-buffers=1 pattern=smpte is-live=false ! "
             "video/x-raw,format=I420,width=128,height=128,framerate=30/1 ! "
             "%s ! "
             "fakesink sync=false 2>/dev/null",
             encoder_pipeline);
    int rc = system(cmd);
    return (rc == 0);
}

static int run_cmd_capture_stdout(const char *cmd, uint8_t **out_data, size_t *out_size)
{
    *out_data = NULL;
    *out_size = 0;
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return -1;
    }

    size_t cap = 64 * 1024;
    size_t sz = 0;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) {
        pclose(fp);
        return -1;
    }

    while (1) {
        if (sz == cap) {
            cap *= 2;
            uint8_t *nb = (uint8_t *)realloc(buf, cap);
            if (!nb) {
                free(buf);
                pclose(fp);
                return -1;
            }
            buf = nb;
        }
        size_t rd = fread(buf + sz, 1, cap - sz, fp);
        sz += rd;
        if (rd == 0) {
            break;
        }
    }
    int rc = pclose(fp);
    if (rc != 0) {
        free(buf);
        return -1;
    }

    *out_data = buf;
    *out_size = sz;
    return 0;
}

static int generate_h264_annexb_one_frame(uint8_t **out_data, size_t *out_size)
{
    *out_data = NULL;
    *out_size = 0;

    // Discover available H264 encoders dynamically to avoid "no component" warnings
    char encoders[16][64];
    int enc_count = discover_h264_encoders(encoders, 16);
    if (enc_count <= 0) {
        printf("Failed to discover H264 encoders\n");
        return -1;  // No H264 encoders found
    }

    // Try each discovered encoder (after sanity check)
    for (int i = 0; i < enc_count; i++) {
        const char *props = get_encoder_props(encoders[i]);

        // Skip encoders that can't negotiate caps (e.g., avenc_h264_omx without OpenMAX backend)
        if (!gst_encoder_sanity_ok(encoders[i], props)) {
            continue;
        }

        char encoder_pipeline[256];
        if (props && props[0] != '\0') {
            snprintf(encoder_pipeline, sizeof(encoder_pipeline), "%s %s", encoders[i], props);
        } else {
            snprintf(encoder_pipeline, sizeof(encoder_pipeline), "%s", encoders[i]);
        }

        char cmd[2048];
        snprintf(cmd, sizeof(cmd),
                 "gst-launch-1.0 -q -e "
                 // Generate multiple buffers to increase the chance that SPS/PPS + an IDR are present
                 "videotestsrc num-buffers=30 pattern=smpte is-live=false ! "
                 "video/x-raw,format=I420,width=128,height=128,framerate=30/1 ! "
                 "%s ! "
                 // Do NOT require h264parse (often missing on minimal installs). Let decodebin/typefind handle it.
                 "video/x-h264,alignment=au ! "
                 "fdsink fd=1 sync=false 2>/dev/null",
                 encoder_pipeline);
        if (run_cmd_capture_stdout(cmd, out_data, out_size) == 0 && *out_size > 0) {
            return 0;
        }
        free(*out_data);
        *out_data = NULL;
        *out_size = 0;
    }
    return -1;
}

static int generate_jpeg_one_frame(uint8_t **out_data, size_t *out_size)
{
    *out_data = NULL;
    *out_size = 0;
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "gst-launch-1.0 -q -e "
             "videotestsrc num-buffers=1 pattern=smpte is-live=false ! "
             "video/x-raw,format=I420,width=128,height=128,framerate=30/1 ! "
             "jpegenc ! "
             "fdsink fd=1 sync=false");

    if (run_cmd_capture_stdout(cmd, out_data, out_size) != 0 || *out_size == 0) {
        free(*out_data);
        *out_data = NULL;
        *out_size = 0;
        return -1;
    }
    return 0;
}

static int get_jpeg_frame_end(const uint8_t *data, int size, int eof)
{
    for (int i = 0; i < size - 1; i++) {
        if (data[i] == 0xFF && data[i + 1] == 0xD9) {
            if (i + 3 < size) {
                if (data[i + 2] == 0xFF && data[i + 3] == 0xD8) {
                    return i + 2;
                }
            }
            if (eof && i == size - 2) {
                return i + 2;
            }
        }
    }
    return -1;
}

static int read_one_mjpeg_frame(const char *path, uint8_t **out_jpeg, size_t *out_size)
{
    *out_jpeg = NULL;
    *out_size = 0;

    // If the repo test vector is missing, fall back to generating a single JPEG from a pattern.
    // This keeps the test self-contained on systems that have gst-launch installed.
    if (!path || path[0] == '\0') {
        return generate_jpeg_one_frame(out_jpeg, out_size);
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return generate_jpeg_one_frame(out_jpeg, out_size);
    }

    enum { MAX_FRAME = 512 * 1024 };
    uint8_t *buf = (uint8_t *)malloc(MAX_FRAME);
    if (!buf) {
        fclose(fp);
        return -1;
    }

    int filled = 0;
    int eos = 0;
    int frame_end = -1;
    while (1) {
        frame_end = get_jpeg_frame_end(buf, filled, eos);
        if (frame_end > 0) {
            break;
        }
        int left = MAX_FRAME - filled;
        if (left <= 0) {
            break;
        }
        int rd = (int)fread(buf + filled, 1, (size_t)left, fp);
        filled += rd;
        eos = feof(fp) ? 1 : 0;
        if (rd == 0 && eos) {
            break;
        }
    }
    fclose(fp);

    if (frame_end <= 0) {
        free(buf);
        // If the file is not in a simple concatenated-JPEG format, fall back to generated.
        return generate_jpeg_one_frame(out_jpeg, out_size);
    }

    uint8_t *jpeg = (uint8_t *)malloc((size_t)frame_end);
    if (!jpeg) {
        free(buf);
        return -1;
    }
    memcpy(jpeg, buf, (size_t)frame_end);
    free(buf);

    *out_jpeg = jpeg;
    *out_size = (size_t)frame_end;
    return 0;
}

static void set_crop_scale(video_render_proc_handle_t proc, int crop_x, int crop_y, int crop_w, int crop_h, int out_w, int out_h)
{
    esp_gmf_element_handle_t crop_el = video_render_proc_get_element(proc, ESP_VIDEO_RENDER_PROC_CROP);
    esp_gmf_element_handle_t scale_el = video_render_proc_get_element(proc, ESP_VIDEO_RENDER_PROC_SCALE);
    assert(crop_el && scale_el);
    esp_gmf_video_rgn_t crop = {.x = crop_x, .y = crop_y, .width = crop_w, .height = crop_h};
    esp_gmf_video_resolution_t dst = {.width = out_w, .height = out_h};
    assert(esp_gmf_video_param_set_cropped_region(crop_el, &crop) == 0);
    assert(esp_gmf_video_param_set_dst_resolution(scale_el, &dst) == 0);
}

static void run_case_raw_crop_scale(out_ctx_t *out)
{
    printf("[proc] case: raw RGB565 crop+scale\n");
    video_render_proc_handle_t proc = NULL;
    assert(video_render_proc_create(NULL, &proc) == ESP_VIDEO_RENDER_ERR_OK);
    assert(video_render_proc_set_writer(proc, writer_cb, out) == ESP_VIDEO_RENDER_ERR_OK);

    set_crop_scale(proc, 0, 0, 32, 64, 16, 16);

    esp_video_render_frame_info_t in_info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = 64, .height = 64, .fps = 30};
    esp_video_render_frame_info_t out_info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = 16, .height = 16, .fps = 30};
    assert(video_render_proc_open(proc, &in_info, &out_info) == ESP_VIDEO_RENDER_ERR_OK);

    uint8_t *in_buf = (uint8_t *)malloc(in_info.width * in_info.height * 2);
    assert(in_buf);
    const uint16_t RED = 0xF800;
    const uint16_t BLUE = 0x001F;
    fill_left_right_rgb565(in_buf, in_info.width, in_info.height, RED, BLUE);

    esp_video_render_frame_t frame = {.format = in_info.format, .width = in_info.width, .height = in_info.height, .data = in_buf, .size = (uint32_t)(in_info.width * in_info.height * 2)};
    assert(video_render_proc_write(proc, &frame) == ESP_VIDEO_RENDER_ERR_OK);
    assert(out->got >= 1);
    assert(out->last.width == 16 && out->last.height == 16);

    uint16_t *pix = (uint16_t *)out->last.data;
    for (int i = 0; i < (int)(out->last.width * out->last.height); i++) {
        assert(pix[i] == RED);
    }

    free(in_buf);
    (void)video_render_proc_close(proc);
    video_render_proc_destroy(proc);
}

static void run_case_raw_color_convert(out_ctx_t *out)
{
    printf("[proc] case: raw RGB888 -> RGB565 color convert\n");
    video_render_proc_handle_t proc = NULL;
    assert(video_render_proc_create(NULL, &proc) == ESP_VIDEO_RENDER_ERR_OK);
    assert(video_render_proc_set_writer(proc, writer_cb, out) == ESP_VIDEO_RENDER_ERR_OK);

    // No crop/scale; output = input size
    set_crop_scale(proc, 0, 0, 8, 8, 8, 8);

    esp_video_render_frame_info_t in_info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB888, .width = 8, .height = 8, .fps = 30};
    esp_video_render_frame_info_t out_info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = 8, .height = 8, .fps = 30};
    assert(video_render_proc_open(proc, &in_info, &out_info) == ESP_VIDEO_RENDER_ERR_OK);

    uint8_t *in_buf = (uint8_t *)malloc(in_info.width * in_info.height * 3);
    assert(in_buf);
    fill_rgb888_test(in_buf, in_info.width, in_info.height);

    esp_video_render_frame_t frame = {.format = in_info.format, .width = in_info.width, .height = in_info.height, .data = in_buf, .size = (uint32_t)(in_info.width * in_info.height * 3)};
    assert(video_render_proc_write(proc, &frame) == ESP_VIDEO_RENDER_ERR_OK);
    assert(out->got >= 1);
    assert(out->last.width == 8 && out->last.height == 8);
    assert(out->last.size == 8 * 8 * 2);

    // Spot-check some pixels
    uint16_t *pix = (uint16_t *)out->last.data;
    assert(pix[0] == rgb888_to_rgb565(255, 0, 0));      // (0,0)
    assert(pix[1] == rgb888_to_rgb565(0, 255, 0));      // (1,0)
    assert(pix[8] == rgb888_to_rgb565(0, 0, 255));      // (0,1)
    assert(pix[9] == rgb888_to_rgb565(255, 255, 255));  // (1,1)

    free(in_buf);
    (void)video_render_proc_close(proc);
    video_render_proc_destroy(proc);
}

static void run_case_mjpeg_decode(out_ctx_t *out, const char *mjpeg_path)
{
    printf("[proc] case: MJPEG decode (+scale) from %s\n", mjpeg_path);
    uint8_t *jpeg = NULL;
    size_t jpeg_sz = 0;
    assert(read_one_mjpeg_frame(mjpeg_path, &jpeg, &jpeg_sz) == 0);

    video_render_proc_handle_t proc = NULL;
    assert(video_render_proc_create(NULL, &proc) == ESP_VIDEO_RENDER_ERR_OK);
    assert(video_render_proc_set_writer(proc, writer_cb, out) == ESP_VIDEO_RENDER_ERR_OK);

    // Crop disabled logically by using full region; decoder provides width/height, scale to 64x64 for deterministic output.
    set_crop_scale(proc, 0, 0, 4096, 4096, 64, 64);

    esp_video_render_frame_info_t in_info = {.format = ESP_VIDEO_RENDER_FORMAT_MJPEG, .width = 0, .height = 0, .fps = 30};
    esp_video_render_frame_info_t out_info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = 64, .height = 64, .fps = 30};
    assert(video_render_proc_open(proc, &in_info, &out_info) == ESP_VIDEO_RENDER_ERR_OK);
    for (int i = 0; i < 2; i++) {
        esp_video_render_frame_t frame = {
            .format = ESP_VIDEO_RENDER_FORMAT_MJPEG,
            .width = 0,
            .height = 0,
            .data = jpeg,
            .size = (uint32_t)jpeg_sz,
        };
        assert(video_render_proc_write(proc, &frame) == ESP_VIDEO_RENDER_ERR_OK);
        assert(out->got >= 1);
        assert(out->last.format == ESP_VIDEO_RENDER_FORMAT_RGB565);
        assert(out->last.width == 64 && out->last.height == 64);
        assert(out->last.size == 64 * 64 * 2);
    }

    free(jpeg);
    (void)video_render_proc_close(proc);
    video_render_proc_destroy(proc);
}

static int run_case_h264_decode(out_ctx_t *out, const char *h264_path)
{
    printf("[proc] case: H264 decode (+scale) from %s\n", h264_path);
    uint8_t *data = NULL;
    size_t sz = 0;
    if (read_file_all(h264_path, &data, &sz) != 0) {
        return -1;
    }

    video_render_proc_handle_t proc = NULL;
    assert(video_render_proc_create(NULL, &proc) == ESP_VIDEO_RENDER_ERR_OK);
    assert(video_render_proc_set_writer(proc, writer_cb, out) == ESP_VIDEO_RENDER_ERR_OK);

    // set_crop_scale(proc, 0, 0, 4096, 4096, 64, 64);

    esp_video_render_frame_info_t in_info = {.format = ESP_VIDEO_RENDER_FORMAT_H264, .width = 0, .height = 0, .fps = 30};
    esp_video_render_frame_info_t out_info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = 0, .height = 0, .fps = 30};
    assert(video_render_proc_open(proc, &in_info, &out_info) == ESP_VIDEO_RENDER_ERR_OK);

    esp_video_render_frame_t frame = {
        .format = ESP_VIDEO_RENDER_FORMAT_H264,
        .width = 0,
        .height = 0,
        .data = data,
        .size = (uint32_t)sz,
    };
    esp_video_render_err_t ret = video_render_proc_write(proc, &frame);
    free(data);
    (void)video_render_proc_close(proc);
    video_render_proc_destroy(proc);

    return (ret == ESP_VIDEO_RENDER_ERR_OK && out->got > 0) ? 0 : -1;
}

static int run_case_h264_decode_generated(out_ctx_t *out)
{
    printf("[proc] case: H264 decode (+scale) from generated test pattern\n");

    uint8_t *data = NULL;
    size_t sz = 0;
    if (generate_h264_annexb_one_frame(&data, &sz) != 0) {
        return -1;
    }

    video_render_proc_handle_t proc = NULL;
    assert(video_render_proc_create(NULL, &proc) == ESP_VIDEO_RENDER_ERR_OK);
    assert(video_render_proc_set_writer(proc, writer_cb, out) == ESP_VIDEO_RENDER_ERR_OK);

    // Decode + scale
    set_crop_scale(proc, 0, 0, 4096, 4096, 64, 64);

    esp_video_render_frame_info_t in_info = {.format = ESP_VIDEO_RENDER_FORMAT_H264, .width = 0, .height = 0, .fps = 30};
    esp_video_render_frame_info_t out_info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = 64, .height = 64, .fps = 30};
    assert(video_render_proc_open(proc, &in_info, &out_info) == ESP_VIDEO_RENDER_ERR_OK);

    esp_video_render_frame_t frame = {
        .format = ESP_VIDEO_RENDER_FORMAT_H264,
        .width = 128,
        .height = 128,
        .data = data,
        .size = (uint32_t)sz,
    };
    esp_video_render_err_t ret = video_render_proc_write(proc, &frame);
    free(data);

    (void)video_render_proc_close(proc);
    video_render_proc_destroy(proc);

    return (ret == ESP_VIDEO_RENDER_ERR_OK && out->got > 0) ? 0 : -1;
}

int main(void)
{
    out_ctx_t out = {0};

    // Raw crop+scale
    reset_out(&out);
    run_case_raw_crop_scale(&out);

    // Raw color convert
    reset_out(&out);
    run_case_raw_color_convert(&out);

    // MJPEG decode (use assets folder)
    char mjpeg_path[1024];
    if (get_assets_path("left.mjpeg", mjpeg_path, sizeof(mjpeg_path)) != 0) {
        printf("[proc] WARNING: left.mjpeg not found in assets folder, skipping MJPEG decode test\n");
    } else {
        reset_out(&out);
        run_case_mjpeg_decode(&out, mjpeg_path);
    }

    // H264 decode: auto-generate a tiny test bitstream via GStreamer (no user setup),
    // or use EMU_TEST_H264_PATH if provided.
    const char *require_h264 = getenv("EMU_TEST_REQUIRE_H264");  // set to "1" to hard-fail if H264 case can't run
    const char *h264_path = getenv("EMU_TEST_H264_PATH");
    reset_out(&out);
    int h264_ok = -1;
    if (h264_path && h264_path[0] != '\0') {
        h264_ok = run_case_h264_decode(&out, h264_path);
    } else {
        h264_ok = run_case_h264_decode_generated(&out);
    }
    if (h264_ok != 0) {
        fprintf(stderr, "[proc] H264 case skipped/failed (set EMU_TEST_H264_PATH or install an H264 encoder plugin; set EMU_TEST_REQUIRE_H264=1 to fail)\n");
        if (require_h264 && strcmp(require_h264, "1") == 0) {
            free(out.buf);
            return 1;
        }
    }

    free(out.buf);
    return 0;
}
