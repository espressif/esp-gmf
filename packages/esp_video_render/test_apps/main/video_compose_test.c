/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_video_render.h"
#include "esp_vui_overlay.h"
#include "video_render_blend_flow.h"
#include "video_render_internal.h"
#include "video_render_sys.h"

const esp_video_render_backend_ops_t *video_render_get_fake_backend(void);

#define TEST_W             320
#define TEST_H             240
#define MAX_MONITOR_DIRTY  16
#define MAX_REGIONS        32

typedef struct {
    esp_video_render_handle_t  render;
    video_render_t            *vr;
    esp_vui_overlay_rgn_t     *regions[MAX_REGIONS];
    int                        region_count;
} compose_fixture_t;

typedef struct {
    int                            call_count;
    int                            dirty_count;
    esp_video_render_dirty_rect_t  dirty[MAX_MONITOR_DIRTY];
} dirty_monitor_result_t;

static dirty_monitor_result_t g_monitor;

static void reset_monitor(void)
{
    memset(&g_monitor, 0, sizeof(g_monitor));
}

static int dirty_monitor_cb(const esp_video_render_dirty_rect_t *dirty, int filled)
{
    g_monitor.call_count++;
    g_monitor.dirty_count = filled > MAX_MONITOR_DIRTY ? MAX_MONITOR_DIRTY : filled;
    if (dirty && g_monitor.dirty_count > 0) {
        memcpy(g_monitor.dirty, dirty, g_monitor.dirty_count * sizeof(g_monitor.dirty[0]));
    }
    return 0;
}

static int dirty_rect_cmp(const void *a, const void *b)
{
    const esp_video_render_dirty_rect_t *lhs = (const esp_video_render_dirty_rect_t *)a;
    const esp_video_render_dirty_rect_t *rhs = (const esp_video_render_dirty_rect_t *)b;
    if (lhs->rect.x != rhs->rect.x) {
        return lhs->rect.x - rhs->rect.x;
    }
    if (lhs->rect.y != rhs->rect.y) {
        return lhs->rect.y - rhs->rect.y;
    }
    if (lhs->rect.width != rhs->rect.width) {
        return lhs->rect.width - rhs->rect.width;
    }
    if (lhs->rect.height != rhs->rect.height) {
        return lhs->rect.height - rhs->rect.height;
    }
    return (int)lhs->opaque - (int)rhs->opaque;
}

static void dump_dirty(const char *prefix, const esp_video_render_dirty_rect_t *dirty, int count)
{
    printf("%s count=%d\n", prefix, count);
    for (int i = 0; i < count; ++i) {
        printf("  [%d] %d-%d %dx%d opaque=%d\n",
               i,
               dirty[i].rect.x, dirty[i].rect.y,
               dirty[i].rect.width, dirty[i].rect.height,
               dirty[i].opaque);
    }
}

