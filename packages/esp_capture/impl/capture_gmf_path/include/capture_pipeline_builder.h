/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stddef.h>
#include <stdlib.h>
#include "esp_capture_audio_src_if.h"
#include "esp_capture_video_src_if.h"
#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Provides pipeline builder interface and template builders for simplified usage
 *
 * @note  The pipeline builder serves three key purposes:
 *          1. Describes pipeline elements and their interconnections
 *          2. Provides negotiation functionality for stream configuration
 *          3. Serves as the interface between pipeline manager and pipeline instances
 *
 *        The pipeline manager uses this builder to:
 *          - Instantiate pipelines
 *          - Bind input/output ports
 *          - Control pipeline lifecycle via esp_gmf_pipeline_run/stop
 */

/**
 * @brief  Bitmask flag to force negotiation of all pipeline paths regardless of individual enable states
 *
 * @note  When set, this flag causes the system to:
 *          - Perform pre-negotiation for all possible pipeline paths during initialization
 *          - Maintain negotiation results even if paths are dynamically enabled later
 *          - Ensure subsequent enable operations will use pre-computed parameters
 *
 *        This is particularly useful when:
 *          - Path configured but enable dynamically
 *          - Avoiding runtime renegotiation overhead is critical
 */
#define ESP_CAPTURE_PIPELINE_NEGO_ALL_MASK  (0xFF)

/**
 * @brief  Capture pipeline builder interface type
 *
 * @note  This is an opaque type that represents the pipeline builder interface.
 *        The actual implementation is defined in the source file
 */
typedef struct esp_capture_pipeline_builder_if_t esp_capture_pipeline_builder_if_t;

/**
 * @brief  GMF capture pipeline structure
 *
 * @note  This structure contains information about a GMF pipeline instance,
 *        including its handle, path association, and thread binding
 */
typedef struct {
    void    *pipeline;   /*!< GMF pipeline handle */
    uint8_t  path_mask;  /*!< Mask for pipeline path association:
                               0x1: belongs to first path
                               0x2: belongs to second path
                               0x3: belongs to both first and second path */
    char    *name;       /*!< Pipeline bound thread name */
} esp_capture_gmf_pipeline_t;

/**
 * @brief  Configuration for GMF capture pipeline
 *
 * @note  This structure defines the elements and their order in a pipeline
 */
typedef struct {
    const char **element_tags;  /*!< Array of element tags in pipeline order */
    uint8_t      element_num;   /*!< Number of elements in the pipeline */
} esp_capture_gmf_pipeline_cfg_t;

/**
 * @brief  Definition of capture pipeline builder interface
 *
 * @note  This structure defines the interface for building and managing GMF pipelines
 */
struct esp_capture_pipeline_builder_if_t {
    /**
     * @brief  Create pipeline builder instance
     *
     * @note  During creation, GMF pool can be initialized or pre-defined pipelines can be built up
     *
     * @param[in]  builder  Pointer to the pipeline builder interface
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to create
     */
    int (*create)(esp_capture_pipeline_builder_if_t *builder);

    /**
     * @brief  Register element into GMF pool
     *
     * @note  User registered elements have higher priority and are placed at the pool head
     *
     * @param[in]  builder  Pointer to the pipeline builder interface
     * @param[in]  element  Element handle to register
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to register into GMF pool
     */
    int (*reg_element)(esp_capture_pipeline_builder_if_t *builder, esp_gmf_element_handle_t element);

    /**
     * @brief  Build pipeline manually
     *
     * @note  This API builds a pipeline using user-specified elements connected in the order
     *        specified in the element array
     *
     * @param[in]  builder   Pointer to the pipeline builder interface
     * @param[in]  sink_idx  Sink index to place the built pipeline
     * @param[in]  pipe_cfg  Pipeline configuration (elements and link order)
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to build pipeline
     */
    int (*build_pipeline)(esp_capture_pipeline_builder_if_t *builder, uint8_t sink_idx, esp_capture_gmf_pipeline_cfg_t *pipe_cfg);

    /**
     * @brief  Set sink configuration
     *
     * @param[in]  builder   Pointer to the pipeline builder interface
     * @param[in]  sink_idx  Sink index to configure
     * @param[in]  sink_cfg  Sink configuration to apply
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to set configuration
     */
    int (*set_sink_cfg)(esp_capture_pipeline_builder_if_t *builder, uint8_t sink_idx, esp_capture_stream_info_t *sink_cfg);

    /**
     * @brief  Get sink configuration
     *
     * @param[in]  builder   Pointer to the pipeline builder interface
     * @param[in]  sink_idx  Sink index to query
     * @param[out] sink_cfg  Pointer to store sink configuration
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to get configuration
     */
    int (*get_sink_cfg)(esp_capture_pipeline_builder_if_t *builder, uint8_t sink_idx, esp_capture_stream_info_t *sink_cfg);

    /**
     * @brief  Get all pipelines information
     *
     * @param[in]   builder       Pointer to the pipeline builder interface
     * @param[out]  pipeline      Array to store pipeline information
     * @param[out]  pipeline_num  Pointer to store number of pipelines
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to get pipeline information
     */
    int (*get_pipelines)(esp_capture_pipeline_builder_if_t *builder, esp_capture_gmf_pipeline_t *pipeline, uint8_t *pipeline_num);

    /**
     * @brief  Get element handle from pipeline by tag
     *
     * @param[in]   builder   Pointer to the pipeline builder interface
     * @param[in]   sink_idx  Sink index containing the element
     * @param[in]   tag       Element tag to find
     * @param[out]  element   Pointer to store element handle
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to get element
     */
    int (*get_element)(esp_capture_pipeline_builder_if_t *builder, uint8_t sink_idx, const char *tag, esp_gmf_element_handle_t *element);

