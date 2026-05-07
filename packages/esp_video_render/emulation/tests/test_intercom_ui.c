/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "assets_path.h"
#include "esp_video_render.h"
#include "esp_video_render_backend.h"
#include "esp_vui_container.h"
#include "esp_vui_overlay.h"
#include "esp_vui_widget.h"
#include "esp_vui_widget_default.h"
#include "esp_video_render_utils.h"
#include "sdl_backend.h"
#include "widget/esp_vui_image_widget.h"
#include "widget/esp_vui_text_widget.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX  4096
#endif  /* PATH_MAX */

#define BASE_W  720
#define BASE_H  576

/**
 * Emulation test: video intercom contact + incoming call UI with JPEG icons
 *  and blend_transparent_color compositing.
 *  Dumps frames via sdl_backend_dump_jpg for visual.
 */

static esp_video_render_img_t s_ic_call;
static esp_video_render_img_t s_acc;
static esp_video_render_img_t s_rej;

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

static void get_size(int *w, int *h)
{
    const char *s = getenv("INTERCOM_UI_SIZE");
    int sw = 0;
    int sh = 0;
    if (s && sscanf(s, "%dx%d", &sw, &sh) == 2) {
        *w = clamp_i(sw, 320, 1280);
        *h = clamp_i(sh, 240, 800);
        return;
    }
    *w = BASE_W;
    *h = BASE_H;
}

static int sx(int x, int W)
{
    return x * W / BASE_W;
}

static int sy(int y, int H)
{
    return y * H / BASE_H;
}

static void mark_dirty(esp_vui_widget_t *w)
{
    if (!w || !w->container) {
        return;
    }
    esp_vui_container_compose_lock(w->container);
    w->dirty = w->rect;
    esp_vui_container_notify_compose_changed(w->container, &w->dirty, true);
    esp_vui_container_compose_unlock(w->container);
}

static uint8_t *read_file(const char *path, int *size)
{
    FILE *p = fopen(path, "rb");
    if (!p) {
        return NULL;
    }
    fseek(p, 0, SEEK_END);
    *size = (int)ftell(p);
    if (*size <= 0) {
        fclose(p);
        return NULL;
    }
    fseek(p, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc(*size);
    if (!buf) {
        fclose(p);
        return NULL;
    }
    fread(buf, 1, *size, p);
    fclose(p);
    return buf;
}

static int load_jpeg_rgb565(void *pool, const char *path, esp_video_render_img_t *out)
{
    esp_video_render_img_t image = {};
    esp_video_render_frame_t frame = {};
    int ret = 0;
    do {
        image.info.format = ESP_VIDEO_RENDER_FORMAT_MJPEG;
        image.info.width = 0;
        image.info.height = 0;
        image.size = 0;
        image.data = read_file(path, &image.size);
        if (!image.data) {
            ret = -1;
            break;
        }
        ret = esp_video_render_decode_image(pool, &image,
                                            ESP_VIDEO_RENDER_FORMAT_RGB565, &frame);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            printf("Failed to decode image %s ret %d\n", path, ret);
            break;
        }
        out->info.format = frame.format;
        out->info.width = frame.width;
        out->info.height = frame.height;
        out->data = frame.data;
        out->size = frame.size;
    } while (0);
    // Free raw image data
    if (image.data) {
        free(image.data);
    }
    return ret;
}

static void free_img(esp_video_render_img_t *img)
{
    if (img && img->data) {
        free(img->data);
        img->data = NULL;
    }
}

static void fill_gradient_rgb565(uint8_t *buf, int w, int h, int frame)
{
    uint16_t *p = (uint16_t *)buf;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t r = (uint8_t)((x + frame * 2) * 0x1F / w);
            uint8_t g = (uint8_t)((y + frame) * 0x3F / h);
            uint8_t b = (uint8_t)((x + y + frame) * 0x1F / (w + h));
            p[y * w + x] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
}

static void get_out_path(const char *name, char *buf, size_t n)
{
    char resolved[PATH_MAX];
    const char *f = __FILE__;
    if (realpath(f, resolved)) {
        f = resolved;
    }
    const char *m = strstr(f, "/emulation/tests/");
    if (m) {
        size_t pre = (size_t)(m - f) + strlen("/emulation");
        if (pre >= n) {
            pre = n - 1;
        }
        memcpy(buf, f, pre);
        buf[pre] = '\0';
        snprintf(buf + strlen(buf), n - strlen(buf), "/%s", name);
        return;
    }
    snprintf(buf, n, "%s", name);
}

