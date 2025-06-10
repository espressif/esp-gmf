/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once
#include "esp_gmf_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Type of events for audio simple player
 */
typedef enum {
    ESP_ASP_EVENT_TYPE_STATE = 1,        /*!< State change event, the payload is esp_asp_state_t */
    ESP_ASP_EVENT_TYPE_MUSIC_INFO  = 2,  /*!< Information event, the payload is esp_asp_music_info_t */
} esp_asp_event_type_t;

/**
 * @brief  Audio simple player states
 */
typedef enum {
    ESP_ASP_STATE_NONE     = 0,  /*!< No specific state */
    ESP_ASP_STATE_RUNNING  = 1,  /*!< Running state */
    ESP_ASP_STATE_PAUSED   = 2,  /*!< Paused state */
    ESP_ASP_STATE_STOPPED  = 3,  /*!< Stopped state */
    ESP_ASP_STATE_FINISHED = 4,  /*!< Finished state */
    ESP_ASP_STATE_ERROR    = 5,  /*!< Error state */
} esp_asp_state_t;

/**
 * @brief  Structure representing music-related information
 */
typedef struct {
    int       sample_rate;   /*!< Sample rate */
    int       bitrate;       /*!< Bits per second */
    uint16_t  channels : 8;  /*!< Number of channels */
    uint16_t  bits     : 8;  /*!< Bit depth */
} esp_asp_music_info_t;

/**
 * @brief  Packet containing information of the audio simple player
 */
typedef struct {
    esp_asp_event_type_t  type;          /*!< Type of the event */
    void                 *payload;       /*!< Pointer to the payload data */
    int                   payload_size;  /*!< Size of the payload data */
} esp_asp_event_pkt_t;

typedef void *esp_asp_handle_t;  /*!< Handle to audio simple player instance */

typedef int (*esp_asp_data_func)(uint8_t *data, int data_size, void *ctx);  /*!< Data callback function type */
typedef int (*esp_asp_event_func)(esp_asp_event_pkt_t *pkt, void *ctx);     /*!< Event callback function type */
typedef int (*esp_asp_prev_func_t)(esp_asp_handle_t *handle, void *ctx);    /*!< A callback function type for previous action */

/**
 * @brief  Callback structure for input and output data
 */
typedef struct {
    esp_asp_data_func  cb;        /*!< Data callback function */
    void              *user_ctx;  /*!< User context passed to the callback */
} esp_asp_func_t;

/**
 * @brief  Configuration structure for the audio simple player
 */
typedef struct {
    esp_asp_func_t       in;                    /*!< Input data callback, it is required only for raw data(raw://xxx/xxx.mp3), and is ignored in other cases */
    esp_asp_func_t       out;                   /*!< Output data callback configuration, it is required for all cases */
    int                  task_prio;             /*!< Priority of the player task */
    int                  task_stack;            /*!< Size of the task stack */
    uint8_t              task_core;             /*!< CPU core affinity for the task */
    bool                 task_stack_in_ext : 1; /*!< Whether the task stack is in external memory */
    esp_asp_prev_func_t  prev;                  /*!< An optional callback invoked before the pipeline starts (e.g., configure linked elements before running) */
    void                *prev_ctx;              /*!< User context passed to the previous action callback */
} esp_asp_cfg_t;

/**
 * @brief  Create a new audio simple player instance
 *
 * @param[in]   cfg     Configuration structure for the player
 * @param[out]  handle  Pointer to store the created player handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_MEMORY_LACK  Memory allocation failure
 */
esp_gmf_err_t esp_audio_simple_player_new(esp_asp_cfg_t *cfg, esp_asp_handle_t *handle);

/**
 * @brief  Set event callback function for audio simple player
 *
 * @param[in]  handle    Handle to audio simple player instance
 * @param[in]  event_cb  Event callback function
 * @param[in]  ctx       User context for event callback
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_audio_simple_player_set_event(esp_asp_handle_t handle, const esp_asp_event_func event_cb, void *ctx);

/**
 * @brief  Run the audio simple player using the given URI. After this function is called, it will set up a pipeline based on the given URI,
 *         unless esp_audio_simple_player_set_pipeline was called previously. The player determines the audio format based on the file extension,
 *         and currently supports formats including AAC, MP3, AMR, FLAC, WAV, M4A, RAW_OPUS, and TS. If the previous action callback is set,
 *         it will be invoked after the pipeline setup is complete, but before it starts running.
 *
 * @note  For the URI, the `scheme`, `host` and `path` segments are mandatory.
 *        If these are missing, an `ESP_GMF_ERR_INVALID_URI` error will be returned.
 *        For example, with the URI `"file://sdcard/test.mp3"`,
 *        the `scheme` field is `file`, the `host` field is `sdcard`, and the `path` field is `/test.mp3`.
 *
 *        The supported URIs like:
 *           - "https://dl.espressif.com/dl/audio/gs-16b-2c-44100hz.mp3"
 *           - "embed://tone/0_test.mp3"
 *           - "file://sdcard/test.mp3"
 *           - "raw://sdcard/test.mp3", it is required the esp_asp_cfg_t input data callback
 *
 * @param[in]  handle      Handle to audio simple player instance
 * @param[in]  uri         URI of the audio resource
 * @param[in]  music_info  Music information, it is applicable for raw encoded data, such as PCM, without an OGG header in Opus, otherwise it is ignored
 *
 * @return
 *       - ESP_GMF_ERR_OK             On success
 *       - ESP_GMF_ERR_INVALID_ARG    Invalid argument
 *       - ESP_GMF_ERR_INVALID_URI    The URI can't be parsed
 *       - ESP_GMF_ERR_INVALID_STATE  Resource not found
 *       - ESP_GMF_ERR_MEMORY_LACK    Memory allocation failure
 *       - ESP_GMF_ERR_NOT_FOUND      Resource not found
 *       - ESP_GMF_ERR_NOT_SUPPORT    The in stream is not correct
 *       - ESP_GMF_ERR_FAIL           Others error
 */