    /**
     * @brief  Negotiate for pipelines
     *
     * @note  Path mask indicates which pipelines need to be negotiated:
     *        0x1: Only pipeline connected to sink 0 needs negotiation
     *        0x2: Only pipeline connected to sink 1 needs negotiation
     *        0x3: Both pipelines connected to sink 0 and sink 1 need negotiation
     *        ESP_CAPTURE_PIPELINE_NEGO_ALL_MASK: All paths need negotiation
     *
     * @param[in]  builder    Pointer to the pipeline builder interface
     * @param[in]  sink_mask  Pipeline sink masks
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to negotiate
     */
    int (*negotiate)(esp_capture_pipeline_builder_if_t *builder, uint8_t sink_mask);

    /**
     * @brief  Release pipelines
     *
     * @note  This will destroy all created pipelines
     *
     * @param[in]  builder  Pointer to the pipeline builder interface
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to release pipelines
     */
    int (*release_pipelines)(esp_capture_pipeline_builder_if_t *builder);

    /**
     * @brief  Destroy pipeline builder
     *
     * @param[in]  builder  Pointer to the pipeline builder interface
     */
    void (*destroy)(esp_capture_pipeline_builder_if_t *builder);
};

/**
 * @brief  Configuration for GMF audio pipeline
 *
 * @note  This structure defines the configuration for building an audio pipeline,
 *        including sources, sinks, and element pools
 */
typedef struct {
    esp_capture_audio_src_if_t  **aud_src;           /*!< Array of audio source interfaces */
    uint8_t                       aud_src_num;       /*!< Number of audio sources */
    uint8_t                       aud_sink_num;      /*!< Number of audio sinks */
    void                         *element_pool;      /*!< Audio element pools */
} esp_capture_gmf_audio_pipeline_cfg_t;

/**
 * @brief  Configuration for GMF video pipeline
 *
 * @note  This structure defines the configuration for building a video pipeline,
 *        including sources, sinks, and frame counts
 */
typedef struct {
    esp_capture_video_src_if_t  **vid_src;       /*!< Array of video source interfaces */
    uint8_t                       vid_src_num;   /*!< Number of video sources */
    uint8_t                       vid_sink_num;  /*!< Number of video sinks */
} esp_capture_gmf_video_pipeline_cfg_t;

/**
 * @brief  Configuration for auto GMF audio pipeline builder
 *
 * @note  This structure defines the configuration for automatically building
 *        an audio pipeline with a single source
 */
typedef struct {
    esp_capture_audio_src_if_t  *aud_src;       /*!< Audio source interface */
    void                        *element_pool;  /*!< Audio element pools */
} esp_capture_gmf_auto_audio_pipeline_cfg_t;

/**
 * @brief  Configuration for auto GMF video pipeline builder
 *
 * @note  This structure defines the configuration for automatically building
 *        a video pipeline with a single source
 */
typedef struct {
    esp_capture_video_src_if_t  *vid_src;       /*!< Video source interface */
    void                        *element_pool;  /*!< Video element pools */
} esp_capture_gmf_auto_video_pipeline_cfg_t;

/**
 * @brief  Create audio pipeline builder using audio pipeline configuration
 *
 * @note  The created builder must be destroyed using `esp_capture_destroy_pipeline` when no longer needed
 *
 * @param[in]  cfg  Pointer to audio pipeline configuration
 *
 * @return
 *       - NULL    Not enough memory
 *       - Others  Audio pipeline builder interface
 */
esp_capture_pipeline_builder_if_t *esp_capture_create_audio_pipeline(esp_capture_gmf_audio_pipeline_cfg_t *cfg);

/**
 * @brief  Create video pipeline builder using video pipeline configuration
 *
 * @note  The created builder must be destroyed using `esp_capture_destroy_pipeline` when no longer needed
 *
 * @param[in]  cfg  Pointer to video pipeline configuration
 *
 * @return
 *       - NULL    Not enough memory
 *       - Others  Video pipeline builder interface
 */
esp_capture_pipeline_builder_if_t *esp_capture_create_video_pipeline(esp_capture_gmf_video_pipeline_cfg_t *cfg);

/**
 * @brief  Create auto audio pipeline builder using auto audio pipeline configuration
 *
 * @note  The created builder must be destroyed using `esp_capture_destroy_pipeline` when no longer needed
 *
 * @param[in]  cfg  Pointer to auto audio pipeline configuration
 *
 * @return
 *       - NULL    Not enough memory
 *       - Others  Auto audio pipeline builder interface
 */
esp_capture_pipeline_builder_if_t *esp_capture_create_auto_audio_pipeline(esp_capture_gmf_auto_audio_pipeline_cfg_t *cfg);

/**
 * @brief  Create auto video pipeline builder using auto video pipeline configuration
 *
 * @note  The created builder must be destroyed using esp_capture_destroy_pipeline when no longer needed
 *
 * @param[in]  cfg  Pointer to auto video pipeline configuration
 *
 * @return
 *       - NULL    Not enough memory
 *       - Others  Auto video pipeline builder interface
 */
esp_capture_pipeline_builder_if_t *esp_capture_create_auto_video_pipeline(esp_capture_gmf_auto_video_pipeline_cfg_t *cfg);

/**
 * @brief  Destroy pipeline builder
 *
 * @note  This function will release all resources associated with the pipeline builder
 *
 * @param[in]  builder  Pointer to the pipeline builder interface
 */
static inline void esp_capture_destroy_pipeline(esp_capture_pipeline_builder_if_t *builder)
{
    if (builder) {
        // Builder will free memory in destroy function
        builder->destroy(builder);
    }
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */
