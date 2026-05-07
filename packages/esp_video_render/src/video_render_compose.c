/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "video_render_compose.h"
#include "esp_video_render_log.h"

static bool dirty_all_opaque(const esp_video_render_dirty_rect_t *dirty, int filled)
{
    for (int i = 0; i < filled; ++i) {
        if (dirty[i].opaque == false) {
            return false;
        }
    }
    return filled > 0;
}

int merge_dirty_rect(esp_video_render_dirty_rect_t *dirty, int filled, int limit,
                     esp_video_render_rect_t *new_rect,
                     bool opaque)
{
    if (filled == 0) {
        dirty[filled].rect = *new_rect;
        dirty[filled].opaque = opaque;
        filled++;
        return filled;
    }
    for (int i = 0; i < filled; ++i) {
        bool is_in = false;
        if (rect_intersect(&dirty[i].rect, new_rect, NULL, &is_in)) {
            if (is_in) {
                dirty[i].opaque &= opaque;
                // already contain in list do nothing
                return filled;
            }
            bool covered_by_opaque = opaque;
            if (covered_by_opaque && rect_in(&dirty[i].rect, new_rect)) {
                dirty[i].rect = *new_rect;
                dirty[i].opaque = true;
            } else if (dirty[i].opaque && rect_in(new_rect, &dirty[i].rect)) {
                covered_by_opaque = true;
            } else {
                dirty[i].rect = rect_union(&dirty[i].rect, new_rect);
                covered_by_opaque = false;
            }
            dirty[i].opaque = covered_by_opaque;
            // After union, check if this merged rect intersects any other rects
            for (int j = 0; j < filled; ++j) {
                if (i != j && rect_intersect(&dirty[i].rect, &dirty[j].rect, NULL, NULL)) {
                    if (covered_by_opaque && rect_in(&dirty[j].rect, &dirty[i].rect)) {
                    } else if (dirty[j].opaque && rect_in(&dirty[i].rect, &dirty[j].rect)) {
                        dirty[i] = dirty[j];
                        covered_by_opaque = true;
                    } else {
                        dirty[i].rect = rect_union(&dirty[i].rect, &dirty[j].rect);
                        covered_by_opaque = false;
                    }
                    dirty[i].opaque = covered_by_opaque;
                    // Remove jth rect by shifting left
                    for (int k = j; k < filled - 1; ++k) {
                        dirty[k] = dirty[k + 1];
                    }
                    filled--;
                    if (j < i) {
                        --i;  // adjust i if removed before
                    }
                    --j;
                }
            }
            return filled;
        }
    }

    // No intersection, append new rect if space allows
    if (filled < limit) {
        dirty[filled].rect = *new_rect;
        dirty[filled].opaque = opaque;
        filled++;
        return filled;
    }

    // Exceeded max_count, fallback: merge everything into first rect
    dirty[0].rect = rect_union(&dirty[0].rect, new_rect);
    dirty[0].opaque = false;
    for (int i = 1; i < filled; ++i) {
        if (rect_in(&dirty[i].rect, &dirty[0].rect)) {
            for (int j = i; j < filled - 1; ++j) {
                dirty[j] = dirty[j + 1];
            }
            filled--;
            i--;
        }
    }
    return filled;
}

int merge_compose_dirty_rect(video_render_compose_t *compose,
                             const esp_video_render_rect_t *new_rect,
                             bool opaque)
{
    esp_video_render_rect_t rect = *new_rect;
    int filled = merge_dirty_rect(compose->dirty_area, compose->dirty_count, VIDEO_RENDER_COMPOSE_MAX_DIRTY_AREA,
                                  &rect, opaque);
    compose->dirty_count = filled;
    compose->opaque = dirty_all_opaque(compose->dirty_area, filled);
    return filled;
}

int video_compose_calc_dirty_area(video_render_compose_t *first_compose,
                                  video_render_next_compose_cb get_next,
                                  esp_video_render_dirty_rect_t *dirty_area,
                                  uint8_t filled,
                                  uint8_t limit_count)
{
    for (video_render_compose_t *cur = first_compose; cur; cur = get_next(cur)) {
        video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "    visible %d alpha %d fresh %d\n",
                            cur->visible, cur->alpha, cur->is_fresh);

        // Always treat prev_rect as a removed region that needs to be cleared
        // This handles cases where regions move (prev_rect = old position needs clearing)
        if (cur->prev_rect.width > 0 && cur->prev_rect.height > 0) {
            // Removed region - treat as non-opaque and add to dirty area
            filled = merge_dirty_rect(dirty_area, filled, limit_count, &cur->prev_rect, false);
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "    prev_rect added to dirty: %d-%d %dx%d\n",
                                cur->prev_rect.x, cur->prev_rect.y, cur->prev_rect.width, cur->prev_rect.height);
            cur->prev_rect.width = 0;
            cur->prev_rect.height = 0;
            if (cur->visible == false) {
                continue;
            }
        }

        if (cur->is_empty) {
            // No need to calc for region no data
            continue;
        }
        if (cur->visible == false) {
            if (cur->is_visible) {
                filled = merge_dirty_rect(dirty_area, filled, limit_count, &cur->disp_rect, false);
            }
            continue;
        }
        // If not visible and already hide do nothing
        if (cur->alpha == 0) {
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "    invisible\n");
            cur->dirty_count = 0;
            continue;
        }
        // Has fresh content
        if (cur->is_fresh) {
            // Fresh content - add new position to dirty area
            cur->dirty_count = 1;
            cur->dirty_area[0].rect = cur->disp_rect;
            cur->dirty_area[0].opaque = cur->opaque & (cur->alpha == 255);
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "    Fresh dirty set: %d-%d %dx%d\n",
                                cur->disp_rect.x, cur->disp_rect.y, cur->disp_rect.width, cur->disp_rect.height);
        }
        video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "  stream %p still have %d filled:%d\n", cur, cur->dirty_count, filled);
        for (int i = 0; i < cur->dirty_count; ++i) {
            esp_video_render_rect_t *rect = &cur->dirty_area[i].rect;
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "  %d cur: %d-%d %dx%d\n", i, rect->x, rect->y, rect->width, rect->height);
            bool opaque = cur->dirty_area[i].opaque && (cur->alpha == 255);
            filled = merge_dirty_rect(dirty_area, filled, limit_count, &cur->dirty_area[i].rect, opaque);
        }
    }
    return filled;
}