static int expect_monitor(const char *label,
                          const esp_video_render_dirty_rect_t *expected,
                          int expected_count)
{
    esp_video_render_dirty_rect_t actual_sorted[MAX_MONITOR_DIRTY];
    esp_video_render_dirty_rect_t expect_sorted[MAX_MONITOR_DIRTY];

    if (expected_count == 0) {
        if (g_monitor.call_count != 0) {
            printf("[compose][%s] expected no callback but got %d\n", label, g_monitor.call_count);
            dump_dirty("actual", g_monitor.dirty, g_monitor.dirty_count);
            return -1;
        }
        return 0;
    }

    if (g_monitor.call_count != 1) {
        printf("[compose][%s] expected 1 callback, got %d\n", label, g_monitor.call_count);
        dump_dirty("actual", g_monitor.dirty, g_monitor.dirty_count);
        return -1;
    }
    if (g_monitor.dirty_count != expected_count) {
        printf("[compose][%s] expected dirty_count=%d got=%d\n", label, expected_count, g_monitor.dirty_count);
        dump_dirty("actual", g_monitor.dirty, g_monitor.dirty_count);
        dump_dirty("expect", expected, expected_count);
        return -1;
    }

    memcpy(actual_sorted, g_monitor.dirty, expected_count * sizeof(actual_sorted[0]));
    memcpy(expect_sorted, expected, expected_count * sizeof(expect_sorted[0]));
    qsort(actual_sorted, expected_count, sizeof(actual_sorted[0]), dirty_rect_cmp);
    qsort(expect_sorted, expected_count, sizeof(expect_sorted[0]), dirty_rect_cmp);
    for (int i = 0; i < expected_count; ++i) {
        if (actual_sorted[i].rect.x != expect_sorted[i].rect.x ||
            actual_sorted[i].rect.y != expect_sorted[i].rect.y ||
            actual_sorted[i].rect.width != expect_sorted[i].rect.width ||
            actual_sorted[i].rect.height != expect_sorted[i].rect.height ||
            actual_sorted[i].opaque != expect_sorted[i].opaque) {
            printf("[compose][%s] dirty mismatch at index %d\n", label, i);
            dump_dirty("actual", actual_sorted, expected_count);
            dump_dirty("expect", expect_sorted, expected_count);
            return -1;
        }
    }
    return 0;
}

static int run_blend_and_expect(compose_fixture_t *fx,
                                const char *label,
                                const esp_video_render_dirty_rect_t *expected,
                                int expected_count)
{
    reset_monitor();
    video_render_mutex_lock(fx->vr->render_mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    esp_video_render_err_t ret = video_render_blend_execute(fx->vr, &fx->vr->backend);
    video_render_mutex_unlock(fx->vr->render_mutex);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        printf("[compose][%s] blend failed ret=%d\n", label, ret);
        return -1;
    }
    return expect_monitor(label, expected, expected_count);
}

static video_render_stream_t *append_stream(compose_fixture_t *fx,
                                            const esp_video_render_rect_t *rect,
                                            bool with_video,
                                            bool opaque,
                                            uint16_t color)
{
    video_render_stream_t *stream = (video_render_stream_t *)video_render_calloc(1, sizeof(*stream));
    if (!stream) {
        return NULL;
    }
    stream->video_render = fx->vr;
    stream->running = true;
    stream->compose.visible = true;
    stream->compose.alpha = 255;
    stream->compose.opaque = opaque;
    stream->compose.disp_rect = *rect;
    stream->compose.zorder = fx->vr->active_stream_num;
    stream->compose.is_fresh = with_video;
    stream->compose.is_empty = with_video ? false : true;
    stream->mutex = video_render_mutex_create();
    if (!stream->mutex) {
        video_render_free(stream);
        return NULL;
    }
    if (with_video) {
        stream->frame_info.format = ESP_VIDEO_RENDER_FORMAT_RGB565;
        stream->frame_info.width = rect->width;
        stream->frame_info.height = rect->height;
        stream->fb.info.format = ESP_VIDEO_RENDER_FORMAT_RGB565;
        stream->fb.info.width = rect->width;
        stream->fb.info.height = rect->height;
        stream->fb.size = (uint32_t)(rect->width * rect->height * 2);
        stream->fb.data = (uint8_t *)video_render_malloc_align(stream->fb.size, 64);
        if (!stream->fb.data) {
            video_render_mutex_destroy(stream->mutex);
            video_render_free(stream);
            return NULL;
        }
        // Route the manually allocated test framebuffer through the normal
        // stream-close ownership path so teardown frees it on both ESP32 and emulation.
        stream->cached_data = stream->fb.data;
        uint16_t *pix = (uint16_t *)stream->fb.data;
        for (uint32_t i = 0; i < stream->fb.size / 2; ++i) {
            pix[i] = color;
        }
    }

    if (!fx->vr->stream_list) {
        fx->vr->stream_list = stream;
    } else {
        video_render_stream_t *tail = fx->vr->stream_list;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = stream;
    }
    fx->vr->active_stream_num++;
    return stream;
}

