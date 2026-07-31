/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_fourcc.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/** @brief Invalid / unknown format (not a registered FourCC) */
#define ESP_PLAYER_FORMAT_NONE  ((esp_fourcc_t)0)

/**
 * @brief  Media container / codec format as a 32-bit FourCC
 *
 * @note  Values are official GMF FourCC constants from esp_fourcc.h (gmf_core), e.g.
 *        ESP_FOURCC_MP4, ESP_FOURCC_AAC, ESP_FOURCC_H264. Do not define parallel format
 *        constants in esp_player; see esp_fourcc.h for the full list.
 *        For esp_player_set_dec_cfg() in esp_player_advance.h, complex audio decoders may
 *        also use esp_audio_simple_dec_type_t values where they match the same FourCC.
 */
typedef esp_fourcc_t esp_player_format_t;

/**
 * @brief  Synchronization mode enumeration
 *
 * @note  Defines reference clock source for audio/video synchronization.
 *        ESP_PLAYER_SYNC_MODE_NONE disables sync and lets each stream freerun.
 */
typedef enum {
    ESP_PLAYER_SYNC_MODE_SYSTEM = 0,  /*!< System clock sync mode */
    ESP_PLAYER_SYNC_MODE_AUDIO  = 1,  /*!< Audio clock sync mode (audio as reference) */
    ESP_PLAYER_SYNC_MODE_VIDEO  = 2,  /*!< Video clock sync mode (video as reference) */
    ESP_PLAYER_SYNC_MODE_NONE   = 3,  /*!< No A/V sync; audio and video freerun independently */
    ESP_PLAYER_SYNC_MODE_MAX    = 4,  /*!< Maximum sync mode value (for boundary checking) */
} esp_player_sync_mode_t;

/**
 * @brief  Playback data source (convenience bundle for one playback session)
 *
 * @note  Passed to esp_player_set_data_src(), which applies fields in order:
 *            esp_player_set_av_mask()
 *            → esp_player_set_url()
 *            → esp_player_set_sync_mode()
 *
 *            url       — media URI (see esp_player_set_url())
 *            av_mask   — ESP_PLAYER_MASK_AUDIO, ESP_PLAYER_MASK_VIDEO, or ESP_PLAYER_MASK_AV
 *                        Selects which decode/render pipelines are built
 *            sync_mode — ESP_PLAYER_SYNC_MODE_SYSTEM, ESP_PLAYER_SYNC_MODE_AUDIO,
 *                        ESP_PLAYER_SYNC_MODE_VIDEO, or ESP_PLAYER_SYNC_MODE_NONE.
 *                        Only affects playback when av_mask is ESP_PLAYER_MASK_AV;
 *                        for audio-only or video-only keep the default
 */
typedef struct {
    const char             *url;        /*!< Media URI */
    uint8_t                 av_mask;    /*!< ESP_PLAYER_MASK_AUDIO / VIDEO / AV */
    esp_player_sync_mode_t  sync_mode;  /*!< A/V sync mode; meaningful only for ESP_PLAYER_MASK_AV */
} esp_player_data_src_t;

/**
 * @brief  Initialize esp_player_data_src_t for container/file playback
 */
#define ESP_PLAYER_DATA_SRC(_url, _mask)  {   \
    .url       = (_url),                      \
    .av_mask   = (uint8_t)(_mask),            \
    .sync_mode = ESP_PLAYER_SYNC_MODE_AUDIO,  \
}

/**
 * @brief  Player error code enumeration
 *
 * @note  Error codes returned by player API calls
 */
typedef enum {
    ESP_PLAYER_ERR_OK            = 0,   /*!< Operation successful */
    ESP_PLAYER_ERR_EOS           = 1,   /*!< End of Stream (reached end of file) */
    ESP_PLAYER_ERR_FAIL          = -1,  /*!< Operation failed (generic error) */
    ESP_PLAYER_ERR_INVALID_ARG   = -2,  /*!< Invalid parameter error */
    ESP_PLAYER_ERR_NO_MEM        = -3,  /*!< Out of memory error */
    ESP_PLAYER_ERR_TIMEOUT       = -4,  /*!< Operation timeout error */
    ESP_PLAYER_ERR_NOT_SUPPORT   = -5,  /*!< Unsupported feature or format */
    ESP_PLAYER_ERR_INVALID_STATE = -6,  /*!< Operation not allowed in current player state */
} esp_player_err_t;

/**
 * @brief  Audio stream information structure
 *
 * @note  spec_info points into the extractor internal memory. Valid while the extractor
 *        is alive (between esp_player_run() and the next esp_player_set_url() or
 *        esp_player_deinit()). Copy out immediately after esp_player_get_track_info() if
 *        a longer lifetime is needed.
 */
typedef struct {
    esp_player_format_t  format;           /*!< Audio encoding format (FourCC) */
    uint32_t             sample_rate;      /*!< Sample rate in Hz, e.g. 44100, 48000 */
    uint8_t              channels;         /*!< Number of channels, e.g. 1 (mono), 2 (stereo) */
    uint8_t              bits_per_sample;  /*!< Bits per sample, e.g. 8, 16, 24, 32 */
    void                *spec_info;        /*!< Format-specific extended info; extractor-owned */
    uint16_t             spec_info_len;    /*!< Extended information length in bytes */
    uint32_t             bitrate;          /*!< Stream bitrate in bits per second (0 if unknown) */
} esp_player_audio_stream_info_t;

/**
 * @brief  Video stream information structure
 */
typedef struct {
    esp_player_format_t  format;   /*!< Video encoding format (FourCC) */
    uint16_t             width;    /*!< Video width in pixels */
    uint16_t             height;   /*!< Video height in pixels */
    uint16_t             fps;      /*!< Video frame rate (Frames Per Second) */
    uint32_t             bitrate;  /*!< Stream bitrate in bits per second (0 if unknown) */
} esp_player_video_stream_info_t;

