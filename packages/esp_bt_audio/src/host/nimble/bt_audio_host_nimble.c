/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_hs_adv.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_bt_audio_host.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "bt_audio_host_ops.h"

#define BT_AUDIO_NIMBLE_HOST_STOP_TIMEOUT_MS 5000

typedef struct {
    SemaphoreHandle_t host_exit_sem;                                  /*!< Signals completion of the NimBLE host task */
} bt_audio_host_nimble_t;

extern uint16_t r_ble_ll_iso_free_buf_num_get(uint16_t conn_handle);
extern int ble_hs_hci_iso_tx(uint16_t conn_handle, const uint8_t *sdu, uint16_t sdu_len,
                             bool ts_flag, uint32_t time_stamp, uint16_t pkt_seq_num);

static const char *TAG = "BT_AUD_HOST_NIMBLE";
static bt_audio_host_nimble_t *s_host;

static int nimble_gap_cb(struct ble_gap_event *event, void *arg)
{
    if (!event) {
        return 0;
    }
    bt_audio_host_post_gap_event((uint8_t)event->type, event);
    return 0;
}

static esp_err_t nimble_ext_adv_configure(uint8_t handle, const bt_audio_ext_adv_params_t *params)
{
    struct ble_gap_ext_adv_params np = {0};

    np.connectable = params->connectable ? 1 : 0;
    np.scannable = params->scannable ? 1 : 0;
    np.legacy_pdu = params->legacy_pdu ? 1 : 0;
    np.own_addr_type = params->own_addr_type;
    np.primary_phy = params->primary_phy;
    np.secondary_phy = params->secondary_phy;
    np.tx_power = params->tx_power;
    np.sid = params->sid;
    np.itvl_min = params->itvl_min;
    np.itvl_max = params->itvl_max;

    int rc = ble_gap_ext_adv_configure(handle, &np, NULL, nimble_gap_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Configure ext adv failed: handle %u, error %d", handle, rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t nimble_ext_adv_set_data(uint8_t handle, const uint8_t *data, size_t len)
{
    struct os_mbuf *m = os_msys_get_pkthdr(len, 0);
    if (!m) {
        ESP_LOGE(TAG, "Set ext adv data failed: no memory for mbuf");
        return ESP_ERR_NO_MEM;
    }
    int rc = os_mbuf_append(m, data, len);
    if (rc) {
        ESP_LOGE(TAG, "Set ext adv data failed: append mbuf error %d", rc);
        os_mbuf_free_chain(m);
        return ESP_FAIL;
    }
    rc = ble_gap_ext_adv_set_data(handle, m);
    if (rc) {
        ESP_LOGE(TAG, "Set ext adv data failed: handle %u, error %d", handle, rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t nimble_ext_adv_start(uint8_t handle, int duration, int max_events)
{
    int rc = ble_gap_ext_adv_start(handle, duration, max_events);
    if (rc != 0) {
        ESP_LOGE(TAG, "Start ext adv failed: handle %u, error %d", handle, rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t nimble_ext_adv_stop(uint8_t handle)
{
    int rc = ble_gap_ext_adv_stop(handle);
    if (rc != 0) {
        ESP_LOGE(TAG, "Stop ext adv failed: handle %u, error %d", handle, rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t nimble_disc(uint8_t own_addr_type, const bt_audio_scan_params_t *params,
                             uint32_t timeout_ms)
{
    struct ble_gap_disc_params dp = {0};

    dp.passive = params->passive ? 1 : 0;
    dp.filter_duplicates = params->filter_duplicates ? 1 : 0;
    dp.itvl = params->itvl;
    dp.window = params->window;

    int rc = ble_gap_disc(own_addr_type, (int32_t)timeout_ms, &dp, nimble_gap_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Start discovery failed: %d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t nimble_disc_cancel(void)
{
    int rc = ble_gap_disc_cancel();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Cancel discovery failed: %d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t nimble_connect(uint8_t own_addr_type, const bt_audio_addr_t *peer,
                                const bt_audio_conn_params_t *params, uint32_t timeout_ms)
{
    ble_addr_t p = {0};
    struct ble_gap_conn_params cp = {0};

    p.type = peer->type;
    memcpy(p.val, peer->val, sizeof(p.val));

    cp.scan_itvl = params->scan_itvl;
    cp.scan_window = params->scan_window;
    cp.itvl_min = params->itvl_min;
    cp.itvl_max = params->itvl_max;
    cp.latency = params->latency;
    cp.supervision_timeout = params->supervision_timeout;

    int rc = ble_gap_connect(own_addr_type, &p, (int32_t)timeout_ms, &cp,
                             nimble_gap_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Connect failed: %d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t nimble_disconnect(uint16_t conn_handle, uint8_t reason)
{
    int rc = ble_gap_terminate(conn_handle, reason);
    if (rc != 0) {
        ESP_LOGE(TAG, "Disconnect failed: conn_handle %u, error %d", conn_handle, rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t nimble_conn_find(uint16_t conn_handle, bt_audio_conn_desc_t *desc)
{
    struct ble_gap_conn_desc d = {0};

    if (!desc) {
        return ESP_ERR_INVALID_ARG;
    }

    int rc = ble_gap_conn_find(conn_handle, &d);
    if (rc != 0) {
        ESP_LOGE(TAG, "Find connection failed: conn_handle %u, error %d", conn_handle, rc);
        return ESP_ERR_NOT_FOUND;
    }
    memcpy(desc->peer_id_addr, d.peer_id_addr.val, sizeof(desc->peer_id_addr));
    return ESP_OK;
}

static esp_err_t nimble_acl_connected(uint16_t conn_handle, const bt_audio_addr_t *peer)
{
    return ESP_OK;
}

static esp_err_t nimble_acl_disconnected(uint16_t conn_handle)
{
    return ESP_OK;
}

static esp_err_t nimble_security_initiate(uint16_t conn_handle)
{
    int rc = ble_gap_security_initiate(conn_handle);
    if (rc != 0) {
        ESP_LOGE(TAG, "Security initiate failed: conn_handle %u, error %d", conn_handle, rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t nimble_periodic_adv_configure(uint8_t adv_handle,
                                               const bt_audio_periodic_adv_params_t *params)
{
    struct ble_gap_periodic_adv_params pp = {0};

    pp.include_tx_power = params->include_tx_power ? 1 : 0;
    pp.itvl_min = params->itvl_min;
    pp.itvl_max = params->itvl_max;

    int rc = ble_gap_periodic_adv_configure(adv_handle, &pp);
    if (rc != 0) {
        ESP_LOGE(TAG, "Configure periodic adv failed: handle %u, error %d", adv_handle, rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t nimble_periodic_adv_set_data(uint8_t adv_handle, const uint8_t *data, size_t len)
{
    struct os_mbuf *m = os_msys_get_pkthdr(len, 0);
    if (!m) {
        ESP_LOGE(TAG, "Set periodic adv data failed: no memory for mbuf");
        return ESP_ERR_NO_MEM;
    }
    int rc = os_mbuf_append(m, data, len);
    if (rc) {
        ESP_LOGE(TAG, "Set periodic adv data failed: append mbuf error %d", rc);
        os_mbuf_free_chain(m);
        return ESP_FAIL;
    }
    rc = ble_gap_periodic_adv_set_data(adv_handle, m);
    if (rc) {
        ESP_LOGE(TAG, "Set periodic adv data failed: handle %u, error %d", adv_handle, rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t nimble_periodic_adv_start(uint8_t adv_handle)
{
    int rc = ble_gap_periodic_adv_start(adv_handle);
    if (rc != 0) {
        ESP_LOGE(TAG, "Start periodic adv failed: handle %u, error %d", adv_handle, rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t nimble_periodic_adv_stop(uint8_t adv_handle)
{
    int rc = ble_gap_periodic_adv_stop(adv_handle);
    if (rc != 0) {
        ESP_LOGE(TAG, "Stop periodic adv failed: handle %u, error %d", adv_handle, rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t nimble_pa_sync_create(const bt_audio_addr_t *addr, uint8_t sid,
                                       const bt_audio_periodic_sync_params_t *params)
{
    ble_addr_t a = {0};
    struct ble_gap_periodic_sync_params sp = {0};

    a.type = addr->type;
    memcpy(a.val, addr->val, sizeof(a.val));
    sp.skip = params->skip;
    sp.sync_timeout = params->sync_timeout;

    int rc = ble_gap_periodic_adv_sync_create(&a, sid, &sp, nimble_gap_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Create PA sync failed: %d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t nimble_pa_sync_terminate(uint16_t sync_handle)
{
    int rc = ble_gap_periodic_adv_sync_terminate(sync_handle);
    if (rc != 0) {
        ESP_LOGE(TAG, "Terminate PA sync failed: sync_handle %u, error %d", sync_handle, rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t nimble_pa_sync_create_cancel(void)
{
    int rc = ble_gap_periodic_adv_sync_create_cancel();
    if (rc != 0) {
        ESP_LOGE(TAG, "Cancel PA sync create failed: %d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t nimble_pa_sync_receive(uint16_t conn_handle,
                                        const bt_audio_periodic_sync_params_t *params)
{
    struct ble_gap_periodic_sync_params sp = {0};

    sp.skip = params->skip;
    sp.sync_timeout = params->sync_timeout;

    int rc = ble_gap_periodic_adv_sync_receive(conn_handle, &sp, nimble_gap_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Receive PA sync failed: conn_handle %u, error %d", conn_handle, rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t nimble_id_infer_auto(int privacy, uint8_t *out_addr_type)
{
    int rc = ble_hs_id_infer_auto(privacy, out_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Infer own address type failed: %d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static const char *nimble_svc_gap_device_name(void)
{
    return ble_svc_gap_device_name();
}

static uint16_t nimble_iso_free_buf_num_get(uint16_t conn_handle)
{
    return r_ble_ll_iso_free_buf_num_get(conn_handle);
}

static esp_err_t nimble_hci_iso_tx(uint16_t conn_handle, const uint8_t *sdu, uint16_t sdu_len,
                                   bool ts_flag, uint32_t time_stamp, uint16_t pkt_seq_num)
{
    int rc = ble_hs_hci_iso_tx(conn_handle, sdu, sdu_len, ts_flag, time_stamp, pkt_seq_num);
    if (rc != 0) {
        ESP_LOGE(TAG, "Send HCI ISO data failed: conn_handle %u, error %d", conn_handle, rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t nimble_register_event_cb(void)
{
    return ESP_OK;
}

static void nimble_post_gap_event(uint8_t gap_event_type, void *event)
{
    extern void esp_ble_audio_gap_app_post_event(uint8_t type, void *event);
    extern void esp_ble_audio_gatt_app_post_event(uint8_t type, void *event);

    switch (gap_event_type) {
        case BLE_GAP_EVENT_EXT_DISC:
        case BLE_GAP_EVENT_PERIODIC_SYNC:
        case BLE_GAP_EVENT_PERIODIC_REPORT:
        case BLE_GAP_EVENT_PERIODIC_SYNC_LOST:
        case BLE_GAP_EVENT_PERIODIC_TRANSFER:
        case BLE_GAP_EVENT_PERIODIC_TRANSFER_V2:
        case BLE_GAP_EVENT_CONNECT:
        case BLE_GAP_EVENT_DISCONNECT:
        case BLE_GAP_EVENT_ENC_CHANGE:
            esp_ble_audio_gap_app_post_event(gap_event_type, event);
            break;
        case BLE_GAP_EVENT_MTU:
        case BLE_GAP_EVENT_NOTIFY_RX:
        case BLE_GAP_EVENT_NOTIFY_TX:
        case BLE_GAP_EVENT_SUBSCRIBE:
            esp_ble_audio_gatt_app_post_event(gap_event_type, event);
            break;
        default:
            break;
    }
}

static esp_err_t bt_audio_host_nimble_init(void)
{
    static const bt_audio_host_ops_t ops = {
        .ext_adv_configure = nimble_ext_adv_configure,
        .ext_adv_set_data = nimble_ext_adv_set_data,
        .ext_adv_start = nimble_ext_adv_start,
        .ext_adv_stop = nimble_ext_adv_stop,
        .disc = nimble_disc,
        .disc_cancel = nimble_disc_cancel,
        .connect = nimble_connect,
        .disconnect = nimble_disconnect,
        .conn_find = nimble_conn_find,
        .acl_connected = nimble_acl_connected,
        .acl_disconnected = nimble_acl_disconnected,
        .security_initiate = nimble_security_initiate,
        .periodic_adv_configure = nimble_periodic_adv_configure,
        .periodic_adv_set_data = nimble_periodic_adv_set_data,
        .periodic_adv_start = nimble_periodic_adv_start,
        .periodic_adv_stop = nimble_periodic_adv_stop,
        .pa_sync_create = nimble_pa_sync_create,
        .pa_sync_terminate = nimble_pa_sync_terminate,
        .pa_sync_create_cancel = nimble_pa_sync_create_cancel,
        .pa_sync_receive = nimble_pa_sync_receive,
        .id_infer_auto = nimble_id_infer_auto,
        .svc_gap_device_name = nimble_svc_gap_device_name,
        .iso_free_buf_num_get = nimble_iso_free_buf_num_get,
        .hci_iso_tx = nimble_hci_iso_tx,
        .register_event_cb = nimble_register_event_cb,
        .post_gap_event = nimble_post_gap_event,
    };

    return bt_audio_ops_set_host(&ops);
}

extern void ble_store_config_init(void);

static void bt_audio_host_nimble_teardown(void)
{
    if (!s_host) {
        return;
    }
    if (s_host->host_exit_sem) {
        vSemaphoreDelete(s_host->host_exit_sem);
        s_host->host_exit_sem = NULL;
    }
    heap_caps_free(s_host);
    s_host = NULL;
}

static const char *bt_audio_nimble_uuid16_name(const ble_uuid_t *uuid)
{
    if (uuid->type != BLE_UUID_TYPE_16) {
        return NULL;
    }

    switch (ble_uuid_u16(uuid)) {
        /* GAP / GATT */
        case 0x1800:
            return "GAP";
        case 0x1801:
            return "GATT";
        case 0x2a00:
            return "Device Name";
        case 0x2a01:
            return "Appearance";
        case 0x2a05:
            return "Service Changed";
        case 0x2b29:
            return "Client Sup Features";
        case 0x2b3a:
            return "Server Sup Features";

        /* LE Audio services */
        case 0x1843:
            return "AICS";
        case 0x1844:
            return "VCS";
        case 0x1845:
            return "VOCS";
        case 0x1846:
            return "CSIS";
        case 0x1848:
            return "MCS";
        case 0x1849:
            return "GMCS";
        case 0x184b:
            return "TBS";
        case 0x184c:
            return "GTBS";
        case 0x184d:
            return "MICS";
        case 0x184e:
            return "ASCS";
        case 0x184f:
            return "BASS";
        case 0x1850:
            return "PACS";
        case 0x1851:
            return "Basic Audio Ann";
        case 0x1852:
            return "Broadcast Audio Ann";
        case 0x1853:
            return "CAS";
        case 0x1854:
            return "HAS";
        case 0x1855:
            return "TMAS";
        case 0x1856:
            return "Public Broadcast Ann";
        case 0x1858:
            return "GMAS";

        /* Common */
        case 0x2b51:
            return "TMAP Role";
        case 0x2bba:
            return "CCID";

        /* AICS chars */
        case 0x2b77:
            return "AICS State";
        case 0x2b78:
            return "AICS Gain Settings";
        case 0x2b79:
            return "AICS Input Type";
        case 0x2b7a:
            return "AICS Input Status";
        case 0x2b7b:
            return "AICS Control";
        case 0x2b7c:
            return "AICS Description";

        /* VCS chars */
        case 0x2b7d:
            return "VCS State";
        case 0x2b7e:
            return "VCS Control";
        case 0x2b7f:
            return "VCS Flags";

        /* VOCS chars */
        case 0x2b80:
            return "VOCS State";
        case 0x2b81:
            return "VOCS Location";
        case 0x2b82:
            return "VOCS Control";
        case 0x2b83:
            return "VOCS Description";

        /* CSIS chars */
        case 0x2b84:
            return "CSIS SIRK";
        case 0x2b85:
            return "CSIS Set Size";
        case 0x2b86:
            return "CSIS Set Lock";
        case 0x2b87:
            return "CSIS Rank";

        /* MCS chars */
        case 0x2b93:
            return "MCS Player Name";
        case 0x2b94:
            return "MCS Icon Obj ID";
        case 0x2b95:
            return "MCS Icon URL";
        case 0x2b96:
            return "MCS Track Changed";
        case 0x2b97:
            return "MCS Track Title";
        case 0x2b98:
            return "MCS Track Duration";
        case 0x2b99:
            return "MCS Track Position";
        case 0x2b9a:
            return "MCS Playback Speed";
        case 0x2b9b:
            return "MCS Seeking Speed";
        case 0x2b9c:
            return "MCS Track Seg Obj ID";
        case 0x2b9d:
            return "MCS Cur Track Obj ID";
        case 0x2b9e:
            return "MCS Next Track Obj ID";
        case 0x2b9f:
            return "MCS Parent Grp Obj ID";
        case 0x2ba0:
            return "MCS Cur Grp Obj ID";
        case 0x2ba1:
            return "MCS Playing Order";
        case 0x2ba2:
            return "MCS Playing Orders";
        case 0x2ba3:
            return "MCS Media State";
        case 0x2ba4:
            return "MCS Media CP";
        case 0x2ba5:
            return "MCS Media CP Opcodes";
        case 0x2ba6:
            return "MCS Search Res Obj ID";
        case 0x2ba7:
            return "MCS Search CP";

        /* TBS chars */
        case 0x2bb3:
            return "TBS Provider Name";
        case 0x2bb4:
            return "TBS UCI";
        case 0x2bb5:
            return "TBS Technology";
        case 0x2bb6:
            return "TBS URI List";
        case 0x2bb7:
            return "TBS Signal Strength";
        case 0x2bb8:
            return "TBS Signal Interval";
        case 0x2bb9:
            return "TBS List Cur Calls";
        case 0x2bbb:
            return "TBS Status Flags";
        case 0x2bbc:
            return "TBS Incoming URI";
        case 0x2bbd:
            return "TBS Call State";
        case 0x2bbe:
            return "TBS Call CP";
        case 0x2bbf:
            return "TBS Optional Opcodes";
        case 0x2bc0:
            return "TBS Term Reason";
        case 0x2bc1:
            return "TBS Incoming Call";
        case 0x2bc2:
            return "TBS Friendly Name";

        /* MICS / ASCS / BASS chars */
        case 0x2bc3:
            return "MICS Mute";
        case 0x2bc4:
            return "ASCS ASE Snk";
        case 0x2bc5:
            return "ASCS ASE Src";
        case 0x2bc6:
            return "ASCS ASE CP";
        case 0x2bc7:
            return "BASS CP";
        case 0x2bc8:
            return "BASS Recv State";

        /* PACS chars */
        case 0x2bc9:
            return "PACS Snk";
        case 0x2bca:
            return "PACS Snk Loc";
        case 0x2bcb:
            return "PACS Src";
        case 0x2bcc:
            return "PACS Src Loc";
        case 0x2bcd:
            return "PACS Avail Ctx";
        case 0x2bce:
            return "PACS Sup Ctx";

        default:
            return NULL;
    }
}

static void bt_audio_nimble_gatt_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char *buf = heap_caps_malloc_prefer(BLE_UUID_STR_LEN, 2,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                        MALLOC_CAP_DEFAULT);
    if (!buf) {
        ESP_LOGE(TAG, "No memory for UUID string buffer");
        return;
    }
    const char *name;

    switch (ctxt->op) {
        case BLE_GATT_REGISTER_OP_SVC:
            name = bt_audio_nimble_uuid16_name(ctxt->svc.svc_def->uuid);
            ESP_LOGI(TAG, "Register service %-20s (%s), handle %u",
                     name ? name : "?",
                     ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                     ctxt->svc.handle);
            break;

        case BLE_GATT_REGISTER_OP_CHR:
            name = bt_audio_nimble_uuid16_name(ctxt->chr.chr_def->uuid);
            ESP_LOGI(TAG, "Register characteristic %-20s (%s), handles %u/%u",
                     name ? name : "?",
                     ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                     ctxt->chr.def_handle, ctxt->chr.val_handle);
            break;

        case BLE_GATT_REGISTER_OP_DSC:
            name = bt_audio_nimble_uuid16_name(ctxt->dsc.dsc_def->uuid);
            ESP_LOGI(TAG, "Register descriptor %-20s (%s), handle %u",
                     name ? name : "?",
                     ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                     ctxt->dsc.handle);
            break;

        default:
            assert(0);
            break;
    }
    heap_caps_free(buf);
}

static void bt_audio_nimble_on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset: reason=%d", reason);
}

static void bt_audio_nimble_on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0) {
        ESP_LOGE(TAG, "NimBLE sync failed: ensure address failed");
    }
}

static void bt_audio_nimble_host_task(void *param)
{
    nimble_port_run();
    if (s_host && s_host->host_exit_sem) {
        xSemaphoreGive(s_host->host_exit_sem);
    }
    nimble_port_freertos_deinit();
}

esp_err_t bt_audio_host_nimble_stack_init(void *cfg)
{
    esp_bt_audio_host_nimble_cfg_t *host_cfg = (esp_bt_audio_host_nimble_cfg_t *)cfg;
    esp_err_t ret;

    ESP_RETURN_ON_FALSE(host_cfg != NULL, ESP_ERR_INVALID_ARG, TAG, "NimBLE host init failed: cfg is NULL");
    ESP_RETURN_ON_FALSE(s_host == NULL, ESP_ERR_INVALID_STATE, TAG,
                        "NimBLE host init failed: already initialized");

    ret = esp_nimble_init();
    ESP_RETURN_ON_ERROR(ret, TAG, "NimBLE host init failed: esp_nimble_init");

    s_host = heap_caps_calloc_prefer(1, sizeof(*s_host), 2,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    if (!s_host) {
        ESP_LOGE(TAG, "NimBLE host init failed: allocate host context");
        esp_nimble_deinit();
        return ESP_ERR_NO_MEM;
    }

    s_host->host_exit_sem = xSemaphoreCreateBinary();
    if (!s_host->host_exit_sem) {
        ESP_LOGE(TAG, "NimBLE host init failed: no memory for host exit semaphore");
        bt_audio_host_nimble_teardown();
        esp_nimble_deinit();
        return ESP_ERR_NO_MEM;
    }

    ble_hs_cfg.gatts_register_cb = bt_audio_nimble_gatt_register_cb;
    ble_hs_cfg.reset_cb = bt_audio_nimble_on_reset;
    ble_hs_cfg.sync_cb = bt_audio_nimble_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;

    ret = ble_svc_gap_device_name_set(host_cfg->dev_name);
    if (ret != 0) {
        ESP_LOGE(TAG, "NimBLE host init failed: set device name (%d)", ret);
        bt_audio_host_nimble_teardown();
        esp_nimble_deinit();
        return ESP_FAIL;
    }

    ble_store_config_init();
    nimble_port_freertos_init(bt_audio_nimble_host_task);
    ret = bt_audio_host_nimble_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE host init failed: set host ops");
        nimble_port_stop();
        if (xSemaphoreTake(s_host->host_exit_sem, pdMS_TO_TICKS(BT_AUDIO_NIMBLE_HOST_STOP_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGE(TAG, "NimBLE host init failed: host task stop timed out");
        }
        esp_nimble_deinit();
        bt_audio_host_nimble_teardown();
        return ret;
    }
    return ESP_OK;
}

void bt_audio_host_nimble_stack_deinit(void)
{
    if (!s_host) {
        return;
    }

    nimble_port_stop();
    if (!s_host->host_exit_sem) {
        ESP_LOGE(TAG, "NimBLE host deinit failed: host exit semaphore is NULL");
        return;
    }
    if (xSemaphoreTake(s_host->host_exit_sem, pdMS_TO_TICKS(BT_AUDIO_NIMBLE_HOST_STOP_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "NimBLE host deinit failed: host task stop timed out");
        return;
    }

    esp_nimble_deinit();
    bt_audio_host_nimble_teardown();
}
