/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Host-neutral Advertising Data (AD) type constants.
 *
 * These values are Bluetooth SIG assigned numbers. Keep them host-neutral so
 * common LE Audio code does not depend on NimBLE or Bluedroid GAP headers.
 */
#define BT_AUDIO_AD_TYPE_FLAGS             0x01
#define BT_AUDIO_AD_TYPE_INCOMP_UUIDS16    0x02
#define BT_AUDIO_AD_TYPE_COMP_UUIDS16      0x03
#define BT_AUDIO_AD_TYPE_INCOMP_NAME       0x08
#define BT_AUDIO_AD_TYPE_COMP_NAME         0x09
#define BT_AUDIO_AD_TYPE_TX_PWR_LVL        0x0A
#define BT_AUDIO_AD_TYPE_SVC_DATA_UUID16   0x16
#define BT_AUDIO_AD_TYPE_APPEARANCE        0x19
#define BT_AUDIO_AD_TYPE_MFG_DATA          0xFF
#define BT_AUDIO_AD_TYPE_BROADCAST_NAME    0x30

#define BT_AUDIO_LE_PHY_1M                  1
#define BT_AUDIO_LE_PHY_2M                  2
#define BT_AUDIO_ADV_ITVL_MS(t)             ((t) * 1000 / 625)
#define BT_AUDIO_PERIODIC_ADV_ITVL_MS(t)    ((t) * 1000 / 1250)
#define BT_AUDIO_OWN_ADDR_PUBLIC            0x00
#define BT_AUDIO_HOST_EALREADY              2
#define BT_AUDIO_ERR_REM_USER_CONN_TERM     0x13

/**
 * @brief  Bluetooth device address in a host-neutral format.
 */
typedef struct {
    uint8_t  type;    /*!< Address type */
    uint8_t  val[6];  /*!< Address bytes */
} bt_audio_addr_t;

/**
 * @brief  Extended advertising configuration shared by host adapters.
 */
typedef struct {
    bool      connectable;    /*!< True if the advertising set is connectable */
    bool      scannable;      /*!< True if the advertising set is scannable */
    bool      legacy_pdu;     /*!< True if legacy advertising PDUs are used */
    uint8_t   own_addr_type;  /*!< Local address type */
    uint8_t   primary_phy;    /*!< Primary advertising PHY */
    uint8_t   secondary_phy;  /*!< Secondary advertising PHY */
    int8_t    tx_power;       /*!< Requested transmit power */
    uint8_t   sid;            /*!< Advertising SID */
    uint32_t  itvl_min;       /*!< Minimum advertising interval */
    uint32_t  itvl_max;       /*!< Maximum advertising interval */
} bt_audio_ext_adv_params_t;

/**
 * @brief  Periodic advertising configuration shared by host adapters.
 */
typedef struct {
    bool      include_tx_power;  /*!< True to include Tx power in periodic advertising */
    uint32_t  itvl_min;          /*!< Minimum periodic advertising interval */
    uint32_t  itvl_max;          /*!< Maximum periodic advertising interval */
} bt_audio_periodic_adv_params_t;

/**
 * @brief  Periodic advertising sync parameters shared by host adapters.
 */
typedef struct {
    uint16_t  skip;          /*!< Number of periodic advertising events that can be skipped */
    uint16_t  sync_timeout;  /*!< Periodic sync timeout */
} bt_audio_periodic_sync_params_t;

/**
 * @brief  Scan parameters shared by host adapters.
 */
typedef struct {
    bool      passive;            /*!< True to use passive scanning */
    bool      filter_duplicates;  /*!< True to filter duplicate reports */
    uint16_t  itvl;               /*!< Scan interval */
    uint16_t  window;             /*!< Scan window */
} bt_audio_scan_params_t;

/**
 * @brief  Connection parameters shared by host adapters.
 */
typedef struct {
    uint16_t  scan_itvl;            /*!< Scan interval used for connection establishment */
    uint16_t  scan_window;          /*!< Scan window used for connection establishment */
    uint16_t  itvl_min;             /*!< Minimum connection interval */
    uint16_t  itvl_max;             /*!< Maximum connection interval */
    uint16_t  latency;              /*!< Connection latency */
    uint16_t  supervision_timeout;  /*!< Supervision timeout */
} bt_audio_conn_params_t;

