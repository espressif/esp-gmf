/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_err.h"
#include "esp_gmf_pool.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Register I/O elements into the GMF pool based on sdkconfig configuration
 *         This function will initialize and register the following I/O elements if enabled:
 *         - Codec device I/O for recording (CONFIG_GMF_IO_INIT_CODEC_DEV_RX)
 *         - Codec device I/O for playback (CONFIG_GMF_IO_INIT_CODEC_DEV_TX)
 *         - File reader I/O (CONFIG_GMF_IO_INIT_FILE_READER)
 *         - File writer I/O (CONFIG_GMF_IO_INIT_FILE_WRITER)
 *         - HTTP reader I/O (CONFIG_GMF_IO_INIT_HTTP_READER)
 *         - HTTP writer I/O (CONFIG_GMF_IO_INIT_HTTP_WRITER)
 *         - Flash reader I/O (CONFIG_GMF_IO_INIT_FLASH_READER)
 *
 * @param[in]  pool  Handle to the GMF pool
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_MEMORY_LACK  Memory allocation failed
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t gmf_loader_setup_io_default(esp_gmf_pool_handle_t pool);

/**
 * @brief  Placeholder function to clean up and release resources used by GMF I/O elements from the given pool
 *
 * @note  Currently does nothing. Actual cleanup is handled by `esp_gmf_pool_deinit()`
 *
 * @param[in]  pool  Handle to the GMF pool
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t gmf_loader_teardown_io_default(esp_gmf_pool_handle_t pool);

/**
 * @brief  Register the codec elements selected in sdkconfig into the GMF pool
 *
 * @note  This will register `esp_audio_codec`'s default interface at the first time invoked,
 *        and will keep a counter to manage the lifecycle of the registered interface
 *        The registered interface of `esp_audio_codec` will be unregistered
 *        with `gmf_loader_teardown_audio_codec_default()` when the counter reaches 0
 *
 * @param[in]  pool  Handle to the GMF pool
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_MEMORY_LACK  Memory allocation failed
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t gmf_loader_setup_audio_codec_default(esp_gmf_pool_handle_t pool);

/**
 * @brief  Cleans up and releases resources used by codec elements
 *
 * @note  This function maintains an internal counter to track the number of times
 *        codec elements have been initialized. When calling this function multiple times,
 *        the actual cleanup will only occur when the counter reaches zero, ensuring
 *        safe resource management.
 *
 * @param[in]  pool  Handle to the GMF pool
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t gmf_loader_teardown_audio_codec_default(esp_gmf_pool_handle_t pool);

/**
 * @brief  Register the audio effect elements selected in sdkconfig into the GMF pool, including:
 *         - ALC (Automatic Level Control)
 *         - EQ (Equalizer)
 *         - Channel converter
 *         - Bit depth converter
 *         - Sample rate converter
 *         - Fade effect
 *         - Sonic effect
 *         - Dynamic Range Compressor (DRC)
 *         - Multi-Band Compressor (MBC)
 *         - Deinterleave
 *         - Interleave
 *         - Audio mixer
 *
 * @param[in]  pool  Handle to the GMF pool
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_MEMORY_LACK  Memory allocation failed
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t gmf_loader_setup_audio_effects_default(esp_gmf_pool_handle_t pool);

/**
 * @brief  Cleans up and releases resources used by effect elements
 *
 * @note  Currently does nothing. Actual cleanup is handled by `esp_gmf_pool_deinit()`
 *
 * @param[in]  pool  Handle to the GMF pool
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t gmf_loader_teardown_audio_effects_default(esp_gmf_pool_handle_t pool);

/**
 * @brief  Register the AI Audio elements selected in sdkconfig into the GMF pool, including:
 *         - AEC (Acoustic Echo Cancellation)
 *         - WakeNet (Wake word detection)
 *         - AFE (Audio Front End) with configurable features:
 *         - VAD (Voice Activity Detection)
 *         - Wake word detection
 *         - AEC
 *         The AFE manager will be created with customizable task settings for feed and fetch tasks
 *
 * @note  `esp_gmf_afe_manager` will be create automatically if AFE element is enabled in sdkconfig,
 *        so `gmf_loader_teardown_ai_audio` is used to clean up
 *        More than one `esp_gmf_afe_manager` is meaningless, so `gmf_loader_setup_ai_audio`
 *        will print a warning log if `esp_gmf_afe_manager` already exists
 *
 * @param[in]  pool  Handle to the GMF pool
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_MEMORY_LACK  Memory allocation failed
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t gmf_loader_setup_ai_audio_default(esp_gmf_pool_handle_t pool);

/**
 * @brief  Cleans up and releases resources used by AI audio elements
 *
 * @note  This function maintains an internal counter to track the number of times
 *        AI audio elements have been initialized. When calling this function multiple times,
 *        the actual cleanup will only occur when the counter reaches zero, ensuring
 *        safe resource management.
 *
 * @param[in]  pool  Handle to the GMF pool
 *
 * @return
 *       - ESP_GMF_ERR_OK             Success
 *       - ESP_GMF_ERR_INVALID_ARG    Invalid argument
 *       - ESP_GMF_ERR_INVALID_STATE  AI audio context is NULL
 */