/**
 * @brief  Track type enumeration
 */
typedef enum {
    ESP_PLAYER_TRACK_TYPE_VIDEO = 0,  /*!< Video track */
    ESP_PLAYER_TRACK_TYPE_AUDIO = 1,  /*!< Audio track */
    ESP_PLAYER_TRACK_TYPE_MAX   = 2,  /*!< Maximum track type value (for boundary checking) */
} esp_player_track_type_t;

/**
 * @brief  Track information structure
 *
 * @note  Uses union to store video or audio track information
 */
typedef struct {
    esp_player_track_type_t  track_type;  /*!< Track type (video/audio) */
    union {
        esp_player_video_stream_info_t  video_info;  /*!< Video stream information (used when track_type is video) */
        esp_player_audio_stream_info_t  audio_info;  /*!< Audio stream information (used when track_type is audio) */
    };
} esp_player_track_info_t;

/**
 * @brief  Player main state enumeration
 *
 * @note  Queried via esp_player_get_state(). This is the authoritative playback
 *        lifecycle state. Prefer it over mirroring ESP_PLAYER_EVENT_* for UI and
 *        control decisions. Transient conditions such as buffering are reported
 *        only via events (ESP_PLAYER_EVENT_BUFFERING / BUFFERED), not as states.
 */
typedef enum {
    ESP_PLAYER_STATE_IDLE      = 0,  /*!< Idle: freshly initialized or not configured */
    ESP_PLAYER_STATE_PREPARING = 1,  /*!< Preparing: run requested, pipelines starting */
    ESP_PLAYER_STATE_PLAYING   = 2,  /*!< Playing: pipelines running and rendering */
    ESP_PLAYER_STATE_PAUSED    = 3,  /*!< Paused: render tasks suspended */
    ESP_PLAYER_STATE_STOPPED   = 4,  /*!< Stopped: explicitly stopped by user */
    ESP_PLAYER_STATE_FINISHED  = 5,  /*!< Finished: reached end of media naturally */
    ESP_PLAYER_STATE_ERROR     = 6,  /*!< Error: after failure */
} esp_player_state_t;

/**
 * @brief  Player event type enumeration
 *
 * @note  Do not call APIs that require locking from inside the event callback
 */
typedef enum {
    ESP_PLAYER_EVENT_NONE              = 0,   /*!< No event */
    ESP_PLAYER_EVENT_PLAYED            = 1,   /*!< Playback started event */
    ESP_PLAYER_EVENT_PAUSED            = 2,   /*!< Playback paused event */
    ESP_PLAYER_EVENT_STOPPED           = 3,   /*!< Playback stopped event */
    ESP_PLAYER_EVENT_SEEK_DONE         = 4,   /*!< Seek operation completed event */
    ESP_PLAYER_EVENT_FINISHED          = 5,   /*!< Playback finished event (reached end of file) */
    ESP_PLAYER_EVENT_BUFFERING         = 6,   /*!< Buffering event (insufficient data, needs buffering) */
    ESP_PLAYER_EVENT_BUFFERED          = 7,   /*!< Buffering completed event (sufficient data, ready to play) */
    ESP_PLAYER_EVENT_ERROR             = 8,   /*!< Playback error event (payload: esp_player_error_source_t) */
    ESP_PLAYER_EVENT_TRACK_INFO_PARSED = 9,   /*!< Track information parsed event (optional, carries track information) */
    ESP_PLAYER_EVENT_AUDIO_INFO_PARSED = 10,  /*!< Audio information parsed event (optional, carries audio information) */
    ESP_PLAYER_EVENT_VIDEO_INFO_PARSED = 11,  /*!< Video information parsed event (optional, carries video information) */
} esp_player_event_type_t;

/**
 * @brief  Player error source enumeration
 *
 * @note  Identifies which pipeline component raised a runtime error. Distinct from
 *        esp_player_err_t (synchronous API return codes). Delivered as the payload of
 *        ESP_PLAYER_EVENT_ERROR so the application can differentiate extractor, decoder,
 *        and renderer failures.
 */
typedef enum {
    ESP_PLAYER_ERROR_SOURCE_NONE          = 0,  /*!< No error */
    ESP_PLAYER_ERROR_SOURCE_EXTRACTOR     = 1,  /*!< Extractor failed */
    ESP_PLAYER_ERROR_SOURCE_AUDIO_DECODER = 2,  /*!< Audio decoder failed */
    ESP_PLAYER_ERROR_SOURCE_AUDIO_RENDER  = 3,  /*!< Audio renderer failed */
    ESP_PLAYER_ERROR_SOURCE_VIDEO_DECODER = 4,  /*!< Video decoder failed */
    ESP_PLAYER_ERROR_SOURCE_VIDEO_RENDER  = 5,  /*!< Video renderer failed */
} esp_player_error_source_t;

/**
 * @brief  Player event message structure
 */
typedef struct {
    esp_player_event_type_t  event_type;  /*!< Event type */
    void                    *data;        /*!< Event data pointer (structure depends on event type) */
    uint32_t                 data_len;    /*!< Event data length in bytes */
} esp_player_event_msg_t;

/**
 * @brief  Player event callback function type
 *
 * @param[in]  event_msg  Event message pointer containing event type and data
 * @param[in]  ctx        User context pointer passed when registering callback
 *
 * @return
 *       - ESP_PLAYER_ERR_OK  Success
 *       - otherwise          Other error code
 */
typedef esp_player_err_t (*esp_player_event_callback_t)(esp_player_event_msg_t *event_msg, void *ctx);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
