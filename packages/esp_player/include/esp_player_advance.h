/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

/**
 * @file esp_player_advance.h
 * @brief  ESP Player advanced API (custom decoders, external frame submit, per-handle tuning, ID3 metadata)
 *
 * @note  Optional APIs for replacing built-in decoder elements, pushing container-less
 *        encoded frames (fill/block URLs), runtime decoder configuration,
 *        per-handle GMF task / buffer tuning (multi-player), and ID3 tags.
 *        Most applications only need esp_player.h.
 */

#include "esp_gmf_pool.h"
#include "esp_gmf_task.h"

#include "esp_player.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Submitted frame type (video GOP or BT/audio packet classification)
 *
 * @note  Used with esp_player_submit_frame() only. Stored in esp_player_frame_t::frame_type.
 *        Reserved for future use; the player currently ignores this field.
 */
typedef enum {
    ESP_PLAYER_FRAME_TYPE_DEFAULT = 0,  /*!< Type not specified (default) */
    ESP_PLAYER_FRAME_TYPE_I_FRAME = 1,  /*!< Intra / key frame */
    ESP_PLAYER_FRAME_TYPE_P_FRAME = 2,  /*!< Predicted frame */
    ESP_PLAYER_FRAME_TYPE_B_FRAME = 3,  /*!< Bi-directional predicted frame */
    ESP_PLAYER_FRAME_TYPE_AUDI    = 4,  /*!< Compressed audio frame (e.g. BT codec payload) */
} esp_player_frame_type_t;

/**
 * @brief  Frame payload for esp_player_submit_frame()
 */
typedef struct {
    void                    *data;        /*!< Frame data pointer */
    uint32_t                 data_len;    /*!< Frame data length in bytes */
    uint64_t                 pts;         /*!< Presentation timestamp in microseconds */
    esp_player_frame_type_t  frame_type;  /*!< Frame type; reserved, ignored by the player today */
    bool                     is_bad;      /*!< Bad frame flag; maps to packet-loss concealment when true */
    bool                     eos;         /*!< End-of-stream flag; set true on the last frame */
} esp_player_frame_t;

/**
 * @brief  Per-handle GMF task configuration (optional; unset = built-in defaults in player_defaults_cfg.h)
 */
typedef struct {
    esp_gmf_task_config_t  extractor;
    esp_gmf_task_config_t  audio_decoder;
    esp_gmf_task_config_t  audio_render;
    esp_gmf_task_config_t  video_decoder;
    esp_gmf_task_config_t  video_render;
} esp_player_task_config_t;

/**
 * @brief  Per-handle buffer tuning (optional; unset = built-in defaults in player_defaults_cfg.h)
 *
 * @note  Buffer config mainly contain 2 part
 *
 *        Common:
 *
 *         Applies to all sources (`file://`, `http://`, `https://`, `hls://`):
 *
 *        - `extractor_pool_size` — demux output pool (bytes). Non-zero: same size for audio and
 *          video; 0: built-in per-track default (audio and video may differ). Caps compressed
 *          frame data held before the fixed-depth extractor-to-decoder queue.
 *
 *        Network:
 *
 *        Applies to `http://`, `https://`, and `hls://` only; ignored for `file://`:
 *
 *        - `http_read_buf_size` — HTTP/HLS read-ahead ring buffer (bytes); 0 = built-in default
 *        - `prebuffer_resume_ms`, `rebuffer_enter_ms`, `rebuffer_resume_ms`, `rebuffer_grace_ms`,
 *           buffering gate thresholds (ms); 0 = built-in default per field
 *
 *        Gate is enabled when built-in `ESP_PLAYER_DEFAULT_NETWORK_BUFFERING` is non-zero. The
 *        player estimates per-track buffered duration from extractor queue depth and uses
 *        `effective_buffer_ms = MIN(audio_buffered_ms, video_buffered_ms)` among active tracks.
 *
 *        Data path:
 *            network URL
 *              -> HTTP read buffer (`http_read_buf_size`)
 *              -> extractor (`extractor_pool_size`)
 *              -> extractor-to-decoder queue
 *                   | Gate      | Condition                                       | Action                  |
 *                   |-----------|-------------------------------------------------|-------------------------|
 *                   | PRE       | effective >= `prebuffer_resume_ms`              | leave startup buffering |
 *                   | RE enter  | effective <= `rebuffer_enter_ms` for `grace_ms` | enter re-buffering      |
 *                   | RE resume | effective >= `rebuffer_resume_ms`               | resume playback         |
 *              -> decoder
 *              -> render
 */
typedef struct {
    /* Common — all URL schemes */
    uint32_t  extractor_pool_size;  /*!< Demux pool (bytes); non-zero: shared A/V; 0 = built-in default */

    /* Network — http/https/hls only (ignored for file://) */
    uint32_t  http_read_buf_size;  /*!< HTTP/HLS read-ahead (bytes); 0 = built-in default */

    /* Network — buffering gate */
    uint32_t  prebuffer_resume_ms;  /*!< Startup: resume when effective >= threshold (ms); 0 = built-in default */
    uint32_t  rebuffer_enter_ms;    /*!< Runtime: enter when effective <= threshold (ms); 0 = built-in default */
    uint32_t  rebuffer_resume_ms;   /*!< Runtime: resume when effective >= threshold (ms); 0 = built-in default */
    uint32_t  rebuffer_grace_ms;    /*!< Runtime: low-buffer duration before enter (ms); 0 = built-in default */
} esp_player_buffer_config_t;

