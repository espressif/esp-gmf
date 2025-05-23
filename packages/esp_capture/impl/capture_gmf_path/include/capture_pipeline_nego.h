/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "capture_pipeline_builder.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Provides helper functions for automated pipeline negotiation and setup
 *
 * @note  Current implementation supports only simple paths containing:
 *          - A single processing element performing uniform operations
 *
 *        Available functions:
 *        @li `esp_capture_xxx_pipeline_auto_negotiate`:
 *             - Performs automatic source-to-sink negotiation for all sinks
 *             - Automatically configures complete pipeline paths
 *
 *        @li `esp_capture_xxx_pipeline_auto_setup`:
 *             - Configures all elements in a pipeline sequentially (head to tail)
 */

/**
 * @brief  Calculate maximum audio sink configuration for negotiation
 *
 * @note  This macro updates the destination configuration with the maximum values
 *        from the sink configuration for sample rate, channel count, and bits per sample
 */
#define MAX_AUD_SINK_CFG(dst, sink) do {                          \
    if (sink.audio_info.sample_rate > dst.sample_rate) {          \
        dst.sample_rate = sink.audio_info.sample_rate;            \
    }                                                             \
    if (sink.audio_info.channel > dst.channel) {                  \
        dst.channel = sink.audio_info.channel;                    \
    }                                                             \
    if (sink.audio_info.bits_per_sample > dst.bits_per_sample) {  \
        dst.bits_per_sample = sink.audio_info.bits_per_sample;    \
    }                                                             \
} while (0);

/**
 * @brief  Calculate maximum video sink configuration for negotiation
 *
 * @note  This macro updates the destination configuration with the maximum values
 *        from the sink configuration for frame rate, width, and height
 */
#define MAX_VID_SINK_CFG(dst, sink) do {        \
    if (sink.video_info.fps > dst.fps) {        \
        dst.fps = sink.video_info.fps;          \
    }                                           \
    if (sink.video_info.width > dst.width) {    \
        dst.width = sink.video_info.width;      \
    }                                           \
    if (sink.video_info.height > dst.height) {  \
        dst.height = sink.video_info.height;    \
    }                                           \
} while (0);

/**
 * @brief  AUto setup for audio pipeline with provided source and sink information
 *
 * @note  This API is only suitable for setup a single pipeline
 *        For multiple pipelines, use esp_capture_audio_pipeline_auto_negotiate
 *
 * @param[in]   pipeline   Pipeline handle
 * @param[in]   src_info   Audio source information
 * @param[in]   sink_info  Audio sink information
 * @param[out]  dst_info   Actual audio information after pipeline processing
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Invalid argument
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Not supported
 */
esp_capture_err_t esp_capture_audio_pipeline_auto_setup(void *pipeline, esp_capture_audio_info_t *src_info,
                                                        esp_capture_audio_info_t *sink_info,
                                                        esp_capture_audio_info_t *dst_info);

/**
 * @brief  Auto negotiate audio pipelines with path mask
 *
 * @note  This API will get pipelines matching the sink_mask from the builder and negotiate them
 *        It is only suitable for simple cases where the path contains one process element
 *        with the same function (e.g., one resampler)
 *        For complex cases (e.g., pipeline with multiple resamplers), write a custom
 *        negotiation function instead
 *
 * @param[in]  builder    Pipeline builder interface
 * @param[in]  sink_mask  Pipeline path mask
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Invalid argument
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Not supported
 */
esp_capture_err_t esp_capture_audio_pipeline_auto_negotiate(esp_capture_pipeline_builder_if_t *builder,
                                                            uint8_t sink_mask);

/**
 * @brief  Auto setup for one video pipeline with provided source and sink information
 *
 * @note  This API is only suitable for negotiating a single pipeline
 *        For multiple pipelines, use `esp_capture_video_pipeline_auto_negotiate`
 *
 * @param[in]   pipeline   Pipeline handle
 * @param[in]   src_info   Video source information
 * @param[in]   sink_info  Video sink information
 * @param[out]  dst_info   Actual video information after pipeline processing
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Invalid argument
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Not supported
 */
esp_capture_err_t esp_capture_video_pipeline_auto_setup(void *pipeline, esp_capture_video_info_t *src_info,
                                                        esp_capture_video_info_t *sink_info,
                                                        esp_capture_video_info_t *dst_info);

/**
 * @brief  Auto negotiate video pipelines with path mask
 *
 * @note  This API will get pipelines matching the sink_mask from the builder and negotiate them
 *        It is only suitable for simple cases where the path contains one process element
 *        with the same function (e.g., one color converter)
 *        For complex cases (e.g., pipeline with multiple color converters), write a custom negotiation function instead
 *
 * @param[in]  builder    Pipeline builder interface
 * @param[in]  sink_mask  Pipeline path mask
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Invalid argument
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Not supported
 */
esp_capture_err_t esp_capture_video_pipeline_auto_negotiate(esp_capture_pipeline_builder_if_t *builder,
                                                            uint8_t sink_mask);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
