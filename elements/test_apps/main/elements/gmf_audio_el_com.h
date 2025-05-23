/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _GMF_AUDIO_EL_COM_H_
#define _GMF_AUDIO_EL_COM_H_

#include "esp_gmf_element.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_audio_param.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct {
    uint8_t *data;
    size_t   size;
    size_t   buf_length;
    bool     is_released;
    bool     is_done;
} sweep_data_t;

typedef struct {
    uint32_t                  in_frame_count;
    bool                      is_done;
    uint32_t                  src_codec;
    esp_gmf_info_sound_t      src_info;
    uint8_t                  *src_data;
    uint32_t                  src_size;
    uint32_t                  in_acquire_count;
    uint32_t                  in_release_count;
    QueueHandle_t             sweep_queue1;
    QueueHandle_t             sweep_queue2;
    TaskHandle_t              task_hd;
    esp_gmf_pipeline_handle_t pipeline;
    bool                      change_info;
    bool                      is_sweep_signal;
} audio_el_in_test_t;

typedef struct {
    uint32_t             out_codec;
    esp_gmf_info_sound_t out_info;
    uint8_t             *out_data;
    uint32_t             out_size;
    uint32_t             out_frame_count;
    uint32_t             out_max_size;
    bool                 no_need_free;
    uint32_t             out_acquire_count;
    uint32_t             out_release_count;
} audio_el_out_test_t;

typedef struct {
    esp_gmf_pool_handle_t     pool;
    esp_gmf_pipeline_handle_t pipe;
    esp_gmf_task_handle_t     task;
    esp_gmf_element_handle_t  current_hd[5];
    int32_t                   el_cnt;
    audio_el_in_test_t       *in_inst;
    audio_el_out_test_t      *out_inst;
    uint32_t                  in_port_num;
    uint32_t                  out_port_num;
    TaskHandle_t              cfg_task_hd;
    EventGroupHandle_t        pipe_sync_evt;
    void                      (*config_func)(esp_gmf_element_handle_t, void *);
    bool                      is_end;
} audio_el_res_t;

void eq_config_callback(esp_gmf_element_handle_t self, void *ctx);

void fade_config_callback(esp_gmf_element_handle_t self, void *ctx);

void mixer_config_callback(esp_gmf_element_handle_t self, void *ctx);

void encoder_config_callback(esp_gmf_element_handle_t self, void *ctx);

void decoder_config_callback(esp_gmf_element_handle_t self, void *ctx);

void sonic_config_callback(esp_gmf_element_handle_t self, void *ctx);

void alc_config_callback(esp_gmf_element_handle_t self, void *ctx);

void bit_cvt_config_callback(esp_gmf_element_handle_t self, void *ctx);

void ch_cvt_config_callback(esp_gmf_element_handle_t self, void *ctx);

void rate_cvt_config_callback(esp_gmf_element_handle_t self, void *ctx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _GMF_AUDIO_EL_COM_H_ */
