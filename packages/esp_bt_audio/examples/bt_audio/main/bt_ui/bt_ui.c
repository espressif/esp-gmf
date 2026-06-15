/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#include "dev_display_lcd.h"
#include "dev_lcd_touch.h"
#include "bt_ui.h"
#include "lvgl.h"
#include "jpeg_decoder.h"
#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_playback.h"
#include "esp_bt_audio_tel.h"

#define UI_COVER_QUEUE_SIZE       2
#define UI_COVER_TASK_STACK_SIZE  4096
#define UI_COVER_TASK_PRIO        5
#define LVGL_DRAW_BUF_LINES       (BT_UI_HEIGHT / 2)

#define BT_UI_FONT_LARGE       (&lv_font_notosanssc_regular_28)
#define BT_UI_FONT_MEDIUM      (&lv_font_notosanssc_regular_28)
#define BT_UI_FONT_SMALL       (&lv_font_montserrat_20)
#define BT_UI_FONT_ICON        (&lv_font_montserrat_28)
#define BT_UI_FONT_COVER_ICON  (&lv_font_montserrat_32)

/* Landscape (800x480) layout */
#define COVER_SIZE        360
#define COVER_IMG_SIZE    340
#define MEDIA_LEFT_PAD    32
#define MEDIA_GAP         32
#define RIGHT_PANEL_X     (MEDIA_LEFT_PAD + COVER_SIZE + MEDIA_GAP)
#define RIGHT_PANEL_W     (BT_UI_WIDTH - RIGHT_PANEL_X - MEDIA_LEFT_PAD)
#define BTN_ROW_HEIGHT    88
#define BTN_W             92
#define BTN_H             68
#define NAME_AREA_LINES   5
#define NAME_AREA_H       240
#define DIALER_KEYPAD_SZ  92
#define DIALER_KEY_GAP    16
#define DIALER_START_Y    82
#define DIALER_ROW_GAP    28

#define CONTENT_H          BT_UI_HEIGHT
#define VOL_BAR_W          6
#define VOL_BAR_H          (BT_UI_HEIGHT / 5)
#define VOL_BAR_RIGHT_GAP  12

LV_FONT_DECLARE(lv_font_notosanssc_regular_28)

#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
LV_IMAGE_DECLARE(speakers_dark_dark_small);
LV_IMAGE_DECLARE(speakers_dark_light_small);
LV_IMAGE_DECLARE(speakers_light_dark_small);
LV_IMAGE_DECLARE(cis_stream_icon);
LV_IMAGE_DECLARE(bis_stream_icon);

/* Source art is 440x260; keep aspect when fitting the zone below the title. */
#define SPEAKER_SRC_W      440
#define SPEAKER_SRC_H      260
#define LE_TITLE_AREA_H    96
#define LE_SPEAKER_ZONE_W  (RIGHT_PANEL_W - 16)
#define LE_SPEAKER_ZONE_H  (COVER_SIZE - LE_TITLE_AREA_H - BTN_ROW_HEIGHT)
#if (LE_SPEAKER_ZONE_W * SPEAKER_SRC_H / SPEAKER_SRC_W) <= LE_SPEAKER_ZONE_H
#define SPEAKER_IMG_W  LE_SPEAKER_ZONE_W
#define SPEAKER_IMG_H  (LE_SPEAKER_ZONE_W * SPEAKER_SRC_H / SPEAKER_SRC_W)
#else
#define SPEAKER_IMG_H  LE_SPEAKER_ZONE_H
#define SPEAKER_IMG_W  (LE_SPEAKER_ZONE_H * SPEAKER_SRC_W / SPEAKER_SRC_H)
#endif  /* (LE_SPEAKER_ZONE_W * SPEAKER_SRC_H / SPEAKER_SRC_W) <= LE_SPEAKER_ZONE_H */
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */

#define DIALER_BUF_SIZE  32

/**
 * @brief  Cover image payload queued to the UI task.
 */
typedef struct {
    uint8_t  *data;  /*!< Encoded image buffer owned by the UI task */
    size_t    size;  /*!< Encoded image buffer size in bytes */
} ui_cover_msg_t;

/**
 * @brief  Runtime state for the Bluetooth demo UI.
 */
struct bt_ui_t {
    lv_disp_t                    *disp;                      /*!< Display instance used by LVGL */
    lv_obj_t                     *splash;                    /*!< Splash screen root object */
    lv_obj_t                     *main;                      /*!< Main UI root object */
    lv_obj_t                     *tabview;                   /*!< Main tabview (tab 0 = dialer, tab 1 = media) */
    lv_obj_t                     *media;                     /*!< Media page root object */
    lv_obj_t                     *dialer;                    /*!< Dialer page root object */
    lv_obj_t                     *volume_bar;                /*!< Floating volume indicator */
    int                           volume;                    /*!< Tracked volume level */
    esp_bt_audio_stream_handle_t  stream_type_stream;        /*!< Stream used to derive media placeholder state */
    bool                          stream_type_is_broadcast;  /*!< True when stream_type_stream is a BIS stream */
    QueueHandle_t                 cover_queue;               /*!< Queue carrying cover image updates */
};

/**
 * @brief  Cached LVGL widgets used by the dialer page.
 */
typedef struct {
    lv_obj_t *number_label;    /*!< Dialed number and call-state label */
    lv_obj_t *call_btn;        /*!< Dial/end-call button */
    lv_obj_t *call_btn_label;  /*!< Icon label inside call_btn */
    lv_obj_t *answer_btn;      /*!< Answer button shown for incoming calls */
    lv_obj_t *back_btn;        /*!< Backspace button */
    void     *call_ctx;        /*!< Pointer to dialer_call_ctx_t */
} bt_ui_dialer_refs_t;

/**
 * @brief  Dialer callback context and current call state.
 */
typedef struct {
    void (*call_cb)(const char *number, void *ctx);  /*!< Dial callback */
    void *call_cb_ctx;                               /*!< Context passed to call_cb */
    void (*end_call_cb)(void *ctx);                  /*!< End-call callback */
    void *end_call_cb_ctx;                           /*!< Context passed to end_call_cb */
    void (*answer_call_cb)(void *ctx);               /*!< Answer-call callback */
    void *answer_call_cb_ctx;                        /*!< Context passed to answer_call_cb */
    bool  in_call;                                   /*!< True while the dialer is in a call state */
} dialer_call_ctx_t;

static char dialer_number_buf[DIALER_BUF_SIZE];
static lv_obj_t *dialer_number_label;
static lv_timer_t *s_vol_bar_hide_timer = NULL;
static const char *TAG = "BT_UI";

