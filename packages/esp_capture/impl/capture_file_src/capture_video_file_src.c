/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_capture_types.h"
#include "esp_capture_video_src_if.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "data_queue.h"
#include "capture_os.h"

#define TAG "VID_FILE_SRC"

#define NAL_UNIT_TYPE_SPS 7
#define NAL_UNIT_TYPE_PPS 8
#define MAX_FRAME_SIZE    40 * 1024
#define MAX_FRAME_NUM     2
#define READ_SIZE         1024
#define MAX_FILE_PATH_LEN 128

typedef struct {
    esp_capture_video_src_if_t  base;
    esp_capture_video_info_t    vid_info;
    char                        file_path[MAX_FILE_PATH_LEN];
    uint8_t                     frame_cache[READ_SIZE];
    int                         cached_size;
    FILE                       *fp;
    bool                        is_open;
    bool                        is_start;
    bool                        nego_ok;
    data_q_t                   *frame_q;
} video_file_src_t;

static int sps_bit_pos = 0;

static inline uint32_t read_bits(uint8_t *buffer, uint32_t count)
{
    uint32_t res = 0;
    uint8_t index = (sps_bit_pos >> 3);
    uint8_t bit_num = sps_bit_pos & 0x7;
    uint8_t out_bit_num = count - 1;
    for (uint8_t c = 0; c < count; c++) {
        if (buffer[index] << bit_num & 0x80) {
            res |= (1 << out_bit_num);
        }
        if (++bit_num > 7) {
            bit_num = 0;
            index++;
        }
        out_bit_num--;
    }
    sps_bit_pos += count;
    return res;
}

static inline uint32_t read_ueg(uint8_t *buffer)
{
    uint32_t bits = 0;
    while (read_bits(buffer, 1) == 0) {
        bits++;
    }
    uint32_t res = 0;
    if (bits) {
        uint32_t val = read_bits(buffer, bits);
        res = (uint32_t)((1 << bits) - 1 + val);
    }
    return res;
}

static int32_t read_eg(uint8_t *buffer)
{
    uint32_t val = read_ueg(buffer);
    if (val & 0x01) {
        return (val + 1) / 2;
    } else {
        return -(val / 2);
    }
}

static inline void skip_scaling_list(uint8_t *buffer, uint8_t count)
{
    uint32_t delta_scale, last_scale = 8, next_scale = 8;
    for (uint8_t j = 0; j < count; j++) {
        if (next_scale != 0) {
            delta_scale = read_eg(buffer);
            next_scale = (last_scale + delta_scale + 256) & 0xFF;
        }
        last_scale = (next_scale == 0 ? last_scale : next_scale);
    }
}

static inline void parse_sps(uint8_t *buffer, size_t sps_size, esp_capture_video_info_t *info)
{
    sps_bit_pos = 8;
    uint8_t profile = read_bits(buffer, 8);
    sps_bit_pos += 16;
    read_ueg(buffer);
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 || profile == 83 || profile == 86 || profile == 118 || profile == 128) {
        uint32_t chroma_format = read_ueg(buffer);
        if (chroma_format == 3) {
            sps_bit_pos++;
        }
        read_ueg(buffer);
        read_ueg(buffer);
        sps_bit_pos++;
        if (read_bits(buffer, 1)) {
            for (int i = 0; i < (chroma_format != 3 ? 8 : 12); i++) {
                if (read_bits(buffer, 1)) {
                    if (i < 6) {
                        skip_scaling_list(buffer, 16);
                    } else {
                        skip_scaling_list(buffer, 64);
                    }
                }
            }
        }
    }
    read_ueg(buffer);
    uint32_t pic_order_type = read_ueg(buffer);
    if (pic_order_type == 0) {
        read_ueg(buffer);
    } else if (pic_order_type == 1) {
        sps_bit_pos++;
        read_eg(buffer);
        read_eg(buffer);
        for (int i = 0; i < read_ueg(buffer); i++) {
            read_eg(buffer);
        }
    }
    read_ueg(buffer);
    sps_bit_pos++;

    uint32_t width_minus1 = read_ueg(buffer);
    uint32_t height_minus1 = read_ueg(buffer);
    uint8_t frame_mbs_only = read_bits(buffer, 1);
    if (!frame_mbs_only) {
        sps_bit_pos++;
    }
    sps_bit_pos++;

    uint32_t crop_left = 0;
    uint32_t crop_right = 0;
    uint32_t crop_top = 0;
    uint32_t crop_bottom = 0;
    if (read_bits(buffer, 1)) {
        crop_left = read_ueg(buffer);
        crop_right = read_ueg(buffer);
        crop_top = read_ueg(buffer);
        crop_bottom = read_ueg(buffer);
    }
    info->width = (((width_minus1 + 1) << 4) - crop_left * 2 - crop_right * 2);
    info->height = ((2 - frame_mbs_only) * (height_minus1 + 1) << 4);
    info->height -= ((frame_mbs_only ? 2 : 4) * (crop_top + crop_bottom));
    ESP_LOGI(TAG, "Parse sps ok %dx%d", (int)info->width, (int)info->height);
}