static void stream_move(video_render_stream_t *stream, const esp_video_render_rect_t *rect)
{
    if (stream->fb.data && stream->compose.visible) {
        stream->compose.prev_rect = stream->compose.disp_rect;
    }
    stream->compose.disp_rect = *rect;
    if (stream->fb.data) {
        stream->compose.is_fresh = true;
    }
}

static void stream_set_visible_local(video_render_stream_t *stream, bool visible)
{
    stream->compose.visible = visible;
    if (stream->fb.data) {
        stream->compose.is_fresh = true;
    }
}

static void stream_simulate_close(video_render_stream_t *stream)
{
    if (stream->fb.data && stream->compose.visible) {
        stream->compose.prev_rect = stream->compose.disp_rect;
    }
    stream->compose.visible = false;
    stream->compose.is_fresh = true;
}

static esp_vui_overlay_rgn_t *add_region(compose_fixture_t *fx,
                                         esp_vui_overlay_handle_t overlay,
                                         const esp_video_render_rect_t *rect,
                                         bool opaque,
                                         bool visible)
{
    if (fx->region_count >= MAX_REGIONS) {
        return NULL;
    }
    esp_vui_overlay_rgn_t *region = (esp_vui_overlay_rgn_t *)video_render_calloc(1, sizeof(*region));
    if (!region) {
        return NULL;
    }
    region->compose.visible = visible;
    region->compose.alpha = 255;
    region->compose.opaque = opaque;
    region->compose.disp_rect = *rect;
    region->frame.width = rect->width;
    region->frame.height = rect->height;
    region->frame.format = ESP_VIDEO_RENDER_FORMAT_RGB565;
    if (esp_vui_overlay_add_region(overlay, region) != ESP_VIDEO_RENDER_ERR_OK) {
        video_render_free(region);
        return NULL;
    }
    fx->regions[fx->region_count++] = region;
    return region;
}

static int init_fixture(compose_fixture_t *fx)
{
    memset(fx, 0, sizeof(*fx));
    esp_video_render_cfg_t cfg = {
        .pool = NULL,
        .fps = 30,
    };
    if (esp_video_render_create(&cfg, &fx->render) != ESP_VIDEO_RENDER_ERR_OK) {
        return -1;
    }
    fx->vr = (video_render_t *)fx->render;

    esp_video_render_lcd_cfg_t backend_cfg_raw = {
        .out_format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        .width = TEST_W,
        .height = TEST_H,
    };
    esp_video_render_backend_cfg_t backend_cfg = {
        .ops = video_render_get_fake_backend(),
        .cfg = &backend_cfg_raw,
        .cfg_size = sizeof(backend_cfg_raw),
    };
    if (esp_video_render_set_display(fx->render, &backend_cfg) != ESP_VIDEO_RENDER_ERR_OK) {
        esp_video_render_destroy(fx->render);
        memset(fx, 0, sizeof(*fx));
        return -1;
    }
    video_render_set_dirty_monitor_cb(fx->vr, dirty_monitor_cb);
    return 0;
}

static void cleanup_fixture(compose_fixture_t *fx)
{
    if (fx->render) {
        esp_video_render_destroy(fx->render);
    }
    for (int i = 0; i < fx->region_count; ++i) {
        video_render_free(fx->regions[i]);
    }
    memset(fx, 0, sizeof(*fx));
}

