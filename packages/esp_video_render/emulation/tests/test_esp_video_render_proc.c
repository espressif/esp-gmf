#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "esp_video_render_types.h"
#include "assets_path.h"

#define PROC_OUT_W  240
#define PROC_OUT_H  240

int video_render_proc_wrapper_test(int count);

static int read_file_all(const char *path, uint8_t **out_data, size_t *out_size)
{
    *out_data = NULL;
    *out_size = 0;
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0) {
        fclose(fp);
        return -1;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)size);
    if (buf == NULL) {
        fclose(fp);
        return -1;
    }
    if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        free(buf);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    *out_data = buf;
    *out_size = (size_t)size;
    return 0;
}

static int run_cmd_capture_stdout(const char *cmd, uint8_t **out_data, size_t *out_size)
{
    *out_data = NULL;
    *out_size = 0;
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        return -1;
    }
    size_t cap = 64 * 1024;
    size_t size = 0;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (buf == NULL) {
        pclose(fp);
        return -1;
    }
    while (1) {
        if (size == cap) {
            cap *= 2;
            uint8_t *next = (uint8_t *)realloc(buf, cap);
            if (next == NULL) {
                free(buf);
                pclose(fp);
                return -1;
            }
            buf = next;
        }
        size_t rd = fread(buf + size, 1, cap - size, fp);
        size += rd;
        if (rd == 0) {
            break;
        }
    }
    if (pclose(fp) != 0 || size == 0) {
        free(buf);
        return -1;
    }
    *out_data = buf;
    *out_size = size;
    return 0;
}

static int generate_jpeg_asset_fallback(esp_video_render_img_t *image)
{
    uint8_t *data = NULL;
    size_t size = 0;
    const char *cmd =
        "gst-launch-1.0 -q -e "
        "videotestsrc num-buffers=1 pattern=smpte is-live=false ! "
        "video/x-raw,format=I420,width=128,height=128,framerate=30/1 ! "
        "jpegenc ! fdsink fd=1 sync=false 2>/dev/null";
    if (run_cmd_capture_stdout(cmd, &data, &size) != 0) {
        return -1;
    }
    memset(image, 0, sizeof(*image));
    image->info.format = ESP_VIDEO_RENDER_FORMAT_MJPEG;
    image->info.width = 0;
    image->info.height = 0;
    image->data = data;
    image->size = (uint32_t)size;
    return 0;
}

static int get_jpeg_frame_end(const uint8_t *data, int size, int eof)
{
    for (int i = 0; i < size - 1; i++) {
        if (data[i] == 0xFF && data[i + 1] == 0xD9) {
            if (i + 3 < size && data[i + 2] == 0xFF && data[i + 3] == 0xD8) {
                return i + 2;
            }
            if (eof && i == size - 2) {
                return i + 2;
            }
        }
    }
    return -1;
}

static int try_load_named_asset(const char *name, esp_video_render_img_t *image)
{
    char path[1024];
    uint8_t *data = NULL;
    size_t size = 0;
    if (get_assets_path(name, path, sizeof(path)) != 0) {
        return -1;
    }
    if (read_file_all(path, &data, &size) != 0) {
        return -1;
    }
    memset(image, 0, sizeof(*image));
    image->info.format = ESP_VIDEO_RENDER_FORMAT_MJPEG;
    image->info.width = 0;
    image->info.height = 0;
    image->data = data;
    image->size = (uint32_t)size;
    return 0;
}

static int try_load_jpeg_from_mjpeg(const char *name, esp_video_render_img_t *image)
{
    char path[1024];
    FILE *fp = NULL;
    uint8_t *buf = NULL;
    if (get_assets_path(name, path, sizeof(path)) != 0) {
        return -1;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }
    enum { MAX_FRAME = 512 * 1024 };
    buf = (uint8_t *)malloc(MAX_FRAME);
    if (buf == NULL) {
        fclose(fp);
        return -1;
    }
    int filled = 0;
    int eof = 0;
    int frame_end = -1;
    while (filled < MAX_FRAME) {
        frame_end = get_jpeg_frame_end(buf, filled, eof);
        if (frame_end > 0) {
            break;
        }
        int rd = (int)fread(buf + filled, 1, (size_t)(MAX_FRAME - filled), fp);
        filled += rd;
        eof = feof(fp) ? 1 : 0;
        if (rd == 0 && eof) {
            break;
        }
    }
    fclose(fp);
    if (frame_end <= 0) {
        free(buf);
        return -1;
    }
    memset(image, 0, sizeof(*image));
    image->data = (uint8_t *)malloc((size_t)frame_end);
    if (image->data == NULL) {
        free(buf);
        return -1;
    }
    memcpy(image->data, buf, (size_t)frame_end);
    image->size = (uint32_t)frame_end;
    image->info.format = ESP_VIDEO_RENDER_FORMAT_MJPEG;
    image->info.width = 0;
    image->info.height = 0;
    free(buf);
    return 0;
}

int video_render_proc_test_prepare_image(esp_video_render_img_t *image, esp_video_render_frame_info_t *out_info)
{
    out_info->format = ESP_VIDEO_RENDER_FORMAT_RGB565;
    out_info->width = PROC_OUT_W;
    out_info->height = PROC_OUT_H;
    if (try_load_named_asset("left.jpg", image) == 0) {
        return 0;
    }
    return generate_jpeg_asset_fallback(image);
}

int main(void)
{
    int ret = video_render_proc_wrapper_test(1);
    if (ret != 0) {
        fprintf(stderr, "[proc] video proc wrapper test failed\n");
        return 1;
    }
    printf("[proc] video proc wrapper test passed\n");
    return 0;
}
