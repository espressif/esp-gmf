/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_video_render_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

// Minimal stub API used by esp_video_render.c to configure crop/scale elements.
// In emulation, `esp_gmf_element_handle_t` is an opaque pointer owned by proc_emulation.c.

typedef void *esp_gmf_element_handle_t;

typedef struct {
    int  x;
    int  y;
    int  width;
    int  height;
} esp_gmf_video_cropped_region_t;

// Alias used by esp_video_render.c
typedef esp_gmf_video_cropped_region_t esp_gmf_video_rgn_t;

typedef struct {
    int  width;
    int  height;
} esp_gmf_video_resolution_t;

// Emulation-only element handle payload.
// `video_render_proc_*` creates these and returns them as `esp_gmf_element_handle_t`.
typedef enum {
    EMU_GMF_VIDEO_EL_NONE = 0,
    EMU_GMF_VIDEO_EL_CROP,
    EMU_GMF_VIDEO_EL_SCALE,
    EMU_GMF_VIDEO_EL_ROTATE,
} emu_gmf_video_el_kind_t;

typedef struct {
    emu_gmf_video_el_kind_t         kind;
    esp_gmf_video_cropped_region_t  crop;
    esp_gmf_video_resolution_t      dst;
    uint16_t                        degree;
    bool                            crop_dirty;
    bool                            dst_dirty;
    bool                            degree_dirty;
} emu_gmf_video_element_t;

int esp_gmf_video_param_set_cropped_region(esp_gmf_element_handle_t element, const esp_gmf_video_rgn_t *region);
int esp_gmf_video_param_set_dst_resolution(esp_gmf_element_handle_t element, const esp_gmf_video_resolution_t *res);
int esp_gmf_video_param_set_rotate_angle(esp_gmf_element_handle_t element, uint16_t degree);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
