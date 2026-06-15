/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <inttypes.h>
#include <string.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_bit_defs.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lc3_dec.h"
#include "esp_lc3_enc.h"
#include "esp_log.h"

#include "esp_ble_audio_codec_api.h"
#include "esp_ble_audio_defs.h"
#include "esp_ble_audio_bap_api.h"
#include "esp_ble_iso_common_api.h"

#include "bt_audio_evt_dispatcher.h"
#include "bt_audio_le_stream.h"

#define BT_AUDIO_LE_STREAM_QUEUE_SIZE  20

static const char *TAG = "BT_AUD_LE_STREAM";

static inline void bt_audio_le_stream_release_packet(esp_bt_audio_stream_packet_t *packet)
{
    if (packet->data_owner) {
        heap_caps_free(packet->data_owner);
        packet->data_owner = NULL;
    }
    packet->data = NULL;
    packet->size = 0;
    packet->bad_frame = false;
    packet->is_done = false;
}

static inline bool bt_audio_le_stream_send_packet(QueueHandle_t queue, esp_bt_audio_stream_packet_t *packet)
{
    esp_bt_audio_stream_packet_t old_packet = {0};
    if (uxQueueSpacesAvailable(queue) == 0 && xQueueReceive(queue, &old_packet, 0) == pdTRUE) {
        bt_audio_le_stream_release_packet(&old_packet);
    }
    if (xQueueSend(queue, packet, 0) != pdTRUE) {
        bt_audio_le_stream_release_packet(packet);
        return false;
    }
    return true;
}

static inline void bt_audio_le_stream_flush_queue(bt_audio_le_stream_t *stream)
{
    if (!stream || !stream->base.data_q) {
        return;
    }

    esp_bt_audio_stream_packet_t packet = {0};
    uint32_t flushed = 0;
    while (xQueueReceive(stream->base.data_q, &packet, 0) == pdTRUE) {
        bt_audio_le_stream_release_packet(&packet);
        flushed++;
    }
    if (flushed) {
        ESP_LOGD(TAG, "Flushed %" PRIu32 " stale LE stream packets", flushed);
    }
}

#if CONFIG_SOC_MODEM_SUPPORT_ETM
static void bt_audio_le_stream_tx_sync_trigger(bt_audio_le_stream_t *stream, uint32_t time_stamp, uint32_t pd)
{
    extern int r_ble_ll_iso_i2s_start_tx(uint32_t time);
    extern int r_ble_ll_iso_i2s_tx_sync(uint32_t time);
    extern uint32_t r_ble_ll_iso_i2s_last_tx_sync_get(void);
    static uint32_t last_tx_sync = 0;
    int rc = 0;

    if (pd == 0) {
        return;
    }

    if (stream->first_packet) {
        ESP_LOGD(TAG, "TX sync trigger: time_stamp %" PRIu32 ", pd %" PRIu32, time_stamp, pd);
        rc = r_ble_ll_iso_i2s_start_tx(time_stamp + pd);
        if (rc != 0) {
            ESP_LOGE(TAG, "Failed to start TX sync: %d, time_stamp %" PRIu32 ", pd %" PRIu32, rc, time_stamp, pd);
        } else {
            stream->first_packet = false;
        }
    }
    uint32_t prev_tx_sync = r_ble_ll_iso_i2s_last_tx_sync_get();
    if (prev_tx_sync != last_tx_sync) {
        ESP_LOGD(TAG, "[0] TX sync sync: %d, prev_tx_sync %" PRIu32 ", last_tx_sync %" PRIu32
                 ", time_stamp %" PRIu32 ", pd %" PRIu32,
                 rc, prev_tx_sync, last_tx_sync, time_stamp, pd);
        return;
    }
    uint32_t tx_sync = last_tx_sync;
    last_tx_sync = time_stamp;
    while (1) {
        rc = r_ble_ll_iso_i2s_tx_sync(last_tx_sync);
        if (rc == 0) {
            break;
        }
        ESP_LOGD(TAG, "[1] TX sync sync: %d, prev_tx_sync %" PRIu32 ", last_tx_sync %" PRIu32
                 ", time_stamp %" PRIu32 ", pd %" PRIu32,
                 rc, prev_tx_sync, tx_sync, time_stamp, pd);
        last_tx_sync += pd;
    }
}
#endif  /* CONFIG_SOC_MODEM_SUPPORT_ETM */

