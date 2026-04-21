/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <math.h>
#include "esp_vui_widget_default.h"
#include "esp_vui_container.h"
#include "esp_video_render.h"
#include "video_render_utils.h"
#include "esp_video_render_blender.h"
#include "esp_log.h"
#include "esp_video_render_types.h"
#include "video_render_compose.h"
#include "video_render_sys.h"
#include "xiaozhi_panel_widgets.h"

#ifndef M_PI
#define M_PI  3.14159265358979323846
#endif  /* M_PI */

// Define RGB565_PACK if not available from video_pattern.h
#ifndef RGB565_PACK
#define RGB565_PACK(r, g, b)  ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#endif  /* RGB565_PACK */

// Text alignment constants (from text_widget.c)
#define TEXT_ALIGN_LEFT    0
#define TEXT_ALIGN_CENTER  1
#define TEXT_ALIGN_RIGHT   2
#define TEXT_ALIGN_TOP     0
#define TEXT_ALIGN_MIDDLE  1
#define TEXT_ALIGN_BOTTOM  2

#define TAG  "XIAOZHI_PANEL"

#define STATUS_BAR_HEIGHT  30
#define EMOJI_SIZE         120
#define TEXT_LINE_HEIGHT   30
#define MAX_TEXT_LINES     4
#define MAX_TEXT_LINE_LEN  64

// UI element sizes
#define WIFI_ICON_SIZE       20
#define BATTERY_ICON_WIDTH   24
#define BATTERY_ICON_HEIGHT  12
#define SPEAKING_ICON_SIZE   16

typedef enum {
    IMAGE_TYPE_STATUS_BAR,
    IMAGE_TYPE_WIFI_ICON,
    IMAGE_TYPE_BATTERY_ICON,
    IMAGE_TYPE_SPEAKING_INDICATOR,
    IMAGE_TYPE_COUNT,
} image_type_t;

typedef struct xiaozhi_panel_ctx {
    esp_vui_container_handle_t  container;

    // Status bar widgets
    esp_vui_widget_t *status_bar_widget;    // Status bar background
    esp_vui_widget_t *wifi_icon_widget;     // WiFi icon
    esp_vui_widget_t *status_text_widget;   // Status text
    esp_vui_widget_t *battery_icon_widget;  // Battery icon

    // Main content widgets
    esp_vui_widget_t *emoji_widget;  // Emoji display
    esp_vui_widget_t *text_widget;   // Multi-line text

    // Speaking indicator
    esp_vui_widget_t *speaking_indicator_widget;

    // State
    char             status_text[32];
    wifi_strength_t  wifi_strength;
    battery_level_t  battery_level;
    bool             is_speaking;
    char             emoji_text[8];

    // Colors
    esp_video_render_clr_t  bg_color;
    esp_video_render_clr_t  status_bar_color;
    esp_video_render_clr_t  text_color;
    esp_video_render_clr_t  icon_color;
    esp_video_render_clr_t  speaking_color;
    uint8_t                *image_data[IMAGE_TYPE_COUNT];
} xiaozhi_panel_ctx_t;

static inline uint16_t rgb565_from_clr(const esp_video_render_clr_t *c)
{
    return RGB565_PACK(c->r, c->g, c->b);
}

static esp_video_render_err_t ensure_image_alloc(esp_video_render_img_t *img,
                                                 esp_video_render_format_t fmt,
                                                 int width, int height)
{
    if (img == NULL || width <= 0 || height <= 0) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    img->info.format = fmt;
    img->info.width = width;
    img->info.height = height;
    uint32_t need = video_render_get_image_size(&img->info);
    if (img->data == NULL || img->size < need) {
        if (img->data) {
            video_render_free(img->data);
        }
        img->data = video_render_calloc(1, need);
        if (img->data == NULL) {
            return ESP_VIDEO_RENDER_ERR_NO_MEM;
        }
        img->size = need;
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

// Fill whole RGB565 image with a color
static void fill_image_rgb565(esp_video_render_img_t *img, uint16_t rgb565)
{
    if (img == NULL || img->data == NULL) {
        return;
    }
    int px = img->info.width * img->info.height;
    uint16_t *p = (uint16_t *)img->data;
    for (int i = 0; i < px; i++) {
        p[i] = rgb565;
    }
}

static void init_existing_image(esp_video_render_img_t *img,
                                uint8_t *data,
                                int width,
                                int height)
{
    memset(img, 0, sizeof(*img));
    img->info.format = ESP_VIDEO_RENDER_FORMAT_RGB565;
    img->info.width = width;
    img->info.height = height;
    img->data = data;
    img->size = (uint32_t)(width * height * 2);
}

// Create a solid image widget quickly
static esp_vui_widget_t *create_solid_image_widget(esp_vui_container_handle_t container,
                                                   esp_video_render_img_t *img,
                                                   int width, int height,
                                                   const esp_video_render_clr_t *color,
                                                   const esp_video_render_pos_t *pos)
{
    if (ensure_image_alloc(img, ESP_VIDEO_RENDER_FORMAT_RGB565, width, height) != ESP_VIDEO_RENDER_ERR_OK) {
        return NULL;
    }
    fill_image_rgb565(img, rgb565_from_clr(color));
    esp_vui_widget_t *w = esp_vui_image_widget_init(container, img, pos);
    if (w == NULL) {
        video_render_free(img->data);
        img->data = NULL;
        return NULL;
    }
    return w;
}

int set_widget_font(esp_vui_widget_t *widget, const char *font_name, int font_size)
{
    if (!widget || font_size <= 0 || !font_name) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    bool as_emoji = false;
    const uint8_t *start = NULL;
    const uint8_t *end = NULL;
    if (strcmp(font_name, "DejaVuSans.ttf") != 0) {
        as_emoji = true;
    }
#ifndef __EMU__
    extern const uint8_t dejavu_sans_ttf_start[] asm("_binary_DejaVuSans_ttf_start");
    extern const uint8_t dejavu_sans_ttf_end[] asm("_binary_DejaVuSans_ttf_end");
    extern const uint8_t noto_color_emoji_bitmap_subset_ttf_start[] asm("_binary_NotoColorEmojiBitmap_Subset_ttf_start");
    extern const uint8_t noto_color_emoji_bitmap_subset_ttf_end[] asm("_binary_NotoColorEmojiBitmap_Subset_ttf_end");
    extern const uint8_t noto_emoji_ttf_start[] asm("_binary_NotoEmoji_Regular_ttf_start");
    extern const uint8_t noto_emoji_ttf_end[] asm("_binary_NotoEmoji_Regular_ttf_end");
    if (strcmp(font_name, "DejaVuSans.ttf") == 0) {
        start = dejavu_sans_ttf_start;
        end = dejavu_sans_ttf_end;
    } else if (strcmp(font_name, "NotoColorEmojiBitmap-Subset.ttf") == 0) {
        start = noto_color_emoji_bitmap_subset_ttf_start;
        end = noto_color_emoji_bitmap_subset_ttf_end;
    } else if (strcmp(font_name, "NotoEmoji-Regular.ttf") == 0) {
        start = noto_emoji_ttf_start;
        end = noto_emoji_ttf_end;
    }
#endif  /* __EMU__ */
    esp_video_render_err_t r = ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    // Try embedded font first (if start/end are valid)
    if (start && end) {
        int mem_size = (int)(end - start);

        if (mem_size > 0) {
            if (as_emoji) {
                r = esp_vui_text_widget_set_emoji_font_from_mem(widget, font_name, start, mem_size, font_size);
            } else {
                r = esp_vui_text_widget_set_font_from_mem(widget, font_name, start, mem_size, font_size);
            }
        }
        return r;
    }
    // Fallback to file path (for emulation)
#ifdef __EMU__
#include "assets_path.h"
    char font_path[1024];
    if (get_assets_path(font_name, font_path, sizeof(font_path)) == 0) {
        if (as_emoji) {
            r = esp_vui_text_widget_set_emoji_font(widget, font_path, font_size);
        } else {
            r = esp_vui_text_widget_set_font(widget, font_path, font_size);
        }
        if (r == ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGI(TAG, "Loaded font from assets: %s", font_path);
        }
    }
#endif  /* __EMU__ */
    return r;
}

static void setup_text_widget_common(esp_vui_widget_t *widget,
                                     const esp_video_render_clr_t *text_color,
                                     const esp_video_render_clr_t *bg_color,
                                     int align_h, int align_v,
                                     const char *text)
{
    if (!widget) {
        return;
    }
    if (text_color) {
        esp_vui_text_widget_set_text_color(widget, (esp_video_render_clr_t *)text_color);
    }
    if (bg_color) {
        // transparent background (false)
        esp_vui_text_widget_set_bg_color(widget, (esp_video_render_clr_t *)bg_color, false);
    }
    esp_vui_text_widget_set_align(widget, align_h, align_v);
    if (text) {
        esp_vui_text_widget_set_text(widget, text);
    }
}

// Helper: Create WiFi icon image
static esp_video_render_err_t create_wifi_icon_image(esp_video_render_img_t *img,
                                                     wifi_strength_t strength,
                                                     esp_video_render_clr_t *color)
{
    if (img == NULL || color == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    int size = WIFI_ICON_SIZE;
    if (img->data == NULL) {
        esp_video_render_err_t ret = ensure_image_alloc(img, ESP_VIDEO_RENDER_FORMAT_RGB565, size, size);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            return ret;
        }
    }
    // Fill background with status bar color (200,200,200) so icon blends properly
    uint16_t bg_rgb565 = RGB565_PACK(200, 200, 200);
    fill_image_rgb565(img, bg_rgb565);

    // Draw WiFi arcs in black - make them thicker and more visible
    uint16_t rgb565 = RGB565_PACK(color->r, color->g, color->b);
    int center_x = size / 2;
    int center_y = size / 2;
    int radius = size / 2 - 2;
    int arc_count = (int)strength;
    if (arc_count > 3) {
        arc_count = 3;
    }
    if (arc_count == 0) {
        arc_count = 1;  // Always show at least one arc
    }

    for (int i = 0; i < arc_count; i++) {
        int arc_radius = radius - (arc_count - 1 - i) * (radius / 3);
        int start_angle = 225;
        int end_angle = 315;

        // Draw thicker arcs by drawing multiple points around each angle
        for (int angle = start_angle; angle <= end_angle; angle += 1) {
            double rad = angle * M_PI / 180.0;
            int px = center_x + (int)(arc_radius * cos(rad));
            int py = center_y + (int)(arc_radius * sin(rad));

            // Draw a 2x2 block at each point to make it thicker
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int x = px + dx;
                    int y = py + dy;
                    if (x >= 0 && x < size && y >= 0 && y < size) {
                        ((uint16_t *)img->data)[y * size + x] = rgb565;
                    }
                }
            }
        }
    }

    // Draw center dot (2x2 for visibility)
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int x = center_x + dx;
            int y = center_y + dy;
            if (x >= 0 && x < size && y >= 0 && y < size) {
                ((uint16_t *)img->data)[y * size + x] = rgb565;
            }
        }
    }

    // Debug: Verify icon was drawn (check center pixel)
    uint16_t check_pixel = ((uint16_t *)img->data)[center_y * size + center_x];
    ESP_LOGI(TAG, "WiFi icon created: strength=%d arc_count=%d center_pixel=0x%04x bg=0x%04x icon=0x%04x",
             (int)strength, arc_count, check_pixel, bg_rgb565, rgb565);

    return ESP_VIDEO_RENDER_ERR_OK;
}

