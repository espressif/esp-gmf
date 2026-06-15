/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BT_UI_H
#define BT_UI_H

#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "lvgl.h"
#include "esp_bt_audio_stream.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define BT_UI_WIDTH   800
#define BT_UI_HEIGHT  480
/**
 * @brief  Media stream type shown in the cover placeholder.
 */
typedef enum {
    BT_UI_MEDIA_STREAM_TYPE_UNKNOWN = 0,  /*!< Unknown or hidden stream type */
    BT_UI_MEDIA_STREAM_TYPE_CIS,          /*!< Connected Isochronous Stream */
    BT_UI_MEDIA_STREAM_TYPE_BIS,          /*!< Broadcast Isochronous Stream */
} bt_ui_media_stream_type_t;

/**
 * @brief  Callback table for BT UI interactions.
 *
 *         The application fills this struct with its own callback
 *         implementations and passes it to bt_ui_create().
 *         All callback pointers are optional — set to NULL if unused.
 */
typedef struct {
    void (*dial_cb)(const char *number, void *ctx);    /*!< Call button pressed with dialed number */
    void *dial_ctx;                                    /*!< Context passed to dial_cb */
    void (*end_call_cb)(void *ctx);                    /*!< End-call / reject button pressed (red × button) */
    void *end_call_ctx;                                /*!< Context passed to end_call_cb */
    void (*answer_call_cb)(void *ctx);                 /*!< Answer button pressed — only shown during incoming state */
    void *answer_call_ctx;                             /*!< Context passed to answer_call_cb */
    void (*play_pause_cb)(bool want_play, void *ctx);  /*!< Play/pause toggled */
    void *play_pause_ctx;                              /*!< Context passed to play_pause_cb */
    void (*prev_cb)(void *ctx);                        /*!< Previous-track button pressed */
    void (*next_cb)(void *ctx);                        /*!< Next-track button pressed */
    void *prev_next_ctx;                               /*!< Context passed to prev_cb and next_cb */
} bt_ui_config_t;

/**
 * @brief  Opaque handle for the BT UI instance.
 *
 *         Created by bt_ui_create() and passed to all other bt_ui_* functions.
 */
typedef struct bt_ui_t bt_ui_t;

/**
 * @brief  Initialize the display hardware (LVGL port, LCD, touch).
 *
 *         Must be called once before bt_ui_create().
 *
 * @return
 *       - ESP_OK                 on success
 *       - ESP_ERR_INVALID_STATE  or ESP_FAIL on failure
 */
esp_err_t bt_ui_init(void);

/**
 * @brief  Create the full BT UI widget tree on the active screen.
 *
 *         Builds splash screen, main container with tabview (dialer + media),
 *         and volume bar.  Starts the internal cover-art task.
 *
 * @param[in]  device_name  Null-terminated device name shown on splash screen.
 * @param[in]  config       Callback table.  May be NULL if no callbacks needed.
 *
 * @return
 *       - Opaque  UI handle on success
 *       - NULL    on failure
 */
bt_ui_t *bt_ui_create(const char *device_name, const bt_ui_config_t *config);

/**
 * @brief  Show or hide the splash / main screen depending on connection state.
 *
 * @param[in]  ui         UI handle from bt_ui_create().
 * @param[in]  connected  true = show main UI; false = show splash.
 */
void bt_ui_set_connected(bt_ui_t *ui, bool connected);

/**
 * @brief  Update the on-screen volume bar and the tracked volume level.
 *
 * @param[in]  ui      UI handle from bt_ui_create().
 * @param[in]  volume  Volume value (clamped to 0–100 internally).
 */
void bt_ui_update_volume(bt_ui_t *ui, int volume);

/**
 * @brief  Return the currently tracked volume level.
 *
 * @param[in]  ui  UI handle from bt_ui_create().
 *
 * @return
 *       - Volume  value (0–100).
 */