static int run_contact_scene(esp_vui_overlay_handle_t overlay, int W, int H, const char *font_path)
{
    esp_video_render_clr_t bg = {.r = 18, .g = 24, .b = 38};
    esp_video_render_clr_t row_bg = {.r = 32, .g = 42, .b = 58};
    esp_video_render_clr_t white = {.r = 245, .g = 248, .b = 255};
    esp_video_render_clr_t black = {.r = 0, .g = 0, .b = 0};

    esp_video_render_frame_info_t root_info = {
        .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        .width = (uint16_t)W,
        .height = (uint16_t)H,
    };
    esp_video_render_pos_t z = {.x = 0, .y = 0};
    esp_vui_container_handle_t root = NULL;
    assert(esp_vui_container_create(overlay, &root_info, &z, false, &root) == ESP_VIDEO_RENDER_ERR_OK);
    esp_vui_container_set_bg_color(root, &bg);

    esp_video_render_frame_info_t title_fit = {
        .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        .width = (uint16_t)sx(680, W),
        .height = (uint16_t)sy(48, H),
    };
    esp_video_render_pos_t title_pos = {.x = (uint16_t)sx(20, W), .y = (uint16_t)sy(16, H)};
    esp_vui_widget_t *wtitle =
        esp_vui_text_widget_init(root, &title_fit, &title_pos, title_fit.width, title_fit.height);
    assert(wtitle);
    if (font_path) {
        (void)esp_vui_text_widget_set_font(wtitle, font_path, sy(28, H));
    }
    esp_vui_text_widget_set_text_color(wtitle, &white);
    esp_vui_text_widget_set_bg_color(wtitle, &bg, true);
    esp_vui_text_widget_set_align(wtitle, 0, 1);
    esp_vui_text_widget_set_text(wtitle, "Contacts");

    const char *names[] = {"Alice", "Bob", "Front Door"};
    char icon_path[PATH_MAX];
    assert(get_assets_path("intercom/ic_call.jpg", icon_path, sizeof(icon_path)) == 0);

    memset(&s_ic_call, 0, sizeof(s_ic_call));
    void *pool = NULL;
    assert(esp_vui_widget_get_pool(wtitle, &pool) == ESP_VIDEO_RENDER_ERR_OK);
    assert(load_jpeg_rgb565(pool, icon_path, &s_ic_call) == 0);

    int row_h = sy(96, H);
    int gap = sy(12, H);
    int y0 = sy(72, H);

    for (int i = 0; i < 3; i++) {
        int y = y0 + i * (row_h + gap);
        esp_video_render_frame_info_t row_info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = (uint16_t)(W - sx(32, W)),
            .height = (uint16_t)row_h,
        };
        esp_video_render_pos_t row_pos = {.x = (uint16_t)sx(16, W), .y = (uint16_t)y};
        esp_vui_container_handle_t row = NULL;
        assert(esp_vui_container_create(overlay, &row_info, &row_pos, true, &row) == ESP_VIDEO_RENDER_ERR_OK);
        esp_vui_container_set_bg_color(row, &row_bg);

        esp_video_render_frame_info_t name_fit = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = (uint16_t)(row_info.width - s_ic_call.info.width - sx(40, W)),
            .height = (uint16_t)sy(36, H),
        };
        esp_video_render_pos_t name_pos = {
            .x = (uint16_t)sx(16, W),
            .y = (uint16_t)((row_h - name_fit.height) / 2),
        };
        esp_vui_widget_t *wn =
            esp_vui_text_widget_init(row, &name_fit, &name_pos, name_fit.width, name_fit.height);
        assert(wn);
        if (font_path) {
            (void)esp_vui_text_widget_set_font(wn, font_path, sy(24, H));
        }
        esp_vui_text_widget_set_text_color(wn, &white);
        esp_vui_text_widget_set_bg_color(wn, &row_bg, true);
        esp_vui_text_widget_set_align(wn, 0, 1);
        esp_vui_text_widget_set_text(wn, names[i]);

        esp_video_render_pos_t ip = {
            .x = (uint16_t)(row_info.width - s_ic_call.info.width - sx(12, W)),
            .y = (uint16_t)((row_h - s_ic_call.info.height) / 2),
        };
        esp_vui_widget_t *wi = esp_vui_image_widget_init(row, &s_ic_call, &ip);
        assert(wi);
        esp_vui_image_widget_set_transparent_color(wi, true, &black);
        mark_dirty(wn);
        mark_dirty(wi);
    }

    mark_dirty(wtitle);
    esp_vui_container_notify_compose_changed(root, NULL, true);
    return 0;
}