// Helper: Create battery icon image
static esp_video_render_err_t create_battery_icon_image(esp_video_render_img_t *img,
                                                        battery_level_t level,
                                                        esp_video_render_clr_t *color)
{
    if (img == NULL || color == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    int width = BATTERY_ICON_WIDTH;
    int height = BATTERY_ICON_HEIGHT;
    if (img->data == NULL) {
        esp_video_render_err_t ret = ensure_image_alloc(img, ESP_VIDEO_RENDER_FORMAT_RGB565, width, height);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            return ret;
        }
    }

    // Fill background with status bar color (200,200,200) so icon blends properly
    uint16_t bg_rgb565 = RGB565_PACK(200, 200, 200);
    fill_image_rgb565(img, bg_rgb565);

    // Draw battery in black - make outline thicker for visibility
    uint16_t rgb565 = RGB565_PACK(color->r, color->g, color->b);
    int outline_thickness = 2;
    int terminal_width = 3;
    int terminal_height = height / 3;

    // Draw battery outline (thicker for visibility)
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width - terminal_width; x++) {
            // Draw outline - make it thicker by checking distance from edges
            bool is_outline = (y < outline_thickness || y >= height - outline_thickness || x < outline_thickness || x >= width - terminal_width - outline_thickness);
            if (is_outline) {
                ((uint16_t *)img->data)[y * width + x] = rgb565;
            }
        }
    }

    // Draw terminal (thicker for visibility)
    int term_y_start = (height - terminal_height) / 2;
    for (int y = term_y_start; y < term_y_start + terminal_height; y++) {
        for (int x = width - terminal_width; x < width; x++) {
            // Draw terminal outline - make it thicker
            bool is_terminal_outline = (y <= term_y_start + 1 || y >= term_y_start + terminal_height - 2 || x <= width - terminal_width + 1 || x >= width - 1);
            if (is_terminal_outline) {
                ((uint16_t *)img->data)[y * width + x] = rgb565;
            }
        }
    }

    // Fill battery level (make it more visible)
    int fill_width = (width - terminal_width - outline_thickness * 2) * level / 4;
    if (fill_width > 0) {
        int fill_x = outline_thickness;
        int fill_y = outline_thickness;
        int fill_h = height - outline_thickness * 2;

        // Draw fill with a small margin for visibility (leave 1 pixel border)
        for (int y = fill_y + 1; y < fill_y + fill_h - 1; y++) {
            for (int x = fill_x + 1; x < fill_x + fill_width; x++) {
                ((uint16_t *)img->data)[y * width + x] = rgb565;
            }
        }
    }

    // Debug: Verify battery was drawn (check top-left corner which should be outline)
    uint16_t check_pixel_outline = ((uint16_t *)img->data)[0];                                             // Top-left corner (should be black outline)
    uint16_t check_pixel_inside = ((uint16_t *)img->data)[outline_thickness * width + outline_thickness];  // Inside (should be bg or fill)
    ESP_LOGI(TAG, "Battery icon created: level=%d fill_width=%d outline_pixel=0x%04x inside_pixel=0x%04x bg=0x%04x icon=0x%04x",
             (int)level, fill_width, check_pixel_outline, check_pixel_inside, bg_rgb565, rgb565);

    return ESP_VIDEO_RENDER_ERR_OK;
}

