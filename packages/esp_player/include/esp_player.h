/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

/**
 * @file esp_player.h
 * @brief  ESP Player core API interface
 *
 * @note  Core playback API: init, configuration, control, and events.
 *        Supports audio-only, video-only, and A/V playback.
 */

#include <stdint.h>
#include <stdbool.h>

#include "esp_player_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Audio/video data filter masks
 *
 * @note  Passed to esp_player_set_av_mask() / esp_player_data_src_t.av_mask.
 *        Controls extractor filter behavior (passed as extract_mask) and
 *        which decoder/render pipelines are built for the session.
 */

#define ESP_PLAYER_MASK_AUDIO  (1 << 0)
#define ESP_PLAYER_MASK_VIDEO  (1 << 1)
#define ESP_PLAYER_MASK_AV     (ESP_PLAYER_MASK_AUDIO | ESP_PLAYER_MASK_VIDEO)

/**
 * @brief  Player default configuration macro
 */
#define ESP_PLAYER_CONFIG_DEFAULT()  {  \
    .audio_render_hd = NULL,            \
    .video_render_hd = NULL,            \
}

/**
 * @brief  Player configuration structure
 *
 * @note  Passed to esp_player_init(). Assign non-NULL render handles for every
 *        playback path you will enable via esp_player_set_av_mask() /
 *        esp_player_data_src_t.av_mask:
 *        - `ESP_PLAYER_MASK_AUDIO` — `audio_render_hd` required
 *        - `ESP_PLAYER_MASK_VIDEO` — `video_render_hd` required
 *        - `ESP_PLAYER_MASK_AV`    — both required
 *        Pass NULL only for paths that will never be used (e.g. `video_render_hd`
 *        for audio-only). `ESP_PLAYER_CONFIG_DEFAULT()` leaves both NULL; set
 *        handles before calling esp_player_init().
 */
typedef struct {
    void *audio_render_hd;  /*!< esp_audio_render_stream_handle_t; from esp_audio_render_stream_get().
                                 See `https://github.com/espressif/esp-gmf/blob/main/packages/esp_audio_render/include/esp_audio_render.h` for details */
    void *video_render_hd;  /*!< esp_video_render_handle_t; from esp_video_render_create().
                                 One render may host multiple streams with independent zorder.
                                 Player opens the decoded-video stream internally via esp_video_render_stream_open();
                                 default zorder is 0. Use esp_video_render_stream_set_zorder() on other streams to compose above playback video.
                                 See `https://github.com/espressif/esp-gmf/blob/main/packages/esp_video_render/include/esp_video_render.h` for details */
} esp_player_config_t;

/**
 * @brief  Player handle type
 *
 * @note  Opaque handle to an internal player instance. Created by esp_player_init()
 *        and destroyed by esp_player_deinit().
 */
typedef struct esp_player_stream *esp_player_handle_t;

/**
 * @brief  Initialize player
 *
 * @note  Creates a player instance in IDLE state. No media is opened and playback
 *        does not start.
 *        Provide valid audio_render_hd / video_render_hd for paths enabled via
 *        esp_player_set_av_mask(); pass NULL for unused paths.
 *
 * @param[in]   config  Player configuration; must not be NULL
 * @param[out]  handle  Output player handle
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Initialization successful
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid parameters
 *       - ESP_PLAYER_ERR_NO_MEM       Out of memory
 *       - ESP_PLAYER_ERR_FAIL         Initialization failed
 */
esp_player_err_t esp_player_init(esp_player_config_t *config, esp_player_handle_t *handle);

/**
 * @brief  Deinitialize player
 *
 * @note  Stops current playback if running, then frees all related resources.
 *
 * @param[in]  handle  Player handle
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Deinitialization successful
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid handle
 */
esp_player_err_t esp_player_deinit(esp_player_handle_t handle);

/**
 * @brief  Set audio/video data filter mask
 *
 * @note  Selects the playback mode for the session, tracks not covered by the mask are not played.
 *        Call after esp_player_init() and before esp_player_run(); also allowed
 *        in STOPPED or FINISHED to switch mode.
 *        fill:/// and block:/// URLs do not support ESP_PLAYER_MASK_AV.
 *
 * @param[in]  handle  Player handle
 * @param[in]  mask    ESP_PLAYER_MASK_AUDIO, ESP_PLAYER_MASK_VIDEO, or ESP_PLAYER_MASK_AV
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Setting successful
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid handle or mask is 0
 *       - ESP_PLAYER_ERR_NOT_SUPPORT  Mask not built in menuconfig or invalid for current build
 */