#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
static const lv_image_dsc_t *speaker_img_for_location(uint32_t location)
{
    bool left = (location & ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT) != 0;
    bool right = (location & ESP_BT_AUDIO_AUDIO_LOC_FRONT_RIGHT) != 0;
    if (left && right) {
        return &speakers_dark_dark_small;
    }
    if (left) {
        return &speakers_dark_light_small;
    }
    if (right) {
        return &speakers_light_dark_small;
    }
    return &speakers_dark_dark_small;
}
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */

static void vol_bar_hide_timer_cb(lv_timer_t *t)
{
    lv_obj_t *bar = (lv_obj_t *)lv_timer_get_user_data(t);
    if (bar != NULL) {
        lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    }
    lv_timer_del(t);
    s_vol_bar_hide_timer = NULL;
}

lv_obj_t *bt_ui_volume_bar_create(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, VOL_BAR_W, VOL_BAR_H);
    lv_obj_align(cont, LV_ALIGN_RIGHT_MID, -VOL_BAR_RIGHT_GAP, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_radius(cont, 2, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x505060), 0);

    lv_obj_t *fill = lv_obj_create(cont);
    lv_obj_remove_style_all(fill);
    lv_obj_set_width(fill, VOL_BAR_W);
    lv_obj_set_height(fill, 0);
    lv_obj_align(fill, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(fill, 2, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fill, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_style_pad_all(fill, 0, 0);

    lv_obj_set_user_data(cont, fill);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
    return cont;
}

void bt_ui_volume_bar_set_level(lv_obj_t *vol_bar, int level_percent)
{
    if (vol_bar == NULL) {
        return;
    }
    if (level_percent < 0) {
        level_percent = 0;
    }
    if (level_percent > 100) {
        level_percent = 100;
    }
    lv_obj_t *fill = (lv_obj_t *)lv_obj_get_user_data(vol_bar);
    if (fill == NULL) {
        return;
    }
    int32_t h = lv_obj_get_height(vol_bar);
    int32_t fill_h = (int32_t)((int64_t)h * level_percent / 100);
    if (fill_h < 0) {
        fill_h = 0;
    }
    lv_obj_set_height(fill, (lv_coord_t)fill_h);
    lv_obj_align(fill, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_clear_flag(vol_bar, LV_OBJ_FLAG_HIDDEN);
    if (s_vol_bar_hide_timer != NULL) {
        lv_timer_reset(s_vol_bar_hide_timer);
    } else {
        s_vol_bar_hide_timer = lv_timer_create(vol_bar_hide_timer_cb, 1000, vol_bar);
        lv_timer_set_repeat_count(s_vol_bar_hide_timer, 1);
    }
}

static void bt_ui_cover_task(void *arg)
{
    bt_ui_t *ui = (bt_ui_t *)arg;
    ui_cover_msg_t msg = {0};
    while (true) {
        if (xQueueReceive(ui->cover_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (ui->media != NULL && msg.data != NULL && msg.size > 0) {
            lvgl_port_lock(0);
            bt_ui_media_set_cover_image(ui->media, msg.data, msg.size);
            lvgl_port_unlock();
        } else if (msg.data != NULL) {
            free(msg.data);
        }
    }
}

esp_err_t bt_ui_init(void)
{
    dev_display_lcd_handles_t *lcd_handles = NULL;
    dev_display_lcd_config_t *lcd_cfg = NULL;
    esp_err_t ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&lcd_handles);
    if (ret != ESP_OK || lcd_handles == NULL) {
        ESP_LOGE(TAG, "Failed to get LCD handle: %s", esp_err_to_name(ret));
        return ret == ESP_OK ? ESP_ERR_INVALID_STATE : ret;
    }
    ret = esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&lcd_cfg);
    if (ret != ESP_OK || lcd_cfg == NULL) {
        ESP_LOGE(TAG, "Failed to get LCD config: %s", esp_err_to_name(ret));
        return ret == ESP_OK ? ESP_ERR_INVALID_STATE : ret;
    }

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port init failed");

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_handles->io_handle,
        .panel_handle = lcd_handles->panel_handle,
        .buffer_size = lcd_cfg->lcd_width * LVGL_DRAW_BUF_LINES,
        .double_buffer = true,
        .hres = lcd_cfg->lcd_width,
        .vres = lcd_cfg->lcd_height,
        .monochrome = false,
        .rotation = {
            .swap_xy = lcd_cfg->swap_xy,
            .mirror_x = lcd_cfg->mirror_x,
            .mirror_y = lcd_cfg->mirror_y,
        },
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif  /* LVGL_VERSION_MAJOR >= 9 */
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
        },
    };

    lv_disp_t *disp = NULL;
    if (strcmp(lcd_cfg->sub_type, "rgb") == 0) {
#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT
        const lvgl_port_display_rgb_cfg_t rgb_cfg = {
            .flags = {
                .bb_mode = lcd_cfg->sub_cfg.rgb.panel_config.bounce_buffer_size_px > 0,
                .avoid_tearing = lcd_cfg->sub_cfg.rgb.panel_config.num_fbs > 1,
            },
        };
        disp_cfg.flags.direct_mode = rgb_cfg.flags.avoid_tearing;
        disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT */
    } else {
        disp = lvgl_port_add_disp(&disp_cfg);
    }
    if (disp == NULL) {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return ESP_FAIL;
    }

#ifdef CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUPPORT
    dev_lcd_touch_handles_t *touch_handles = NULL;
    ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_LCD_TOUCH, (void **)&touch_handles);
    if (ret == ESP_OK && touch_handles != NULL && touch_handles->touch_handle != NULL) {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = disp,
            .handle = touch_handles->touch_handle,
        };
        if (lvgl_port_add_touch(&touch_cfg) == NULL) {
            ESP_LOGW(TAG, "Failed to add LVGL touch input");
        }
    } else {
        ESP_LOGW(TAG, "LCD touch unavailable: %s", esp_err_to_name(ret));
    }
#endif  /* CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUPPORT */
    return ESP_OK;
}

