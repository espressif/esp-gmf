/**
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_gmf_audio_element.h"
#include "esp_gmf_info.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define DEFAULT_RUN_LOOPS (200)

typedef struct {
    uint8_t  open_order;
} pipeline_state_t;

typedef struct {
    esp_gmf_audio_element_t  parent;
    uint8_t                  open_order;
    uint8_t                  open_count;
    uint8_t                  close_count;
    uint8_t                  reset_count;
    int                      running_count;
    esp_gmf_info_sound_t     snd_info;
    esp_gmf_job_err_t        open_return;
    esp_gmf_job_err_t        close_return;
    esp_gmf_job_err_t        reset_return;
    esp_gmf_job_err_t        process_return;
    esp_gmf_err_t            event_return;
} general_el_t;

typedef struct {
    bool               is_dependent;
    bool               report_in_process;
    int                report_pos;
    pipeline_state_t  *state;
    esp_gmf_job_err_t  init_return;
} general_el_cfg_t;

/**
 * @brief  Initialize a general element for testing
 *
 * @param[in]   config  Pointer to the general element configuration
 * @param[out]  handle  Pointer to store the initialized element handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  If handle is invalid
 *       - ESP_GMF_ERR_MEMORY_LACK  Memory allocation failed
 */
esp_gmf_err_t general_el_init(general_el_cfg_t *config, esp_gmf_element_handle_t *handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