esp_gmf_err_t gmf_loader_teardown_ai_audio_default(esp_gmf_pool_handle_t pool);

/**
 * @brief  Register the video encoder and decoder elements selected in sdkconfig into the GMF pool
 *
 * @note  This will register video codec's default interface at the first time invoked,
 *        and will keep a counter to manage the lifecycle of the registered interface.
 *        The registered interface of video codec will be unregistered
 *        with `gmf_loader_teardown_video_codec_default()` when the counter reaches 0.
 *        The video encoder and decoder will be registered based on CONFIG_GMF_VIDEO_CODEC_INIT_ENCODER
 *        and CONFIG_GMF_VIDEO_CODEC_INIT_DECODER configurations.
 *
 * @param[in]  pool  Handle to the GMF pool
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_MEMORY_LACK  Memory allocation failed
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t gmf_loader_setup_video_codec_default(esp_gmf_pool_handle_t pool);

/**
 * @brief  Cleans up and releases resources used by video codec elements
 *
 * @note  Currently does nothing. Actual cleanup is handled by `esp_gmf_pool_deinit()`
 *
 * @param[in]  pool  Handle to the GMF pool
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t gmf_loader_teardown_video_codec_default(esp_gmf_pool_handle_t pool);

/**
 * @brief  Register the video effect elements selected in sdkconfig into the GMF pool
 *
 * @note  This function will initialize the following effects if enabled:
 *        - PPA: Hardware Pixel Processing Accelerator
 *        - FPS Convert: Frame rate conversion
 *        - Overlay: Video overlay effects
 *        - Color Convert: Convert between different color formats and spaces and it is implemented in software
 *        - Rotate: Rotate video frames by any angle using software and it is implemented in software
 *        - Scale: Resize video frames with different algorithms using software and it is implemented in software
 *        - Crop: Extract regions from video frames using software and it is implemented in software
 *
 * @param[in]  pool  Handle to the GMF pool
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_MEMORY_LACK  Memory allocation failed
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t gmf_loader_setup_video_effects_default(esp_gmf_pool_handle_t pool);

/**
 * @brief  Cleans up and releases resources used by video effect elements
 *
 * @note  Currently does nothing. Actual cleanup is handled by `esp_gmf_pool_deinit()`
 *
 * @param[in]  pool  Handle to the GMF pool
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t gmf_loader_teardown_video_effects_default(esp_gmf_pool_handle_t pool);

/**
 * @brief  Register the misc elements selected in sdkconfig into the GMF pool
 *
 * @note  This function will initialize the following elements if enabled:
 *        - Copier: Copy data between elements. Support one-to-one, one-to-many
 *
 * @param[in]  pool  Handle to the GMF pool
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_MEMORY_LACK  Memory allocation failed
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t gmf_loader_setup_misc_default(esp_gmf_pool_handle_t pool);

/**
 * @brief  Placeholder function to clean up and release resources used by default misc elements from the given pool
 *
 * @note  Currently does nothing. Actual cleanup is handled by `esp_gmf_pool_deinit()`
 *
 * @param[in]  pool  Handle to the GMF pool.
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t gmf_loader_teardown_misc_default(esp_gmf_pool_handle_t pool);

/**
 * @brief  Initialize and register GMF elements to the GMF pool
 *
 *         This function initializes the GMF elements based on the sdkconfig configuration
 *         and registers them into the specified GMF pool for subsequent use. The elements
 *         include I/O, codec, effects and AI audio elements if enabled in sdkconfig
 *
 * @param[in]  pool  Handle to the GMF pool where elements will be registered
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_MEMORY_LACK  Memory allocation failed
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument (e.g. NULL pool handle)
 */
esp_gmf_err_t gmf_loader_setup_all_defaults(esp_gmf_pool_handle_t pool);

/**
 * @brief  Calls each module's teardown function to clean up and release resources from the underlying libraries
 *         The element resources are still destroyed by esp_gmf_pool_deinit
 *
 * @param[in]  pool  Handle to the GMF pool
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t gmf_loader_teardown_all_defaults(esp_gmf_pool_handle_t pool);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