// Helper: Create speaking indicator image
static esp_video_render_err_t create_speaking_indicator_image(esp_video_render_img_t *img,
                                                              bool is_speaking,
                                                              esp_video_render_clr_t *color)
{
    if (img == NULL || color == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    int size = SPEAKING_ICON_SIZE;
    if (img->data == NULL) {
        if (ensure_image_alloc(img, ESP_VIDEO_RENDER_FORMAT_RGB565, size, size) != ESP_VIDEO_RENDER_ERR_OK) {
            return ESP_VIDEO_RENDER_ERR_NO_MEM;
        }
    }

    // Clear background (white)
    fill_image_rgb565(img, RGB565_PACK(255, 255, 255));

    uint16_t rgb565 = RGB565_PACK(color->r, color->g, color->b);
    int center_x = size / 2;
    int center_y = size / 2;
    int radius = is_speaking ? 6 : 4;

    // Draw circle
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int dx = x - center_x;
            int dy = y - center_y;
            int dist_sq = dx * dx + dy * dy;
            int radius_sq = radius * radius;

            if (dist_sq <= radius_sq) {
                ((uint16_t *)img->data)[y * size + x] = rgb565;
            }
        }
    }
    // Draw outer pulse ring if speaking
    if (is_speaking) {
        int outer_radius = radius + 3;
        int outer_radius_sq = outer_radius * outer_radius;
        int inner_radius_sq = (radius + 1) * (radius + 1);

        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                int dx = x - center_x;
                int dy = y - center_y;
                int dist_sq = dx * dx + dy * dy;

                if (dist_sq >= inner_radius_sq && dist_sq <= outer_radius_sq) {
                    ((uint16_t *)img->data)[y * size + x] = rgb565;
                }
            }
        }
    }

    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t xiaozhi_panel_create(esp_vui_overlay_handle_t overlay,
                                            esp_video_render_frame_info_t *frame_info,
                                            esp_video_render_pos_t *pos,
                                            bool with_cache,
                                            void **panel_handle)
{
    if (overlay == NULL || frame_info == NULL || pos == NULL || panel_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    xiaozhi_panel_ctx_t *panel = (xiaozhi_panel_ctx_t *)video_render_calloc(1, sizeof(xiaozhi_panel_ctx_t));
    if (panel == NULL) {
        ESP_LOGE(TAG, "Failed to allocate panel context");
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }

    // Initialize default values
    strncpy(panel->status_text, "激活设备", sizeof(panel->status_text) - 1);
    panel->wifi_strength = WIFI_STRENGTH_STRONG;
    panel->battery_level = BATTERY_LEVEL_100;
    panel->is_speaking = false;
    strncpy(panel->emoji_text, "😊", sizeof(panel->emoji_text) - 1);

    // Default colors
    panel->bg_color = (esp_video_render_clr_t) {.r = 255, .g = 255, .b = 255};
    panel->status_bar_color = (esp_video_render_clr_t) {.r = 200, .g = 200, .b = 200};  // Darker gray for visibility
    panel->text_color = (esp_video_render_clr_t) {.r = 0, .g = 0, .b = 0};              // Black text
    panel->icon_color = (esp_video_render_clr_t) {.r = 0, .g = 0, .b = 0};              // Black icons
    panel->speaking_color = (esp_video_render_clr_t) {.r = 255, .g = 0, .b = 0};
    // Create main container
    esp_video_render_err_t ret = esp_vui_container_create(overlay, frame_info, pos, with_cache, &panel->container);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to create container");
        video_render_free(panel);
        return ret;
    }
    esp_vui_container_set_bg_color(panel->container, &panel->bg_color);

    // Create status bar background widget
    esp_video_render_pos_t status_bar_pos = {.x = 0, .y = 0};
    esp_video_render_img_t status_img = {0};
    panel->status_bar_widget = create_solid_image_widget(panel->container,
                                                         &status_img,
                                                         frame_info->width, STATUS_BAR_HEIGHT,
                                                         &panel->status_bar_color, &status_bar_pos);
    if (panel->status_bar_widget) {
        panel->image_data[IMAGE_TYPE_STATUS_BAR] = status_img.data;
        ESP_LOGI(TAG, "Status bar widget created: %dx%d at %d,%d",
                 frame_info->width, STATUS_BAR_HEIGHT, status_bar_pos.x, status_bar_pos.y);
    } else {
        ESP_LOGE(TAG, "Failed to create status bar widget");
    }

    // Create WiFi icon widget
    esp_video_render_img_t wifi_img = {0};
    ret = create_wifi_icon_image(&wifi_img, panel->wifi_strength, &panel->icon_color);
    if (ret == ESP_VIDEO_RENDER_ERR_OK) {
        panel->image_data[IMAGE_TYPE_WIFI_ICON] = wifi_img.data;
        esp_video_render_pos_t wifi_pos = {.x = 5, .y = (STATUS_BAR_HEIGHT - wifi_img.info.height) / 2};
        panel->wifi_icon_widget = esp_vui_image_widget_init(panel->container, &wifi_img, &wifi_pos);
        if (panel->wifi_icon_widget) {
            ESP_LOGI(TAG, "WiFi icon widget created: %dx%d at %d,%d",
                     wifi_img.info.width, wifi_img.info.height, wifi_pos.x, wifi_pos.y);
        } else {
            ESP_LOGE(TAG, "Failed to create WiFi icon widget");
        }
    } else {
        ESP_LOGE(TAG, "Failed to create WiFi icon image: %d", ret);
    }

    // Create battery icon widget (before status text so text renders on top)
    esp_video_render_img_t battery_img = {0};
    ret = create_battery_icon_image(&battery_img, panel->battery_level, &panel->icon_color);
    if (ret == ESP_VIDEO_RENDER_ERR_OK) {
        panel->image_data[IMAGE_TYPE_BATTERY_ICON] = battery_img.data;
        esp_video_render_pos_t battery_pos = {
            .x = frame_info->width - battery_img.info.width - 5,
            .y = (STATUS_BAR_HEIGHT - battery_img.info.height) / 2};
        panel->battery_icon_widget = esp_vui_image_widget_init(panel->container, &battery_img, &battery_pos);
        if (panel->battery_icon_widget) {
            ESP_LOGI(TAG, "Battery icon widget created: %dx%d at %d,%d",
                     battery_img.info.width, battery_img.info.height, battery_pos.x, battery_pos.y);
        } else {
            ESP_LOGE(TAG, "Failed to create battery icon widget");
        }
    } else {
        ESP_LOGE(TAG, "Failed to create battery icon image: %d", ret);
    }

    // Create status text widget LAST (so it renders on top of icons)
    // Place it in the center area between WiFi (left) and Battery (right)
    int left_guard = 5  /*wifi left margin*/ + WIFI_ICON_SIZE + 5  /*gap*/;
    int right_guard = BATTERY_ICON_WIDTH + 5  /*battery right margin*/ + 5  /*gap inside*/;
    int status_text_width = frame_info->width - left_guard - right_guard;
    if (status_text_width < 40) {
        status_text_width = frame_info->width;  // fallback if too small
    }
    esp_video_render_frame_info_t status_text_info = {
        .format = frame_info->format,
        .width = status_text_width,
        .height = STATUS_BAR_HEIGHT};
    esp_video_render_pos_t status_text_pos = {.x = (status_text_width == (int)frame_info->width) ? 0 : left_guard, .y = 0};
    panel->status_text_widget = esp_vui_text_widget_init(panel->container, &status_text_info,
                                                         &status_text_pos, status_text_info.width, status_text_info.height);
    if (panel->status_text_widget) {
        // Load font for status text FIRST (before setting text)
        int font_size = 20;  // Status bar font size
        ret = set_widget_font(panel->status_text_widget, "DejaVuSans.ttf", font_size);
        if (ret == ESP_VIDEO_RENDER_ERR_OK) {
            esp_video_render_clr_t text_color = {.r = 0, .g = 0, .b = 0};
            esp_video_render_clr_t bg_color = panel->status_bar_color;  // match bar color
            // Opaque background (same color as status bar) so cache redraws fill correctly
            esp_vui_text_widget_set_text_color(panel->status_text_widget, &text_color);
            esp_vui_text_widget_set_bg_color(panel->status_text_widget, &bg_color, false  /*opaque*/);
            esp_vui_text_widget_set_text(panel->status_text_widget, panel->status_text);
            esp_vui_text_widget_set_align(panel->status_text_widget, TEXT_ALIGN_CENTER, TEXT_ALIGN_MIDDLE);
            ESP_LOGI(TAG, "Status text widget created: text='%s'", panel->status_text);
        } else {
            ESP_LOGE(TAG, "Cannot set text without font - font loading failed");
        }
    } else {
        ESP_LOGE(TAG, "Failed to create status text widget");
    }

    // Create emoji widget (using text widget with emoji font)
    int emoji_y = STATUS_BAR_HEIGHT + 20;
    esp_video_render_frame_info_t emoji_info = {
        .format = frame_info->format,
        .width = EMOJI_SIZE,
        .height = EMOJI_SIZE};
    esp_video_render_pos_t emoji_pos = {
        .x = (frame_info->width - EMOJI_SIZE) / 2,
        .y = emoji_y};
    panel->emoji_widget = esp_vui_text_widget_init(panel->container, &emoji_info,
                                                   &emoji_pos, emoji_info.width, emoji_info.height);
    if (panel->emoji_widget) {
        int emoji_font_size = 80;  // Large font for emoji
        ret = set_widget_font(panel->emoji_widget, "NotoColorEmojiBitmap-Subset.ttf", emoji_font_size);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ret = set_widget_font(panel->emoji_widget, "NotoEmoji-Regular.ttf", emoji_font_size);
        }
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ret = set_widget_font(panel->emoji_widget, "DejaVuSans.ttf", emoji_font_size);
        }
        if (ret == ESP_VIDEO_RENDER_ERR_OK) {
            esp_video_render_clr_t emoji_text_color = {.r = 0, .g = 0, .b = 0};
            esp_video_render_clr_t emoji_bg = {.r = 255, .g = 255, .b = 255};
            setup_text_widget_common(panel->emoji_widget, &emoji_text_color, &emoji_bg,
                                     TEXT_ALIGN_CENTER, TEXT_ALIGN_MIDDLE, panel->emoji_text);
            ESP_LOGI(TAG, "Emoji widget created: text='%s', pos=%d,%d, size=%dx%d",
                     panel->emoji_text, emoji_pos.x, emoji_pos.y, emoji_info.width, emoji_info.height);
        } else {
            ESP_LOGE(TAG, "Failed to load font for emoji widget");
        }
    } else {
        ESP_LOGE(TAG, "Failed to create emoji widget");
    }

    // Create text widget for multi-line text (centered)
    int text_y = emoji_y + EMOJI_SIZE + 20;
    int text_height = frame_info->height - text_y - 30;
    esp_video_render_frame_info_t text_info = {
        .format = frame_info->format,
        .width = frame_info->width,  // Full width for proper centering
        .height = text_height};
    esp_video_render_pos_t text_pos = {.x = 0, .y = text_y};  // Start at x=0 for full-width centering
    panel->text_widget = esp_vui_text_widget_init(panel->container, &text_info,
                                                  &text_pos, text_info.width, text_info.height);
    if (panel->text_widget) {
        // Load font for text widget
        int font_size = 24;
        ret = set_widget_font(panel->text_widget, "DejaVuSans.ttf", font_size);
        if (ret == ESP_VIDEO_RENDER_ERR_OK) {
            esp_video_render_clr_t text_bg = {.r = 255, .g = 255, .b = 255};
            setup_text_widget_common(panel->text_widget, &panel->text_color, &text_bg,
                                     TEXT_ALIGN_CENTER, TEXT_ALIGN_TOP, NULL);
            ESP_LOGI(TAG, "Text widget created: pos=%d,%d, size=%dx%d",
                     text_pos.x, text_pos.y, text_info.width, text_info.height);
        } else {
            ESP_LOGE(TAG, "Cannot set text without font - font loading failed");
        }
    } else {
        ESP_LOGE(TAG, "Failed to create text widget");
    }

    // Create speaking indicator widget
    esp_video_render_img_t speaking_img = {0};
    ret = create_speaking_indicator_image(&speaking_img, panel->is_speaking, &panel->icon_color);
    if (ret == ESP_VIDEO_RENDER_ERR_OK) {
        panel->image_data[IMAGE_TYPE_SPEAKING_INDICATOR] = speaking_img.data;
        esp_video_render_pos_t speaking_pos = {
            .x = (frame_info->width - speaking_img.info.width) / 2,
            .y = frame_info->height - speaking_img.info.height - 5};
        panel->speaking_indicator_widget = esp_vui_image_widget_init(panel->container, &speaking_img, &speaking_pos);
        if (panel->speaking_indicator_widget) {
            ESP_LOGI(TAG, "Speaking indicator widget created: %dx%d at %d,%d",
                     speaking_img.info.width, speaking_img.info.height, speaking_pos.x, speaking_pos.y);
        } else {
            ESP_LOGE(TAG, "Failed to create speaking indicator widget");
        }
    } else {
        ESP_LOGE(TAG, "Failed to create speaking indicator image: %d", ret);
    }
    esp_video_render_rect_t dirty = {
        .height = frame_info->height,
        .width = frame_info->width,
    };
    esp_vui_container_notify_compose_changed(panel->container, &dirty, true);
    *panel_handle = panel;
    ESP_LOGI(TAG, "Xiaozhi panel created: container=%dx%d at %d,%d",
             frame_info->width, frame_info->height, pos->x, pos->y);

    return ESP_VIDEO_RENDER_ERR_OK;
}

