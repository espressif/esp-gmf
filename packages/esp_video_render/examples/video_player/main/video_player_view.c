/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "video_player_view.h"
#include "esp_vui_container.h"
#include "esp_vui_overlay.h"
#include "esp_vui_widget.h"
#include "esp_vui_widget_default.h"
#include "esp_timer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct player_view video_player_view_t;

typedef struct {
    uint16_t  pad;
    uint16_t  gap;
    uint16_t  status_h;
    uint16_t  fps_w;
    uint16_t  fps_h;
    uint16_t  icon_btn;
    uint16_t  icon_center;
    uint16_t  font_time;
    uint16_t  font_file;
    uint16_t  font_fps;
    uint16_t  text_h;
    uint16_t  time_w;
    uint16_t  file_w;
    int       progress_h;
    uint16_t  volume_w;
    uint16_t  volume_h;
    uint16_t  x_play;
    int       x_progress;
    uint16_t  x_time;
    uint16_t  x_file;
    uint16_t  x_mute;
    uint16_t  x_volume;
    uint16_t  y_row;
    int       y_progress;
    uint16_t  y_text;
    uint16_t  y_volume;
    int       progress_w;
    bool      compact;
} video_player_view_layout_t;

struct player_view {
    esp_vui_overlay_handle_t    overlay;
    esp_vui_container_handle_t  status_bar;
    esp_vui_container_handle_t  center_layer;
    esp_vui_container_handle_t  fps_layer;
    esp_vui_widget_t           *w_btn_play;
    esp_vui_widget_t           *w_btn_pause;
    esp_vui_widget_t           *w_progress;
    esp_vui_widget_t           *w_time;
    esp_vui_widget_t           *w_filename;
    esp_vui_widget_t           *w_mute_on;
    esp_vui_widget_t           *w_mute_off;
    esp_vui_widget_t           *w_volume;
    esp_vui_widget_t           *w_center_play;
    esp_vui_widget_t           *w_fps;
    esp_video_render_img_t      icon_btn_play;
    esp_video_render_img_t      icon_btn_pause;
    esp_video_render_img_t      icon_mute_on;
    esp_video_render_img_t      icon_mute_off;
    esp_video_render_img_t      volume_img;
    esp_video_render_img_t      icon_center_play;
    esp_video_render_img_t      progress_img;
    int                         duration_sec;
    int                         pos_sec;
    bool                        playing;
    bool                        muted;
    bool                        controls_visible;
    int                         hide_timeout_ms;
    int64_t                     last_command_ms;
    char                        file_name[64];
    char                        fps_text[24];
    uint16_t                    screen_w;
    uint16_t                    screen_h;
    uint16_t                    volume_pct;
    video_player_view_layout_t  layout;
    bool                        format_be;
};

typedef struct {
    uint16_t  panel;
    uint16_t  track;
    uint16_t  fill;
    uint16_t  knob;
} slider_style_t;

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

static inline uint16_t rgb565(bool be, uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t rgb565 = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    return be ? __builtin_bswap16(rgb565) : rgb565;
}

static void put_px(esp_video_render_img_t *img, int x, int y, uint16_t c)
{
    if (!img || !img->data) {
        return;
    }
    if (x < 0 || y < 0 || x >= img->info.width || y >= img->info.height) {
        return;
    }
    ((uint16_t *)img->data)[y * img->info.width + x] = c;
}

static void fill_img(esp_video_render_img_t *img, uint16_t c)
{
    if (!img || !img->data) {
        return;
    }
    uint16_t *p = (uint16_t *)img->data;
    int n = img->info.width * img->info.height;
    for (int i = 0; i < n; i++) {
        p[i] = c;
    }
}

static void fill_circle(esp_video_render_img_t *img, int cx, int cy, int r, uint16_t c)
{
    for (int y = cy - r; y <= cy + r; y++) {
        for (int x = cx - r; x <= cx + r; x++) {
            int dx = x - cx;
            int dy = y - cy;
            if (dx * dx + dy * dy <= r * r) {
                put_px(img, x, y, c);
            }
        }
    }
}