esp_player_err_t esp_player_set_av_mask(esp_player_handle_t handle, uint8_t mask);

/**
 * @brief  Set playback URL
 *
 * @note  URI grammar (RFC 3986)
 *
 *        scheme://[authority]/path[?query][#fragment]
 *        authority = [userinfo@]host[:port]
 *
 * @par Supported schemes
 *
 *        | Scheme      | Authority | Path / resource |
 *        |-------------|-----------|-----------------|
 *        | file        | not used  | VFS absolute path |
 *        | http/https  | host[:port], optional user:pass@ | resource path |
 *        | fill/block  | not used  | /name.codec (virtual; see below) |
 *
 * @par Authority
 *
 *        Only http and https use authority; the full URI (including userinfo, host, and port)
 *        is passed to the HTTP IO layer unchanged.
 *
 *        | Scheme      | Authority usage |
 *        |-------------|-----------------|
 *        | http/https  | Required: host[:port]; optional user:pass@ for basic auth |
 *        | file        | Not used — leave empty (file:///path). Player extracts the VFS path only |
 *        | fill/block  | Not used — must be empty (fill:///name.codec) |
 *
 *        file compat: file://sdcard/... (non-RFC double slash) is accepted; "sdcard" is treated
 *        as part of the path, not as authority.
 *
 * @par Query (?query)
 *
 *        query parameters — only keys present in the URL are applied; others keep codec defaults:
 *
 *        Common (all codecs):
 *            sr=<Hz>     sample rate, e.g. sr=16000
 *            ch=<n>      channel count, e.g. ch=1
 *            bits=<n>    bits per sample (PCM/AAC/ADPCM/LC3), e.g. bits=16
 *
 *        AAC:
 *            no_adts=<0|1>   frames have no ADTS header (e.g. BT A2DP)
 *            aac_plus=<0|1>  enable HE-AAC / AAC+ decoder
 *
 *        OPUS:
 *            frame_dms=<N>   frame duration (enum)
 *            self_del=<0|1>  self-delimited framing
 *
 *        LC3:
 *            frame_dms=<N>   frame duration (dm)
 *            nbyte=<N>       bytes per frame
 *            cbr=<0|1>       constant bit-rate mode
 *            len_pre=<0|1>   length-prefixed frames
 *            plc=<0|1>       packet-loss concealment
 *
 *        SBC:
 *            plc=<0|1>       packet-loss concealment
 *
 * @par Fragment (#fragment)
 *
 *        Not used in the current implementation, reserved for future use.
 *
 * @par Local files (file)
 *
 *        - file:///sdcard/music/test.mp3               — canonical
 *        - /sdcard/music/test.mp3                      — bare VFS path
 *        - file://sdcard/music/test.mp3                — compat (double slash)
 *        - /sdcard/music/test.pcm?sr=8000&ch=2&bits=16 — raw PCM; sr/ch/bits required
 *
 * @par Network streams (http / https)
 *
 *        - http://192.168.1.10:8080/stream.aac                     — canonical
 *        - https://user:pass@example.com/audio/test.mp4            — optional user:pass@
 *        - http://example.com/live/playlist.m3u8                   — HLS auto-detected when path ends with .m3u8
 *        - https://example.com/audio/test.pcm?sr=8000&ch=2&bits=16 — raw PCM; sr/ch/bits required
 *
 * @par External frame submit (fill / block)
 *
 *        Virtual URL — no IO backend; no file is opened at path.
 *        After esp_player_run(), push frames via esp_player_submit_frame().
 *
 *        Authority must be empty.
 *        path = /name.codec — name is arbitrary; only the .codec extension selects the decoder.
 *        Bare fill:/// — use esp_player_set_dec_cfg() before run (see esp_player_advance.h).
 *
 *        - fill:///test.pcm?sr=16000&ch=1&bits=16       — FILL, raw 16-bit mono PCM
 *        - block:///test.pcm?sr=16000&ch=1&bits=16      — BLOCK (zero-copy), same format
 *        - fill:///test.aac                             — AAC; params from ADTS bitstream
 *        - fill:///test.aac?no_adts=1                   — AAC without ADTS (e.g. BT A2DP)
 *        - fill:///test.aac?no_adts=1&aac_plus=1        — HE-AAC without ADTS header
 *        - fill:///test.opus?sr=16000&ch=2&frame_dms=20 — OPUS raw frames
 *
 * @note  Allowed only in IDLE, STOPPED, or FINISHED. Setting a new URL closes the previous
 *        input IO and resets pipeline state; av_mask and sync mode are preserved.
 *
 * @param[in]  handle  Player handle
 * @param[in]  url     Media URI
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Setting successful
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid parameters or malformed URI
 *       - ESP_PLAYER_ERR_NOT_SUPPORT  Not allowed in current state
 */
