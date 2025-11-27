/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_audio_render_types.h"
#include "esp_gmf_element.h"
#include "esp_gmf_task.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Audio render configuration
 */
typedef struct {
    uint8_t                          max_stream_num;    /*!< Maximum supported stream number */
    esp_audio_render_write_cb_t      out_writer;        /*!< Audio render write callback */
    void                            *out_ctx;           /*!< Audio render write context */
    esp_audio_render_sample_info_t   out_sample_info;   /*!< Output sample information, needs to be aligned with actual device setting */
    void                            *pool;              /*!< GMF pool handle */
    uint16_t                         process_period;    /*!< Audio processing interval in milliseconds (default: 20ms)
                                                             This determines how frequently the audio mixer processes input streams
                                                             Only valid for multiple streams which need mix processor
                                                             - Shorter periods: Faster audio response but require more precise timing
                                                             - Longer periods: More tolerant to buffer variations but increase latency
                                                             This value controls the ring_fifo size used by `esp_audio_render_stream_write` also */
    uint8_t                          process_buf_align; /*!< When use hardware or optimized processor may need special buffer alignment
                                                             If set to 0, the default value is 16 */
} esp_audio_render_cfg_t;

/**
 * @brief  Audio render stream identification
 */
typedef uint8_t esp_audio_render_stream_id_t;
#define ESP_AUDIO_RENDER_STREAM_ID(n)   ((esp_audio_render_stream_id_t)(n))
#define ESP_AUDIO_RENDER_FIRST_STREAM   ESP_AUDIO_RENDER_STREAM_ID(0)
#define ESP_AUDIO_RENDER_SECOND_STREAM  ESP_AUDIO_RENDER_STREAM_ID(1)
#define ESP_AUDIO_RENDER_ALL_STREAM     ESP_AUDIO_RENDER_STREAM_ID(0xFF)

/**
 * @brief  Audio render handle
 *
 * @note  Audio render handle means handle to an audio render system
 *        An audio render system can contain one or multiple input render streams and one mixed processor
 *        Audio render will mix all input streams (if more than one) and finally output through user writer callback
 */
typedef void* esp_audio_render_handle_t;

/**
 * @brief  Audio render stream handle
 */
typedef void* esp_audio_render_stream_handle_t;

/**
 * @brief  Audio render event type
 *
 * @note  Render events will help to check whether render is actually running
 *        So that when no render stream is running, the device can be suspended to save energy
 */
typedef enum {
    ESP_AUDIO_RENDER_EVENT_TYPE_NONE,    /*!< No event */
    ESP_AUDIO_RENDER_EVENT_TYPE_OPENED,  /*!< Render is opened (at least one stream is opened) */
    ESP_AUDIO_RENDER_EVENT_TYPE_CLOSED,  /*!< Render is closed (all streams are closed) */
} esp_audio_render_event_type_t;

/**
 * @brief  Audio render event callback
 *
 * @param[in]   event_type  Audio render event type
 * @param[out]  ctx         Event context
 *
 * @return
 *       - 0       On success
 *       - Others  Failed to handle event
 */
typedef int (*esp_audio_render_event_cb_t)(esp_audio_render_event_type_t event_type, void *ctx);

/**
 * @brief  Create audio render
 *
 * @param[in]   cfg     Audio render configuration
 * @param[out]  render  Audio render handle to store
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK           On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_NO_MEM       Not enough memory
 */
esp_audio_render_err_t esp_audio_render_create(esp_audio_render_cfg_t *cfg, esp_audio_render_handle_t *render);

/**
 * @brief  Reconfigures the audio render task parameters
 *
 * @note  This function must be called when no audio stream is active (no stream is opened yet)
 *
 * @note  Default configuration (applied if never called):
 *          - `stack_in_ext` = true (task stack allocated in external SPI-RAM)
 *          - Other parameters use values from Kconfig defaults
 *
 * @param[in]  render  Audio render handle
 * @param[in]  cfg     Pointer to task configuration
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK             On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_INVALID_STATE  Called when any stream is opened
 */
esp_audio_render_err_t esp_audio_render_task_reconfigure(esp_audio_render_handle_t render, esp_gmf_task_config_t *cfg);

/**
 * @brief  Set event callback for audio render
 *
 * @param[in]  render    Audio render handle
 * @param[in]  event_cb  Audio render event callback
 * @param[in]  ctx       User context
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK           On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_audio_render_err_t esp_audio_render_set_event_cb(esp_audio_render_handle_t render,
                                                     esp_audio_render_event_cb_t event_cb, void *ctx);

/**
 * @brief  Set final output sample information for audio render
 *
 * @note  This API is used to update the default output sample information in `esp_audio_render_cfg_t`
 *        Only allowed to set before any stream is opened
 *        It is used to keep alignment with actual device setting but not apply to device
 *
 * @param[in]  render       Audio render handle
 * @param[in]  sample_info  Render sample information
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK             On Success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_INVALID_STATE  Sample information change not supported in current state
 */