// Update status bar
esp_video_render_err_t xiaozhi_panel_set_status(void *panel_handle,
                                                const char *status_text,
                                                wifi_strength_t wifi_strength,
                                                battery_level_t battery_level)
{
    if (panel_handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    xiaozhi_panel_ctx_t *panel = (xiaozhi_panel_ctx_t *)panel_handle;
    esp_vui_container_compose_lock(panel->container);
    if (status_text && panel->status_text_widget) {
        strncpy(panel->status_text, status_text, sizeof(panel->status_text) - 1);
        panel->status_text[sizeof(panel->status_text) - 1] = '\0';
        esp_video_render_err_t ret = esp_vui_text_widget_set_text(panel->status_text_widget, panel->status_text);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGW(TAG, "Failed to set status text: %d", ret);
        } else {
            ESP_LOGI(TAG, "Status text updated: '%s'", panel->status_text);
        }
    }

    if (wifi_strength != panel->wifi_strength && panel->wifi_icon_widget) {
        panel->wifi_strength = wifi_strength;
        esp_video_render_img_t wifi_img;
        init_existing_image(&wifi_img, panel->image_data[IMAGE_TYPE_WIFI_ICON], WIFI_ICON_SIZE, WIFI_ICON_SIZE);
        if (create_wifi_icon_image(&wifi_img, wifi_strength, &panel->icon_color) == ESP_VIDEO_RENDER_ERR_OK) {
            panel->image_data[IMAGE_TYPE_WIFI_ICON] = wifi_img.data;
            panel->wifi_icon_widget->dirty = panel->wifi_icon_widget->rect;
            esp_vui_container_notify_compose_changed(panel->container, &panel->wifi_icon_widget->dirty, true);
        }
    }

    if (battery_level != panel->battery_level && panel->battery_icon_widget) {
        panel->battery_level = battery_level;
        esp_video_render_img_t battery_img;
        init_existing_image(&battery_img, panel->image_data[IMAGE_TYPE_BATTERY_ICON],
                            BATTERY_ICON_WIDTH, BATTERY_ICON_HEIGHT);
        if (create_battery_icon_image(&battery_img, battery_level, &panel->icon_color) == ESP_VIDEO_RENDER_ERR_OK) {
            panel->image_data[IMAGE_TYPE_BATTERY_ICON] = battery_img.data;
            panel->battery_icon_widget->dirty = panel->battery_icon_widget->rect;
            esp_vui_container_notify_compose_changed(panel->container, &panel->battery_icon_widget->dirty, true);
        }
    }
    esp_vui_container_compose_unlock(panel->container);
    return ESP_VIDEO_RENDER_ERR_OK;
}