static int run_incoming_scene(esp_vui_overlay_handle_t overlay, int W, int H, const char *font_path)
{
    esp_video_render_clr_t dim = {.r = 12, .g = 16, .b = 24};
    esp_video_render_clr_t white = {.r = 250, .g = 252, .b = 255};
    esp_video_render_clr_t black = {.r = 0, .g = 0, .b = 0};

    esp_video_render_frame_info_t full = {
        .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        .width = (uint16_t)W,
        .height = (uint16_t)H,
    };
    esp_video_render_pos_t z = {.x = 0, .y = 0};
    esp_vui_container_handle_t layer = NULL;
    assert(esp_vui_container_create(overlay, &full, &z, false, &layer) == ESP_VIDEO_RENDER_ERR_OK);

    esp_video_render_frame_info_t tbig = {
        .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        .width = (uint16_t)sx(680, W),
        .height = (uint16_t)sy(40, H),
    };
    esp_video_render_pos_t p1 = {.x = (uint16_t)sx(20, W), .y = (uint16_t)sy(100, H)};
    esp_vui_widget_t *wt =
        esp_vui_text_widget_init(layer, &tbig, &p1, tbig.width, tbig.height);
    esp_video_render_pos_t p2 = {.x = (uint16_t)sx(20, W), .y = (uint16_t)sy(150, H)};
    esp_vui_widget_t *wp =
        esp_vui_text_widget_init(layer, &tbig, &p2, tbig.width, (uint16_t)sy(56, H));
    assert(wt && wp);
    if (font_path) {
        (void)esp_vui_text_widget_set_font(wt, font_path, sy(26, H));
        (void)esp_vui_text_widget_set_font(wp, font_path, sy(36, H));
    }
    esp_vui_text_widget_set_text_color(wt, &white);
    esp_vui_text_widget_set_text_color(wp, &white);
    esp_vui_text_widget_set_bg_color(wt, &dim, true);
    esp_vui_text_widget_set_bg_color(wp, &dim, true);
    esp_vui_text_widget_set_align(wt, 1, 1);
    esp_vui_text_widget_set_align(wp, 1, 1);
    esp_vui_text_widget_set_text(wt, "Incoming video call");
    esp_vui_text_widget_set_text(wp, "Alice");

    char pa[PATH_MAX];
    char pr[PATH_MAX];
    assert(get_assets_path("intercom/ic_accept.jpg", pa, sizeof(pa)) == 0);
    assert(get_assets_path("intercom/ic_reject.jpg", pr, sizeof(pr)) == 0);
    memset(&s_acc, 0, sizeof(s_acc));
    memset(&s_rej, 0, sizeof(s_rej));
    void *pool = NULL;
    assert(esp_vui_widget_get_pool(wt, &pool) == ESP_VIDEO_RENDER_ERR_OK);
    assert(load_jpeg_rgb565(pool, pa, &s_acc) == 0);
    assert(load_jpeg_rgb565(pool, pr, &s_rej) == 0);

    esp_video_render_pos_t pos_a = {
        .x = (uint16_t)(W / 2 - s_acc.info.width - sx(24, W)),
        .y = (uint16_t)(H - sy(140, H)),
    };
    esp_video_render_pos_t pos_r = {
        .x = (uint16_t)(W / 2 + sx(24, W)),
        .y = (uint16_t)(H - sy(140, H)),
    };
    esp_vui_widget_t *wa = esp_vui_image_widget_init(layer, &s_acc, &pos_a);
    esp_vui_widget_t *wr = esp_vui_image_widget_init(layer, &s_rej, &pos_r);
    assert(wa && wr);
    esp_vui_image_widget_set_transparent_color(wa, true, &black);
    esp_vui_image_widget_set_transparent_color(wr, true, &black);

    mark_dirty(wt);
    mark_dirty(wp);
    mark_dirty(wa);
    mark_dirty(wr);
    esp_vui_container_notify_compose_changed(layer, NULL, true);
    return 0;
}