esp_audio_render_err_t esp_audio_render_set_out_sample_info(esp_audio_render_handle_t render,
                                                            esp_audio_render_sample_info_t *sample_info);

/**
 * @brief  Add audio processor into processor after stream mixed
 *
 * @note  This API only allows to set when no stream is opened
 *        When `max_stream_num` is set to 1, it is equal to `esp_audio_render_stream_add_proc`
 *        Basic sample rate conversion, bit conversion and channel conversion will be added into process automatically no need to add use this API
 *        Make sure that the added processor element can be find in configure `esp_audio_render_cfg_t.pool`
 *
 * @param[in]   render     Audio render handle
 * @param[in]   proc_type  Array of processor types
 * @param[in]   proc_num   Number of processors
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK             On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_NOT_FOUND      Mixed processor not found
 *       - ESP_AUDIO_RENDER_ERR_NO_MEM         Not enough memory
 *       - ESP_AUDIO_RENDER_ERR_INVALID_STATE  Not allowed to add in current state
 */
esp_audio_render_err_t esp_audio_render_add_mixed_proc(esp_audio_render_handle_t render,
                                                       esp_audio_render_proc_type_t proc_type[], uint8_t proc_num);

/**
 * @brief  Get element handle from mixed processor by process type
 *
 * @note  Element handle can be obtained instantly after adding
 *        User can do settings for the element, pay attention to whether parameters can be set during running
 *        Check element header files for details
 *
 * @param[in]   render     Audio render handle
 * @param[in]   proc_type  Processor type
 * @param[out]  element    Element handle to store
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK           On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_NOT_FOUND    Element not found
 */
esp_audio_render_err_t esp_audio_render_get_mixed_element(esp_audio_render_handle_t render,
                                                          esp_audio_render_proc_type_t proc_type,
                                                          esp_gmf_element_handle_t *element);

/**
 * @brief  Enable or disable solo play mode for a specific audio stream
 *
 * @note   In solo mode, the audio renderer bypasses the mixer and plays only the specified stream
 *         - Can be called anytime during runtime
 *         - Set `stream_id` to `ESP_AUDIO_RENDER_ALL_STREAM` to disable solo mode and re-enable mixing
 *
 * @param[in]   render     Audio render handle
 * @param[in]   stream_id  Stream identification
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK           On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_audio_render_err_t esp_audio_render_set_solo_stream(esp_audio_render_handle_t render,
                                                        esp_audio_render_stream_id_t stream_id);

/**
 * @brief  Get stream handle by stream identification
 *
 * @note  Stream handle can be obtained once render is created by `esp_audio_render_create`
 *
 * @param[in]   render         Audio render handle
 * @param[in]   stream_id      Stream identification
 * @param[out]  stream_handle  Stream handle
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK             On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_NOT_FOUND      Stream not found
 */
esp_audio_render_err_t esp_audio_render_stream_get(esp_audio_render_handle_t render,
                                                   esp_audio_render_stream_id_t stream_id,
                                                   esp_audio_render_stream_handle_t *stream_handle);

/**
 * @brief  Set mixer gain for audio render stream (optional)
 *
 * @note  Currently only support to set when none stream is opened yet
 *        When this API not call it will use default mixer gain [0, sqrt(1.0 / max_stream_num)]
 *        Target gain should be limited to avoid clipping after mixed
 *
 * @param[in]   stream         Audio render handle
 * @param[in]   mixer_gain     Mixer gain to set
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK             On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_INVALID_STATE  Mixer already running
 */
esp_audio_render_err_t esp_audio_render_stream_set_mixer_gain(esp_audio_render_stream_handle_t stream,
                                                              esp_audio_render_mixer_gain_t *mixer_gain);

/**
 * @brief  Open audio render stream
 *
 * @param[in]   stream       Stream handle
 * @param[in]   sample_info  Stream input sample information
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK             On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_INVALID_STATE  Stream already opened
 *       - ESP_AUDIO_RENDER_ERR_NO_RESOURCE    No resource for this stream
 */
esp_audio_render_err_t esp_audio_render_stream_open(esp_audio_render_stream_handle_t stream,
                                                    esp_audio_render_sample_info_t *sample_info);

