/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "assets_path.h"
#include "../../examples/video_player/main/video_player_view.h"
#include "esp_timer.h"
#include "sdl_backend.h"

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX  4096
#endif  /* PATH_MAX */
#define TEST_VIDEO_PATH  "left.mjpeg"

typedef struct {
    FILE    *fp;
    bool     eos;
    uint8_t *carry;
    int      carry_size;
    uint8_t  frame_buf[1024 * 1024];
    int      last_size;
} mjpeg_reader_t;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static int clamp_i(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static int find_mjpeg_end(const uint8_t *data, int size, bool eof)
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

static int mjpeg_read_one(mjpeg_reader_t *r, esp_video_render_frame_t *frame)
{
    int filled = 0;
    if (r->carry_size > 0) {
        memmove(r->frame_buf, r->carry, r->carry_size);
        filled = r->carry_size;
        r->carry_size = 0;
        r->carry = NULL;
    } else if (r->eos) {
        fseek(r->fp, 0, SEEK_SET);
        r->eos = false;
    }
    int frame_end = -1;
    while (frame_end <= 0) {
        frame_end = find_mjpeg_end(r->frame_buf, filled, r->eos);
        if (frame_end > 0) {
            break;
        }
        int left = (int)sizeof(r->frame_buf) - filled;
        if (left <= 0) {
            return -1;
        }
        int rd = (int)fread(r->frame_buf + filled, 1, left, r->fp);
        if (rd == 0 && ferror(r->fp)) {
            return -1;
        }
        filled += rd;
        r->eos = feof(r->fp);
        if (rd == 0 && r->eos) {
            fseek(r->fp, 0, SEEK_SET);
            r->eos = false;
        }
    }
    r->last_size = frame_end;
    r->carry = r->frame_buf + frame_end;
    r->carry_size = filled - frame_end;
    frame->format = ESP_VIDEO_RENDER_FORMAT_MJPEG;
    frame->width = 240;
    frame->height = 240;
    frame->data = r->frame_buf;
    frame->size = (uint32_t)frame_end;
    return 0;
}

static void get_out_paths(char *play_path, size_t play_n, char *pause_path, size_t pause_n)
{
    const char *f = __FILE__;
    const char *m = strstr(f, "/emulation/tests/");
    if (m) {
        size_t pre = (size_t)(m - f) + strlen("/emulation");
        char root[PATH_MAX];
        if (pre >= sizeof(root)) {
            pre = sizeof(root) - 1;
        }
        memcpy(root, f, pre);
        root[pre] = '\0';
        snprintf(play_path, play_n, "%s/play.jpg", root);
        snprintf(pause_path, pause_n, "%s/pause_mute.jpg", root);
        return;
    }
    snprintf(play_path, play_n, "play.jpg");
    snprintf(pause_path, pause_n, "pause_mute.jpg");
}

static void get_display_size(int *w, int *h)
{
    const int default_w = 760;
    const int default_h = 360;
    const char *s = getenv("PLAYER_VIEW_SIZE");
    int sw = 0;
    int sh = 0;
    if (s && sscanf(s, "%dx%d", &sw, &sh) == 2) {
        *w = clamp_i(sw, 240, 1600);
        *h = clamp_i(sh, 180, 1200);
        return;
    }
    *w = default_w;
    *h = default_h;
}

int main(void)
{
    int width = 0;
    int height = 0;
    get_display_size(&width, &height);

    char mjpeg_path[PATH_MAX];
    if (get_assets_path(TEST_VIDEO_PATH, mjpeg_path, sizeof(mjpeg_path)) != 0) {
        printf("[player_view] " TEST_VIDEO_PATH " not found under assets, skip\n");
        return 0;
    }
    char font_path[PATH_MAX];
    const char *font = NULL;
    if (get_assets_path("DejaVuSans.ttf", font_path, sizeof(font_path)) == 0) {
        font = font_path;
    }
    int fps = 30;

    video_player_view_cfg_t cfg = {
        .width = width,
        .height = height,
        .stream = NULL,
        .file_name = TEST_VIDEO_PATH,
        .font_path = font,
        .initial_duration_sec = 21,
        .hide_timeout_ms = 1200,
        .status_bar_alpha = 128,
        .fps_alpha = 200,
    };
    esp_video_render_handle_t render = NULL;
    esp_video_render_stream_handle_t stream = NULL;
    esp_video_render_stream_handle_t overlay_stream = NULL;
    esp_video_render_cfg_t render_cfg = {.pool = NULL, .fps = fps};
    assert(esp_video_render_create(&render_cfg, &render) == ESP_VIDEO_RENDER_ERR_OK);
    sdl_backend_cfg_t bk_cfg = {
        .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        .width = width,
        .height = height,
    };
    esp_video_render_backend_cfg_t disp = {
        .ops = esp_video_render_get_sdl_backend(),
        .cfg = &bk_cfg,
        .cfg_size = sizeof(bk_cfg),
    };
    assert(esp_video_render_set_display(render, &disp) == ESP_VIDEO_RENDER_ERR_OK);
    esp_video_render_stream_info_t stream_info = {
        .info = {.format = ESP_VIDEO_RENDER_FORMAT_MJPEG, .width = width, .height = height, .fps = fps},
        .cached = false,
    };
    assert(esp_video_render_stream_open(render, &stream_info, &stream) == ESP_VIDEO_RENDER_ERR_OK);
    stream_info.info.width = width;
    stream_info.info.height = height;
    assert(esp_video_render_stream_open(render, &stream_info, &overlay_stream) == ESP_VIDEO_RENDER_ERR_OK);
    esp_video_render_rect_t video_rect = {.x = 0, .y = 0, .width = width, .height = height};
    assert(esp_video_render_stream_set_disp_rect(stream, &video_rect) == ESP_VIDEO_RENDER_ERR_OK);
    (void)esp_video_render_stream_render_async(stream);

    cfg.stream = overlay_stream;
    video_player_view_t *pv = NULL;
    assert(video_player_view_init(&cfg, &pv) == 0);

    mjpeg_reader_t reader = {0};
    reader.fp = fopen(mjpeg_path, "rb");
    assert(reader.fp != NULL);

    bool playing = true;
    int duration_sec = 21;
    int pos_sec = 2;
    bool muted = false;
    int64_t t0 = now_ms();
    int frames = 0;
    int fps_window_frames = 0;
    int64_t fps_last_ms = t0;

    assert(video_player_view_set_duration(pv, duration_sec) == 0);
    assert(video_player_view_set_position(pv, pos_sec) == 0);
    assert(video_player_view_set_mute(pv, muted) == 0);
    assert(video_player_view_set_playing(pv, playing) == 0);

    for (int i = 0; i < 200; i++) {
        esp_video_render_frame_t frame = {0};
        if (playing) {
            assert(mjpeg_read_one(&reader, &frame) == 0);
            assert(esp_video_render_stream_write(stream, &frame) == ESP_VIDEO_RENDER_ERR_OK);
            frames++;
            fps_window_frames++;
        }
        int64_t now = now_ms();
        if (now - fps_last_ms >= 500) {
            float fps = (fps_window_frames * 1000.0f) / (float)(now - fps_last_ms + 1);
            (void)video_player_view_set_fps(pv, fps);
            fps_last_ms = now;
            fps_window_frames = 0;
            pos_sec = frames / 30;
            assert(video_player_view_set_position(pv, pos_sec) == 0);
            muted = !muted;
            video_player_view_set_mute(pv, muted);
        }
        // (void)video_player_view_tick(pv);
        usleep(20000);
    }

    char play_path[PATH_MAX];
    char pause_path[PATH_MAX];
    get_out_paths(play_path, sizeof(play_path), pause_path, sizeof(pause_path));
    assert(sdl_backend_dump_jpg(play_path) == 0);

    playing = false;
    muted = true;
    pos_sec = 7;
    assert(video_player_view_set_playing(pv, playing) == 0);
    assert(video_player_view_set_mute(pv, muted) == 0);
    assert(video_player_view_set_position(pv, pos_sec) == 0);
    (void)video_player_view_set_fps(pv, 0.0f);
    // Workaround, write extra frame to let UI can overlay onto video
    esp_video_render_frame_t frame = {0};
    assert(mjpeg_read_one(&reader, &frame) == 0);
    assert(esp_video_render_stream_write(stream, &frame) == ESP_VIDEO_RENDER_ERR_OK);
    (void)video_player_view_tick(pv);
    usleep(100000);
    assert(sdl_backend_dump_jpg(pause_path) == 0);
    printf("[player_view] generated: %s and %s\n", play_path, pause_path);

    fclose(reader.fp);
    video_player_view_deinit(pv);
    esp_video_render_stream_close(stream);
    esp_video_render_destroy(render);
    return 0;
}