/**
 * @brief  Connection descriptor shared by host adapters.
 */
typedef struct {
    uint8_t  peer_id_addr[6];  /*!< Peer identity address bytes */
} bt_audio_conn_desc_t;

/**
 * @brief  Configure an extended advertising set.
 *
 * @param[in]  handle  Advertising set handle
 * @param[in]  params  Host-neutral extended advertising parameters
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_ext_adv_configure_t)(uint8_t handle,
                                                       const bt_audio_ext_adv_params_t *params);

/**
 * @brief  Set extended advertising data.
 *
 * @param[in]  handle  Advertising set handle
 * @param[in]  data    Advertising data buffer
 * @param[in]  len     Advertising data length in bytes
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_ext_adv_set_data_t)(uint8_t handle, const uint8_t *data, size_t len);

/**
 * @brief  Start an extended advertising set.
 *
 * @param[in]  handle      Advertising set handle
 * @param[in]  duration    Advertising duration in host stack units
 * @param[in]  max_events  Maximum number of advertising events, or 0 for no limit
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_ext_adv_start_t)(uint8_t handle, int duration, int max_events);

/**
 * @brief  Stop an extended advertising set.
 *
 * @param[in]  handle  Advertising set handle
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_ext_adv_stop_t)(uint8_t handle);

/**
 * @brief  Start LE discovery.
 *
 * @param[in]  own_addr_type  Local address type
 * @param[in]  params         Host-neutral scan parameters
 * @param[in]  timeout_ms     Discovery timeout in milliseconds
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_disc_t)(uint8_t own_addr_type, const bt_audio_scan_params_t *params,
                                          uint32_t timeout_ms);

/**
 * @brief  Cancel LE discovery.
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_disc_cancel_t)(void);

/**
 * @brief  Open an LE ACL connection.
 *
 * @param[in]  own_addr_type  Local address type
 * @param[in]  peer           Peer address
 * @param[in]  params         Host-neutral connection parameters
 * @param[in]  timeout_ms     Connection timeout in milliseconds
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_connect_t)(uint8_t own_addr_type, const bt_audio_addr_t *peer,
                                             const bt_audio_conn_params_t *params, uint32_t timeout_ms);

/**
 * @brief  Close an LE ACL connection.
 *
 * @param[in]  conn_handle  Connection handle
 * @param[in]  reason       Disconnect reason
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_disconnect_t)(uint16_t conn_handle, uint8_t reason);

/**
 * @brief  Find LE connection details.
 *
 * @param[in]   conn_handle  Connection handle
 * @param[out]  desc         Connection descriptor
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_conn_find_t)(uint16_t conn_handle, bt_audio_conn_desc_t *desc);

/**
 * @brief  Notify the active host that an LE ACL link is connected.
 *
 * @param[in]  conn_handle  Connection handle
 * @param[in]  peer         Peer address
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_acl_connected_t)(uint16_t conn_handle, const bt_audio_addr_t *peer);

/**
 * @brief  Notify the active host that an LE ACL link is disconnected.
 *
 * @param[in]  conn_handle  Connection handle
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_acl_disconnected_t)(uint16_t conn_handle);

/**
 * @brief  Initiate LE link security.
 *
 * @param[in]  conn_handle  Connection handle
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_security_initiate_t)(uint16_t conn_handle);

/**
 * @brief  Configure periodic advertising.
 *
 * @param[in]  adv_handle  Advertising set handle
 * @param[in]  params      Host-neutral periodic advertising parameters
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_periodic_adv_configure_t)(uint8_t adv_handle,
                                                            const bt_audio_periodic_adv_params_t *params);

/**
 * @brief  Set periodic advertising data.
 *
 * @param[in]  adv_handle  Advertising set handle
 * @param[in]  data        Periodic advertising data buffer
 * @param[in]  len         Periodic advertising data length in bytes
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_periodic_adv_set_data_t)(uint8_t adv_handle, const uint8_t *data, size_t len);

/**
 * @brief  Start periodic advertising.
 *
 * @param[in]  adv_handle  Advertising set handle
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_periodic_adv_start_t)(uint8_t adv_handle);

/**
 * @brief  Stop periodic advertising.
 *
 * @param[in]  adv_handle  Advertising set handle
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_periodic_adv_stop_t)(uint8_t adv_handle);

/**
 * @brief  Create a periodic advertising sync.
 *
 * @param[in]  addr    Advertiser address
 * @param[in]  sid     Advertising SID
 * @param[in]  params  Host-neutral periodic sync parameters
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_pa_sync_create_t)(const bt_audio_addr_t *addr, uint8_t sid,
                                                    const bt_audio_periodic_sync_params_t *params);

/**
 * @brief  Terminate a periodic advertising sync.
 *
 * @param[in]  sync_handle  Periodic advertising sync handle
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_pa_sync_terminate_t)(uint16_t sync_handle);

/**
 * @brief  Cancel pending periodic advertising sync creation.
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_pa_sync_create_cancel_t)(void);

/**
 * @brief  Enable receiving periodic advertising sync through PAST.
 *
 * @param[in]  conn_handle  Connection handle carrying PAST
 * @param[in]  params       Host-neutral periodic sync parameters
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_pa_sync_receive_t)(uint16_t conn_handle,
                                                     const bt_audio_periodic_sync_params_t *params);

/**
 * @brief  Infer the local address type automatically.
 *
 * @param[in]   privacy        Whether privacy should be considered by the host stack
 * @param[out]  out_addr_type  Local address type
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_id_infer_auto_t)(int privacy, uint8_t *out_addr_type);

/**
 * @brief  Get the GAP device name from the active host stack.
 *
 * @return
 *       - Device name string on success
 *       - NULL if no name is available
 */
