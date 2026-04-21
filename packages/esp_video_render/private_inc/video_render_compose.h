/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "video_render_sys.h"
#include "esp_video_render_types.h"
#include "esp_vui_overlay.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Next compose callback function type
 *
 * @param[in]  cur  Current compose structure
 *
 * @return
 *       - Next  compose structure pointer
 *       - NULL  if no more
 */
typedef video_render_compose_t *(*video_render_next_compose_cb)(video_render_compose_t *cur);

/**
 * @brief  Check if rectangle a is inside rectangle b
 *
 * @param[in]  a  First rectangle
 * @param[in]  b  Second rectangle
 *
 * @return
 *       - true   Rectangle a is inside rectangle b
 *       - false  Rectangle a is not inside rectangle b
 */
static inline bool rect_in(const esp_video_render_rect_t *a, const esp_video_render_rect_t *b)
{
    return (b->x <= a->x && b->y <= a->y && b->x + b->width >= a->x + a->width && b->y + b->height >= a->y + a->height);
}

/**
 * @brief  Check if two rectangles are equal
 *
 * @param[in]  a  First rectangle
 * @param[in]  b  Second rectangle
 *
 * @return
 *       - true   Rectangles are equal
 *       - false  Rectangles are not equal
 */
static inline bool rect_equal(const esp_video_render_rect_t *a, const esp_video_render_rect_t *b)
{
    return (a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height);
}

/**
 * @brief  Calculate union of two rectangles
 *
 * @param[in]  a  First rectangle
 * @param[in]  b  Second rectangle
 *
 * @return
 */
static inline esp_video_render_rect_t rect_union(esp_video_render_rect_t *a, esp_video_render_rect_t *b)
{
    esp_video_render_rect_t r;
    if (a->width == 0 || a->height == 0) {
        return *b;
    }
    if (b->width == 0 || b->height == 0) {
        return *a;
    }
    int x1 = a->x < b->x ? a->x : b->x;
    int y1 = a->y < b->y ? a->y : b->y;
    int x2 = (a->x + a->width) > (b->x + b->width) ? (a->x + a->width) : (b->x + b->width);
    int y2 = (a->y + a->height) > (b->y + b->height) ? (a->y + a->height) : (b->y + b->height);
    r.x = x1;
    r.y = y1;
    r.width = x2 - x1;
    r.height = y2 - y1;
    return r;
}

/**
 * @brief  Calculate intersection of two rectangles
 *
 * @param[in]   a      First rectangle
 * @param[in]   b      Second rectangle
 * @param[out]  out    Intersection rectangle (can be NULL)
 * @param[out]  is_in  Whether rectangle b is inside rectangle a (can be NULL)
 *
 * @return
 *       - true   Rectangles intersect
 *       - false  Rectangles do not intersect
 */
static inline bool rect_intersect(const esp_video_render_rect_t *a, const esp_video_render_rect_t *b, esp_video_render_rect_t *out, bool *is_in)
{
    bool in_a = rect_in(b, a);
    if (is_in) {
        *is_in = in_a;
    }
    if (in_a) {
        if (out) {
            *out = *b;
        }
        return true;
    }
    int x1 = a->x > b->x ? a->x : b->x;
    int y1 = a->y > b->y ? a->y : b->y;
    int x2 = (a->x + a->width) < (b->x + b->width) ? (a->x + a->width) : (b->x + b->width);
    int y2 = (a->y + a->height) < (b->y + b->height) ? (a->y + a->height) : (b->y + b->height);
    if (x2 > x1 && y2 > y1) {
        if (out) {
            out->x = x1;
            out->y = y1;
            out->width = x2 - x1;
            out->height = y2 - y1;
        }
        return true;
    }
    return false;
}

/**
 * @brief  Merge dirty rectangle into dirty rectangle array
 *
 * @param[in,out]  dirty     Dirty rectangle array
 * @param[in]      filled    Number of filled rectangles
 * @param[in]      limit     Maximum number of rectangles
 * @param[in]      new_rect  New rectangle to merge
 * @param[in]      opaque    Whether rectangle is opaque
 * @return
 *       - Number  of rectangles after merge
 */
int merge_dirty_rect(esp_video_render_dirty_rect_t *dirty, int filled, int limit,
                     esp_video_render_rect_t *new_rect,
                     bool opaque);

/**
 * @brief  Merge a dirty rectangle into compose dirty tracking
 *
 * @param[in,out]  compose   Compose information
 * @param[in]      new_rect  New rectangle to merge
 * @param[in]      opaque    Whether rectangle is opaque
 * @return
 *       - Number  of rectangles after merge
 */
int merge_compose_dirty_rect(video_render_compose_t *compose,
                             const esp_video_render_rect_t *new_rect,
                             bool opaque);

/**
 * @brief  Calculate dirty area for video composition
 *
 * @param[in]   first_compose   First compose structure
 * @param[in]   get_next        Callback to get next compose structure
 * @param[out]  dirty_area      Dirty area array
 * @param[in]   already_filled  Number of already filled areas
 * @param[in]   limited         Maximum number of dirty areas
 *
 * @return
 *       - Number  of dirty areas calculated
 */
int video_compose_calc_dirty_area(video_render_compose_t *first_compose,
                                  video_render_next_compose_cb get_next,
                                  esp_video_render_dirty_rect_t *dirty_area,
                                  uint8_t already_filled,
                                  uint8_t limited);
#ifdef __cplusplus
}
#endif  /* __cplusplus */