// Set speaking state
esp_video_render_err_t xiaozhi_panel_set_speaking(void *panel_handle, bool is_speaking)
{
    if (panel_handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    xiaozhi_panel_ctx_t *panel = (xiaozhi_panel_ctx_t *)panel_handle;

    if (is_speaking != panel->is_speaking && panel->speaking_indicator_widget) {
        panel->is_speaking = is_speaking;
        esp_video_render_img_t speaking_img;
        init_existing_image(&speaking_img, panel->image_data[IMAGE_TYPE_SPEAKING_INDICATOR],
                            SPEAKING_ICON_SIZE, SPEAKING_ICON_SIZE);
        esp_video_render_clr_t color = is_speaking ? panel->speaking_color : panel->icon_color;
        esp_vui_container_compose_lock(panel->container);
        if (create_speaking_indicator_image(&speaking_img, is_speaking, &color) == ESP_VIDEO_RENDER_ERR_OK) {
            panel->image_data[IMAGE_TYPE_SPEAKING_INDICATOR] = speaking_img.data;
            panel->speaking_indicator_widget->dirty = panel->speaking_indicator_widget->rect;
            esp_vui_container_notify_compose_changed(panel->container, &panel->speaking_indicator_widget->dirty, true);
        }
        esp_vui_container_compose_unlock(panel->container);
    }

    return ESP_VIDEO_RENDER_ERR_OK;
}

// Set emoji
esp_video_render_err_t xiaozhi_panel_set_emoji(void *panel_handle, const char *emoji)
{
    if (panel_handle == NULL || emoji == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    xiaozhi_panel_ctx_t *panel = (xiaozhi_panel_ctx_t *)panel_handle;
    strncpy(panel->emoji_text, emoji, sizeof(panel->emoji_text) - 1);
    panel->emoji_text[sizeof(panel->emoji_text) - 1] = '\0';

    if (panel->emoji_widget) {
        // Ensure text color is set when updating emoji
        esp_video_render_clr_t emoji_text_color = {.r = 0, .g = 0, .b = 0};
        esp_vui_text_widget_set_text_color(panel->emoji_widget, &emoji_text_color);
        esp_vui_text_widget_set_text(panel->emoji_widget, panel->emoji_text);
    }

    return ESP_VIDEO_RENDER_ERR_OK;
}

// Set text lines
esp_video_render_err_t xiaozhi_panel_set_text_lines(void *panel_handle, char *lines)
{
    if (panel_handle == NULL || lines == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    xiaozhi_panel_ctx_t *panel = (xiaozhi_panel_ctx_t *)panel_handle;
    if (panel->text_widget) {
        esp_vui_text_widget_set_text(panel->text_widget, lines);
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

// Destroy panel
esp_video_render_err_t xiaozhi_panel_destroy(void *panel_handle)
{
    if (panel_handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    xiaozhi_panel_ctx_t *panel = (xiaozhi_panel_ctx_t *)panel_handle;
    // Destroy container (this will destroy all widgets)
    esp_vui_container_destroy(panel->container);
    for (int i = 0; i < IMAGE_TYPE_COUNT; i++) {
        if (panel->image_data[i]) {
            video_render_free(panel->image_data[i]);
        }
    }
    video_render_free(panel);
    return ESP_VIDEO_RENDER_ERR_OK;
}