static int case_single_stream_rect_changes(void)
{
    compose_fixture_t fx;
    if (init_fixture(&fx) != 0) {
        return -1;
    }
    int ret = -1;
    do {
        const esp_video_render_rect_t r0 = {.x = 10, .y = 10, .width = 40, .height = 30};
        const esp_video_render_rect_t r1 = {.x = 100, .y = 20, .width = 40, .height = 30};
        const esp_video_render_rect_t r2 = {.x = 110, .y = 20, .width = 40, .height = 30};
        video_render_stream_t *stream = append_stream(&fx, &r0, true, true, 0xF800);
        if (!stream) {
            break;
        }

        {
            const esp_video_render_dirty_rect_t expect[] = {
                {{10, 10, 40, 30}, true},
            };
            if (run_blend_and_expect(&fx, "single-initial", expect, 1) != 0) {
                break;
            }
        }
        if (run_blend_and_expect(&fx, "single-idle", NULL, 0) != 0) {
            break;
        }

        stream_move(stream, &r1);
        {
            const esp_video_render_dirty_rect_t expect[] = {
                {{10, 10, 40, 30}, false},
                {{100, 20, 40, 30}, true},
            };
            if (run_blend_and_expect(&fx, "single-move-nonoverlap", expect, 2) != 0) {
                break;
            }
        }

        stream_move(stream, &r2);
        {
            const esp_video_render_dirty_rect_t expect[] = {
                {{100, 20, 50, 30}, false},
            };
            if (run_blend_and_expect(&fx, "single-move-overlap", expect, 1) != 0) {
                break;
            }
        }

        stream_set_visible_local(stream, false);
        {
            const esp_video_render_dirty_rect_t expect[] = {
                {{110, 20, 40, 30}, false},
            };
            if (run_blend_and_expect(&fx, "single-hide", expect, 1) != 0) {
                break;
            }
        }

        stream_set_visible_local(stream, true);
        {
            const esp_video_render_dirty_rect_t expect[] = {
                {{110, 20, 40, 30}, true},
            };
            if (run_blend_and_expect(&fx, "single-show", expect, 1) != 0) {
                break;
            }
        }

        stream_simulate_close(stream);
        {
            const esp_video_render_dirty_rect_t expect[] = {
                {{110, 20, 40, 30}, false},
            };
            if (run_blend_and_expect(&fx, "single-close", expect, 1) != 0) {
                break;
            }
        }
        ret = 0;
    } while (0);
    cleanup_fixture(&fx);
    return ret;
}

static int case_two_stream_layout_and_close(void)
{
    int ret = -1;
    compose_fixture_t fx;

    if (init_fixture(&fx) != 0) {
        return -1;
    }
    do {
        const esp_video_render_rect_t a = {.x = 10, .y = 10, .width = 60, .height = 50};
        const esp_video_render_rect_t b = {.x = 120, .y = 20, .width = 60, .height = 50};
        video_render_stream_t *s0 = append_stream(&fx, &a, true, true, 0xF800);
        video_render_stream_t *s1 = append_stream(&fx, &b, true, true, 0x07E0);
        if (!s0 || !s1) {
            break;
        }
        {
            const esp_video_render_dirty_rect_t expect[] = {
                {{10, 10, 60, 50}, true},
                {{120, 20, 60, 50}, true},
            };
            if (run_blend_and_expect(&fx, "dual-nonoverlap", expect, 2) != 0) {
                break;
            }
        }
        stream_simulate_close(s1);
        {
            const esp_video_render_dirty_rect_t expect[] = {
                {{120, 20, 60, 50}, false},
            };
            if (run_blend_and_expect(&fx, "dual-close-one", expect, 1) != 0) {
                break;
            }
        }
        ret = 0;
    } while (0);
    cleanup_fixture(&fx);
    if (ret != 0) {
        return ret;
    }

    if (init_fixture(&fx) != 0) {
        return -1;
    }
    do {
        const esp_video_render_rect_t a = {.x = 10, .y = 10, .width = 60, .height = 60};
        const esp_video_render_rect_t b = {.x = 50, .y = 20, .width = 60, .height = 60};
        ret = -1;
        if (!append_stream(&fx, &a, true, true, 0xF800) ||
            !append_stream(&fx, &b, true, true, 0x07E0)) {
            break;
        }
        {
            const esp_video_render_dirty_rect_t expect[] = {
                {{10, 10, 100, 70}, false},
            };
            if (run_blend_and_expect(&fx, "dual-overlap-partial", expect, 1) != 0) {
                break;
            }
        }
        ret = 0;
    } while (0);
    cleanup_fixture(&fx);
    if (ret != 0) {
        return ret;
    }

    if (init_fixture(&fx) != 0) {
        return -1;
    }
    do {
        const esp_video_render_rect_t a = {.x = 10, .y = 10, .width = 100, .height = 100};
        const esp_video_render_rect_t b = {.x = 30, .y = 30, .width = 20, .height = 20};
        ret = -1;
        if (!append_stream(&fx, &a, true, true, 0xF800) ||
            !append_stream(&fx, &b, true, true, 0x07E0)) {
            break;
        }
        {
            const esp_video_render_dirty_rect_t expect[] = {
                {{10, 10, 100, 100}, true},
            };
            if (run_blend_and_expect(&fx, "dual-overlap-contained", expect, 1) != 0) {
                break;
            }
        }
        ret = 0;
    } while (0);
    cleanup_fixture(&fx);
    return ret;
}