esp_player_err_t esp_player_set_url(esp_player_handle_t handle, const char *url);

/**
 * @brief  Set A/V synchronization mode for the next playback
 *
 * @note  Only meaningful when av_mask is ESP_PLAYER_MASK_AV.
 *        ESP_PLAYER_SYNC_MODE_AUDIO   — video follows audio (default)
 *        ESP_PLAYER_SYNC_MODE_VIDEO   — video-led sync
 *        ESP_PLAYER_SYNC_MODE_SYSTEM  — wall-clock sync
 *        ESP_PLAYER_SYNC_MODE_NONE    — no A/V sync; audio and video freerun independently
 *        Allowed in IDLE, STOPPED, or FINISHED. esp_player_run() does not change the mode.
 *
 * @param[in]  handle     Player handle
 * @param[in]  sync_mode  Sync mode; must be less than ESP_PLAYER_SYNC_MODE_MAX
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Setting successful
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid handle or sync_mode
 *       - ESP_PLAYER_ERR_NOT_SUPPORT  Not allowed in current state
 *       - ESP_PLAYER_ERR_FAIL         Sync module not ready
 */
esp_player_err_t esp_player_set_sync_mode(esp_player_handle_t handle, esp_player_sync_mode_t sync_mode);

/**
 * @brief  Configure mask, URL, and sync mode in one call
 *
 * @note  Applies esp_player_set_av_mask(), esp_player_set_url(), and esp_player_set_sync_mode().
 *        For playlist track changes with unchanged mask/sync, call esp_player_set_url() only.
 *        On failure, remaining steps are not executed.
 *
 * @param[in]  handle  Player handle
 * @param[in]  src     Data source descriptor; must not be NULL
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           All steps succeeded
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid handle or src
 *       - ESP_PLAYER_ERR_NOT_SUPPORT  State, mask, URL, or sync mode not supported
 *       - ESP_PLAYER_ERR_NO_MEM       Out of memory
 *       - ESP_PLAYER_ERR_FAIL         Sync or I/O failed
 */
esp_player_err_t esp_player_set_data_src(esp_player_handle_t handle, const esp_player_data_src_t *src);

/**
 * @brief  Set event callback function
 *
 * @note  Events are delivered on internal player threads.
 *        Do not call player APIs that take the player lock from inside the callback.
 *        Allowed only in IDLE, STOPPED, or FINISHED. Pass NULL to unregister.
 *
 * @param[in]  handle  Player handle
 * @param[in]  cb      Event callback; NULL to unregister
 * @param[in]  ctx     User context passed to callback
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Setting successful
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid handle
 *       - ESP_PLAYER_ERR_NOT_SUPPORT  Not allowed in current state
 */
esp_player_err_t esp_player_set_event_cb(esp_player_handle_t handle, esp_player_event_callback_t cb, void *ctx);

/**
 * @brief  Set event queue
 *
 * @note  Optional alternative to esp_player_set_event_cb().
 *        Copies esp_player_event_msg_t into a FreeRTOS queue (QueueHandle_t).
 *        Create the queue with item size >= sizeof(esp_player_event_msg_t)
 *        (e.g. xQueueCreate(n, sizeof(esp_player_event_msg_t))).
 *        Callback and queue may both be registered.
 *        Allowed only in IDLE, STOPPED, or FINISHED.
 *
 * @param[in]  handle  Player handle
 * @param[in]  queue   FreeRTOS queue for esp_player_event_msg_t; NULL to unregister.
 *                     Item size must be >= sizeof(esp_player_event_msg_t).
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Setting successful
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid handle
 *       - ESP_PLAYER_ERR_NOT_SUPPORT  Not allowed in current state
 */
esp_player_err_t esp_player_set_event_queue(esp_player_handle_t handle, void *queue);

/**
 * @brief  Start playback (non-blocking)
 *
 * @note  Opens the configured media source and starts playback. Returns
 *        immediately after the start request is accepted.
 *        Use esp_player_set_event_cb() or esp_player_set_event_queue() to
 *        receive ESP_PLAYER_EVENT_PLAYED when playback actually starts,
 *        and ESP_PLAYER_EVENT_FINISHED or ESP_PLAYER_EVENT_ERROR when done.
 *        May be called again after esp_player_stop() or natural finish.
 *
 * @param[in]  handle  Player handle
 *
 * @return
 *       - ESP_PLAYER_ERR_OK             Start request accepted
 *       - ESP_PLAYER_ERR_INVALID_ARG    Invalid handle
 *       - ESP_PLAYER_ERR_INVALID_STATE  Not IDLE, STOPPED, or FINISHED
 *       - ESP_PLAYER_ERR_FAIL           Not ready to play or start failed
 */