static void fill_rect(esp_video_render_img_t *img, int x0, int y0, int w, int h, uint16_t c)
{
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            put_px(img, x, y, c);
        }
    }
}

static void fill_triangle_right(esp_video_render_img_t *img, int x0, int y0, int w, int h, uint16_t c)
{
    for (int y = 0; y < h; y++) {
        int span = (y <= h / 2) ? (y * w / (h / 2 + 1)) : ((h - 1 - y) * w / (h / 2 + 1));
        for (int x = 0; x <= span; x++) {
            put_px(img, x0 + x, y0 + y, c);
        }
    }
}

static void fill_triangle_left(esp_video_render_img_t *img, int x0, int y0, int w, int h, uint16_t c)
{
    for (int y = 0; y < h; y++) {
        int span = (y <= h / 2) ? (y * w / (h / 2 + 1)) : ((h - 1 - y) * w / (h / 2 + 1));
        for (int x = w - span; x <= w; x++) {
            put_px(img, x0 + x, y0 + y, c);
        }
    }
}

static void draw_diag_line(esp_video_render_img_t *img, int x0, int y0, int x1, int y1, int thickness, uint16_t c)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);

    if (steps <= 0) {
        put_px(img, x0, y0, c);
        return;
    }

    for (int i = 0; i <= steps; i++) {
        int x = x0 + (dx * i) / steps;
        int y = y0 + (dy * i) / steps;

        for (int t = -thickness / 2; t <= thickness / 2; t++) {
            put_px(img, x + t, y, c);
            put_px(img, x, y + t, c);
        }
    }
}

static void fill_round_bar(esp_video_render_img_t *img, int x, int y, int w, int h, uint16_t c)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    int r = h / 2;
    fill_rect(img, x + r, y, w - 2 * r, h, c);
    fill_circle(img, x + r, y + r, r, c);
    fill_circle(img, x + w - r - 1, y + r, r, c);
}

static int alloc_icon(esp_video_render_img_t *img, bool be, int w, int h)
{
    img->info.format = be ? ESP_VIDEO_RENDER_FORMAT_RGB565_BE : ESP_VIDEO_RENDER_FORMAT_RGB565;
    img->info.width = w;
    img->info.height = h;
    img->size = (uint32_t)(w * h * 2);
    img->data = (uint8_t *)malloc(img->size);
    if (!img->data) {
        return -1;
    }
    memset(img->data, 0, img->size);
    return 0;
}

static void free_icon(esp_video_render_img_t *img)
{
    if (!img) {
        return;
    }
    free(img->data);
    img->data = NULL;
    img->size = 0;
}

static void draw_slider_image(esp_video_render_img_t *img, int value, int max, const slider_style_t *style)
{
    if (!img || !style) {
        return;
    }
    if (max <= 0) {
        max = 1;
    }

    value = clamp_i(value, 0, max);

    int w = img->info.width;
    int h = img->info.height;

    int tx = clamp_i(w / 32, 4, 12);
    int th = clamp_i(h / 3, 6, 12);
    int ty = (h - th) / 2;

    int tw = w - 2 * tx;
    int fw = (tw * value) / max;

    int knob_x = tx + fw;
    knob_x = clamp_i(knob_x, tx, tx + tw);

    fill_img(img, style->panel);
    fill_round_bar(img, tx, ty, tw, th, style->track);

    if (fw > 0) {
        fill_round_bar(img, tx, ty, fw, th, style->fill);
    }
    /* Make knob nearly as tall as slider image for better visibility. */
    int knob_r = clamp_i((h - 2) / 2, 8, 24);
    fill_circle(img, knob_x, ty + th / 2, knob_r, style->knob);
}