bt_ui_t *bt_ui_create(const char *device_name, const bt_ui_config_t *config)
{
    bt_ui_t *ui = calloc(1, sizeof(bt_ui_t));
    if (ui == NULL) {
        ESP_LOGE(TAG, "Failed to allocate UI handle");
        return NULL;
    }
    ui->volume = 50;

    /* Cover-art queue */
    ui->cover_queue = xQueueCreate(UI_COVER_QUEUE_SIZE, sizeof(ui_cover_msg_t));
    if (ui->cover_queue == NULL) {
        ESP_LOGW(TAG, "Create UI cover queue failed");
        free(ui);
        return NULL;
    }
    BaseType_t task_ret = xTaskCreate(bt_ui_cover_task, "ui_cover", UI_COVER_TASK_STACK_SIZE,
                                      ui, UI_COVER_TASK_PRIO, NULL);
    if (task_ret != pdPASS) {
        vQueueDelete(ui->cover_queue);
        free(ui);
        ESP_LOGW(TAG, "Create UI cover task failed");
        return NULL;
    }

    lvgl_port_lock(0);
    lv_obj_t *scr = lv_scr_act();
    ui->splash = bt_ui_splash_create(scr, device_name);

    ui->main = lv_obj_create(scr);
    lv_obj_remove_style_all(ui->main);
    lv_obj_set_size(ui->main, BT_UI_WIDTH, BT_UI_HEIGHT);
    lv_obj_align(ui->main, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(ui->main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ui->main, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(ui->main, LV_OBJ_FLAG_HIDDEN);

    ui->tabview = lv_tabview_create(ui->main);
    lv_tabview_set_tab_bar_size(ui->tabview, 0);
    lv_obj_set_pos(ui->tabview, 0, 0);
    lv_obj_set_size(ui->tabview, BT_UI_WIDTH, BT_UI_HEIGHT);

    lv_obj_t *tab_dialer = lv_tabview_add_tab(ui->tabview, "");
    lv_obj_t *tab_music = lv_tabview_add_tab(ui->tabview, "");
    lv_obj_clear_flag(tab_dialer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(tab_music, LV_OBJ_FLAG_SCROLLABLE);

    ui->dialer = bt_ui_dialer_create(tab_dialer,
                                     config ? config->dial_cb : NULL, config ? config->dial_ctx : NULL,
                                     config ? config->end_call_cb : NULL, config ? config->end_call_ctx : NULL,
                                     config ? config->answer_call_cb : NULL, config ? config->answer_call_ctx : NULL);
    ui->media = bt_ui_media_create(tab_music,
                                   config ? config->play_pause_cb : NULL, config ? config->play_pause_ctx : NULL,
                                   config ? config->prev_cb : NULL, config ? config->next_cb : NULL,
                                   config ? config->prev_next_ctx : NULL);
    ui->volume_bar = bt_ui_volume_bar_create(ui->main);
    bt_ui_volume_bar_set_level(ui->volume_bar, ui->volume);
    lv_tabview_set_act(ui->tabview, 1, LV_ANIM_OFF);
    lvgl_port_unlock();

    return ui;
}

void bt_ui_set_connected(bt_ui_t *ui, bool connected)
{
    if (ui == NULL || ui->splash == NULL || ui->main == NULL) {
        return;
    }
    lvgl_port_lock(0);
    if (connected) {
        lv_obj_add_flag(ui->splash, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui->main, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(ui->splash, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->main, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_port_unlock();
}

void bt_ui_update_volume(bt_ui_t *ui, int volume)
{
    if (ui == NULL) {
        return;
    }
    ui->volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    if (ui->volume_bar == NULL) {
        return;
    }
    lvgl_port_lock(0);
    bt_ui_volume_bar_set_level(ui->volume_bar, ui->volume);
    lvgl_port_unlock();
}

int bt_ui_get_volume(const bt_ui_t *ui)
{
    return ui ? ui->volume : 0;
}

void bt_ui_update_playback_status(bt_ui_t *ui, uint32_t play_status)
{
    if (ui == NULL || ui->media == NULL) {
        return;
    }
    lvgl_port_lock(0);
    bt_ui_media_set_playing(ui->media, play_status == ESP_BT_AUDIO_PLAYBACK_STATUS_PLAYING);
    lvgl_port_unlock();
}

void bt_ui_update_track(bt_ui_t *ui, const char *title, const char *artist)
{
    if (ui == NULL || ui->media == NULL) {
        return;
    }
    lvgl_port_lock(0);
    bt_ui_media_set_track(ui->media, title, artist);
    lvgl_port_unlock();
}

static bt_ui_media_stream_type_t stream_type_from_profile(esp_bt_audio_stream_profile_t profile)
{
    switch (profile) {
        case ESP_BT_AUDIO_STREAM_PROFILE_LE_UNICAST:
            return BT_UI_MEDIA_STREAM_TYPE_CIS;
        case ESP_BT_AUDIO_STREAM_PROFILE_LE_BROADCAST:
            return BT_UI_MEDIA_STREAM_TYPE_BIS;
        default:
            return BT_UI_MEDIA_STREAM_TYPE_UNKNOWN;
    }
}

void bt_ui_update_stream_type(bt_ui_t *ui, esp_bt_audio_stream_handle_t stream,
                              esp_bt_audio_stream_state_t state)
{
    if (ui == NULL || ui->media == NULL || stream == NULL) {
        return;
    }
    if (state == ESP_BT_AUDIO_STREAM_STATE_STARTED) {
        esp_bt_audio_stream_profile_t profile = ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN;
        if (esp_bt_audio_stream_get_profile(stream, &profile) != ESP_OK) {
            return;
        }
        bt_ui_media_stream_type_t type = stream_type_from_profile(profile);
        if (type == BT_UI_MEDIA_STREAM_TYPE_UNKNOWN) {
            return;
        }
        ui->stream_type_stream = stream;
        ui->stream_type_is_broadcast = (type == BT_UI_MEDIA_STREAM_TYPE_BIS);
        lvgl_port_lock(0);
        bt_ui_media_set_stream_type(ui->media, type);
        if (ui->stream_type_is_broadcast) {
            bt_ui_media_set_track(ui->media, "正在收听广播", "");
        }
        lvgl_port_unlock();
    } else if ((state == ESP_BT_AUDIO_STREAM_STATE_STOPPED || state == ESP_BT_AUDIO_STREAM_STATE_RELEASED) &&
               ui->stream_type_stream == stream) {
        bool clear_broadcast_title = ui->stream_type_is_broadcast;
        ui->stream_type_stream = NULL;
        ui->stream_type_is_broadcast = false;
        lvgl_port_lock(0);
        bt_ui_media_set_stream_type(ui->media, BT_UI_MEDIA_STREAM_TYPE_UNKNOWN);
        if (clear_broadcast_title) {
            bt_ui_media_set_track(ui->media, "", "");
        }
        lvgl_port_unlock();
    }
}

void bt_ui_update_call_state(bt_ui_t *ui, int state, const char *number)
{
    if (ui == NULL || ui->dialer == NULL) {
        return;
    }
    lvgl_port_lock(0);
    bt_ui_dialer_set_call_state(ui->dialer, state, number);
    /* Switch to the dialer tab on any call activity; return to media when idle */
    if (ui->tabview != NULL) {
        lv_tabview_set_act(ui->tabview, state != 0 ? 0 : 1, LV_ANIM_ON);
    }
    lvgl_port_unlock();
}

void bt_ui_post_cover(bt_ui_t *ui, const uint8_t *data, size_t size)
{
    if (ui == NULL || ui->cover_queue == NULL || data == NULL || size == 0) {
        return;
    }
    uint8_t *copy = heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    if (copy == NULL) {
        ESP_LOGW(TAG, "No memory for cover art copy, size %u", (unsigned)size);
        return;
    }
    memcpy(copy, data, size);
    ui_cover_msg_t msg = {
        .data = copy,
        .size = size,
    };
    if (xQueueSend(ui->cover_queue, &msg, 0) != pdTRUE) {
        free(copy);
        ESP_LOGW(TAG, "Cover art UI queue full, drop update");
    }
}

lv_obj_t *bt_ui_splash_create(lv_obj_t *parent, const char *device_name)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, BT_UI_WIDTH, BT_UI_HEIGHT);
    lv_obj_center(root);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x102840), 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(root);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(label, BT_UI_FONT_LARGE, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(label, "Device Name:\n%s", device_name && device_name[0] ? device_name : "(BT)");
    lv_obj_center(label);
    return root;
}

/**
 * @brief  Cached LVGL widgets and callback state used by the media page.
 */
typedef struct {
    lv_obj_t *play_btn;               /*!< Play/pause button */
    lv_obj_t *title_label;            /*!< Track title label */
    lv_obj_t *artist_label;           /*!< Track artist label */
    lv_obj_t *cover_cont;             /*!< Cover-art container */
    lv_obj_t *cover_img;              /*!< Decoded cover-art image */
    lv_obj_t *cover_placeholder;      /*!< Placeholder shown when no cover art is available */
    lv_obj_t *cover_type_label;       /*!< Fallback text/icon label for stream type */
    lv_obj_t *cover_type_desc_label;  /*!< Broadcast location description label */
    lv_obj_t *cover_type_img;         /*!< Stream-type image label */
    uint8_t  *cover_data;             /*!< Decoded cover-art buffer */
    size_t    cover_size;             /*!< Decoded cover-art buffer size */
#if LVGL_VERSION_MAJOR >= 9
    lv_image_dsc_t  cover_dsc;      /*!< Persistent descriptor for LVGL (src points here) */
#else
    lv_img_dsc_t  cover_img_dsc;   /*!< Persistent descriptor for LVGL (src points here) */
#endif  /* LVGL_VERSION_MAJOR >= 9 */
    void (*play_pause_cb)(bool want_play, void *ctx);  /*!< Play/pause callback */
    void *play_pause_ctx;                              /*!< Context passed to play_pause_cb */
    void (*prev_cb)(void *ctx);                        /*!< Previous-track callback */
    void (*next_cb)(void *ctx);                        /*!< Next-track callback */
    void *prev_next_ctx;                               /*!< Context passed to prev_cb and next_cb */
#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
    lv_obj_t *speaker_img;          /*!< Speaker-location illustration */
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */
} bt_ui_media_refs_t;

static void btn_play_pause_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *root = lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(btn)));
    bt_ui_media_refs_t *refs = root ? (bt_ui_media_refs_t *)lv_obj_get_user_data(root) : NULL;
    if (refs && refs->play_pause_cb) {
        /* VALUE_CHANGED runs after toggle: checked => user requested play */
        bool want_play = lv_obj_has_state(btn, LV_STATE_CHECKED);
        refs->play_pause_cb(want_play, refs->play_pause_ctx);
    }
}

static void btn_prev_click_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *root = lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(btn)));
    bt_ui_media_refs_t *refs = root ? (bt_ui_media_refs_t *)lv_obj_get_user_data(root) : NULL;
    if (refs && refs->prev_cb) {
        refs->prev_cb(refs->prev_next_ctx);
    }
}