esp_player_err_t esp_player_run(esp_player_handle_t handle);

/**
 * @brief  Start playback and block until end or failure
 *
 * @note  Same prepare/start path as esp_player_run(), but blocks until FINISHED, ERROR,
 *        or esp_player_stop() from another task.
 *        Do not call from the event callback.
 *
 * @param[in]  handle  Player handle
 *
 * @return
 *       - ESP_PLAYER_ERR_OK             Playback completed naturally
 *       - ESP_PLAYER_ERR_INVALID_ARG    Invalid handle
 *       - ESP_PLAYER_ERR_INVALID_STATE  Not IDLE, STOPPED, or FINISHED
 *       - ESP_PLAYER_ERR_FAIL           Prepare failed or playback error
 */
esp_player_err_t esp_player_run_to_end(esp_player_handle_t handle);

/**
 * @brief  Pause playback
 *
 * @note  Pauses active playback. Only effective while media is playing.
 *        Emits ESP_PLAYER_EVENT_PAUSED when pause completes.
 *
 * @param[in]  handle  Player handle
 *
 * @return
 *       - ESP_PLAYER_ERR_OK             Pause successful
 *       - ESP_PLAYER_ERR_INVALID_ARG    Invalid handle
 *       - ESP_PLAYER_ERR_INVALID_STATE  Not PLAYING
 */
esp_player_err_t esp_player_pause(esp_player_handle_t handle);

/**
 * @brief  Resume playback
 *
 * @note  Resumes from PAUSED. Allowed state: PAUSED only.
 *        Emits ESP_PLAYER_EVENT_PLAYED.
 *
 * @param[in]  handle  Player handle
 *
 * @return
 *       - ESP_PLAYER_ERR_OK             Resume successful
 *       - ESP_PLAYER_ERR_INVALID_ARG    Invalid handle
 *       - ESP_PLAYER_ERR_INVALID_STATE  Not PAUSED
 */
esp_player_err_t esp_player_resume(esp_player_handle_t handle);

/**
 * @brief  Stop playback
 *
 * @note  Stops current playback. Effective while PREPARING, PLAYING, or PAUSED.
 *        Safe to call when playback is already stopped; returns OK without effect.
 *        Emits ESP_PLAYER_EVENT_STOPPED when stop completes.
 *        Call esp_player_run() again to replay the same source.
 *
 * @param[in]  handle  Player handle
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Stop successful or already stopped
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid handle
 */
esp_player_err_t esp_player_stop(esp_player_handle_t handle);

/**
 * @brief  Seek to specified time
 *
 * @note  time_ms is target presentation time in milliseconds.
 *        PLAYING/PAUSED: performs real seek on active pipelines; emits ESP_PLAYER_EVENT_SEEK_DONE.
 *        STOPPED/FINISHED: updates internal sync PTS only (bookmark).
 *        Not supported for fill/block virtual URLs.
 *        Not allowed in IDLE, PREPARING, or while another seek is in progress.
 *
 * @param[in]  handle   Player handle
 * @param[in]  time_ms  Target position (ms)
 *
 * @return
 *       - ESP_PLAYER_ERR_OK             Seek completed
 *       - ESP_PLAYER_ERR_INVALID_ARG    Invalid handle
 *       - ESP_PLAYER_ERR_NO_MEM         Failed to allocate seek command payload
 *       - ESP_PLAYER_ERR_INVALID_STATE  IDLE, PREPARING, or seek already in progress
 *       - ESP_PLAYER_ERR_FAIL           Seek aborted by error
 */
esp_player_err_t esp_player_seek(esp_player_handle_t handle, uint64_t time_ms);

/**
 * @brief  Set playback speed
 *
 * @note  speed = 1.0 is normal; must be greater than 0.
 *        Updates playback speed in the sync module and audio render.
 *        May be called during PLAYING or PAUSED.
 *
 * @param[in]  handle  Player handle
 * @param[in]  speed   Speed multiplier (> 0.0)
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Setting successful
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid handle or speed <= 0
 *       - ESP_PLAYER_ERR_FAIL         Audio render rejected the speed change
 */
esp_player_err_t esp_player_set_speed(esp_player_handle_t handle, float speed);

