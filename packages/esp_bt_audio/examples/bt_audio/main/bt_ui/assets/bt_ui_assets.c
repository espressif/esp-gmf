/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "assets/bt_ui_assets.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "sdkconfig.h"

#define BT_UI_FONTS_MAGIC        0x53434D46u /* SCMF */
#define BT_UI_FONTS_VERSION      2u
#define BT_UI_FONTS_MAX          4u
#define BT_UI_FONTS_HEADER_SIZE  256u
#define BT_UI_ASSETS_MAGIC       0x414D4353u /* SCMA */
#define BT_UI_ASSETS_VERSION     1u
#define BT_UI_ASSETS_LABEL       "assets"
#define BT_UI_IMAGES_MAX         8u
#define BT_UI_ASSETS_HEADER_SIZE 256u

typedef struct {
    uint32_t size_px;
    uint32_t line_height;
    uint32_t base_line;
    int32_t  underline_position;
    uint32_t underline_thickness;
    uint32_t bpp;
    uint32_t bitmap_format;
    uint32_t kern_scale;
    uint32_t bitmap_off;
    uint32_t bitmap_len;
    uint32_t dsc_off;
    uint32_t dsc_count;
    uint32_t cmap_off;
    uint32_t cmap_count;
    uint32_t reserved;
} bt_ui_font_desc_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    bt_ui_font_desc_t fonts[BT_UI_FONTS_MAX];
} bt_ui_fonts_header_t;

typedef struct {
    uint32_t id;
    uint32_t w;
    uint32_t h;
    uint32_t stride;
    uint32_t cf;
    uint32_t data_off;
    uint32_t data_len;
} bt_ui_image_desc_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t fonts_off;
    uint32_t fonts_size;
    uint32_t image_count;
    uint32_t reserved[3];
    bt_ui_image_desc_t images[BT_UI_IMAGES_MAX];
} bt_ui_assets_header_t;

typedef struct {
    uint32_t range_start;
    uint16_t range_length;
    uint16_t glyph_id_start;
    uint32_t unicode_list_off;
    uint32_t glyph_id_ofs_list_off;
    uint16_t list_length;
    uint16_t type;
} bt_ui_cmap_rec_t;

typedef struct {
    lv_font_t              font;
    lv_font_fmt_txt_dsc_t  dsc;
    lv_font_fmt_txt_cmap_t *cmaps;
} bt_ui_runtime_font_t;

typedef struct {
    const void                 *mmap_ptr;
    esp_partition_mmap_handle_t mmap_handle;
    size_t                      mmap_size;
    bt_ui_runtime_font_t       *font_28;
    lv_image_dsc_t              images[BT_UI_IMAGES_MAX];
    uint32_t                    image_ids[BT_UI_IMAGES_MAX];
    uint32_t                    image_count;
    bool                        ready;
} bt_ui_assets_ctx_t;

_Static_assert(sizeof(bt_ui_image_desc_t) == 28, "image desc size");
_Static_assert(sizeof(bt_ui_assets_header_t) == BT_UI_ASSETS_HEADER_SIZE, "assets header size");

static const char *TAG = "bt_ui_assets";
static bt_ui_assets_ctx_t s_assets;

static void *caps_calloc(size_t n, size_t size)
{
    return heap_caps_calloc_prefer(n, size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                   MALLOC_CAP_8BIT);
}

static void free_runtime_font(bt_ui_runtime_font_t *rt)
{
    if (rt == NULL) {
        return;
    }
    free(rt->cmaps);
    free(rt);
}

static bool range_in_map(size_t map_size, uint32_t off, uint32_t count, size_t elem_size)
{
    if (off > map_size) {
        return false;
    }
    return count <= (map_size - off) / elem_size;
}