static void btn_next_click_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *root = lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(btn)));
    bt_ui_media_refs_t *refs = root ? (bt_ui_media_refs_t *)lv_obj_get_user_data(root) : NULL;
    if (refs && refs->next_cb) {
        refs->next_cb(refs->prev_next_ctx);
    }
}

void bt_ui_media_set_playing(lv_obj_t *media_root, bool playing)
{
    if (media_root == NULL) {
        return;
    }
    bt_ui_media_refs_t *refs = (bt_ui_media_refs_t *)lv_obj_get_user_data(media_root);
    if (refs == NULL || refs->play_btn == NULL) {
        return;
    }
    lv_obj_t *lbl = lv_obj_get_child(refs->play_btn, 0);
    if (lbl != NULL && lv_obj_check_type(lbl, &lv_label_class)) {
        lv_label_set_text(lbl, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
        if (playing) {
            lv_obj_add_state(refs->play_btn, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(refs->play_btn, LV_STATE_CHECKED);
        }
    }
}

void bt_ui_media_set_track(lv_obj_t *media_root, const char *title, const char *artist)
{
    if (media_root == NULL) {
        return;
    }
    bt_ui_media_refs_t *refs = (bt_ui_media_refs_t *)lv_obj_get_user_data(media_root);
    if (refs == NULL || refs->title_label == NULL) {
        return;
    }
    /* NULL means leave current; UI labels hold the state across partial updates */
    if (title != NULL) {
        lv_label_set_text(refs->title_label, title[0] != '\0' ? title : "No music playing");
    }
    if (refs->artist_label != NULL && artist != NULL) {
        lv_label_set_text(refs->artist_label, artist[0] != '\0' ? artist : "");
    }
}

void bt_ui_media_set_stream_type(lv_obj_t *media_root, bt_ui_media_stream_type_t type)
{
    if (media_root == NULL) {
        return;
    }
    bt_ui_media_refs_t *refs = (bt_ui_media_refs_t *)lv_obj_get_user_data(media_root);
    if (refs == NULL || refs->cover_placeholder == NULL || refs->cover_type_label == NULL) {
        return;
    }

    const lv_image_dsc_t *icon = NULL;
#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
    if (type == BT_UI_MEDIA_STREAM_TYPE_CIS) {
        icon = &cis_stream_icon;
    } else if (type == BT_UI_MEDIA_STREAM_TYPE_BIS) {
        icon = &bis_stream_icon;
    }
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */
    if (icon != NULL && refs->cover_type_img != NULL) {
        lv_image_set_src(refs->cover_type_img, icon);
        lv_obj_clear_flag(refs->cover_type_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(refs->cover_type_label, LV_OBJ_FLAG_HIDDEN);
        if (refs->cover_type_desc_label != NULL) {
            lv_obj_add_flag(refs->cover_type_desc_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (refs->cover_img != NULL) {
            lv_obj_add_flag(refs->cover_img, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        if (refs->cover_type_img != NULL) {
            lv_obj_add_flag(refs->cover_type_img, LV_OBJ_FLAG_HIDDEN);
        }
        lv_label_set_text(refs->cover_type_label, LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_color(refs->cover_type_label, lv_color_hex(0xa8b8c8), 0);
        lv_obj_clear_flag(refs->cover_type_label, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(refs->cover_placeholder, LV_OBJ_FLAG_HIDDEN);
}

void bt_ui_media_set_cover_image(lv_obj_t *media_root, const uint8_t *data, size_t size)
{
    ESP_LOGD(TAG, "Set cover image: media_root=%p data=%p size=%u", (void *)media_root, (void *)data,
             (unsigned)size);
    if (media_root == NULL) {
        ESP_LOGW(TAG, "Set cover image: media_root is NULL");
        return;
    }
    bt_ui_media_refs_t *refs = (bt_ui_media_refs_t *)lv_obj_get_user_data(media_root);
    if (refs == NULL || refs->cover_cont == NULL) {
        ESP_LOGW(TAG, "Set cover image: refs=%p or cover_cont is NULL", (void *)refs);
        return;
    }
    if (data == NULL || size == 0) {
        ESP_LOGD(TAG, "Set cover image: clear cover, data=%p size=%u", (void *)data, (unsigned)size);
        if (refs->cover_img != NULL) {
            lv_obj_add_flag(refs->cover_img, LV_OBJ_FLAG_HIDDEN);
        }
        if (refs->cover_placeholder != NULL) {
            lv_obj_clear_flag(refs->cover_placeholder, LV_OBJ_FLAG_HIDDEN);
        }
        if (refs->cover_data != NULL) {
            free(refs->cover_data);
            refs->cover_data = NULL;
            refs->cover_size = 0;
        }
        return;
    }

    esp_jpeg_image_cfg_t cfg = {
        .indata = (uint8_t *)data,
        .indata_size = (uint32_t)size,
        .outbuf = NULL,
        .outbuf_size = 0,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,
        .flags = {.swap_color_bytes = 1},
        .advanced = {.working_buffer = NULL, .working_buffer_size = 0},
        .priv = {0},
    };
    esp_jpeg_image_output_t info;
    esp_err_t err = esp_jpeg_get_image_info(&cfg, &info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set cover image failed: esp_jpeg_get_image_info scale 1/1 failed, %s",
                 esp_err_to_name(err));
        free((void *)data);
        return;
    }
    uint32_t src_w = (uint32_t)info.width;
    uint32_t src_h = (uint32_t)info.height;
    ESP_LOGD(TAG, "Set cover image: jpeg info %" PRIu32 "x%" PRIu32 " output_len=%u", src_w, src_h,
             (unsigned)info.output_len);
    esp_jpeg_image_scale_t scale = JPEG_IMAGE_SCALE_0;
    if (src_w > COVER_IMG_SIZE || src_h > COVER_IMG_SIZE) {
        if (src_w / 2 <= COVER_IMG_SIZE && src_h / 2 <= COVER_IMG_SIZE) {
            scale = JPEG_IMAGE_SCALE_1_2;
        } else if (src_w / 4 <= COVER_IMG_SIZE && src_h / 4 <= COVER_IMG_SIZE) {
            scale = JPEG_IMAGE_SCALE_1_4;
        } else {
            scale = JPEG_IMAGE_SCALE_1_8;
        }
    }
    cfg.out_scale = scale;
    if (esp_jpeg_get_image_info(&cfg, &info) != ESP_OK) {
        ESP_LOGE(TAG, "Set cover image failed: esp_jpeg_get_image_info scale %d failed", (int)scale);
        free((void *)data);
        return;
    }
    ESP_LOGD(TAG, "Set cover image: decode scale=%d out %ux%u output_len=%u", (int)scale,
             (unsigned)info.width, (unsigned)info.height, (unsigned)info.output_len);

    uint8_t *outbuf = (uint8_t *)malloc(info.output_len);
    if (outbuf == NULL) {
        ESP_LOGE(TAG, "Set cover image failed: malloc(%u) failed", (unsigned)info.output_len);
        free((void *)data);
        return;
    }
    cfg.outbuf = outbuf;
    cfg.outbuf_size = (uint32_t)info.output_len;
    esp_jpeg_image_output_t outimg;
    err = esp_jpeg_decode(&cfg, &outimg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set cover image failed: esp_jpeg_decode failed, %s", esp_err_to_name(err));
        free(outbuf);
        free((void *)data);
        return;
    }
    ESP_LOGD(TAG, "Set cover image: decoded %ux%u output_len=%u", (unsigned)outimg.width,
             (unsigned)outimg.height, (unsigned)outimg.output_len);
    free((void *)data);

    if (refs->cover_data != NULL) {
        free(refs->cover_data);
    }
    refs->cover_data = outbuf;
    refs->cover_size = outimg.output_len;

    if (refs->cover_img != NULL) {
        uint32_t stride = (uint32_t)outimg.width * 2;  /* RGB565 = 2 bytes per pixel */
#if LVGL_VERSION_MAJOR >= 9
        memset(&refs->cover_dsc, 0, sizeof(refs->cover_dsc));
        refs->cover_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        refs->cover_dsc.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
        refs->cover_dsc.header.flags = 0;
        refs->cover_dsc.header.w = (int32_t)outimg.width;
        refs->cover_dsc.header.h = (int32_t)outimg.height;
        refs->cover_dsc.header.stride = (uint32_t)stride;
        refs->cover_dsc.data_size = (uint32_t)outimg.output_len;
        refs->cover_dsc.data = refs->cover_data;
        lv_image_set_src(refs->cover_img, &refs->cover_dsc);
#else
        memset(&refs->cover_img_dsc, 0, sizeof(refs->cover_img_dsc));
        refs->cover_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
        refs->cover_img_dsc.header.w = (int32_t)outimg.width;
        refs->cover_img_dsc.header.h = (int32_t)outimg.height;
        refs->cover_img_dsc.header.stride = (uint32_t)stride;
        refs->cover_img_dsc.data_size = (uint32_t)outimg.output_len;
        refs->cover_img_dsc.data = refs->cover_data;
        lv_img_set_src(refs->cover_img, &refs->cover_img_dsc);
#endif  /* LVGL_VERSION_MAJOR >= 9 */
        lv_obj_clear_flag(refs->cover_img, LV_OBJ_FLAG_HIDDEN);
    }
    if (refs->cover_placeholder != NULL) {
        lv_obj_add_flag(refs->cover_placeholder, LV_OBJ_FLAG_HIDDEN);
    }
    ESP_LOGD(TAG, "Set cover image: done, cover visible %ux%u", (unsigned)outimg.width,
             (unsigned)outimg.height);
}

lv_obj_t *bt_ui_media_create(lv_obj_t *parent,
                             void (*play_pause_cb)(bool want_play, void *ctx), void *play_pause_ctx,
                             void (*prev_cb)(void *ctx), void (*next_cb)(void *ctx), void *prev_next_ctx)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, BT_UI_WIDTH, CONTENT_H);
    lv_obj_center(root);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x102840), 0);

    lv_obj_t *cover_cont = lv_obj_create(root);
    lv_obj_remove_style_all(cover_cont);
    lv_obj_set_size(cover_cont, COVER_SIZE, COVER_SIZE);
    lv_obj_align(cover_cont, LV_ALIGN_LEFT_MID, MEDIA_LEFT_PAD, 0);
    lv_obj_clear_flag(cover_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cover_placeholder = lv_obj_create(cover_cont);
    lv_obj_remove_style_all(cover_placeholder);
    lv_obj_set_size(cover_placeholder, COVER_IMG_SIZE, COVER_IMG_SIZE);
    lv_obj_align(cover_placeholder, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(cover_placeholder, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cover_placeholder, lv_color_hex(0x3d4f63), 0);
    lv_obj_set_style_radius(cover_placeholder, 8, 0);
    lv_obj_clear_flag(cover_placeholder, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cover_type_label = lv_label_create(cover_placeholder);
    lv_label_set_text(cover_type_label, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(cover_type_label, BT_UI_FONT_COVER_ICON, 0);
    lv_obj_set_style_text_color(cover_type_label, lv_color_hex(0xa8b8c8), 0);
    lv_obj_center(cover_type_label);

    lv_obj_t *cover_type_desc_label = lv_label_create(cover_placeholder);
    lv_label_set_text(cover_type_desc_label, "");
    lv_obj_set_style_text_font(cover_type_desc_label, BT_UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(cover_type_desc_label, lv_color_hex(0xa8b8c8), 0);
    lv_obj_align_to(cover_type_desc_label, cover_type_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    lv_obj_add_flag(cover_type_desc_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *cover_type_img = lv_image_create(cover_placeholder);
    lv_obj_remove_style_all(cover_type_img);
    lv_obj_set_size(cover_type_img, 280, 187);
    lv_obj_center(cover_type_img);
    lv_obj_add_flag(cover_type_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cover_type_img, LV_OBJ_FLAG_SCROLLABLE);

#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *cover_img = lv_image_create(cover_cont);
#else
    lv_obj_t *cover_img = lv_img_create(cover_cont);
#endif  /* LVGL_VERSION_MAJOR >= 9 */
    lv_obj_set_size(cover_img, COVER_IMG_SIZE, COVER_IMG_SIZE);
    lv_obj_align(cover_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(cover_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cover_img, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *right_panel = lv_obj_create(root);
    lv_obj_remove_style_all(right_panel);
    lv_obj_set_size(right_panel, RIGHT_PANEL_W, COVER_SIZE);
    lv_obj_align(right_panel, LV_ALIGN_LEFT_MID, RIGHT_PANEL_X, 0);
    lv_obj_set_flex_flow(right_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(right_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name_cont = lv_obj_create(right_panel);
    lv_obj_remove_style_all(name_cont);
#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
    lv_obj_set_size(name_cont, RIGHT_PANEL_W, LE_TITLE_AREA_H);
#else
    lv_obj_set_size(name_cont, RIGHT_PANEL_W, NAME_AREA_H);
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */
    lv_obj_clear_flag(name_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name_label = lv_label_create(name_cont);
    lv_label_set_text(name_label, "-----");
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xe8eef4), 0);
    lv_obj_set_width(name_label, RIGHT_PANEL_W);
    lv_obj_align(name_label, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_font(name_label, BT_UI_FONT_LARGE, 0);
    lv_obj_set_style_text_line_space(name_label, -4, 0);

    lv_obj_t *artist_label = lv_label_create(name_cont);
    lv_label_set_text(artist_label, "");
    lv_label_set_long_mode(artist_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(artist_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(artist_label, lv_color_hex(0x9098a0), 0);
    lv_obj_set_width(artist_label, RIGHT_PANEL_W);
    lv_obj_align_to(artist_label, name_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 44);
    lv_obj_set_style_text_font(artist_label, BT_UI_FONT_MEDIUM, 0);
    lv_obj_set_style_text_line_space(artist_label, -4, 0);
#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
    lv_obj_add_flag(artist_label, LV_OBJ_FLAG_HIDDEN);
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */

#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
    lv_obj_t *speaker_cont = lv_obj_create(right_panel);
    lv_obj_remove_style_all(speaker_cont);
    lv_obj_set_size(speaker_cont, RIGHT_PANEL_W, 0);
    lv_obj_set_flex_grow(speaker_cont, 1);
    lv_obj_clear_flag(speaker_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *speaker_img = lv_image_create(speaker_cont);
    lv_obj_remove_style_all(speaker_img);
    lv_obj_set_size(speaker_img, SPEAKER_IMG_W, SPEAKER_IMG_H);
    lv_obj_clear_flag(speaker_img, LV_OBJ_FLAG_SCROLLABLE);
#if CONFIG_GMF_EXAMPLE_LE_LOCATION_FRONT_LEFT
    lv_image_set_src(speaker_img, speaker_img_for_location(ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT));
#elif CONFIG_GMF_EXAMPLE_LE_LOCATION_FRONT_RIGHT
    lv_image_set_src(speaker_img, speaker_img_for_location(ESP_BT_AUDIO_AUDIO_LOC_FRONT_RIGHT));
#else
    lv_image_set_src(speaker_img, speaker_img_for_location(0));
#endif  /* CONFIG_GMF_EXAMPLE_LE_LOCATION_FRONT_LEFT */
    lv_obj_center(speaker_img);
#else
    lv_obj_t *spacer = lv_obj_create(right_panel);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, 0, 0);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */

    lv_obj_t *btn_cont = lv_obj_create(right_panel);
    lv_obj_remove_style_all(btn_cont);
    lv_obj_set_size(btn_cont, RIGHT_PANEL_W, BTN_ROW_HEIGHT);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLLABLE);

#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *btn_prev = lv_button_create(btn_cont);
#else
    lv_obj_t *btn_prev = lv_btn_create(btn_cont);
#endif  /* LVGL_VERSION_MAJOR >= 9 */
    lv_obj_set_size(btn_prev, BTN_W, BTN_H);
    lv_obj_set_style_shadow_width(btn_prev, 0, 0);
    lv_obj_t *lbl_prev = lv_label_create(btn_prev);
    lv_label_set_text(lbl_prev, LV_SYMBOL_PREV);
    lv_obj_set_style_text_font(lbl_prev, BT_UI_FONT_ICON, 0);
    lv_obj_center(lbl_prev);
    lv_obj_add_event_cb(btn_prev, btn_prev_click_cb, LV_EVENT_CLICKED, NULL);

#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *btn_play = lv_button_create(btn_cont);
#else
    lv_obj_t *btn_play = lv_btn_create(btn_cont);
#endif  /* LVGL_VERSION_MAJOR >= 9 */
    lv_obj_set_size(btn_play, BTN_W, BTN_H);
    lv_obj_set_style_shadow_width(btn_play, 0, 0);
    lv_obj_add_flag(btn_play, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_t *lbl_play = lv_label_create(btn_play);
    lv_label_set_text(lbl_play, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(lbl_play, BT_UI_FONT_ICON, 0);
    lv_obj_center(lbl_play);
    lv_obj_add_event_cb(btn_play, btn_play_pause_click_cb, LV_EVENT_VALUE_CHANGED, NULL);

#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *btn_next = lv_button_create(btn_cont);
#else
    lv_obj_t *btn_next = lv_btn_create(btn_cont);
#endif  /* LVGL_VERSION_MAJOR >= 9 */
    lv_obj_set_size(btn_next, BTN_W, BTN_H);
    lv_obj_set_style_shadow_width(btn_next, 0, 0);
    lv_obj_t *lbl_next = lv_label_create(btn_next);
    lv_label_set_text(lbl_next, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_font(lbl_next, BT_UI_FONT_ICON, 0);
    lv_obj_center(lbl_next);
    lv_obj_add_event_cb(btn_next, btn_next_click_cb, LV_EVENT_CLICKED, NULL);

    static bt_ui_media_refs_t s_media_refs;
    s_media_refs.play_btn = btn_play;
    s_media_refs.title_label = name_label;
    s_media_refs.artist_label = artist_label;
    s_media_refs.cover_cont = cover_cont;
    s_media_refs.cover_img = cover_img;
    s_media_refs.cover_placeholder = cover_placeholder;
    s_media_refs.cover_type_label = cover_type_label;
    s_media_refs.cover_type_desc_label = cover_type_desc_label;
    s_media_refs.cover_type_img = cover_type_img;
    s_media_refs.cover_data = NULL;
    s_media_refs.cover_size = 0;
    s_media_refs.play_pause_cb = play_pause_cb;
    s_media_refs.play_pause_ctx = play_pause_ctx;
    s_media_refs.prev_cb = prev_cb;
    s_media_refs.next_cb = next_cb;
    s_media_refs.prev_next_ctx = prev_next_ctx;
#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
    s_media_refs.speaker_img = speaker_img;
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */
    lv_obj_set_user_data(root, &s_media_refs);

    return root;
}

static void dialer_digit_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *lbl = (lv_obj_t *)lv_obj_get_child(btn, 0);
    const char *txt = lbl ? lv_label_get_text(lbl) : NULL;
    if (txt == NULL || dialer_number_label == NULL) {
        return;
    }
    size_t len = lv_strlen(dialer_number_buf);
    if (len + 2 >= DIALER_BUF_SIZE) {
        return;
    }
    if (txt[0] != '\0') {
        dialer_number_buf[len] = txt[0];
        dialer_number_buf[len + 1] = '\0';
    }
    lv_label_set_text(dialer_number_label, dialer_number_buf[0] ? dialer_number_buf : "Enter Number");
}

static void dialer_back_click_cb(lv_event_t *e)
{
    (void)e;
    if (dialer_number_label == NULL) {
        return;
    }
    size_t len = lv_strlen(dialer_number_buf);
    if (len > 0) {
        dialer_number_buf[len - 1] = '\0';
    }
    lv_label_set_text(dialer_number_label, dialer_number_buf[0] ? dialer_number_buf : "Enter Number");
}

static void dialer_call_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    dialer_call_ctx_t *ctx = (dialer_call_ctx_t *)lv_obj_get_user_data(btn);
    if (ctx == NULL) {
        return;
    }
    if (ctx->in_call) {
        if (ctx->end_call_cb) {
            ctx->end_call_cb(ctx->end_call_cb_ctx);
        }
        return;
    }
    if (ctx->call_cb) {
        ctx->call_cb(dialer_number_buf, ctx->call_cb_ctx);
    }
}

static void dialer_answer_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    dialer_call_ctx_t *ctx = (dialer_call_ctx_t *)lv_obj_get_user_data(btn);
    if (ctx && ctx->answer_call_cb) {
        ctx->answer_call_cb(ctx->answer_call_cb_ctx);
    }
}

lv_obj_t *bt_ui_dialer_create(lv_obj_t *parent,
                              void (*call_cb)(const char *number, void *ctx), void *call_cb_ctx,
                              void (*end_call_cb)(void *ctx), void *end_call_cb_ctx,
                              void (*answer_call_cb)(void *ctx), void *answer_call_cb_ctx)
{
    dialer_number_buf[0] = '\0';
    dialer_number_label = NULL;

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, BT_UI_WIDTH, CONTENT_H);
    lv_obj_center(root);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x102840), 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    dialer_number_label = lv_label_create(root);
    lv_label_set_text(dialer_number_label, "Enter Number");
    lv_obj_set_style_text_color(dialer_number_label, lv_color_hex(0xe8eef4), 0);
    lv_obj_set_width(dialer_number_label, BT_UI_WIDTH / 2 - 64);
    lv_label_set_long_mode(dialer_number_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(dialer_number_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(dialer_number_label, LV_ALIGN_TOP_RIGHT, -44, 88);
    lv_obj_set_style_text_font(dialer_number_label, BT_UI_FONT_LARGE, 0);

    static const char *keys[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"};
    const int cols = 3, rows = 4;
    const int key_sz = DIALER_KEYPAD_SZ;
    int start_x = 46;
    int start_y = (CONTENT_H - (rows * key_sz + (rows - 1) * DIALER_KEY_GAP)) / 2;
    const lv_color_t key_gray = lv_color_hex(0x6a6a6a);

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int i = r * cols + c;
#if LVGL_VERSION_MAJOR >= 9
            lv_obj_t *btn = lv_button_create(root);
#else
            lv_obj_t *btn = lv_btn_create(root);
#endif  /* LVGL_VERSION_MAJOR >= 9 */
            lv_obj_set_size(btn, key_sz, key_sz);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(btn, key_gray, 0);
            lv_obj_set_pos(btn, start_x + c * (key_sz + DIALER_KEY_GAP), start_y + r * (key_sz + DIALER_KEY_GAP));
            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, keys[i]);
            lv_obj_center(lbl);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xe8eef4), 0);
            lv_obj_set_style_text_font(lbl, BT_UI_FONT_LARGE, 0);
            lv_obj_add_event_cb(btn, dialer_digit_click_cb, LV_EVENT_CLICKED, NULL);
        }
    }

#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *call_btn = lv_button_create(root);
#else
    lv_obj_t *call_btn = lv_btn_create(root);
#endif  /* LVGL_VERSION_MAJOR >= 9 */
    lv_obj_set_size(call_btn, key_sz, key_sz);
    lv_obj_set_style_shadow_width(call_btn, 0, 0);
    lv_obj_set_style_radius(call_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(call_btn, lv_color_hex(0x2E7D32), 0);
    lv_obj_set_pos(call_btn, BT_UI_WIDTH - 284, CONTENT_H - key_sz - 54);
    static dialer_call_ctx_t s_dialer_call_ctx;
    s_dialer_call_ctx.call_cb = call_cb;
    s_dialer_call_ctx.call_cb_ctx = call_cb_ctx;
    s_dialer_call_ctx.end_call_cb = end_call_cb;
    s_dialer_call_ctx.end_call_cb_ctx = end_call_cb_ctx;
    s_dialer_call_ctx.answer_call_cb = answer_call_cb;
    s_dialer_call_ctx.answer_call_cb_ctx = answer_call_cb_ctx;
    s_dialer_call_ctx.in_call = false;
    lv_obj_set_user_data(call_btn, &s_dialer_call_ctx);
    lv_obj_add_event_cb(call_btn, dialer_call_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *call_lbl = lv_label_create(call_btn);
    lv_label_set_text(call_lbl, LV_SYMBOL_CALL);
    lv_obj_center(call_lbl);
    lv_obj_set_style_text_color(call_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(call_lbl, BT_UI_FONT_ICON, 0);

#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *back_btn = lv_button_create(root);
#else
    lv_obj_t *back_btn = lv_btn_create(root);
#endif  /* LVGL_VERSION_MAJOR >= 9 */
    lv_obj_set_size(back_btn, key_sz, key_sz);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_set_style_radius(back_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x795548), 0);
    lv_obj_set_pos(back_btn, BT_UI_WIDTH - 158, CONTENT_H - key_sz - 54);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_BACKSPACE);
    lv_obj_center(back_lbl);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(back_lbl, BT_UI_FONT_ICON, 0);
    lv_obj_add_event_cb(back_btn, dialer_back_click_cb, LV_EVENT_CLICKED, NULL);

#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *answer_btn = lv_button_create(root);
#else
    lv_obj_t *answer_btn = lv_btn_create(root);
#endif  /* LVGL_VERSION_MAJOR >= 9 */
    lv_obj_set_size(answer_btn, key_sz, key_sz);
    lv_obj_set_style_shadow_width(answer_btn, 0, 0);
    lv_obj_set_style_radius(answer_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(answer_btn, lv_color_hex(0x2E7D32), 0);
    lv_obj_set_pos(answer_btn, BT_UI_WIDTH - 158, CONTENT_H - key_sz - 54);
    lv_obj_set_user_data(answer_btn, &s_dialer_call_ctx);
    lv_obj_add_event_cb(answer_btn, dialer_answer_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *answer_lbl = lv_label_create(answer_btn);
    lv_label_set_text(answer_lbl, LV_SYMBOL_CALL);
    lv_obj_center(answer_lbl);
    lv_obj_set_style_text_color(answer_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(answer_lbl, BT_UI_FONT_ICON, 0);
    lv_obj_add_flag(answer_btn, LV_OBJ_FLAG_HIDDEN);

    static bt_ui_dialer_refs_t s_dialer_refs;
    s_dialer_refs.number_label = dialer_number_label;
    s_dialer_refs.call_btn = call_btn;
    s_dialer_refs.call_btn_label = call_lbl;
    s_dialer_refs.answer_btn = answer_btn;
    s_dialer_refs.back_btn = back_btn;
    s_dialer_refs.call_ctx = &s_dialer_call_ctx;
    lv_obj_set_user_data(root, &s_dialer_refs);

    return root;
}

void bt_ui_dialer_set_call_state(lv_obj_t *dialer_root, int state, const char *number)
{
    if (dialer_root == NULL) {
        return;
    }
    bt_ui_dialer_refs_t *refs = (bt_ui_dialer_refs_t *)lv_obj_get_user_data(dialer_root);
    if (refs == NULL || refs->number_label == NULL) {
        return;
    }
    const char *state_text = "";
    switch (state) {
        case 1:
            state_text = "Incoming";
            break;
        case 2:
            state_text = "Dialing...";
            break;
        case 3:
            state_text = "Ringing...";
            break;
        case 4:
            state_text = "In call";
            break;
        case 5:
        case 6:
        case 7:
            state_text = "Held";
            break;
        default:
            break;
    }
    if (state != 0 && number != NULL && number[0] != '\0') {
        lv_label_set_text_fmt(refs->number_label, "%s  %s", state_text, number);
    } else if (state != 0) {
        lv_label_set_text(refs->number_label, state_text);
    } else {
        dialer_number_buf[0] = '\0';
        lv_label_set_text(refs->number_label, "Enter Number");
    }
    dialer_call_ctx_t *ctx = (dialer_call_ctx_t *)refs->call_ctx;
    if (ctx != NULL) {
        ctx->in_call = state != 0;
    }

    bool incoming = (state == ESP_BT_AUDIO_CALL_STATE_INCOMING);
    bool active = (state != 0);

    if (refs->call_btn != NULL) {
        lv_obj_set_style_bg_color(refs->call_btn, lv_color_hex(active ? 0xc62828 : 0x2E7D32), 0);
    }
    if (refs->call_btn_label != NULL) {
        lv_label_set_text(refs->call_btn_label, active ? LV_SYMBOL_CLOSE : LV_SYMBOL_CALL);
    }

    if (refs->answer_btn != NULL) {
        if (incoming) {
            lv_obj_clear_flag(refs->answer_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(refs->answer_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (refs->back_btn != NULL) {
        if (active) {
            lv_obj_add_flag(refs->back_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(refs->back_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