typedef const char *(*bt_audio_host_svc_gap_device_name_t)(void);

/**
 * @brief  Get the number of free ISO TX buffers.
 *
 * @param[in]  conn_handle  ISO connection handle
 *
 * @return
 *       - Number of free ISO TX buffers
 */
typedef uint16_t (*bt_audio_host_iso_free_buf_num_get_t)(uint16_t conn_handle);

/**
 * @brief  Send ISO data through the host HCI path.
 *
 * @param[in]  conn_handle  ISO connection handle
 * @param[in]  sdu          SDU data buffer
 * @param[in]  sdu_len      SDU data length in bytes
 * @param[in]  ts_flag      True if time_stamp is valid
 * @param[in]  time_stamp   ISO packet timestamp
 * @param[in]  pkt_seq_num  ISO packet sequence number
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_hci_iso_tx_t)(uint16_t conn_handle, const uint8_t *sdu, uint16_t sdu_len,
                                                bool ts_flag, uint32_t time_stamp, uint16_t pkt_seq_num);

/**
 * @brief  Register the active host event callback.
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Host adapter error code
 */
typedef esp_err_t (*bt_audio_host_register_event_cb_t)(void);

/**
 * @brief  Forward a raw host GAP event to the Bluetooth audio middleware.
 *
 * @param[in]  gap_event_type  Host adapter GAP event type
 * @param[in]  event           Raw host event pointer
 */
typedef void (*bt_audio_host_post_gap_event_t)(uint8_t gap_event_type, void *event);

/**
 * @brief  Host adapter operations used by common BT Audio code.
 */