/**
 * @brief  Add audio processor into stream
 *
 * @note  Basic sample rate conversion, bit conversion and channel conversion will be added into process automatically no need to add use this API
 *        Make sure that the added processor element can be find in configure `esp_audio_render_cfg_t.pool`
 *
 * @param[in]  stream     Stream handle
 * @param[in]  proc_type  Array pointer of audio processor types
 * @param[in]  proc_num   Number of audio processors
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK             On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_INVALID_STATE  Stream already opened
 *       - ESP_AUDIO_RENDER_ERR_NO_RESOURCE    No resource for any of the audio processors
 *       - ESP_AUDIO_RENDER_ERR_NOT_SUPPORTED  Process not supported for stream
 */
esp_audio_render_err_t esp_audio_render_stream_add_proc(esp_audio_render_stream_handle_t stream,
                                                        esp_audio_render_proc_type_t proc_type[], uint8_t proc_num);

/**
 * @brief  Get element handle by processor type from audio render stream
 *
 * @param[in]   stream     Stream handle
 * @param[in]   proc_type  Processor type
 * @param[out]  element    Element handle to store
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK             On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_NOT_FOUND      Processor type not found
 */
esp_audio_render_err_t esp_audio_render_stream_get_element(esp_audio_render_stream_handle_t stream,
                                                           esp_audio_render_proc_type_t proc_type,
                                                           esp_gmf_element_handle_t *element);

/**
 * @brief  Write PCM data to audio render stream
 *
 * @note  Write behavior differ according When `esp_audio_render_cfg_t.max_stream_num`
 *          - `max_stream_num == 1`: It will do processes and call `out_writer` finally in sync
 *          - `max_stream_num > 1`:  It will do processes then write to a ring_fifo
 *                                   Mixer thread will read from the ring_fifo, do mix process and output through `out_writer`
 *
 * @param[in]  stream    Stream handle
 * @param[in]  pcm_data  PCM data to be written
 * @param[in]  pcm_size  PCM data size
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK             On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_INVALID_STATE  Stream not opened yet
 *       - ESP_AUDIO_RENDER_ERR_FAIL           Failed to write
 */
esp_audio_render_err_t esp_audio_render_stream_write(esp_audio_render_stream_handle_t stream,
                                                     uint8_t *pcm_data, uint32_t pcm_size);

 /**
 * @brief  Fade in/out for audio render stream
 *
 * @note  Fade in/out will only take effect in multiple stream cases
 *        When fade in, mixer gain for this stream will goes from current to target gain
 *        When fade out, mixer gain for this stream will goes from current to initial gain
 *        Allow to set during runtime
 *
 * @param[in]  stream   Stream handle
 * @param[in]  fade_in  Fade operation (true: fade in, false: fade out)
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK             On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG    Invalid input argument
 */
esp_audio_render_err_t esp_audio_render_stream_set_fade(esp_audio_render_stream_handle_t stream,
                                                        bool fade_in);

/**
 * @brief  Pause audio render stream
 *
 * @note  Pause will only take effect in multiple stream cases
 *        Once stream is paused, `esp_audio_render_stream_write` may be blocked due to FIFO being full
 *
 * @param[in]  stream  Stream handle
 * @param[in]  pause   Pause or resume stream (true: pause stream, false: resume stream)
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK             On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG    Invalid input argument
 */
esp_audio_render_err_t esp_audio_render_stream_pause(esp_audio_render_stream_handle_t stream,
                                                     bool pause);

/**
 * @brief  Flush audio render stream
 *
 * @note  Flush will only take effect in multiple stream cases
 *        Flush will clear all buffered data
 *
 * @param[in]  stream  Stream handle
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK             On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG    Invalid input argument
 */
esp_audio_render_err_t esp_audio_render_stream_flush(esp_audio_render_stream_handle_t stream);

/**
 * @brief  Set speed for audio render stream
 *
 * @note  User must place sonic processor into stream processor
 *
 * @param[in]  stream  Stream handle
 * @param[in]  speed   Speed to set, 1.0 for normal speed, bigger means fast, smaller means slow
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK             On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_NOT_SUPPORTED  Sonic processor not found
 */
esp_audio_render_err_t esp_audio_render_stream_set_speed(esp_audio_render_stream_handle_t stream, float speed);

/**
 * @brief  Close audio render stream
 *
 * @param[in]  stream  Stream handle
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK             On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_INVALID_STATE  Stream already closed
 */
esp_audio_render_err_t esp_audio_render_stream_close(esp_audio_render_stream_handle_t stream);

/**
 * @brief  Destroy audio render
 *
 * @note  Do not call any render related API after render is destroyed
 *
 * @param[in]  render  Audio render handle
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK           On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_audio_render_err_t esp_audio_render_destroy(esp_audio_render_handle_t render);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