static void draw_speaker_icon(esp_video_render_img_t *img, bool muted, uint16_t fg)
{
    int w = img->info.width;
    int h = img->info.height;

    fill_img(img, 0);  // transparent key

    /* Speaker body */
    int body_w = w * 14 / 64;
    int body_h = h * 22 / 64;
    int body_x = w * 16 / 64;
    int body_y = (h - body_h) / 2;

    /* Cone */
    int cone_w = w * 22 / 64;
    int cone_h = body_h + (h * 10 / 64);
    int cone_x = body_x + body_w;
    int cone_y = (h - cone_h) / 2;

    fill_rect(img, body_x, body_y, body_w, body_h, fg);
    fill_triangle_left(img, cone_x - cone_w * 22 / 32, cone_y, cone_w, cone_h, fg);

    /* Sound waves */
    if (!muted) {
        int cx = cone_x + cone_w;
        int cy = h / 2;

        int r1 = w * 10 / 64;
        int r2 = w * 16 / 64;

        for (int dy = -r2; dy <= r2; dy++) {
            int y = cy + dy;

            int dx1 = (r1 * r1 - dy * dy);
            int dx2 = (r2 * r2 - dy * dy);

            if (dx1 > 0) {
                int x1 = cx + (int)(sqrtf((float)dx1));
                put_px(img, x1, y, fg);
                put_px(img, x1 + 1, y, fg);  // thickness
            }

            if (dx2 > 0) {
                int x2 = cx + (int)(sqrtf((float)dx2));
                put_px(img, x2, y, fg);
                put_px(img, x2 + 1, y, fg);  // thickness
            }
        }
    }

    /* Slash */
    if (muted) {
        int t = clamp_i(w / 10, 2, 4);
        draw_diag_line(img,
                       w * 14 / 64, h * 14 / 64,
                       w * 52 / 64, h * 50 / 64,
                       t, fg);
    }
}

/* -------------------------------------------------------------------------- */
/* Layout engine                                                              */
/* -------------------------------------------------------------------------- */

static int ui_scale(const video_player_view_t *pv, int base)
{
    int ref = pv->screen_w < pv->screen_h ? pv->screen_w : pv->screen_h;
    return clamp_i((ref * base) / 360, base / 2, base * 2);
}

static void video_player_view_calc_layout(video_player_view_t *pv)
{
    video_player_view_layout_t *l = &pv->layout;

    l->compact = (pv->screen_w < 420 || pv->screen_h < 280);

    l->pad = clamp_i(ui_scale(pv, 12), 8, 18);
    l->gap = clamp_i(ui_scale(pv, 8), 4, 14);

    l->status_h = clamp_i(ui_scale(pv, 56), 44, 90);
    l->text_h = clamp_i(ui_scale(pv, 24), 16, 36);

    l->font_time = clamp_i(ui_scale(pv, 18), 12, 26);
    l->font_file = clamp_i(ui_scale(pv, 16), 10, 22);
    l->font_fps = clamp_i(ui_scale(pv, 14), 10, 18);

    l->icon_btn = clamp_i(ui_scale(pv, 32), 20, 42);
    l->icon_center = clamp_i(ui_scale(pv, 72), 44, 110);

    l->progress_h = clamp_i(ui_scale(pv, 18), 10, 20);
    l->volume_h = clamp_i(ui_scale(pv, 16), 10, 18);
    l->volume_w = clamp_i(ui_scale(pv, 120), 72, 180);

    l->time_w = clamp_i(ui_scale(pv, 130), 70, 190);
    l->file_w = clamp_i(ui_scale(pv, 140), 60, 240);

    l->fps_w = clamp_i(ui_scale(pv, 100), 70, 130);
    l->fps_h = clamp_i(ui_scale(pv, 26), 18, 36);

    int right = pv->screen_w - l->pad;

    l->x_play = l->pad;
    l->x_volume = right - l->volume_w;
    l->x_mute = l->x_volume - l->gap - l->icon_btn;

    l->y_row = (l->status_h - l->icon_btn) / 2;
    l->y_text = (l->status_h - l->text_h) / 2;
    l->y_volume = (l->status_h - l->volume_h) / 2;
    l->y_progress = (l->status_h - l->progress_h) / 2;

    l->x_time = l->x_mute - l->gap - l->time_w;
    l->x_file = l->x_time - l->gap - l->file_w;

    l->x_progress = l->x_play + l->icon_btn + l->gap;
    int progress_right = l->x_file - l->gap;

    l->progress_w = progress_right - l->x_progress;
    l->progress_w = clamp_i(l->progress_w, ui_scale(pv, 90), pv->screen_w - l->pad * 2);

    if (l->progress_w < ui_scale(pv, 120)) {
        int shrink = ui_scale(pv, 120) - l->progress_w;
        l->file_w = clamp_i(l->file_w - shrink, ui_scale(pv, 60), l->file_w);
        l->x_file = l->x_time - l->gap - l->file_w;

        progress_right = l->x_file - l->gap;
        l->progress_w = clamp_i(progress_right - l->x_progress, ui_scale(pv, 90), pv->screen_w);
    }
}