typedef struct {
    bt_audio_host_ext_adv_configure_t           ext_adv_configure;       /*!< Configure ext advertising */
    bt_audio_host_ext_adv_set_data_t            ext_adv_set_data;        /*!< Set ext advertising data */
    bt_audio_host_ext_adv_start_t               ext_adv_start;           /*!< Start ext advertising */
    bt_audio_host_ext_adv_stop_t                ext_adv_stop;            /*!< Stop ext advertising */
    bt_audio_host_disc_t                        disc;                    /*!< Start discovery */
    bt_audio_host_disc_cancel_t                 disc_cancel;             /*!< Cancel discovery */
    bt_audio_host_connect_t                     connect;                 /*!< Open an ACL connection */
    bt_audio_host_disconnect_t                  disconnect;              /*!< Close an ACL connection */
    bt_audio_host_conn_find_t                   conn_find;               /*!< Find connection details */
    bt_audio_host_acl_connected_t               acl_connected;           /*!< Notify ACL connected */
    bt_audio_host_acl_disconnected_t            acl_disconnected;        /*!< Notify ACL disconnected */
    bt_audio_host_security_initiate_t           security_initiate;       /*!< Initiate security */
    bt_audio_host_periodic_adv_configure_t      periodic_adv_configure;  /*!< Configure periodic adv */
    bt_audio_host_periodic_adv_set_data_t       periodic_adv_set_data;   /*!< Set periodic adv data */
    bt_audio_host_periodic_adv_start_t          periodic_adv_start;      /*!< Start periodic adv */
    bt_audio_host_periodic_adv_stop_t           periodic_adv_stop;       /*!< Stop periodic adv */
    bt_audio_host_pa_sync_create_t              pa_sync_create;          /*!< Create PA sync */
    bt_audio_host_pa_sync_terminate_t           pa_sync_terminate;       /*!< Terminate PA sync */
    bt_audio_host_pa_sync_create_cancel_t       pa_sync_create_cancel;   /*!< Cancel pending PA sync */
    bt_audio_host_pa_sync_receive_t             pa_sync_receive;         /*!< Enable PAST receive */
    bt_audio_host_id_infer_auto_t               id_infer_auto;           /*!< Infer local address type */
    bt_audio_host_svc_gap_device_name_t         svc_gap_device_name;     /*!< Get GAP device name */
    bt_audio_host_iso_free_buf_num_get_t        iso_free_buf_num_get;    /*!< Get free ISO TX buffers */
    bt_audio_host_hci_iso_tx_t                  hci_iso_tx;              /*!< Send ISO data */
    bt_audio_host_register_event_cb_t           register_event_cb;       /*!< Register host event callback */
    bt_audio_host_post_gap_event_t              post_gap_event;          /*!< Forward host GAP event */
} bt_audio_host_ops_t;

/**
 * @brief  Set the active host adapter operations.
 *
 * @param[in]  host_ops  Host operation table, or NULL to clear the current table.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If BT Audio operations are not initialized
 */
esp_err_t bt_audio_ops_set_host(const bt_audio_host_ops_t *host_ops);

