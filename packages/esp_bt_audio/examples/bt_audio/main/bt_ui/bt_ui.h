/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BT_UI_H
#define BT_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_stream.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define BT_UI_WIDTH   800
#define BT_UI_HEIGHT  480

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
 * @param[in]  tech       Connected Bluetooth technology used to update UI indicators.
 */
void bt_ui_set_connected(bt_ui_t *ui, bool connected, esp_bt_audio_tech_t tech);

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
 * @brief  Update media UI when stream state changes.
 *
 * @param[in]  ui      UI handle from bt_ui_create().
 * @param[in]  stream  Stream handle (NULL to ignore).
 * @param[in]  state   Stream state (STARTED, STOPPED, RELEASED, etc.).
 */
void bt_ui_update_stream_state(bt_ui_t *ui, esp_bt_audio_stream_handle_t stream,
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

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* BT_UI_H */