int main(void)
{
    int W = 0;
    int H = 0;
    get_size(&W, &H);

    char font_path_buf[PATH_MAX];
    const char *font_path = NULL;
    if (get_assets_path("DejaVuSans.ttf", font_path_buf, sizeof(font_path_buf)) == 0) {
        font_path = font_path_buf;
    }

    esp_video_render_cfg_t rcfg = {.pool = NULL, .fps = 30};
    esp_video_render_handle_t render = NULL;
    assert(esp_video_render_create(&rcfg, &render) == ESP_VIDEO_RENDER_ERR_OK);

    sdl_backend_cfg_t bk = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = W, .height = H};
    esp_video_render_backend_cfg_t disp = {
        .ops = esp_video_render_get_sdl_backend(),
        .cfg = &bk,
        .cfg_size = sizeof(bk),
    };
    assert(esp_video_render_set_display(render, &disp) == ESP_VIDEO_RENDER_ERR_OK);

    esp_video_render_stream_info_t vid_info = {
        .info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565,
                 .width = (uint16_t)W,
                 .height = (uint16_t)H,
                 .fps = 30},
        .cached = false,
    };
    esp_video_render_stream_handle_t stream_vid = NULL;
    assert(esp_video_render_stream_open(render, &vid_info, &stream_vid) == ESP_VIDEO_RENDER_ERR_OK);
    esp_video_render_rect_t vr = {.x = 0, .y = 0, .width = (uint16_t)W, .height = (uint16_t)H};
    assert(esp_video_render_stream_set_disp_rect(stream_vid, &vr) == ESP_VIDEO_RENDER_ERR_OK);
    assert(esp_video_render_stream_set_zorder(stream_vid, 0) == ESP_VIDEO_RENDER_ERR_OK);
    (void)esp_video_render_stream_render_async(stream_vid);

    esp_video_render_stream_info_t ui_info = {
        .info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = (uint16_t)W, .height = (uint16_t)H, .fps = 0},
        .cached = true,
    };
    esp_video_render_stream_handle_t stream_ui = NULL;
    assert(esp_video_render_stream_open(render, &ui_info, &stream_ui) == ESP_VIDEO_RENDER_ERR_OK);
    assert(esp_video_render_stream_set_disp_rect(stream_ui, &vr) == ESP_VIDEO_RENDER_ERR_OK);
    assert(esp_video_render_stream_set_zorder(stream_ui, 2) == ESP_VIDEO_RENDER_ERR_OK);

    esp_vui_overlay_handle_t overlay = NULL;
    assert(esp_video_render_stream_get_overlay(stream_ui, &overlay) == ESP_VIDEO_RENDER_ERR_OK);

    assert(run_contact_scene(overlay, W, H, font_path) == 0);

    uint8_t *frame_buf = (uint8_t *)calloc(1, (size_t)W * (size_t)H * 2u);
    assert(frame_buf);
    for (int i = 0; i < 40; i++) {
        fill_gradient_rgb565(frame_buf, W, H, i);
        esp_video_render_frame_t frame = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = (uint16_t)W,
            .height = (uint16_t)H,
            .data = frame_buf,
            .size = (uint32_t)(W * H * 2),
        };
        assert(esp_video_render_stream_write(stream_vid, &frame) == ESP_VIDEO_RENDER_ERR_OK);
        usleep(15000);
    }

    char out_contact[PATH_MAX];
    get_out_path("intercom_contact.jpg", out_contact, sizeof(out_contact));
    assert(sdl_backend_dump_jpg(out_contact) == 0);
    printf("[intercom_ui] wrote %s\n", out_contact);

    /* Recreate UI stream to clear contact widgets before incoming scene */
    esp_video_render_stream_close(stream_ui);
    stream_ui = NULL;
    free_img(&s_ic_call);

    assert(esp_video_render_stream_open(render, &ui_info, &stream_ui) == ESP_VIDEO_RENDER_ERR_OK);
    assert(esp_video_render_stream_set_disp_rect(stream_ui, &vr) == ESP_VIDEO_RENDER_ERR_OK);
    assert(esp_video_render_stream_set_zorder(stream_ui, 2) == ESP_VIDEO_RENDER_ERR_OK);
    assert(esp_video_render_stream_get_overlay(stream_ui, &overlay) == ESP_VIDEO_RENDER_ERR_OK);

    assert(run_incoming_scene(overlay, W, H, font_path) == 0);
    for (int i = 0; i < 25; i++) {
        fill_gradient_rgb565(frame_buf, W, H, i + 50);
        esp_video_render_frame_t frame = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = (uint16_t)W,
            .height = (uint16_t)H,
            .data = frame_buf,
            .size = (uint32_t)(W * H * 2),
        };
        assert(esp_video_render_stream_write(stream_vid, &frame) == ESP_VIDEO_RENDER_ERR_OK);
        usleep(15000);
    }
    char out_in[PATH_MAX];
    get_out_path("intercom_incoming.jpg", out_in, sizeof(out_in));
    assert(sdl_backend_dump_jpg(out_in) == 0);
    printf("[intercom_ui] wrote %s\n", out_in);

    free(frame_buf);
    esp_video_render_stream_close(stream_ui);
    free_img(&s_acc);
    free_img(&s_rej);
    esp_video_render_stream_close(stream_vid);
    esp_video_render_destroy(render);
    return 0;
}