int bt_ui_get_volume(const bt_ui_t *ui);

/**
 * @brief  Update the play/pause button to reflect playback state.
 *
 * @param[in]  ui           UI handle from bt_ui_create().
 * @param[in]  play_status  Playback status value (non-zero = playing).
 */
void bt_ui_update_playback_status(bt_ui_t *ui, uint32_t play_status);

/**
 * @brief  Set the track title and artist labels.
 *
 *         Pass NULL to leave a label unchanged, or an empty string to clear it.
 *
 * @param[in]  ui      UI handle from bt_ui_create().
 * @param[in]  title   Track title (NULL to leave unchanged).
 * @param[in]  artist  Track artist (NULL to leave unchanged).
 */
void bt_ui_update_track(bt_ui_t *ui, const char *title, const char *artist);

/**
 * @brief  Update the stream-type icon and broadcast title when stream state changes.
 *
 * @param[in]  ui      UI handle from bt_ui_create().
 * @param[in]  stream  Stream handle (NULL to ignore).
 * @param[in]  state   Stream state (STARTED, STOPPED, RELEASED, etc.).
 */
void bt_ui_update_stream_type(bt_ui_t *ui, esp_bt_audio_stream_handle_t stream,
                              esp_bt_audio_stream_state_t state);

/**
 * @brief  Update the dialer display with the current call state.
 *
 * @param[in]  ui      UI handle from bt_ui_create().
 * @param[in]  state   Call state: 0=idle, 1=incoming, 2=dialing, 3=alerting, 4=active, 5-7=held.
 * @param[in]  number  Remote phone number or URI (NULL when unknown).
 */
void bt_ui_update_call_state(bt_ui_t *ui, int state, const char *number);

/**
 * @brief  Post cover-art image data for display on the media page.
 *
 *         The data is copied; the caller retains ownership.
 *         Pass NULL data or zero size to clear the current cover image.
 *
 * @param[in]  ui    UI handle from bt_ui_create().
 * @param[in]  data  Encoded image data (e.g. JPEG).
 * @param[in]  size  Size of the image data in bytes.
 */
void bt_ui_post_cover(bt_ui_t *ui, const uint8_t *data, size_t size);

/**
 * @brief  Create a splash screen showing the device name.
 *
 *         Displays "Device Name:\n<device_name>". Hide this screen when
 *         Bluetooth is connected and show the main UI instead.
 *
 * @param[in]  parent       Parent LVGL object.
 * @param[in]  device_name  Null-terminated device name string.
 *
 * @return
 *       - Splash  screen root object on success
 *       - NULL    on failure
 */
lv_obj_t *bt_ui_splash_create(lv_obj_t *parent, const char *device_name);

/**
 * @brief  Create the media player page with cover art, track info, and playback controls.
 *
 * @param[in]  parent          Parent LVGL object.
 * @param[in]  play_pause_cb   Optional callback invoked when the play/pause button is toggled.
 * @param[in]  play_pause_ctx  Context passed to play_pause_cb.
 * @param[in]  prev_cb         Optional callback invoked when the previous-track button is pressed.
 * @param[in]  next_cb         Optional callback invoked when the next-track button is pressed.
 * @param[in]  prev_next_ctx   Context passed to prev_cb and next_cb.
 *
 * @return
 *       - Media  root object on success
 *       - NULL   on failure
 */
lv_obj_t *bt_ui_media_create(lv_obj_t *parent,
                             void (*play_pause_cb)(bool want_play, void *ctx), void *play_pause_ctx,
                             void (*prev_cb)(void *ctx), void (*next_cb)(void *ctx), void *prev_next_ctx);

/**
 * @brief  Update the play/pause button to reflect the current playback state.
 *
 * @param[in]  media_root  Media root object returned by bt_ui_media_create().
 * @param[in]  playing     true to show pause icon, false to show play icon.
 */
void bt_ui_media_set_playing(lv_obj_t *media_root, bool playing);