/* -------------------------------------------------------------------------- */
/* Icons init/deinit                                                          */
/* -------------------------------------------------------------------------- */

static int init_icons(video_player_view_t *pv)
{
    uint16_t trans = rgb565(pv->format_be, 0, 0, 0);
    uint16_t dark = rgb565(pv->format_be, 14, 24, 36);
    uint16_t white = rgb565(pv->format_be, 245, 248, 250);

    video_player_view_layout_t *l = &pv->layout;

    if (alloc_icon(&pv->icon_btn_play, pv->format_be, l->icon_btn, l->icon_btn) != 0 ||
        alloc_icon(&pv->icon_btn_pause, pv->format_be, l->icon_btn, l->icon_btn) != 0 ||
        alloc_icon(&pv->icon_mute_on, pv->format_be, l->icon_btn, l->icon_btn) != 0 ||
        alloc_icon(&pv->icon_mute_off, pv->format_be, l->icon_btn, l->icon_btn) != 0 ||
        alloc_icon(&pv->volume_img, pv->format_be, l->volume_w, l->volume_h) != 0 ||
        alloc_icon(&pv->icon_center_play, pv->format_be, l->icon_center, l->icon_center) != 0 ||
        alloc_icon(&pv->progress_img, pv->format_be, l->progress_w, l->progress_h) != 0) {
        return -1;
    }

    int btn = l->icon_btn;
    int center = l->icon_center;

    fill_img(&pv->icon_btn_play, trans);
    fill_circle(&pv->icon_btn_play, btn / 2, btn / 2, btn / 2 - 1, dark);
    fill_triangle_right(&pv->icon_btn_play, btn * 3 / 8, btn / 4, btn * 3 / 8, btn / 2, white);

    fill_img(&pv->icon_btn_pause, trans);
    fill_circle(&pv->icon_btn_pause, btn / 2, btn / 2, btn / 2 - 1, dark);
    fill_rect(&pv->icon_btn_pause, btn * 5 / 16, btn / 4, clamp_i(btn / 8, 3, 7), btn / 2, white);
    fill_rect(&pv->icon_btn_pause, btn * 9 / 16, btn / 4, clamp_i(btn / 8, 3, 7), btn / 2, white);

    /* NEW clean mute icons */
    fill_img(&pv->icon_mute_on, trans);
    draw_speaker_icon(&pv->icon_mute_on, false, white);

    fill_img(&pv->icon_mute_off, trans);
    draw_speaker_icon(&pv->icon_mute_off, true, white);

    fill_img(&pv->icon_center_play, trans);
    fill_circle(&pv->icon_center_play, center / 2, center / 2, center / 2 - 2, dark);
    fill_triangle_right(&pv->icon_center_play, center * 5 / 12, center * 5 / 18, center * 5 / 16, center * 4 / 9, white);

    fill_img(&pv->progress_img, rgb565(pv->format_be, 9, 18, 30));
    return 0;
}

static void deinit_icons(video_player_view_t *pv)
{
    free_icon(&pv->icon_btn_play);
    free_icon(&pv->icon_btn_pause);
    free_icon(&pv->icon_mute_on);
    free_icon(&pv->icon_mute_off);
    free_icon(&pv->volume_img);
    free_icon(&pv->icon_center_play);
    free_icon(&pv->progress_img);
}