static esp_err_t bt_audio_le_stream_acquire_read(esp_bt_audio_stream_handle_t handle,
                                                 esp_bt_audio_stream_packet_t *packet,
                                                 uint32_t wait_ms)
{
    bt_audio_le_stream_t *stream = (bt_audio_le_stream_t *)handle;
    ESP_RETURN_ON_FALSE(stream && packet, ESP_ERR_INVALID_ARG, TAG, "Invalid read args");
    ESP_RETURN_ON_FALSE(stream->base.direction == ESP_BT_AUDIO_STREAM_DIR_SINK, ESP_ERR_INVALID_ARG, TAG,
                        "Invalid read direction");
    ESP_RETURN_ON_FALSE(stream->base.data_q, ESP_ERR_INVALID_STATE, TAG, "Stream queue not initialized");

    if (xQueueReceive(stream->base.data_q, packet, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
        /* Timeout is an expected polling result on this hot path; keep it quiet. */
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static esp_err_t bt_audio_le_stream_release_read(esp_bt_audio_stream_handle_t handle,
                                                 esp_bt_audio_stream_packet_t *packet)
{
    bt_audio_le_stream_t *stream = (bt_audio_le_stream_t *)handle;
    ESP_RETURN_ON_FALSE(stream && packet, ESP_ERR_INVALID_ARG, TAG, "Invalid release read args");
    ESP_RETURN_ON_FALSE(stream->base.direction == ESP_BT_AUDIO_STREAM_DIR_SINK, ESP_ERR_INVALID_ARG, TAG,
                        "Invalid read direction");

    bt_audio_le_stream_release_packet(packet);
    return ESP_OK;
}

static esp_err_t bt_audio_le_stream_acquire_write(esp_bt_audio_stream_handle_t handle,
                                                  esp_bt_audio_stream_packet_t *packet,
                                                  uint32_t wanted_size)
{
    bt_audio_le_stream_t *stream = (bt_audio_le_stream_t *)handle;
    ESP_RETURN_ON_FALSE(stream && packet && wanted_size, ESP_ERR_INVALID_ARG, TAG, "Invalid write args");
    ESP_RETURN_ON_FALSE(stream->base.direction == ESP_BT_AUDIO_STREAM_DIR_SOURCE, ESP_ERR_INVALID_ARG, TAG,
                        "Invalid write direction");

    uint8_t *buffer = heap_caps_malloc_prefer(wanted_size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(buffer, ESP_ERR_NO_MEM, TAG, "No memory for write packet");

    packet->data = buffer;
    packet->size = wanted_size;
    packet->bad_frame = false;
    packet->is_done = false;
    packet->data_owner = buffer;
    return ESP_OK;
}

static esp_err_t bt_audio_le_stream_release_write(esp_bt_audio_stream_handle_t handle,
                                                  esp_bt_audio_stream_packet_t *packet,
                                                  uint32_t wait_ms)
{
    extern uint16_t r_ble_ll_iso_free_buf_num_get(uint16_t conn_handle);
    extern int ble_hs_hci_iso_tx(uint16_t conn_handle,
                                 const uint8_t *sdu, uint16_t sdu_len,
                                 bool ts_flag, uint32_t time_stamp,
                                 uint16_t pkt_seq_num);

    bt_audio_le_stream_t *stream = (bt_audio_le_stream_t *)handle;
    ESP_RETURN_ON_FALSE(stream && packet && packet->data, ESP_ERR_INVALID_ARG, TAG, "Invalid release write args");
    ESP_RETURN_ON_FALSE(stream->base.direction == ESP_BT_AUDIO_STREAM_DIR_SOURCE, ESP_ERR_INVALID_ARG, TAG,
                        "Invalid write direction");
    if (!stream->started || !stream->bap_stream.iso || !stream->bap_stream.iso->iso) {
        bt_audio_le_stream_release_packet(packet);
        ESP_LOGW(TAG, "Release write: stream is not started");
        return ESP_OK;
    }

    uint16_t iso_handle = stream->bap_stream.iso->iso->handle;
    uint32_t free_num = r_ble_ll_iso_free_buf_num_get(iso_handle);
    uint8_t retry = 0;
    while (free_num == 0 && retry < 5) {
        if (!stream->started || !stream->bap_stream.iso || !stream->bap_stream.iso->iso) {
            bt_audio_le_stream_release_packet(packet);
            ESP_LOGW(TAG, "Release write: stream stopped while waiting for ISO buffer");
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        free_num = r_ble_ll_iso_free_buf_num_get(iso_handle);
        ESP_LOGI(TAG, "ISO no buffer");
        retry++;
    }
    if (free_num == 0) {
        bt_audio_le_stream_release_packet(packet);
        ESP_LOGW(TAG, "Release write: timed out waiting for ISO buffer");
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = ble_hs_hci_iso_tx(iso_handle, packet->data, packet->size, false, 0, stream->seq++);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send ISO packet: %s", esp_err_to_name(ret));
    }
    bt_audio_le_stream_release_packet(packet);
    return ESP_OK;
}

static void bt_audio_le_stream_fill_codec_info(bt_audio_le_stream_t *stream,
                                               const esp_ble_audio_bap_stream_t *bap_stream)
{
    esp_ble_audio_codec_cfg_freq_t freq_type = 0;
    esp_ble_audio_codec_cfg_frame_dur_t frame_dur_type = 0;
    uint32_t freq = 0;
    uint32_t frame_dur = 0;
    uint8_t blocks = 1;
    uint16_t octets = 0;
    esp_ble_audio_context_t context = ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA;
    esp_ble_audio_location_t locations = 0;

    if (esp_ble_audio_codec_cfg_get_freq(bap_stream->codec_cfg, &freq_type) == ESP_OK) {
        esp_ble_audio_codec_cfg_freq_to_freq_hz(freq_type, &freq);
    }
    if (esp_ble_audio_codec_cfg_get_frame_dur(bap_stream->codec_cfg, &frame_dur_type) == ESP_OK) {
        esp_ble_audio_codec_cfg_frame_dur_to_frame_dur_us(frame_dur_type, &frame_dur);
    }
    esp_ble_audio_codec_cfg_get_frame_blocks_per_sdu(bap_stream->codec_cfg, &blocks, true);
    esp_ble_audio_codec_cfg_get_octets_per_frame(bap_stream->codec_cfg, &octets);
    esp_ble_audio_codec_cfg_meta_get_stream_context(bap_stream->codec_cfg, &context);
    esp_ble_audio_codec_cfg_get_chan_allocation(bap_stream->codec_cfg, &locations, true);

    stream->base.context = (context == ESP_BLE_AUDIO_CONTEXT_TYPE_CONVERSATIONAL) ? ESP_BT_AUDIO_STREAM_CONTEXT_CONVERSATIONAL : ESP_BT_AUDIO_STREAM_CONTEXT_MEDIA;
    stream->base.codec_info.codec_type = ESP_BT_AUDIO_STREAM_CODEC_LC3;
    stream->base.codec_info.sample_rate = freq > 0 ? freq : 0;
    stream->base.codec_info.channels = locations ? __builtin_popcount((unsigned int)locations) : 1;
    stream->base.codec_info.bits = 16;
    stream->base.codec_info.frame_size = (octets > 0 && blocks > 0) ? (uint32_t)(octets * blocks) : 0;

    if (stream->base.direction == ESP_BT_AUDIO_STREAM_DIR_SINK) {
        esp_lc3_dec_cfg_t *dec_cfg = heap_caps_calloc_prefer(1, sizeof(esp_lc3_dec_cfg_t), 2,
                                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                                             MALLOC_CAP_DEFAULT);
        if (!dec_cfg) {
            ESP_LOGW(TAG, "No memory for LC3 decoder config");
            return;
        }
        dec_cfg->nbyte = stream->base.codec_info.frame_size;
        dec_cfg->sample_rate = stream->base.codec_info.sample_rate;
        dec_cfg->channel = stream->base.codec_info.channels;
        dec_cfg->bits_per_sample = 16;
        dec_cfg->frame_dms = (uint8_t)(frame_dur / 100);
        dec_cfg->len_prefixed = false;
        dec_cfg->is_cbr = true;
        dec_cfg->enable_plc = true;
        stream->base.codec_info.codec_cfg = dec_cfg;
        stream->base.codec_info.cfg_size = sizeof(*dec_cfg);
    } else if (stream->base.direction == ESP_BT_AUDIO_STREAM_DIR_SOURCE) {
        esp_lc3_enc_config_t *enc_cfg = heap_caps_calloc_prefer(1, sizeof(esp_lc3_enc_config_t), 2,
                                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                                                MALLOC_CAP_DEFAULT);
        if (!enc_cfg) {
            ESP_LOGW(TAG, "No memory for LC3 encoder config");
            return;
        }
        enc_cfg->nbyte = stream->base.codec_info.frame_size;
        enc_cfg->sample_rate = stream->base.codec_info.sample_rate;
        enc_cfg->channel = stream->base.codec_info.channels;
        enc_cfg->bits_per_sample = 16;
        enc_cfg->frame_dms = (uint8_t)(frame_dur / 100);
        enc_cfg->len_prefixed = false;
        stream->base.codec_info.codec_cfg = enc_cfg;
        stream->base.codec_info.cfg_size = sizeof(*enc_cfg);
    }
}

static void bt_audio_le_stream_enabled(esp_ble_audio_bap_stream_t *bap_stream)
{
    bt_audio_le_stream_t *stream = NULL;
    if (bt_audio_le_stream_find_by_bap_stream(bap_stream, &stream) != ESP_OK) {
        return;
    }

    bt_audio_le_stream_dispatch_allocated(stream);

    if (stream->base.direction == ESP_BT_AUDIO_STREAM_DIR_SINK) {
        esp_err_t err = esp_ble_audio_bap_stream_start(bap_stream);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start sink stream: %s", esp_err_to_name(err));
        }
    }
}

static void bt_audio_le_stream_disabled(esp_ble_audio_bap_stream_t *bap_stream)
{
    bt_audio_le_stream_t *stream = NULL;
    bt_audio_le_stream_find_by_bap_stream(bap_stream, &stream);
    if (stream) {
        stream->started = false;
        bt_audio_le_stream_dispatch_state(stream, ESP_BT_AUDIO_STREAM_STATE_STOPPED);
    }
}

static void bt_audio_le_stream_started(esp_ble_audio_bap_stream_t *bap_stream)
{
    bt_audio_le_stream_t *stream = NULL;
    bt_audio_le_stream_find_by_bap_stream(bap_stream, &stream);
    if (stream) {
        stream->iso_interval = 0;
        if (stream->bap_stream.iso) {
            esp_ble_iso_info_t info = {0};
            if (esp_ble_iso_chan_get_info(stream->bap_stream.iso, &info) == ESP_OK) {
                stream->iso_interval = info.iso_interval * 1250U;
                ESP_LOGI(TAG, "ISO interval: raw=%u, us=%" PRIu32, info.iso_interval, stream->iso_interval);
            } else {
                ESP_LOGW(TAG, "Failed to get ISO channel info");
            }
        }
        bt_audio_le_stream_flush_queue(stream);
        stream->first_packet = true;
        stream->started = true;
        bt_audio_le_stream_dispatch_state(stream, ESP_BT_AUDIO_STREAM_STATE_STARTED);
    }
}

static void bt_audio_le_stream_stopped(esp_ble_audio_bap_stream_t *bap_stream, uint8_t reason)
{
    (void)reason;
    bt_audio_le_stream_t *stream = NULL;
    bt_audio_le_stream_find_by_bap_stream(bap_stream, &stream);
    if (stream) {
        stream->started = false;
        stream->iso_interval = 0;
    }

    if (!stream || !stream->base.data_q) {
        return;
    }

    esp_bt_audio_stream_packet_t packet = {
        .is_done = true,
    };
    if (!bt_audio_le_stream_send_packet(stream->base.data_q, &packet)) {
        ESP_LOGW(TAG, "LE stream queue full");
    }

    bt_audio_le_stream_dispatch_state(stream, ESP_BT_AUDIO_STREAM_STATE_STOPPED);
}

static void bt_audio_le_stream_released(esp_ble_audio_bap_stream_t *bap_stream)
{
    bt_audio_le_stream_t *stream = NULL;
    bt_audio_le_stream_find_by_bap_stream(bap_stream, &stream);
    if (stream) {
        stream->started = false;
        stream->iso_interval = 0;
        bt_audio_le_stream_dispatch_state(stream, ESP_BT_AUDIO_STREAM_STATE_RELEASED);
    }
}

static void bt_audio_le_stream_qos_set(esp_ble_audio_bap_stream_t *bap_stream)
{
    bt_audio_le_stream_t *stream = NULL;
    bt_audio_le_stream_find_by_bap_stream(bap_stream, &stream);
    ESP_LOGD(TAG, "QoS set");
    if (stream && bap_stream->qos) {
        stream->presentation_delay = bap_stream->qos->pd;
    }
}

static void bt_audio_le_stream_recv(esp_ble_audio_bap_stream_t *bap_stream,
                                    const esp_ble_iso_recv_info_t *info,
                                    const uint8_t *data,
                                    uint16_t len)
{
    bt_audio_le_stream_t *stream = NULL;
    bt_audio_le_stream_find_by_bap_stream(bap_stream, &stream);
    if (!stream || len == 0) {
        ESP_LOGD(TAG, "Invalid LE stream or length is 0, stream %p, len %u", stream, len);
        return;
    }

#if CONFIG_SOC_MODEM_SUPPORT_ETM
    if (info) {
        bt_audio_le_stream_tx_sync_trigger(stream, info->ts, stream->presentation_delay);
    }
#endif  /* CONFIG_SOC_MODEM_SUPPORT_ETM */

    esp_bt_audio_stream_packet_t packet = {0};
    packet.data = heap_caps_malloc_prefer(len, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    if (!packet.data) {
        ESP_LOGW(TAG, "No memory for received ISO packet");
        return;
    }
    memcpy(packet.data, data, len);
    packet.size = len;
    packet.bad_frame = (info && !(info->flags & BT_ISO_FLAGS_VALID));
    packet.is_done = false;
    packet.data_owner = packet.data;
    if (packet.bad_frame) {
        ESP_LOGW(TAG, "RX bad frame");
    }
    if (!bt_audio_le_stream_send_packet(stream->base.data_q, &packet)) {
        ESP_LOGW(TAG, "LE stream queue full");
    }
}

static esp_ble_audio_bap_stream_ops_t s_bt_audio_le_stream_ops = {
    .enabled  = bt_audio_le_stream_enabled,
    .qos_set  = bt_audio_le_stream_qos_set,
    .disabled = bt_audio_le_stream_disabled,
    .released = bt_audio_le_stream_released,
    .started  = bt_audio_le_stream_started,
    .stopped  = bt_audio_le_stream_stopped,
    .recv     = bt_audio_le_stream_recv,
};

void bt_audio_le_stream_dispatch_state(bt_audio_le_stream_t *stream, esp_bt_audio_stream_state_t state)
{
    if (!stream) {
        return;
    }

    esp_bt_audio_event_stream_st_t event = {
        .stream_handle = stream,
        .state = state,
    };

    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event);
}

void bt_audio_le_stream_dispatch_allocated(bt_audio_le_stream_t *stream)
{
    if (!stream) {
        return;
    }
    bt_audio_le_stream_flush_queue(stream);
    if (stream->bap_stream.codec_cfg) {
        bt_audio_le_stream_fill_codec_info(stream, &stream->bap_stream);
    }
    bt_audio_le_stream_dispatch_state(stream, ESP_BT_AUDIO_STREAM_STATE_ALLOCATED);
}

esp_err_t bt_audio_le_stream_create(bt_audio_le_stream_t **out_stream)
{
    ESP_RETURN_ON_FALSE(out_stream, ESP_ERR_INVALID_ARG, TAG, "out_stream is NULL");
    *out_stream = NULL;

    bt_audio_le_stream_t *stream = heap_caps_calloc_prefer(1, sizeof(bt_audio_le_stream_t), 2,
                                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                                           MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(stream, ESP_ERR_NO_MEM, TAG, "No memory for stream");

    stream->base.profile = ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN;
    stream->base.data_q = xQueueCreate(BT_AUDIO_LE_STREAM_QUEUE_SIZE, sizeof(esp_bt_audio_stream_packet_t));
    if (!stream->base.data_q) {
        ESP_LOGE(TAG, "Create stream failed: queue allocation failed");
        heap_caps_free(stream);
        stream = NULL;
        return ESP_ERR_NO_MEM;
    }

    stream->base.ops.acquire_read = bt_audio_le_stream_acquire_read;
    stream->base.ops.release_read = bt_audio_le_stream_release_read;
    stream->base.ops.acquire_write = bt_audio_le_stream_acquire_write;
    stream->base.ops.release_write = bt_audio_le_stream_release_write;
    stream->seq = 1;
    stream->first_packet = true;
    stream->started = false;

    esp_err_t ret = esp_ble_audio_bap_stream_cb_register(&stream->bap_stream, &s_bt_audio_le_stream_ops);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register stream callbacks");
        vQueueDelete(stream->base.data_q);
        heap_caps_free(stream);
        return ret;
    }

    *out_stream = stream;
    return ESP_OK;
}

void bt_audio_le_stream_destroy(bt_audio_le_stream_t *stream)
{
    if (!stream) {
        return;
    }

    if (stream->base.data_q) {
        esp_bt_audio_stream_packet_t packet = {0};
        while (xQueueReceive(stream->base.data_q, &packet, 0) == pdTRUE) {
            heap_caps_free(packet.data_owner);
            packet.data_owner = NULL;
        }
        vQueueDelete(stream->base.data_q);
    }
    heap_caps_free(stream->base.codec_info.codec_cfg);
    stream->base.codec_info.codec_cfg = NULL;
    heap_caps_free(stream);
    stream = NULL;
}

esp_err_t bt_audio_le_stream_find_by_bap_stream(esp_ble_audio_bap_stream_t *bap_stream,
                                                bt_audio_le_stream_t **out_stream)
{
    ESP_RETURN_ON_FALSE(out_stream, ESP_ERR_INVALID_ARG, TAG, "Stream output is NULL");
    *out_stream = NULL;
    ESP_RETURN_ON_FALSE(bap_stream, ESP_ERR_INVALID_ARG, TAG, "BAP stream is NULL");
    *out_stream = __containerof(bap_stream, bt_audio_le_stream_t, bap_stream);
    return ESP_OK;
}

esp_err_t esp_bt_audio_stream_get_iso_interval(esp_bt_audio_stream_handle_t handle, uint16_t *iso_interval)
{
    bt_audio_le_stream_t *stream = (bt_audio_le_stream_t *)handle;
    ESP_RETURN_ON_FALSE(stream && iso_interval, ESP_ERR_INVALID_ARG, TAG,
                        "Invalid arguments: handle=%p, iso_interval=%p", stream, iso_interval);
    ESP_RETURN_ON_FALSE(stream->base.profile == ESP_BT_AUDIO_STREAM_PROFILE_LE_UNICAST ||
                            stream->base.profile == ESP_BT_AUDIO_STREAM_PROFILE_LE_BROADCAST,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid profile");
    ESP_RETURN_ON_FALSE(stream->base.data_q, ESP_ERR_INVALID_STATE, TAG, "Stream queue not initialized");
    *iso_interval = stream->iso_interval;
    return ESP_OK;
}