static int case_overlay_region_state_changes(void)
{
    compose_fixture_t fx;
    if (init_fixture(&fx) != 0) {
        return -1;
    }
    int ret = -1;
    do {
        const esp_video_render_rect_t stream_rect = {.x = 0, .y = 0, .width = TEST_W, .height = TEST_H};
        video_render_stream_t *stream = append_stream(&fx, &stream_rect, false, false, 0);
        if (!stream) {
            break;
        }
        esp_vui_overlay_handle_t overlay = NULL;
        if (esp_vui_overlay_create(&(esp_vui_overlay_cfg_t) {.stream = stream, .render = fx.vr}, &overlay) != ESP_VIDEO_RENDER_ERR_OK) {
            break;
        }
        stream->overlay = overlay;

        const esp_video_render_rect_t r1 = {.x = 10, .y = 10, .width = 40, .height = 40};
        const esp_video_render_rect_t r2 = {.x = 70, .y = 10, .width = 30, .height = 30};
        const esp_video_render_rect_t r3 = {.x = 10, .y = 70, .width = 50, .height = 50};
        const esp_video_render_rect_t r4 = {.x = 40, .y = 90, .width = 40, .height = 40};
        esp_vui_overlay_rgn_t *rg1 = add_region(&fx, overlay, &r1, true, true);
        esp_vui_overlay_rgn_t *rg2 = add_region(&fx, overlay, &r2, false, true);
        esp_vui_overlay_rgn_t *rg3 = add_region(&fx, overlay, &r3, true, true);
        esp_vui_overlay_rgn_t *rg4 = add_region(&fx, overlay, &r4, false, true);
        if (!rg1 || !rg2 || !rg3 || !rg4) {
            break;
        }

        {
            const esp_video_render_dirty_rect_t expect[] = {
                {{10, 10, 40, 40}, true},
                {{70, 10, 30, 30}, false},
                {{10, 70, 70, 60}, false},
            };
            if (run_blend_and_expect(&fx, "overlay-initial", expect, 3) != 0) {
                break;
            }
        }

        if (esp_vui_overlay_remove_region(overlay, rg2) != ESP_VIDEO_RENDER_ERR_OK) {
            break;
        }
        {
            const esp_video_render_dirty_rect_t expect[] = {
                {{70, 10, 30, 30}, false},
            };
            if (run_blend_and_expect(&fx, "overlay-remove", expect, 1) != 0) {
                break;
            }
        }

        rg1->compose.visible = false;
        if (esp_vui_overlay_mark_dirty(overlay) != ESP_VIDEO_RENDER_ERR_OK) {
            break;
        }
        {
            const esp_video_render_dirty_rect_t expect[] = {
                {{10, 10, 40, 40}, false},
            };
            if (run_blend_and_expect(&fx, "overlay-hide", expect, 1) != 0) {
                break;
            }
        }

        {
            const esp_video_render_rect_t moved = {.x = 20, .y = 75, .width = 50, .height = 50};
            if (esp_vui_overlay_update_region(overlay, rg3, (esp_video_render_rect_t *)&moved) != ESP_VIDEO_RENDER_ERR_OK) {
                break;
            }
            const esp_video_render_dirty_rect_t expect[] = {
                {{10, 70, 60, 55}, false},
            };
            if (run_blend_and_expect(&fx, "overlay-move", expect, 1) != 0) {
                break;
            }
        }
        ret = 0;
    } while (0);
    cleanup_fixture(&fx);
    return ret;
}