/* -------------------------------------------------------------------------- */
/* UI drawing                                                                 */
/* -------------------------------------------------------------------------- */

static void draw_progress_image(video_player_view_t *pv)
{
    int dur = pv->duration_sec <= 0 ? 1 : pv->duration_sec;
    slider_style_t style = {
        .panel = rgb565(pv->format_be, 0, 0, 0),
        .track = rgb565(pv->format_be, 120, 132, 145),
        .fill = rgb565(pv->format_be, 32, 210, 148),
        .knob = rgb565(pv->format_be, 255, 255, 255),
    };
    draw_slider_image(&pv->progress_img, pv->pos_sec, dur, &style);
}

static void draw_volume_image(video_player_view_t *pv)
{
    int vol = pv->muted ? 0 : pv->volume_pct;

    slider_style_t style = {
        .panel = rgb565(pv->format_be, 0, 0, 0),
        .track = rgb565(pv->format_be, 120, 132, 145),
        .fill = pv->muted ? rgb565(pv->format_be, 110, 118, 126) : rgb565(pv->format_be, 32, 210, 148),
        .knob = rgb565(pv->format_be, 255, 255, 255),
    };

    draw_slider_image(&pv->volume_img, vol, 100, &style);
}

static void format_time(int sec, char *out, size_t out_size)
{
    if (sec < 0) {
        sec = 0;
    }
    snprintf(out, out_size, "%d:%02d", sec / 60, sec % 60);
}

static void widget_set_visible_if_changed(esp_vui_widget_t *w, bool visible)
{
    if (w && w->visible != visible) {
        esp_vui_widget_set_visible(w, visible);
    }
}

static void mark_widget_dirty(esp_vui_widget_t *w)
{
    if (!w || !w->container) {
        return;
    }
    esp_vui_container_compose_lock(w->container);
    w->dirty = w->rect;
    esp_vui_container_notify_compose_changed(w->container, &w->dirty, true);
    esp_vui_container_compose_unlock(w->container);
}

static void update_status_widgets(video_player_view_t *pv)
{
    widget_set_visible_if_changed(pv->w_btn_pause, pv->controls_visible && pv->playing);
    widget_set_visible_if_changed(pv->w_btn_play, pv->controls_visible && !pv->playing);

    widget_set_visible_if_changed(pv->w_center_play, !pv->playing);

    widget_set_visible_if_changed(pv->w_mute_off, pv->controls_visible && pv->muted);
    widget_set_visible_if_changed(pv->w_mute_on, pv->controls_visible && !pv->muted);

    widget_set_visible_if_changed(pv->w_volume, pv->controls_visible);
    widget_set_visible_if_changed(pv->w_progress, pv->controls_visible);
    widget_set_visible_if_changed(pv->w_time, pv->controls_visible);
    widget_set_visible_if_changed(pv->w_filename, pv->controls_visible);
}

static void update_progress_and_time(video_player_view_t *pv)
{
    char pos_txt[16];
    char dur_txt[16];
    char time_txt[64];

    format_time(pv->pos_sec, pos_txt, sizeof(pos_txt));
    format_time(pv->duration_sec, dur_txt, sizeof(dur_txt));
    snprintf(time_txt, sizeof(time_txt), "%s / %s", pos_txt, dur_txt);

    draw_progress_image(pv);
    mark_widget_dirty(pv->w_progress);
    esp_vui_text_widget_set_text(pv->w_time, time_txt);
}

static void touch_controls(video_player_view_t *pv)
{
    pv->controls_visible = true;
    esp_vui_container_set_visible(pv->status_bar, pv->controls_visible);
    pv->last_command_ms = now_ms();
    update_status_widgets(pv);
}

int video_player_view_set_playing(video_player_view_t *pv, bool playing)
{
    if (!pv) {
        return -1;
    }
    pv->playing = playing;
    touch_controls(pv);
    return 0;
}