/**
 * @brief  Set the track title and artist labels.
 *
 *         Pass NULL to leave a label unchanged, or an empty string to clear it.
 *
 * @param[in]  media_root  Media root object returned by bt_ui_media_create().
 * @param[in]  title       Track title (NULL to leave unchanged).
 * @param[in]  artist      Track artist (NULL to leave unchanged).
 */
void bt_ui_media_set_track(lv_obj_t *media_root, const char *title, const char *artist);

/**
 * @brief  Set the stream type icon shown in the cover art placeholder.
 *
 * @param[in]  media_root  Media root object returned by bt_ui_media_create().
 * @param[in]  type        Stream type (CIS, BIS, or UNKNOWN to show default icon).
 */
void bt_ui_media_set_stream_type(lv_obj_t *media_root, bt_ui_media_stream_type_t type);

/**
 * @brief  Set the cover art image from encoded image data.
 *
 *         Takes ownership of \a data; the caller must not free it after this call.
 *         Pass NULL data or zero size to clear the current cover image.
 *
 * @param[in]  media_root  Media root object returned by bt_ui_media_create().
 * @param[in]  data        Encoded image data (e.g. JPEG). Ownership is transferred.
 * @param[in]  size        Size of the image data in bytes.
 */
void bt_ui_media_set_cover_image(lv_obj_t *media_root, const uint8_t *data, size_t size);

/**
 * @brief  Create the dialer page with numeric keypad and call buttons.
 *
 * @param[in]  parent                Parent LVGL object.
 * @param[in]  call_cb               Optional callback invoked when the call button starts a call.
 * @param[in]  call_cb_ctx           Context passed to call_cb.
 * @param[in]  end_call_cb           Optional callback invoked when the red × button is pressed (end/reject).
 * @param[in]  end_call_cb_ctx       Context passed to end_call_cb.
 * @param[in]  answer_call_cb        Optional callback invoked when the green answer button is pressed.
 *                                   The button is only shown during the incoming-call state.
 * @param[in]  answer_call_cb_ctx    Context passed to answer_call_cb.
 *
 * @return
 *       - Dialer  root object on success
 *       - NULL    on failure
 */
lv_obj_t *bt_ui_dialer_create(lv_obj_t *parent,
                              void (*call_cb)(const char *number, void *ctx), void *call_cb_ctx,
                              void (*end_call_cb)(void *ctx), void *end_call_cb_ctx,
                              void (*answer_call_cb)(void *ctx), void *answer_call_cb_ctx);

/**
 * @brief  Update the dialer display with the current call state.
 *
 * @param[in]  dialer_root  Dialer root object returned by bt_ui_dialer_create().
 * @param[in]  state        Call state: 0=idle, 1=incoming, 2=dialing, 3=alerting, 4=active, 5-7=held.
 * @param[in]  number       Remote phone number or URI (NULL when unknown).
 */
void bt_ui_dialer_set_call_state(lv_obj_t *dialer_root, int state, const char *number);

/**
 * @brief  Create a vertical volume bar indicator.
 *
 *         The bar auto-hides after a short delay. Call bt_ui_volume_bar_set_level()
 *         on volume-change events to update and reveal the bar.
 *
 * @param[in]  parent  Parent LVGL object.
 *
 * @return
 *       - Volume  bar root object on success
 *       - NULL    on failure
 */
lv_obj_t *bt_ui_volume_bar_create(lv_obj_t *parent);

/**
 * @brief  Set the volume level shown on the bar.
 *
 *         Reveals the bar if hidden and resets the auto-hide timer.
 *
 * @param[in]  vol_bar        Volume bar root object returned by bt_ui_volume_bar_create().
 * @param[in]  level_percent  Volume level (0–100).
 */
void bt_ui_volume_bar_set_level(lv_obj_t *vol_bar, int level_percent);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* BT_UI_H */