static bt_ui_runtime_font_t *load_runtime_font(const uint8_t *base, size_t map_size,
                                               const bt_ui_font_desc_t *desc)
{
    if (!range_in_map(map_size, desc->bitmap_off, desc->bitmap_len, 1) ||
        !range_in_map(map_size, desc->dsc_off, desc->dsc_count, sizeof(lv_font_fmt_txt_glyph_dsc_t)) ||
        !range_in_map(map_size, desc->cmap_off, desc->cmap_count, sizeof(bt_ui_cmap_rec_t))) {
        ESP_LOGE(TAG, "%lupx font exceeds mapped image", (unsigned long)desc->size_px);
        return NULL;
    }

    if (desc->underline_position < INT8_MIN || desc->underline_position > INT8_MAX ||
        desc->underline_thickness > (uint32_t)INT8_MAX) {
        ESP_LOGE(TAG, "%lupx font underline metrics out of range", (unsigned long)desc->size_px);
        return NULL;
    }

    if (desc->cmap_count == 0 || desc->cmap_count > 511u ||
        (desc->bpp != 1 && desc->bpp != 2 && desc->bpp != 3 && desc->bpp != 4 && desc->bpp != 8) ||
        desc->bitmap_format > 3u) {
        ESP_LOGE(TAG, "%lupx font cmap/bpp/format out of LVGL range", (unsigned long)desc->size_px);
        return NULL;
    }

#if CONFIG_LV_FONT_FMT_TXT_LARGE == 0
    if (desc->bitmap_len > (1u << 20)) {
        ESP_LOGE(TAG, "Font bitmap %lu bytes needs CONFIG_LV_FONT_FMT_TXT_LARGE",
                 (unsigned long)desc->bitmap_len);
        return NULL;
    }
#endif

    bt_ui_runtime_font_t *rt = caps_calloc(1, sizeof(*rt));
    if (rt == NULL) {
        return NULL;
    }

    rt->cmaps = caps_calloc(desc->cmap_count, sizeof(*rt->cmaps));
    if (rt->cmaps == NULL) {
        free_runtime_font(rt);
        return NULL;
    }

    const bt_ui_cmap_rec_t *recs = (const bt_ui_cmap_rec_t *)(base + desc->cmap_off);
    for (uint32_t i = 0; i < desc->cmap_count; ++i) {
        size_t id_ofs_size = (recs[i].type == LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL ||
                              recs[i].type == LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY)
                                 ? sizeof(uint8_t)
                                 : sizeof(uint16_t);
        if ((recs[i].unicode_list_off != 0 &&
             !range_in_map(map_size, recs[i].unicode_list_off, recs[i].list_length, sizeof(uint16_t))) ||
            (recs[i].glyph_id_ofs_list_off != 0 &&
             !range_in_map(map_size, recs[i].glyph_id_ofs_list_off, recs[i].list_length, id_ofs_size))) {
            ESP_LOGE(TAG, "%lupx font cmap %lu exceeds mapped image", (unsigned long)desc->size_px,
                     (unsigned long)i);
            free_runtime_font(rt);
            return NULL;
        }

        rt->cmaps[i].range_start = recs[i].range_start;
        rt->cmaps[i].range_length = recs[i].range_length;
        rt->cmaps[i].glyph_id_start = recs[i].glyph_id_start;
        rt->cmaps[i].list_length = recs[i].list_length;
        rt->cmaps[i].type = (lv_font_fmt_txt_cmap_type_t)recs[i].type;
        rt->cmaps[i].unicode_list = recs[i].unicode_list_off == 0
                                        ? NULL
                                        : (const uint16_t *)(base + recs[i].unicode_list_off);
        rt->cmaps[i].glyph_id_ofs_list =
            recs[i].glyph_id_ofs_list_off == 0 ? NULL
                                               : (const void *)(base + recs[i].glyph_id_ofs_list_off);
    }

    rt->dsc.glyph_bitmap = base + desc->bitmap_off;
    rt->dsc.glyph_dsc = (const lv_font_fmt_txt_glyph_dsc_t *)(base + desc->dsc_off);
    rt->dsc.cmaps = rt->cmaps;
    rt->dsc.kern_dsc = NULL;
    rt->dsc.kern_scale = (uint16_t)desc->kern_scale;
    rt->dsc.cmap_num = (uint16_t)desc->cmap_count;
    rt->dsc.bpp = (uint16_t)desc->bpp;
    rt->dsc.kern_classes = 0;
    rt->dsc.bitmap_format = (uint16_t)desc->bitmap_format;
    rt->dsc.stride = 0;

    rt->font.get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt;
    rt->font.get_glyph_bitmap = lv_font_get_bitmap_fmt_txt;
    rt->font.line_height = (int32_t)desc->line_height;
    rt->font.base_line = (int32_t)desc->base_line;
    rt->font.subpx = LV_FONT_SUBPX_NONE;
    rt->font.underline_position = (int8_t)desc->underline_position;
    rt->font.underline_thickness = (int8_t)desc->underline_thickness;
    rt->font.dsc = &rt->dsc;
    rt->font.fallback = NULL;
    rt->font.user_data = NULL;
    return rt;
}

static bool bind_images(const uint8_t *base, size_t map_size, const bt_ui_image_desc_t *descs,
                        uint32_t count)
{
    bool have_cis = false;
    bool have_bis = false;

    if (count > BT_UI_IMAGES_MAX) {
        ESP_LOGE(TAG, "Too many images: %lu", (unsigned long)count);
        return false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        const bt_ui_image_desc_t *desc = &descs[i];
        if (desc->cf != LV_COLOR_FORMAT_ARGB8888 || desc->w == 0 || desc->h == 0 ||
            desc->w > UINT16_MAX || desc->h > UINT16_MAX || desc->stride > UINT16_MAX ||
            desc->stride != desc->w * 4 || desc->stride == 0 ||
            desc->h > (UINT32_MAX / desc->stride) || desc->data_len != desc->stride * desc->h ||
            desc->data_off > map_size || desc->data_len > map_size - desc->data_off) {
            ESP_LOGE(TAG, "Invalid image id=%lu", (unsigned long)desc->id);
            return false;
        }

        lv_image_dsc_t *dsc = &s_assets.images[i];
        memset(dsc, 0, sizeof(*dsc));
        dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc->header.cf = LV_COLOR_FORMAT_ARGB8888;
        dsc->header.w = (uint16_t)desc->w;
        dsc->header.h = (uint16_t)desc->h;
        dsc->header.stride = (uint16_t)desc->stride;
        dsc->data_size = desc->data_len;
        dsc->data = base + desc->data_off;
        s_assets.image_ids[i] = desc->id;
        if (desc->id == BT_UI_IMAGE_CIS_STREAM) {
            have_cis = true;
        } else if (desc->id == BT_UI_IMAGE_BIS_STREAM) {
            have_bis = true;
        }
        ESP_LOGI(TAG, "  image id=%lu %lux%lu (%lu bytes, flash)", (unsigned long)desc->id,
                 (unsigned long)desc->w, (unsigned long)desc->h, (unsigned long)desc->data_len);
    }

    s_assets.image_count = count;
    if (!have_cis || !have_bis) {
        ESP_LOGE(TAG, "Assets image missing CIS/BIS stream icons");
        return false;
    }
    return true;
}