int video_player_view_set_duration(video_player_view_t *pv, int duration_sec)
{
    if (!pv) {
        return -1;
    }

    if (duration_sec <= 0) {
        duration_sec = 1;
    }
    pv->duration_sec = duration_sec;

    if (pv->pos_sec > pv->duration_sec) {
        pv->pos_sec = pv->duration_sec;
    }
    if (pv->controls_visible) {
        update_progress_and_time(pv);
    }
    return 0;
}

int video_player_view_set_position(video_player_view_t *pv, int pos_sec)
{
    if (!pv) {
        return -1;
    }

    pos_sec = clamp_i(pos_sec, 0, pv->duration_sec);
    pv->pos_sec = pos_sec;

    if (pv->controls_visible) {
        update_progress_and_time(pv);
    }
    return 0;
}

int video_player_view_set_mute(video_player_view_t *pv, bool mute)
{
    if (!pv) {
        return -1;
    }
    if (pv->muted == mute) {
        return 0;
    }
    pv->muted = mute;
    touch_controls(pv);
    draw_volume_image(pv);
    mark_widget_dirty(pv->w_volume);
    return 0;
}

int video_player_view_set_volume(video_player_view_t *pv, int vol)
{
    if (!pv) {
        return -1;
    }
    if (pv->muted == false && pv->volume_pct == vol) {
        return 0;
    }
    pv->muted = false;
    pv->volume_pct = (uint16_t)clamp_i(vol, 0, 100);
    touch_controls(pv);
    draw_volume_image(pv);
    mark_widget_dirty(pv->w_volume);
    return 0;
}

int video_player_view_set_file_name(video_player_view_t *pv, const char *file_name)
{
    if (!pv || !file_name) {
        return -1;
    }
    snprintf(pv->file_name, sizeof(pv->file_name), "%s", file_name);
    esp_vui_text_widget_set_text(pv->w_filename, pv->file_name);
    return 0;
}

int video_player_view_set_fps(video_player_view_t *pv, float fps)
{
    if (!pv) {
        return -1;
    }

    if (fps < 0.0f) {
        fps = 0.0f;
    }
    if (fps > 99.9f) {
        fps = 99.9f;
    }

    snprintf(pv->fps_text, sizeof(pv->fps_text), "%04.1ffps", (double)fps);
    esp_vui_text_widget_set_text(pv->w_fps, pv->fps_text);
    return 0;
}

int video_player_view_tick(video_player_view_t *pv)
{
    if (!pv) {
        return -1;
    }
    // Auto hide during playing after timeout
    if (pv->controls_visible && pv->playing &&
        (now_ms() - pv->last_command_ms) > pv->hide_timeout_ms) {
        pv->controls_visible = false;
        esp_vui_container_set_visible(pv->status_bar, pv->controls_visible);
        update_status_widgets(pv);
    }
    return 0;
}

void video_player_view_deinit(video_player_view_t *pv)
{
    if (!pv) {
        return;
    }
    if (pv->fps_layer) {
        esp_vui_container_destroy(pv->fps_layer);
        pv->fps_layer = NULL;
    }
    if (pv->center_layer) {
        esp_vui_container_destroy(pv->center_layer);
        pv->center_layer = NULL;
    }
    if (pv->status_bar) {
        esp_vui_container_destroy(pv->status_bar);
        pv->status_bar = NULL;
    }
    deinit_icons(pv);
    free(pv);
}

