/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_err.h"
#include "esp_types.h"
#include "esp_audio_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Enumeration for Bluetooth audio profiles stream types
 */
typedef enum {
    ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN = 0,   /*!< Unknown audio profile */
    /* Classic Bluetooth */
    ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_A2DP,  /*!< Classic Bluetooth A2DP stream */
    ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_HFP,   /*!< Classic Bluetooth HFP stream */

    /* LE Audio */
    ESP_BT_AUDIO_STREAM_PROFILE_LE_UNICAST,    /*!< LE Audio Unicast stream */
    ESP_BT_AUDIO_STREAM_PROFILE_LE_BROADCAST,  /*!< LE Audio Broadcast (Auracast) stream */
} esp_bt_audio_stream_profile_t;

/**
 * @brief  Enumeration for Bluetooth audio direction
 */
typedef enum {
    ESP_BT_AUDIO_STREAM_DIR_UNKNOWN = 0x00,  /*!< Unknown stream direction */
    ESP_BT_AUDIO_STREAM_DIR_SINK    = 0x01,  /*!< Sink stream (receiver) */
    ESP_BT_AUDIO_STREAM_DIR_SOURCE  = 0x02,  /*!< Source stream (sender) */
} esp_bt_audio_stream_dir_t;

/**
 * @brief  Enumeration for Bluetooth audio context
 */
typedef enum {
    ESP_BT_AUDIO_STREAM_CONTEXT_UNSPECIFIED    = 0x01,  /*!< Unspecified audio context */
    ESP_BT_AUDIO_STREAM_CONTEXT_CONVERSATIONAL = 0x02,  /*!< Conversational audio context */
    ESP_BT_AUDIO_STREAM_CONTEXT_MEDIA          = 0x04,  /*!< Media audio context */
} esp_bt_audio_stream_context_t;

/**
 * @brief  Enumeration for Bluetooth audio codecs
 */
typedef enum {
    ESP_BT_AUDIO_STREAM_CODEC_UNKNOWN,                   /*!< Unknown audio codec */
    ESP_BT_AUDIO_STREAM_CODEC_SBC = ESP_AUDIO_TYPE_SBC,  /*!< SBC codec */
    ESP_BT_AUDIO_STREAM_CODEC_LC3 = ESP_AUDIO_TYPE_LC3,  /*!< LC3 codec */
} esp_bt_audio_stream_codec_type_t;

/**
 * @brief  Enumeration for Bluetooth audio stream state
 */
typedef enum {
    ESP_BT_AUDIO_STREAM_STATE_ALLOCATED,  /*!< Stream allocated */
    ESP_BT_AUDIO_STREAM_STATE_STARTED,    /*!< Stream started */
    ESP_BT_AUDIO_STREAM_STATE_STOPPED,    /*!< Stream stopped */
    ESP_BT_AUDIO_STREAM_STATE_RELEASED,   /*!< Stream released */
} esp_bt_audio_stream_state_t;

/**
 * @brief  Handle for a Bluetooth stream
 */
typedef void *esp_bt_audio_stream_handle_t;

/**
 * @brief  Structure for a Bluetooth stream packet
 */
typedef struct {
    uint8_t *data;        /*!< Pointer to data payload */
    size_t   size;        /*!< Size of data payload */
    bool     bad_frame;   /*!< True if the data is invalid or corrupted */
    bool     is_done;     /*!< Flag to indicate if this packet buffer marks the end of the stream */
    void    *data_owner;  /*!< Pointer to the data owner, internal use only */
} esp_bt_audio_stream_packet_t;

/**
 * @brief  Type definition for acquiring a read packet
 */
typedef esp_err_t (*esp_bt_audio_stream_acquire_read_t)(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_packet_t *packet, uint32_t wait_ms);

/**
 * @brief  Type definition for releasing a read packet
 */
typedef esp_err_t (*esp_bt_audio_stream_release_read_t)(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_packet_t *packet);

/**
 * @brief  Type definition for acquiring a write packet
 */
typedef esp_err_t (*esp_bt_audio_stream_acquire_write_t)(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_packet_t *packet, uint32_t wanted_size);

/**
 * @brief  Type definition for releasing a write packet
 */
typedef esp_err_t (*esp_bt_audio_stream_release_write_t)(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_packet_t *packet, uint32_t wait_ms);

/**
 * @brief  Structure for Bluetooth stream operations
 */
typedef struct {
    esp_bt_audio_stream_acquire_read_t   acquire_read;   /*!< Function to acquire a packet for reading */
    esp_bt_audio_stream_release_read_t   release_read;   /*!< Function to release a read packet */
    esp_bt_audio_stream_acquire_write_t  acquire_write;  /*!< Function to acquire a packet for writing */
    esp_bt_audio_stream_release_write_t  release_write;  /*!< Function to release a write packet */
} esp_bt_audio_stream_ops_t;

/**
 * @brief  Structure for Bluetooth stream codec information
 */