static inline int is_start_code(video_file_src_t *src, uint8_t *data)
{
    if (data[0] || data[1]) {
        return 0;
    }
    if (data[2] == 1) {
        return 3;
    }
    if (data[2] == 0 && data[3] == 1) {
        return 4;
    }
    return 0;
}

static int try_parse_sps(video_file_src_t *src, uint8_t *data, int size)
{
    for (int i = 0; i < size - 5; i++) {
        int start_code_len = is_start_code(src, data + i);
        if (start_code_len == 0) {
            continue;
        }
        uint8_t nal_type = data[i + start_code_len] & 0x1F;
        if (nal_type == NAL_UNIT_TYPE_SPS) {
            parse_sps(data + i + start_code_len, size - (i + start_code_len + 1), &src->vid_info);
            return 0;
        }
        i += start_code_len;
    }
    return -1;
}

static esp_capture_err_t get_vid_info_by_name(video_file_src_t *src)
{
    char *ext = strrchr(src->file_path, '.');
    if (ext == NULL) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    if (strcmp(ext, ".h264") == 0) {
        int ret = fread(src->frame_cache, 1, READ_SIZE, src->fp);
        if (ret < 0) {
            return ESP_CAPTURE_ERR_NOT_FOUND;
        }
        src->cached_size = ret;
        ret = try_parse_sps(src, src->frame_cache, ret);
        if (ret != 0) {
            return ESP_CAPTURE_ERR_NOT_FOUND;
        }
        src->vid_info.format_id = ESP_CAPTURE_FMT_ID_H264;
        return ESP_CAPTURE_ERR_OK;
    }
    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
}