/**
 * @brief  Custom audio decoder factory
 *
 * @note  Called when the player builds the audio decoder. Return ESP_PLAYER_ERR_OK
 *        with a GMF decoder element to replace the built-in decoder; return
 *        ESP_PLAYER_ERR_NOT_SUPPORT to use the built-in decoder; any other code
 *        aborts playback. The element must use object tag "aud_dec".
 *
 * @param[in]   user_ctx     From esp_player_custom_elements_t::user_ctx
 * @param[in]   codec_cc     Track FourCC (e.g. ESP_FOURCC_MP3)
 * @param[in]   info         Stream info (read-only)
 * @param[out]  out_element  Output GMF element on ESP_PLAYER_ERR_OK
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Custom decoder accepted
 *       - ESP_PLAYER_ERR_NOT_SUPPORT  Fall back to built-in decoder
 *       - Other                       Abort playback
 */
typedef esp_player_err_t (*esp_player_audio_decoder_factory_t)(void *user_ctx, uint32_t codec_cc,
                                                               const esp_player_audio_stream_info_t *info,
                                                               esp_gmf_element_handle_t *out_element);

/**
 * @brief  Custom video decoder factory
 *
 * @note  Same semantics as esp_player_audio_decoder_factory_t. The element must
 *        use object tag "vid_dec".
 *
 * @param[in]   user_ctx     From esp_player_custom_elements_t::user_ctx
 * @param[in]   codec_cc     Track FourCC (e.g. ESP_FOURCC_H264)
 * @param[in]   info         Stream info (read-only)
 * @param[out]  out_element  Output GMF element on ESP_PLAYER_ERR_OK
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Custom decoder accepted
 *       - ESP_PLAYER_ERR_NOT_SUPPORT  Fall back to built-in decoder
 *       - Other                       Abort playback
 */
typedef esp_player_err_t (*esp_player_video_decoder_factory_t)(void *user_ctx,
                                                               uint32_t codec_cc,
                                                               const esp_player_video_stream_info_t *info,
                                                               esp_gmf_element_handle_t *out_element);

/**
 * @brief  Custom audio/video decoder hooks
 *
 * @note  Used to support customized audio/video decoders. If a factory returns
 *        ESP_PLAYER_ERR_OK, it replaces the built-in decoder for that track.
 *        If the factory is NULL or returns ESP_PLAYER_ERR_NOT_SUPPORT, the
 *        built-in decoder is used. Configure with esp_player_set_custom_elements();
 *        if not changed, the previous setting is reused on the next play.
 */
typedef struct {
    esp_player_audio_decoder_factory_t  adec_factory;  /*!< NULL to use built-in audio decoder */
    esp_player_video_decoder_factory_t  vdec_factory;  /*!< NULL to use built-in video decoder */
    void                               *user_ctx;      /*!< Passed to both factories */
} esp_player_custom_elements_t;

/**
 * @brief  Set custom audio/video decoder hooks
 *
 * @note  Copies `custom` into the player. Pass NULL to clear. The caller may free
 *        `custom` after return. Keep `user_ctx` valid while factories may run.
 *
 * @param[in]  handle  Player handle
 * @param[in]  custom  Hook snapshot, or NULL to use built-in decoders only
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Setting successful
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid handle
 *       - ESP_PLAYER_ERR_NOT_SUPPORT  Not allowed in current state
 *       - ESP_PLAYER_ERR_NO_MEM       Out of memory
 */
esp_player_err_t esp_player_set_custom_elements(esp_player_handle_t handle,
                                                const esp_player_custom_elements_t *custom);

/**
 * @brief  Set built-in audio decoder configuration
 *
 * @note  The built-in decoder uses default settings unless configured here.
 *        Call this API to control decode behavior precisely (e.g. AAC no-ADTS,
 *        sample rate). The setting is copied into the player; if not changed,
 *        the next play reuses it.
 *        Ignored when a custom audio decoder factory handles the codec.
 *        For fill/block URLs, simple params may also be set via URL query string.
 *
 * @param[in]  handle  Player handle
 * @param[in]  type    Codec FourCC (ESP_FOURCC_* in esp_fourcc.h)
 * @param[in]  cfg     Decoder configuration; may be freed after return
 * @param[in]  cfg_sz  Configuration size in bytes
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Setting successful
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid parameters
 *       - ESP_PLAYER_ERR_NOT_SUPPORT  Not allowed in current state or unknown codec
 *       - ESP_PLAYER_ERR_FAIL         Reconfig failed while decoder is active
 *       - ESP_PLAYER_ERR_TIMEOUT      Failed to take internal lock
 */