esp_err_t bt_ui_assets_init(void)
{
    if (s_assets.ready) {
        return ESP_OK;
    }

    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                 BT_UI_ASSETS_LABEL);
    ESP_RETURN_ON_FALSE(part != NULL, ESP_ERR_NOT_FOUND, TAG, "assets partition not found");
    ESP_RETURN_ON_FALSE(part->size >= BT_UI_ASSETS_HEADER_SIZE, ESP_ERR_INVALID_SIZE, TAG,
                        "assets partition too small");

    const void *map = NULL;
    esp_partition_mmap_handle_t handle = 0;
    ESP_RETURN_ON_ERROR(esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &map,
                                           &handle),
                        TAG, "mmap assets partition failed");

    const uint8_t *base = (const uint8_t *)map;
    const bt_ui_assets_header_t *assets = (const bt_ui_assets_header_t *)map;
    if (assets->magic != BT_UI_ASSETS_MAGIC || assets->version != BT_UI_ASSETS_VERSION ||
        assets->image_count > BT_UI_IMAGES_MAX || assets->fonts_off > part->size ||
        assets->fonts_size > part->size - assets->fonts_off ||
        assets->fonts_size < BT_UI_FONTS_HEADER_SIZE) {
        esp_partition_munmap(handle);
        ESP_LOGE(TAG, "Invalid assets image (need SCMA v1)");
        return ESP_ERR_INVALID_RESPONSE;
    }

    const uint8_t *font_base = base + assets->fonts_off;
    const bt_ui_fonts_header_t *hdr = (const bt_ui_fonts_header_t *)font_base;
    if (hdr->magic != BT_UI_FONTS_MAGIC || hdr->version != BT_UI_FONTS_VERSION ||
        hdr->count == 0 || hdr->count > BT_UI_FONTS_MAX) {
        esp_partition_munmap(handle);
        ESP_LOGE(TAG, "Invalid fonts image (need SCMF v2 mmap pack)");
        return ESP_ERR_INVALID_RESPONSE;
    }

    bt_ui_runtime_font_t *font_28 = NULL;

    for (uint32_t i = 0; i < hdr->count; ++i) {
        const bt_ui_font_desc_t *desc = &hdr->fonts[i];
        bt_ui_runtime_font_t *rt = load_runtime_font(font_base, assets->fonts_size, desc);
        if (rt == NULL) {
            goto fail;
        }
        if (desc->size_px == 28) {
            free_runtime_font(font_28);
            font_28 = rt;
        } else {
            free_runtime_font(rt);
        }
    }

    if (font_28 == NULL) {
        ESP_LOGE(TAG, "Required 28px font missing in partition");
        goto fail;
    }
    if (!bind_images(base, part->size, assets->images, assets->image_count)) {
        goto fail;
    }

    s_assets.mmap_ptr = map;
    s_assets.mmap_handle = handle;
    s_assets.mmap_size = part->size;
    s_assets.font_28 = font_28;
    s_assets.ready = true;
    ESP_LOGI(TAG, "UI assets mmap'd from '%s' (font and icons remain in flash)", BT_UI_ASSETS_LABEL);
    for (uint32_t i = 0; i < hdr->count; ++i) {
        if (hdr->fonts[i].size_px == 28) {
            ESP_LOGI(TAG, "  %lupx bitmap_len=%lu (flash), cmaps=%lu",
                     (unsigned long)hdr->fonts[i].size_px,
                     (unsigned long)hdr->fonts[i].bitmap_len,
                     (unsigned long)hdr->fonts[i].cmap_count);
        }
    }
    return ESP_OK;

fail:
    free_runtime_font(font_28);
    s_assets.image_count = 0;
    esp_partition_munmap(handle);
    return ESP_FAIL;
}

void bt_ui_assets_deinit(void)
{
    if (!s_assets.ready) {
        return;
    }
    if (s_assets.font_28 != NULL) {
        free_runtime_font(s_assets.font_28);
        s_assets.font_28 = NULL;
    }
    if (s_assets.mmap_handle != 0) {
        esp_partition_munmap(s_assets.mmap_handle);
        s_assets.mmap_handle = 0;
        s_assets.mmap_ptr = NULL;
        s_assets.mmap_size = 0;
    }
    s_assets.image_count = 0;
    s_assets.ready = false;
}

const lv_font_t *bt_ui_font_cn_28(void)
{
    return s_assets.font_28 != NULL ? &s_assets.font_28->font : NULL;
}

const lv_image_dsc_t *bt_ui_image(bt_ui_image_id_t id)
{
    if (!s_assets.ready) {
        return NULL;
    }
    for (uint32_t i = 0; i < s_assets.image_count; ++i) {
        if (s_assets.image_ids[i] == (uint32_t)id) {
            return &s_assets.images[i];
        }
    }
    return NULL;
}
