/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Playback operations
 */
typedef struct {
    esp_err_t (*play)(void);                        /*!< Play */
    esp_err_t (*pause)(void);                       /*!< Pause */
    esp_err_t (*stop)(void);                        /*!< Stop */
    esp_err_t (*next)(void);                        /*!< Next track */
    esp_err_t (*prev)(void);                        /*!< Previous track */
    esp_err_t (*request_metadata)(uint32_t mask);   /*!< Request metadata */
    esp_err_t (*reg_notifications)(uint32_t mask);  /*!< Register remote notifications */
} esp_bt_audio_playback_ops_t;

/**
 * @brief  Media operations
 */
typedef struct {
    esp_err_t (*a2d_media_start)(void *config);  /*!< Start a2dp source media */
    esp_err_t (*a2d_media_stop)(void);           /*!< Stop a2dp source media */
} esp_bt_audio_media_ops_t;

/**
 * @brief  Volume operations
 */
typedef struct {
    esp_err_t (*set_absolute)(uint32_t vol);  /*!< Set absolute volume */
    esp_err_t (*set_relative)(bool up_down);  /*!< Set relative volume, true: up, false: down */
    esp_err_t (*notify)(uint32_t vol);        /*!< Volume changed notification */
} esp_bt_audio_vol_ops_t;

/**
 * @brief  Call control operations
 */
typedef struct {
    esp_err_t (*answer_call)(uint8_t idx);  /*!< Answer an incoming call */
    esp_err_t (*reject_call)(uint8_t idx);  /*!< Reject an incoming call or hang up current call */
    esp_err_t (*dial)(const char *number);  /*!< Dial a number; if number is NULL, redial last number */
} esp_bt_audio_call_ops_t;

/**
 * @brief  Phonebook operations
 */
typedef struct {
    esp_err_t (*fetch)(uint8_t target, uint16_t start_idx, uint16_t count);  /*!< Fetch phonebook entries */
} esp_bt_audio_pb_ops_t;

/**
 * @brief  Classic Bluetooth operations
 */
typedef struct {
    esp_err_t (*start_discovery)(void);                               /*!< Start discovery */
    esp_err_t (*stop_discovery)(void);                                /*!< Stop discovery */
    esp_err_t (*set_scan_mode)(bool connectable, bool discoverable);  /*!< Set scan mode */
    esp_err_t (*a2d_src_connect)(uint8_t *bda);                       /*!< Connect */
    esp_err_t (*a2d_src_disconnect)(uint8_t *bda);                    /*!< Disconnect from A2DP source */
    esp_err_t (*a2d_sink_connect)(uint8_t *bda);                      /*!< Connect to A2DP sink */
    esp_err_t (*a2d_sink_disconnect)(uint8_t *bda);                   /*!< Disconnect from A2DP sink */
    esp_err_t (*hfp_hf_connect)(uint8_t *bda);                        /*!< Connect to HFP HF */
    esp_err_t (*hfp_hf_disconnect)(uint8_t *bda);                     /*!< Disconnect from HFP HF */
    esp_err_t (*pbac_connect)(uint8_t *bda);                          /*!< Connect to PBAC */
    esp_err_t (*pbac_disconnect)(uint8_t *bda);                       /*!< Disconnect from PBAC */
} esp_bt_audio_classic_ops_t;

/**
 * @brief  Initialize Bluetooth operations
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_NO_MEM         No memory available
 *       - ESP_ERR_INVALID_STATE  If already initialized
 */
esp_err_t bt_audio_ops_init(void);

/**
 * @brief  Deinitialize Bluetooth operations
 */
void bt_audio_ops_deinit(void);

/**
 * @brief  Get playback operations
 *
 * @param[out]  playback_ops  Pointer to store playback operations
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    Invalid argument
 *       - ESP_ERR_INVALID_STATE  If not initialized
 */
esp_err_t bt_audio_ops_get_playback(esp_bt_audio_playback_ops_t *playback_ops);

/**
 * @brief  Set playback operations
 *
 * @param[in]  playback_ops  Pointer to playback operations
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    Invalid argument
 *       - ESP_ERR_INVALID_STATE  If not initialized
 */
esp_err_t bt_audio_ops_set_playback(esp_bt_audio_playback_ops_t *playback_ops);

/**
 * @brief  Get media operations
 *
 * @param[out]  media_ops  Pointer to store media operations
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    Invalid argument
 *       - ESP_ERR_INVALID_STATE  If not initialized
 */
esp_err_t bt_audio_ops_get_media(esp_bt_audio_media_ops_t *media_ops);

/**
 * @brief  Set media operations
 *
 * @param[in]  media_ops  Pointer to media operations
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    Invalid argument
 *       - ESP_ERR_INVALID_STATE  If not initialized
 */
esp_err_t bt_audio_ops_set_media(esp_bt_audio_media_ops_t *media_ops);

/**
 * @brief  Get volume operations
 *
 * @param[out]  vol_ops  Pointer to store volume operations
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    Invalid argument
 *       - ESP_ERR_INVALID_STATE  If not initialized
 */
esp_err_t bt_audio_ops_get_vol(esp_bt_audio_vol_ops_t *vol_ops);

/**
 * @brief  Set volume operations
 *
 * @param[in]  vol_ops  Pointer to volume operations
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    Invalid argument
 *       - ESP_ERR_INVALID_STATE  If not initialized
 */
esp_err_t bt_audio_ops_set_vol(esp_bt_audio_vol_ops_t *vol_ops);

/**
 * @brief  Get call control operations
 *
 * @param[out]  call_ops  Pointer to store call operations
 */
esp_err_t bt_audio_ops_get_call(esp_bt_audio_call_ops_t *call_ops);

/**
 * @brief  Set call control operations
 *
 * @param[in]  call_ops  Pointer to call operations
 */
esp_err_t bt_audio_ops_set_call(esp_bt_audio_call_ops_t *call_ops);

/**
 * @brief  Get phonebook operations
 *
 * @param[out]  pb_ops  Pointer to store phonebook operations
 */
esp_err_t bt_audio_ops_get_pb(esp_bt_audio_pb_ops_t *pb_ops);

/**
 * @brief  Set phonebook operations
 *
 * @param[in]  pb_ops  Pointer to phonebook operations
 */
esp_err_t bt_audio_ops_set_pb(esp_bt_audio_pb_ops_t *pb_ops);

/**
 * @brief  Get classic operations
 *
 * @param[out]  classic_ops  Pointer to store classic operations
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    Invalid argument
 *       - ESP_ERR_INVALID_STATE  If not initialized
 */
esp_err_t bt_audio_ops_get_classic(esp_bt_audio_classic_ops_t *classic_ops);

/**
 * @brief  Set classic operations
 *
 * @param[in]  classic_ops  Pointer to classic operations
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    Invalid argument
 *       - ESP_ERR_INVALID_STATE  If not initialized
 */
esp_err_t bt_audio_ops_set_classic(esp_bt_audio_classic_ops_t *classic_ops);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