/**
 * @brief  Get current player state
 *
 * @note  Returns the authoritative main-state snapshot (see esp_player_state_t).
 *        Use this for UI and control decisions instead of mirroring events.
 *        Events remain the preferred mechanism for edge notifications
 *        (finished, error, buffering, seek done). Buffering is not a main state.
 *
 * @param[in]   handle  Player handle
 * @param[out]  state   Output player state
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           State read successfully
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid handle or NULL state
 */
esp_player_err_t esp_player_get_state(esp_player_handle_t handle, esp_player_state_t *state);

/**
 * @brief  Get media total duration
 *
 * @note  Returns longest active track duration in milliseconds from extractor metadata.
 *        Not available for fill/block virtual URLs.
 *
 * @param[in]   handle    Player handle
 * @param[out]  duration  Output duration (ms); set to 0 on failure
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Duration available
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid handle or NULL duration
 *       - ESP_PLAYER_ERR_NOT_SUPPORT  No extractor or no duration metadata
 */
esp_player_err_t esp_player_get_duration(esp_player_handle_t handle, uint64_t *duration);

/**
 * @brief  Get current playback time
 *
 * @note  Returns render PTS in milliseconds. For AV playback, returns the smaller
 *        of audio and video render PTS. For audio-only or video-only playback,
 *        returns the active track PTS.
 *
 * @param[in]   handle        Player handle
 * @param[out]  current_time  Output current position (ms)
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Position read successfully
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid handle, NULL current_time, or sync not ready
 */
esp_player_err_t esp_player_get_play_time(esp_player_handle_t handle, uint64_t *current_time);

/**
 * @brief  Get track number
 *
 * @note  Container playback (extractor present): number of streams of the given track type.
 *        Valid track_idx for esp_player_get_track_info() and esp_player_enable_track()
 *        are in [0, track_num). Single-path playback (no extractor): returns 1 when the
 *        track type is enabled. Call after container metadata is parsed.
 *
 * @param[in]   handle     Player handle
 * @param[in]   type       ESP_PLAYER_TRACK_TYPE_AUDIO or ESP_PLAYER_TRACK_TYPE_VIDEO
 * @param[out]  track_num  Output track number
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Number returned
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid handle, type, or NULL track_num
 *       - ESP_PLAYER_ERR_FAIL         No matching path or extractor query failed
 */
esp_player_err_t esp_player_get_track_num(esp_player_handle_t handle, esp_player_track_type_t type, uint16_t *track_num);

/**
 * @brief  Get track information
 *
 * @note  Container playback (extractor present): queries metadata for track_idx in
 *        [0, track_num) for the given track type (see esp_player_get_track_num()).
 *        Single-path playback (no extractor): only track_idx 0 is valid; returns cached
 *        side information from the active decoder path.
 *        For audio tracks, spec_info points into extractor-owned memory; copy promptly if
 *        a longer lifetime is needed (see esp_player_audio_stream_info_t).
 *
 * @param[in]   handle      Player handle
 * @param[in]   type        ESP_PLAYER_TRACK_TYPE_AUDIO or ESP_PLAYER_TRACK_TYPE_VIDEO
 * @param[in]   track_idx   Track index in [0, track_num) for type
 * @param[out]  track_info  Output track information
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Info copied
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid handle, type, track_idx, or NULL track_info
 *       - ESP_PLAYER_ERR_FAIL         Extractor query failed
 *       - ESP_PLAYER_ERR_NOT_SUPPORT  Type not enabled or side not initialized
 */
esp_player_err_t esp_player_get_track_info(esp_player_handle_t handle, esp_player_track_type_t type, uint16_t track_idx, esp_player_track_info_t *track_info);

/**
 * @brief  Enable or disable a track
 *
 * @note  AV + extractor mode only. Switches demux stream index.
 *        Not supported for single-path or fill/block URLs.
 *
 * @param[in]  handle     Player handle
 * @param[in]  type       ESP_PLAYER_TRACK_TYPE_AUDIO or ESP_PLAYER_TRACK_TYPE_VIDEO
 * @param[in]  track_idx  Track index in [0, track_num) for type
 * @param[in]  enable     true to select/enable, false to disable
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Track state updated
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid handle or track type
 *       - ESP_PLAYER_ERR_FAIL         Extractor rejected the request
 *       - ESP_PLAYER_ERR_NOT_SUPPORT  Not AV extractor mode
 */
esp_player_err_t esp_player_enable_track(esp_player_handle_t handle, esp_player_track_type_t type, uint16_t track_idx, bool enable);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