int video_player_view_init(const video_player_view_cfg_t *cfg, video_player_view_t **out_handle)
{
    if (!cfg || !out_handle || cfg->width <= 0 || cfg->height <= 0 || !cfg->stream) {
        return -1;
    }

    *out_handle = NULL;

    video_player_view_t *pv = (video_player_view_t *)calloc(1, sizeof(video_player_view_t));
    if (!pv) {
        return -1;
    }
    esp_video_render_format_t format = cfg->format ? cfg->format : ESP_VIDEO_RENDER_FORMAT_RGB565;

    pv->screen_w = cfg->width;
    pv->screen_h = cfg->height;

    pv->duration_sec = cfg->initial_duration_sec > 0 ? cfg->initial_duration_sec : 21;
    pv->playing = true;
    pv->controls_visible = true;
    pv->hide_timeout_ms = cfg->hide_timeout_ms > 0 ? cfg->hide_timeout_ms : 5000;
    pv->volume_pct = 82;
    pv->format_be = (format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE ? true : false);

    if (esp_video_render_stream_get_overlay(cfg->stream, &pv->overlay) != ESP_VIDEO_RENDER_ERR_OK) {
        video_player_view_deinit(pv);
        return -1;
    }

    video_player_view_calc_layout(pv);

    if (init_icons(pv) != 0) {
        video_player_view_deinit(pv);
        return -1;
    }

    esp_video_render_frame_info_t status_info = {
        .format = format,
        .width = pv->screen_w,
        .height = pv->layout.status_h,
    };

    esp_video_render_pos_t status_pos = {
        .x = 0,
        .y = pv->screen_h - pv->layout.status_h,
    };

    if (esp_vui_container_create(pv->overlay, &status_info, &status_pos, true, &pv->status_bar) != ESP_VIDEO_RENDER_ERR_OK) {
        video_player_view_deinit(pv);
        return -1;
    }

    (void)esp_vui_container_set_alpha(pv->status_bar, cfg->status_bar_alpha);

    esp_video_render_frame_info_t center_info = {
        .format = format,
        .width = pv->icon_center_play.info.width,
        .height = pv->icon_center_play.info.height,
    };

    esp_video_render_pos_t center_pos = {
        .x = pv->screen_w / 2 - center_info.width / 2,
        .y = pv->screen_h / 2 - center_info.height / 2,
    };

    if (esp_vui_container_create(pv->overlay, &center_info, &center_pos, false, &pv->center_layer) != ESP_VIDEO_RENDER_ERR_OK) {
        video_player_view_deinit(pv);
        return -1;
    }

    int fps_w = pv->layout.fps_w;
    int fps_h = pv->layout.fps_h;

    esp_video_render_frame_info_t fps_info = {
        .format = format,
        .width = fps_w,
        .height = fps_h,
    };

    esp_video_render_pos_t fps_pos = {
        .x = pv->screen_w - fps_w - pv->layout.pad,
        .y = pv->layout.pad / 2,
    };

    if (esp_vui_container_create(pv->overlay, &fps_info, &fps_pos, false, &pv->fps_layer) != ESP_VIDEO_RENDER_ERR_OK) {
        video_player_view_deinit(pv);
        return -1;
    }

    (void)esp_vui_container_set_alpha(pv->fps_layer, cfg->fps_alpha);

    esp_video_render_pos_t p_play = {.x = pv->layout.x_play, .y = pv->layout.y_row};
    esp_video_render_pos_t p_prog = {.x = pv->layout.x_progress, .y = pv->layout.y_progress};
    esp_video_render_pos_t p_time = {.x = pv->layout.x_time, .y = pv->layout.y_text};
    esp_video_render_pos_t p_file = {.x = pv->layout.x_file, .y = pv->layout.y_text};
    esp_video_render_pos_t p_mute = {.x = pv->layout.x_mute, .y = pv->layout.y_row};
    esp_video_render_pos_t p_volume = {.x = pv->layout.x_volume, .y = pv->layout.y_volume};
    esp_video_render_pos_t p_center = {.x = 0, .y = 0};
    esp_video_render_pos_t p_fps = {.x = 0, .y = 0};

    pv->w_btn_play = esp_vui_image_widget_init(pv->status_bar, &pv->icon_btn_play, &p_play);
    pv->w_btn_pause = esp_vui_image_widget_init(pv->status_bar, &pv->icon_btn_pause, &p_play);
    pv->w_progress = esp_vui_image_widget_init(pv->status_bar, &pv->progress_img, &p_prog);
    pv->w_time = esp_vui_text_widget_init(pv->status_bar, &status_info, &p_time, pv->layout.time_w, pv->layout.text_h);
    pv->w_filename = esp_vui_text_widget_init(pv->status_bar, &status_info, &p_file, pv->layout.file_w, pv->layout.text_h);
    pv->w_mute_on = esp_vui_image_widget_init(pv->status_bar, &pv->icon_mute_on, &p_mute);
    pv->w_mute_off = esp_vui_image_widget_init(pv->status_bar, &pv->icon_mute_off, &p_mute);
    pv->w_volume = esp_vui_image_widget_init(pv->status_bar, &pv->volume_img, &p_volume);
    pv->w_center_play = esp_vui_image_widget_init(pv->center_layer, &pv->icon_center_play, &p_center);
    pv->w_fps = esp_vui_text_widget_init(pv->fps_layer, &fps_info, &p_fps, fps_w, fps_h);

    if (!(pv->w_btn_play && pv->w_btn_pause && pv->w_progress && pv->w_time && pv->w_filename &&
          pv->w_mute_on && pv->w_mute_off && pv->w_volume && pv->w_center_play && pv->w_fps)) {
        video_player_view_deinit(pv);
        return -1;
    }
    if (cfg->font_data) {
        (void)esp_vui_text_widget_set_font_from_mem(pv->w_time, "Default", cfg->font_data, cfg->font_data_size, pv->layout.font_time);
        (void)esp_vui_text_widget_set_font_from_mem(pv->w_filename, "Default", cfg->font_data, cfg->font_data_size, pv->layout.font_file);
        (void)esp_vui_text_widget_set_font_from_mem(pv->w_fps, "Default", cfg->font_data, cfg->font_data_size, pv->layout.font_fps);
    } else if (cfg->font_path) {
        (void)esp_vui_text_widget_set_font(pv->w_time, cfg->font_path, pv->layout.font_time);
        (void)esp_vui_text_widget_set_font(pv->w_filename, cfg->font_path, pv->layout.font_file);
        (void)esp_vui_text_widget_set_font(pv->w_fps, cfg->font_path, pv->layout.font_fps);
    }
    esp_video_render_clr_t bar_bg = {.r = 24, .g = 28, .b = 36};
    esp_video_render_clr_t white = {.r = 255, .g = 255, .b = 255};
    esp_video_render_clr_t black = {.r = 0, .g = 0, .b = 0};
    esp_video_render_clr_t trans = {.r = 0, .g = 0, .b = 0};

    esp_vui_text_widget_set_text_color(pv->w_time, &white);
    esp_vui_text_widget_set_text_color(pv->w_filename, &white);
    esp_vui_text_widget_set_text_color(pv->w_fps, &white);

    /* Keep text background opaque to prevent glyph overlap on frequent updates. */
    esp_vui_text_widget_set_bg_color(pv->w_time, &bar_bg, false);
    esp_vui_text_widget_set_bg_color(pv->w_filename, &bar_bg, false);
    esp_vui_text_widget_set_bg_color(pv->w_fps, &black, true);

    esp_vui_text_widget_set_align(pv->w_time, 1, 1);
    esp_vui_text_widget_set_align(pv->w_filename, 0, 1);
    esp_vui_text_widget_set_align(pv->w_fps, 2, 1);

    esp_vui_image_widget_set_transparent_color(pv->w_center_play, true, &trans);
    esp_vui_image_widget_set_transparent_color(pv->w_volume, true, &trans);
    esp_vui_image_widget_set_transparent_color(pv->w_progress, true, &trans);

    (void)esp_vui_container_set_bg_color(pv->status_bar, &bar_bg);

    video_player_view_set_file_name(pv, (cfg->file_name && cfg->file_name[0]) ? cfg->file_name : "video");
    video_player_view_set_fps(pv, 0.0f);

    update_progress_and_time(pv);
    draw_volume_image(pv);

    esp_vui_container_notify_compose_changed(pv->status_bar, &pv->w_volume->rect, true);

    touch_controls(pv);

    *out_handle = pv;
    return 0;
}