static int case_dual_stream_four_region_matrix(void)
{
    compose_fixture_t fx;
    if (init_fixture(&fx) != 0) {
        return -1;
    }
    int ret = -1;
    do {
        const esp_video_render_rect_t stream_rect = {.x = 0, .y = 0, .width = TEST_W, .height = TEST_H};
        video_render_stream_t *stream_a = append_stream(&fx, &stream_rect, false, false, 0);
        video_render_stream_t *stream_b = append_stream(&fx, &stream_rect, false, false, 0);
        if (!stream_a || !stream_b) {
            break;
        }
        esp_vui_overlay_handle_t overlay_a = NULL;
        esp_vui_overlay_handle_t overlay_b = NULL;
        if (esp_vui_overlay_create(&(esp_vui_overlay_cfg_t) {.stream = stream_a, .render = fx.vr}, &overlay_a) != ESP_VIDEO_RENDER_ERR_OK ||
            esp_vui_overlay_create(&(esp_vui_overlay_cfg_t) {.stream = stream_b, .render = fx.vr}, &overlay_b) != ESP_VIDEO_RENDER_ERR_OK) {
            break;
        }
        stream_a->overlay = overlay_a;
        stream_b->overlay = overlay_b;

        if (!add_region(&fx, overlay_a, &(esp_video_render_rect_t) {.x = 10, .y = 10, .width = 60, .height = 60}, true, true) ||
            !add_region(&fx, overlay_a, &(esp_video_render_rect_t) {.x = 20, .y = 20, .width = 20, .height = 20}, true, true) ||
            !add_region(&fx, overlay_a, &(esp_video_render_rect_t) {.x = 40, .y = 40, .width = 40, .height = 40}, false, true) ||
            !add_region(&fx, overlay_a, &(esp_video_render_rect_t) {.x = 140, .y = 20, .width = 30, .height = 30}, true, true) ||
            !add_region(&fx, overlay_b, &(esp_video_render_rect_t) {.x = 160, .y = 100, .width = 70, .height = 50}, true, true) ||
            !add_region(&fx, overlay_b, &(esp_video_render_rect_t) {.x = 170, .y = 110, .width = 20, .height = 20}, false, true) ||
            !add_region(&fx, overlay_b, &(esp_video_render_rect_t) {.x = 250, .y = 120, .width = 30, .height = 30}, true, true) ||
            !add_region(&fx, overlay_b, &(esp_video_render_rect_t) {.x = 255, .y = 125, .width = 10, .height = 10}, true, true)) {
            break;
        }

        {
            const esp_video_render_dirty_rect_t expect[] = {
                {{10, 10, 70, 70}, false},
                {{140, 20, 30, 30}, true},
                {{160, 100, 70, 50}, false},
                {{250, 120, 30, 30}, true},
            };
            if (run_blend_and_expect(&fx, "dual-stream-four-region-matrix", expect, 4) != 0) {
                break;
            }
        }
        ret = 0;
    } while (0);
    cleanup_fixture(&fx);
    return ret;
}

int video_render_compose_monitor_test(void)
{
    if (case_single_stream_rect_changes() != 0) {
        return -1;
    }
    if (case_two_stream_layout_and_close() != 0) {
        return -1;
    }
    if (case_overlay_region_state_changes() != 0) {
        return -1;
    }
    if (case_dual_stream_four_region_matrix() != 0) {
        return -1;
    }
    return 0;
}