esp_gmf_err_t esp_audio_simple_player_run(esp_asp_handle_t handle, const char *uri, esp_asp_music_info_t *music_info);

/**
 * @brief  Run the audio simple player in synchronized mode until played to end or meet any error
 *         For more information, refer to `esp_audio_simple_player_run`
 *
 * @param[in]  handle      Handle to audio simple player instance
 * @param[in]  uri         URI of the audio resource
 * @param[in]  music_info  Music information, it is applicable for raw encoded data, such as PCM, without an OGG header in Opus, otherwise it is ignored
 *
 * @return
 *       - ESP_GMF_ERR_OK             On success
 *       - ESP_GMF_ERR_INVALID_ARG    Invalid argument
 *       - ESP_GMF_ERR_INVALID_URI    The URI can't be parsed
 *       - ESP_GMF_ERR_INVALID_STATE  Resource not found
 *       - ESP_GMF_ERR_MEMORY_LACK    Memory allocation failure
 *       - ESP_GMF_ERR_NOT_FOUND      Resource not found
 *       - ESP_GMF_ERR_NOT_SUPPORT    The in stream not correct
 *       - ESP_GMF_ERR_FAIL           Others error
 */
esp_gmf_err_t esp_audio_simple_player_run_to_end(esp_asp_handle_t handle, const char *uri, esp_asp_music_info_t *music_info);

/**
 * @brief  Stop the audio simple player
 *
 * @param[in]  handle  Handle to audio simple player instance
 *
 * @return
 *       - ESP_GMF_ERR_OK             On success
 *       - ESP_GMF_ERR_INVALID_ARG    If the pipeline or thread handle is invalid
 *       - ESP_GMF_ERR_NOT_SUPPORT    The task state is ESP_GMF_EVENT_STATE_NONE
 *       - ESP_GMF_ERR_INVALID_STATE  The task is not running
 *       - ESP_GMF_ERR_TIMEOUT        Indicating that the synchronization operation has timed out
 */
esp_gmf_err_t esp_audio_simple_player_stop(esp_asp_handle_t handle);

/**
 * @brief  Pause the audio simple player
 *
 * @param[in]  handle  Handle to audio simple player instance
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  If the pipeline or thread handle is invalid
 *       - ESP_GMF_ERR_NOT_SUPPORT  The task state is not ESP_GMF_EVENT_STATE_RUNNING
 *       - ESP_GMF_ERR_TIMEOUT      Indicating that the synchronization operation has timed out
 */
esp_gmf_err_t esp_audio_simple_player_pause(esp_asp_handle_t handle);

/**
 * @brief  Resume the audio simple player
 *
 * @param[in]  handle  Handle to audio simple player instance
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  If the pipeline or thread handle is invalid
 *       - ESP_GMF_ERR_NOT_SUPPORT  The task state is not ESP_GMF_EVENT_STATE_PAUSED
 *       - ESP_GMF_ERR_TIMEOUT      Indicating that the synchronization operation has timed out
 */
esp_gmf_err_t esp_audio_simple_player_resume(esp_asp_handle_t handle);

/**
 * @brief  Get the current state of the audio simple player
 *
 * @param[in]   handle  Handle to audio simple player instance
 * @param[out]  state   Pointer to store the current state
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_audio_simple_player_get_state(esp_asp_handle_t handle, esp_asp_state_t *state);

/**
 * @brief  Get string representation of the audio player state
 *
 * @param  state  Enum value of esp_asp_state_t
 * @return
 *       - const  char* String representation of the state
 */
const char *esp_audio_simple_player_state_to_str(esp_asp_state_t state);

/**
 * @brief  Destroy the audio simple player instance
 *
 * @param[in]  handle  Handle to audio simple player instance
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_audio_simple_player_destroy(esp_asp_handle_t handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