static esp_capture_err_t video_file_src_close(esp_capture_video_src_if_t *h)
{
    video_file_src_t *src = (video_file_src_t *)h;
    if (src->fp != NULL) {
        fclose(src->fp);
        src->fp = NULL;
    }
    if (src->frame_q) {
        data_q_deinit(src->frame_q);
        src->frame_q = NULL;
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t video_file_src_open(esp_capture_video_src_if_t *h)
{
    video_file_src_t *src = (video_file_src_t *)h;
    src->fp = fopen(src->file_path, "rb");
    if (src->fp == NULL) {
        ESP_LOGE(TAG, "open file %s failed", src->file_path);
        return ESP_CAPTURE_ERR_NOT_FOUND;
    }
    src->frame_q = data_q_init(MAX_FRAME_NUM * MAX_FRAME_SIZE);
    if (src->frame_q == NULL) {
        video_file_src_close(h);
        return ESP_CAPTURE_ERR_NO_MEM;
    }
    esp_capture_err_t ret = get_vid_info_by_name(src);
    if (ret != ESP_CAPTURE_ERR_OK) {
        video_file_src_close(h);
        return ret;
    }
    src->is_open = true;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t video_file_src_get_support_codecs(esp_capture_video_src_if_t *h, const esp_capture_format_id_t **codecs,
                                                           uint8_t *num)
{
    video_file_src_t *src = (video_file_src_t *)h;
    *num = 1;
    *codecs = &src->vid_info.format_id;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t video_file_src_negotiate_caps(esp_capture_video_src_if_t *h, esp_capture_video_info_t *in_cap,
                                                       esp_capture_video_info_t *out_caps)
{
    video_file_src_t *src = (video_file_src_t *)h;
    if (src->is_open == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    if (in_cap->format_id != src->vid_info.format_id) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    *out_caps = src->vid_info;
    out_caps->fps = in_cap->fps;
    src->nego_ok = true;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t video_file_src_start(esp_capture_video_src_if_t *h)
{
    video_file_src_t *src = (video_file_src_t *)h;
    if (src->nego_ok == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    src->is_start = true;
    return ESP_CAPTURE_ERR_OK;
}

static int read_h264_frame(video_file_src_t *src, uint8_t *data, int size)
{
    // Find frame in cache
    int fill = 0;
    bool find_frame = false;
    bool use_cache = false;
    if (src->cached_size) {
        memcpy(data, src->frame_cache, src->cached_size);
        fill = src->cached_size;
        src->cached_size = 0;
        use_cache = true;
    }
    while (fill + READ_SIZE < size) {
        int ret = 0;
        int last;
        if (use_cache == false) {
            ret = fread(data + fill, 1, READ_SIZE, src->fp);
            if (ret < 0) {
                return -1;
            }
            last = fill > 5 ? fill - 5 : 0;
        } else {
            last = 0;
        }
        fill += ret;
        for (int i = last; i < fill - 5; i++) {
            int start_code_len = is_start_code(src, data + i);
            if (start_code_len == 0) {
                continue;
            }
            // Already find a frame
            if (find_frame) {
                // Reach file end
                if (use_cache == false && ret < READ_SIZE) {
                    return fill;
                }
                // Copy left buffer into cache
                memcpy(src->frame_cache, data + i, fill - i);
                src->cached_size = fill - i;
                return i;
            }
            uint8_t nal_type = data[i + start_code_len] & 0x1F;
            // Find IDR or non-IDR frame
            if (nal_type == 5 || nal_type == 1) {
                find_frame = true;
            }
            i += start_code_len;
        }
        if (use_cache) {
            use_cache = false;
            continue;
        }
        if (ret < READ_SIZE) {
            break;
        }
    }
    return -1;
}

static esp_capture_err_t video_file_src_acquire_frame(esp_capture_video_src_if_t *h, esp_capture_stream_frame_t *frame)
{
    video_file_src_t *src = (video_file_src_t *)h;
    if (src->is_start == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    uint8_t *data = data_q_get_buffer(src->frame_q, MAX_FRAME_SIZE);
    if (data) {
        int ret = read_h264_frame(src, data, MAX_FRAME_SIZE);
        if (ret > 0) {
            frame->data = data;
            frame->size = ret;
            data_q_send_buffer(src->frame_q, ret);
            return ESP_CAPTURE_ERR_OK;
        }
        data_q_send_buffer(src->frame_q, 0);
    }
    return ESP_CAPTURE_ERR_NOT_FOUND;
}

static esp_capture_err_t video_file_src_release_frame(esp_capture_video_src_if_t *h, esp_capture_stream_frame_t *frame)
{
    video_file_src_t *src = (video_file_src_t *)h;
    if (src->is_start == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    void *data = NULL;
    int size = 0;
    data_q_read_lock(src->frame_q, &data, &size);
    data_q_read_unlock(src->frame_q);
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t video_file_src_stop(esp_capture_video_src_if_t *h)
{
    video_file_src_t *src = (video_file_src_t *)h;
    if (src->fp) {
        fseek(src->fp, 0, SEEK_SET);
    }
    src->is_start = false;
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_video_src_if_t *esp_capture_new_video_file_src(const char *file_name)
{
    video_file_src_t *src = (video_file_src_t *)capture_calloc(1, sizeof(video_file_src_t));
    if (src == NULL) {
        return NULL;
    }
    strncpy(src->file_path, file_name, sizeof(src->file_path) - 1);
    src->base.open = video_file_src_open;
    src->base.get_support_codecs = video_file_src_get_support_codecs;
    src->base.negotiate_caps = video_file_src_negotiate_caps;
    src->base.start = video_file_src_start;
    src->base.acquire_frame = video_file_src_acquire_frame;
    src->base.release_frame = video_file_src_release_frame;
    src->base.stop = video_file_src_stop;
    src->base.close = video_file_src_close;
    return &src->base;
}