esp_player_err_t esp_player_set_dec_cfg(esp_player_handle_t handle, esp_player_format_t type, void *cfg, uint32_t cfg_sz);

/**
 * @brief  Submit one encoded frame (fill/block virtual URL mode)
 *
 * @note  Advanced API for container-less input (Bluetooth raw frames, mic PCM, etc.).
 *        Most applications use file/HTTP URLs and do not need this function.
 *
 *        Workflow:
 *            esp_player_set_url("fill:///…" or "block:///…")
 *            → esp_player_run()
 *            → esp_player_submit_frame() loop
 *            → set frame->eos = true on the last frame
 *            → wait for ESP_PLAYER_EVENT_FINISHED (event callback or
 *              esp_player_set_event_queue()), or call esp_player_stop() when done.
 *
 *        Do not use esp_player_run_to_end() on this path; it blocks before any frame
 *        is submitted.
 *
 *        Waiting semantics per call:
 *            FILL  — timeout_ms limits internal queue enqueue wait only. Decode and
 *                    render continue asynchronously after ESP_PLAYER_ERR_OK. Frame data
 *                    is deep-copied; the caller may reuse frame->data immediately.
 *            BLOCK — blocks until the decoder finishes that frame; keep frame->data
 *                    valid until the call returns. timeout_ms is ignored. After the
 *                    EOS frame returns, still wait for ESP_PLAYER_EVENT_FINISHED so
 *                    the render pipeline can drain.
 *
 *        Callable only in PREPARING or PLAYING. Requires audio-only or video-only
 *        av_mask (ESP_PLAYER_MASK_AV is not supported).
 *
 * @param[in]  handle      Player handle
 * @param[in]  frame       Frame payload; frame->data must not be NULL
 * @param[in]  timeout_ms  FILL: max wait to enqueue (ms). BLOCK: ignored
 *
 * @return
 *       - ESP_PLAYER_ERR_OK             Frame accepted (FILL: enqueued; BLOCK: decoded)
 *       - ESP_PLAYER_ERR_INVALID_ARG    Invalid parameters or wrong mode / av_mask
 *       - ESP_PLAYER_ERR_NOT_SUPPORT    URL was not a fill/block virtual URL
 *       - ESP_PLAYER_ERR_INVALID_STATE  Not PREPARING or PLAYING
 *       - ESP_PLAYER_ERR_TIMEOUT        FILL: queue full or enqueue timed out
 *       - ESP_PLAYER_ERR_FAIL           Queue error
 */
esp_player_err_t esp_player_submit_frame(esp_player_handle_t handle, esp_player_frame_t *frame, uint32_t timeout_ms);

/**
 * @brief  Override GMF task parameters for one player instance
 *
 * @note  Advanced / multi-player tuning. Allowed in IDLE, STOPPED, or FINISHED.
 *        Pass NULL to restore built-in defaults. Takes effect when pipeline tasks
 *        are created on the `esp_player_run()` (after `esp_player_set_url()` /
 *        `esp_player_set_data_src()` as usual).
 *
 * @param[in]  handle  Player handle
 * @param[in]  config  Task configuration, or NULL to clear override
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Success
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid handle
 *       - ESP_PLAYER_ERR_NOT_SUPPORT  Not allowed in current player state
 *       - ESP_PLAYER_ERR_NO_MEM       Allocation failed
 */
esp_player_err_t esp_player_set_task_config(esp_player_handle_t handle, const esp_player_task_config_t *config);

/**
 * @brief  Override buffer / queue parameters for one player instance
 *
 * @note  Advanced / multi-player tuning. Allowed in IDLE, STOPPED, or FINISHED.
 *        Pass NULL to restore built-in defaults. See esp_player_buffer_config_t for the
 *        Common and Network field groups and the buffering gate data path.
 *        `extractor_pool_size` applies when pipelines are (re)created;
 *        `http_read_buf_size` applies when network IO is opened.
 *
 * @param[in]  handle  Player handle
 * @param[in]  config  Buffer configuration, or NULL to clear override
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Success
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid handle
 *       - ESP_PLAYER_ERR_NOT_SUPPORT  Not allowed in current player state
 *       - ESP_PLAYER_ERR_NO_MEM       Allocation failed
 */
esp_player_err_t esp_player_set_buffer_config(esp_player_handle_t handle, const esp_player_buffer_config_t *config);

/**
 * @brief  Get ID3 tag metadata from the current MP3 source
 *
 * @note  Advanced API; not implemented yet. Always returns ESP_PLAYER_ERR_NOT_SUPPORT
 *        until extractor ID3 parsing is added. id3_info type will be defined when implemented.
 *
 * @param[in]   handle    Player handle
 * @param[out]  id3_info  Output ID3 structure (reserved; unused until implemented)
 *
 * @return
 *       - ESP_PLAYER_ERR_NOT_SUPPORT  Not implemented
 *       - ESP_PLAYER_ERR_INVALID_ARG  Invalid handle or NULL id3_info
 */
esp_player_err_t esp_player_get_id3_info(esp_player_handle_t handle, void *id3_info);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