/**
 * @brief  Configure an extended advertising set.
 *
 * @param[in]  handle  Advertising set handle
 * @param[in]  params  Host-neutral extended advertising parameters
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_ext_adv_configure(uint8_t handle, const bt_audio_ext_adv_params_t *params);

/**
 * @brief  Set extended advertising data.
 *
 * @param[in]  handle  Advertising set handle
 * @param[in]  data    Advertising data buffer
 * @param[in]  len     Advertising data length in bytes
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_ext_adv_set_data(uint8_t handle, const uint8_t *data, size_t len);

/**
 * @brief  Start an extended advertising set.
 *
 * @param[in]  handle      Advertising set handle
 * @param[in]  duration    Advertising duration in host stack units
 * @param[in]  max_events  Maximum number of advertising events, or 0 for no limit
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_ext_adv_start(uint8_t handle, int duration, int max_events);

/**
 * @brief  Stop an extended advertising set.
 *
 * @param[in]  handle  Advertising set handle
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_ext_adv_stop(uint8_t handle);

/**
 * @brief  Start LE discovery.
 *
 * @param[in]  own_addr_type  Local address type
 * @param[in]  params         Host-neutral scan parameters
 * @param[in]  timeout_ms     Discovery timeout in milliseconds
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_disc(uint8_t own_addr_type, const bt_audio_scan_params_t *params, uint32_t timeout_ms);

/**
 * @brief  Cancel LE discovery.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_disc_cancel(void);

/**
 * @brief  Open an LE ACL connection.
 *
 * @param[in]  own_addr_type  Local address type
 * @param[in]  peer           Peer address
 * @param[in]  params         Host-neutral connection parameters
 * @param[in]  timeout_ms     Connection timeout in milliseconds
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_connect(uint8_t own_addr_type, const bt_audio_addr_t *peer,
                                const bt_audio_conn_params_t *params, uint32_t timeout_ms);

/**
 * @brief  Close an LE ACL connection.
 *
 * @param[in]  conn_handle  Connection handle
 * @param[in]  reason       Disconnect reason
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_disconnect(uint16_t conn_handle, uint8_t reason);

/**
 * @brief  Find LE connection details.
 *
 * @param[in]   conn_handle  Connection handle
 * @param[out]  desc         Connection descriptor
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_conn_find(uint16_t conn_handle, bt_audio_conn_desc_t *desc);

/**
 * @brief  Notify the active host that an LE ACL link is connected.
 *
 * @param[in]  conn_handle  Connection handle
 * @param[in]  peer         Peer address
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_acl_connected(uint16_t conn_handle, const bt_audio_addr_t *peer);

/**
 * @brief  Notify the active host that an LE ACL link is disconnected.
 *
 * @param[in]  conn_handle  Connection handle
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_acl_disconnected(uint16_t conn_handle);

/**
 * @brief  Initiate LE link security.
 *
 * @param[in]  conn_handle  Connection handle
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_security_initiate(uint16_t conn_handle);

/**
 * @brief  Configure periodic advertising.
 *
 * @param[in]  adv_handle  Advertising set handle
 * @param[in]  params      Host-neutral periodic advertising parameters
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_periodic_adv_configure(uint8_t adv_handle,
                                               const bt_audio_periodic_adv_params_t *params);

/**
 * @brief  Set periodic advertising data.
 *
 * @param[in]  adv_handle  Advertising set handle
 * @param[in]  data        Periodic advertising data buffer
 * @param[in]  len         Periodic advertising data length in bytes
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_periodic_adv_set_data(uint8_t adv_handle, const uint8_t *data, size_t len);

/**
 * @brief  Start periodic advertising.
 *
 * @param[in]  adv_handle  Advertising set handle
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_periodic_adv_start(uint8_t adv_handle);

/**
 * @brief  Stop periodic advertising.
 *
 * @param[in]  adv_handle  Advertising set handle
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_periodic_adv_stop(uint8_t adv_handle);

/**
 * @brief  Create a periodic advertising sync.
 *
 * @param[in]  addr    Advertiser address
 * @param[in]  sid     Advertising SID
 * @param[in]  params  Host-neutral periodic sync parameters
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_pa_sync_create(const bt_audio_addr_t *addr, uint8_t sid,
                                       const bt_audio_periodic_sync_params_t *params);

/**
 * @brief  Terminate a periodic advertising sync.
 *
 * @param[in]  sync_handle  Periodic advertising sync handle
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_pa_sync_terminate(uint16_t sync_handle);

/**
 * @brief  Cancel pending periodic advertising sync creation.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_pa_sync_create_cancel(void);

/**
 * @brief  Enable receiving periodic advertising sync through PAST.
 *
 * @param[in]  conn_handle  Connection handle carrying PAST
 * @param[in]  params       Host-neutral periodic sync parameters
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_pa_sync_receive(uint16_t conn_handle, const bt_audio_periodic_sync_params_t *params);

/**
 * @brief  Infer the local address type automatically.
 *
 * @param[in]   privacy        Whether privacy should be considered by the host stack
 * @param[out]  out_addr_type  Local address type
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_id_infer_auto(int privacy, uint8_t *out_addr_type);

/**
 * @brief  Get the GAP device name from the active host stack.
 *
 * @return
 *       - Device name string on success
 *       - NULL if the host operation is not registered or no name is available
 */
const char *bt_audio_host_svc_gap_device_name(void);

/**
 * @brief  Get the number of free ISO TX buffers.
 *
 * @param[in]  conn_handle  ISO connection handle
 *
 * @return
 *       - Number of free ISO TX buffers
 *       - 0 if the host operation is not registered
 */
uint16_t bt_audio_host_iso_free_buf_num_get(uint16_t conn_handle);

/**
 * @brief  Send ISO data through the host HCI path.
 *
 * @param[in]  conn_handle  ISO connection handle
 * @param[in]  sdu          SDU data buffer
 * @param[in]  sdu_len      SDU data length in bytes
 * @param[in]  ts_flag      True if time_stamp is valid
 * @param[in]  time_stamp   ISO packet timestamp
 * @param[in]  pkt_seq_num  ISO packet sequence number
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_hci_iso_tx(uint16_t conn_handle, const uint8_t *sdu, uint16_t sdu_len,
                                   bool ts_flag, uint32_t time_stamp, uint16_t pkt_seq_num);

/**
 * @brief  Register the active host event callback.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the host operation is not registered
 *       - Other                  Host adapter error code
 */
esp_err_t bt_audio_host_register_event_cb(void);

/**
 * @brief  Forward a raw host GAP event to the Bluetooth audio middleware.
 *
 * @param[in]  gap_event_type  Host adapter GAP event type
 * @param[in]  event           Raw host event pointer
 */
void bt_audio_host_post_gap_event(uint8_t gap_event_type, void *event);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