typedef struct {
    esp_bt_audio_stream_codec_type_t  codec_type;   /*!< Codec type, based on esp_audio_codec */
    void                             *codec_cfg;    /*!< Pointer to codec configuration
                                                       This points to a configuration structure defined by `esp_audio_codec`.
                                                       The concrete structure type is selected automatically according to `codec_type`
                                                       (and whether the stream is used for decoding or encoding).

                                                       Codec configuration mapping (based on current implementation):

                                                       | Direction | codec_type                    | codec_cfg concrete type |
                                                       |-----------|-------------------------------|-------------------------|
                                                       | Source    | ESP_BT_AUDIO_STREAM_CODEC_SBC | esp_sbc_enc_config_t    |
                                                       | Sink      | ESP_BT_AUDIO_STREAM_CODEC_SBC | esp_sbc_dec_cfg_t       |
                                                       | Source    | ESP_BT_AUDIO_STREAM_CODEC_LC3 | esp_lc3_enc_config_t    |
                                                       | Sink      | ESP_BT_AUDIO_STREAM_CODEC_LC3 | esp_lc3_dec_cfg_t       |
                                                      */
    uint32_t                          cfg_size;     /*!< Size of the codec configuration */
    uint32_t                          sample_rate;  /*!< Sample rate in Hz */
    uint32_t                          channels;     /*!< Number of channels */
    uint32_t                          bits;         /*!< Bit width per sample */
    uint32_t                          frame_size;   /*!< Frame size in bytes */
} esp_bt_audio_stream_codec_info_t;

/**
 * @brief  Structure for base Bluetooth stream item
 */
typedef struct {
    void                             *local_data;  /*!< Local data pointer */
    void                             *data_q;      /*!< Data queue */
    uint32_t                          context;     /*!< Audio context */
    esp_bt_audio_stream_profile_t     profile;     /*!< Audio profile */
    esp_bt_audio_stream_dir_t         direction;   /*!< Audio direction */
    esp_bt_audio_stream_codec_info_t  codec_info;  /*!< Codec information */
    esp_bt_audio_stream_ops_t         ops;         /*!< Stream operations */
} esp_bt_audio_stream_base_t;

/**
 * @brief  Get codec information for a stream
 *
 * @param[in]   handle      Stream handle
 * @param[out]  codec_info  Pointer to store codec information
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid argument
 */
esp_err_t esp_bt_audio_stream_get_codec_info(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_codec_info_t *codec_info);

/**
 * @brief  Get audio direction of a stream
 *
 * @param[in]   handle  Stream handle
 * @param[out]  dir     Pointer to store stream direction
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    Invalid argument
 *       - ESP_ERR_INVALID_STATE  Invalid stream state
 */
esp_err_t esp_bt_audio_stream_get_dir(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_dir_t *dir);

/**
 * @brief  Get audio profile of a stream
 *
 * @param[in]   handle   Stream handle
 * @param[out]  profile  Pointer to store stream profile
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid argument
 */
esp_err_t esp_bt_audio_stream_get_profile(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_profile_t *profile);

/**
 * @brief  Get context of a stream
 *
 * @param[in]   handle   Stream handle
 * @param[out]  context  Pointer to store context value
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    Invalid argument
 *       - ESP_ERR_INVALID_STATE  Invalid stream state
 */
esp_err_t esp_bt_audio_stream_get_context(esp_bt_audio_stream_handle_t handle, uint32_t *context);

/**
 * @brief  Get local data associated with a stream
 *
 * @param[in]   handle      Stream handle
 * @param[out]  local_data  Pointer to store local data pointer
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    Invalid argument
 *       - ESP_ERR_INVALID_STATE  Invalid stream state
 */
esp_err_t esp_bt_audio_stream_get_local_data(esp_bt_audio_stream_handle_t handle, void **local_data);

/**
 * @brief  Set local data associated with a stream
 *
 * @param[in]  handle      Stream handle
 * @param[in]  local_data  Pointer to local data
 *
 * @return
 *       - Pointer  to previous local data or NULL
 */
void *esp_bt_audio_stream_set_local_data(esp_bt_audio_stream_handle_t handle, void *local_data);

/**
 * @brief  Acquire a packet buffer from the stream for writing
 *
 * @note  On success, the stream returns a writable buffer in `packet->data` and keeps
 *        the ownership information internally via `packet->data_owner`.
 *
 * @param[in]  handle       Stream handle
 * @param[in]  packet       Pointer to the stream packet
 * @param[in]  wanted_size  Wanted buffer size in bytes
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_stream_acquire_write(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_packet_t *packet, uint32_t wanted_size);

/**
 * @brief  Release a write packet back to the stream
 *
 * @param[in]  handle   Stream handle
 * @param[in]  packet   Pointer to the packet to release
 * @param[in]  wait_ms  Wait time in milliseconds
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_stream_release_write(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_packet_t *packet, uint32_t wait_ms);

/**
 * @brief  Acquire a packet from the stream for reading
 *
 * @param[in]   handle   Stream handle
 * @param[out]  packet   Pointer to store the acquired packet
 * @param[in]   wait_ms  Wait time in milliseconds
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_stream_acquire_read(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_packet_t *packet, uint32_t wait_ms);

/**
 * @brief  Release a read packet back to the stream
 *
 * @param[in]  handle  Stream handle
 * @param[in]  packet  Pointer to the packet to release
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_stream_release_read(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_packet_t *packet);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
