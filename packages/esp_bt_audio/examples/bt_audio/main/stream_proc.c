/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#if CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM
#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#include "esp_board_periph.h"
#include "esp_codec_dev.h"
#include "dev_audio_codec.h"
#endif  /* CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM */

#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_media.h"
#include "esp_bt_audio_stream.h"
#if CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM
#include "esp_bt_audio_le_playback_sync.h"
#endif  /* CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM */
#include "esp_gmf_asrc.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_element.h"
#include "esp_gmf_err.h"
#include "esp_gmf_io_bt.h"
#include "esp_gmf_new_databus.h"
#include "esp_gmf_obj.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_port.h"
#include "esp_gmf_task.h"
#include "codec_defs.h"
#include "stream_proc.h"

#define STREAM_PROC_ASRC_MAX_CH          4
#define STREAM_PROC_ASRC_MAX_WEIGHT_LEN  (STREAM_PROC_ASRC_MAX_CH * STREAM_PROC_ASRC_MAX_CH)
#define STREAM_PROC_CMD_QUEUE_SIZE       16
#define STREAM_PROC_CMD_WAIT_TICKS       pdMS_TO_TICKS(200)
#define STREAM_PROC_TASK_STACK_SIZE      4096
#define STREAM_PROC_TASK_PRIO            20
#define STREAM_PROC_TASK_CORE_ID         1

#if defined(CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_BMS) && (CONFIG_GMF_EXAMPLE_LE_BSRC_STREAM_NUM > 1)
#define STREAM_PROC_DUAL_BIS             1
#define STREAM_PROC_DUAL_BIS_NUM         CONFIG_GMF_EXAMPLE_LE_BSRC_STREAM_NUM
#define STREAM_PROC_DUAL_BIS_RB_BLOCKS   4
#define STREAM_PROC_DUAL_BIS_RB_SIZE     8192
#define STREAM_PROC_DUAL_BIS_PORT_SIZE   4096
#define STREAM_PROC_DUAL_BIS_WEIGHT_LEN  2
#define STREAM_PROC_DUAL_BIS_SRC_CH      2
#define STREAM_PROC_DUAL_BIS_SRC_BITS    16
#else
#define STREAM_PROC_DUAL_BIS             0
#endif  /* CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_BMS && CONFIG_GMF_EXAMPLE_LE_BSRC_STREAM_NUM > 1 */

typedef enum {
    STREAM_PROC_PIPELINE_PREPARE,
    STREAM_PROC_PIPELINE_RUN,
    STREAM_PROC_PIPELINE_STOP_RESET,
    STREAM_PROC_PIPELINE_PLAY_NEXT,
#if STREAM_PROC_DUAL_BIS
    STREAM_PROC_BIS_LEG_CONFIG,
#endif  /* STREAM_PROC_DUAL_BIS */
} stream_proc_pipeline_action_t;

typedef struct {
    stream_proc_pipeline_action_t  action;           /*!< Command action */
    esp_gmf_pipeline_handle_t      pipe;             /*!< Target pipeline */
    const char                    *uri;              /*!< Input URI, or NULL */
    bool                           report_src_info;  /*!< Report source sound information */
    esp_gmf_info_sound_t           src_info;         /*!< Source sound information */
#if STREAM_PROC_DUAL_BIS
    int                            leg;              /*!< Dual BIS leg index */
#endif  /* STREAM_PROC_DUAL_BIS */
} stream_proc_cmd_t;

static const char *TAG = "STREAM_PROC";

static const char *gmf_state_to_str(int state);

/* Playlist configuration */
static const char *playlist[] = {
    "file://sdcard/media0.mp3",
    "file://sdcard/media1.mp3",
    "file://sdcard/media2.mp3",
};
static const size_t playlist_len = sizeof(playlist) / sizeof(playlist[0]);

#if STREAM_PROC_DUAL_BIS
/**
 * @brief  State for one leg of the dual BIS broadcast topology.
 */
typedef struct {
    esp_gmf_pipeline_handle_t     pipe;                                                      /*!< Encoding pipeline */
    esp_gmf_task_handle_t         task;                                                      /*!< Pipeline task */
    esp_gmf_db_handle_t           rb;                                                        /*!< Copier ring buffer */
    esp_bt_audio_stream_handle_t  stream;                                                    /*!< Bluetooth stream */
    esp_gmf_info_sound_t          src_info;                                                  /*!< Source sound info */
    float                         weight[STREAM_PROC_DUAL_BIS_WEIGHT_LEN];                   /*!< ASRC weights */
    bool                          ready;                                                     /*!< Stream assigned and config queued */
    bool                          configured;                                                /*!< Leg bind/ASRC/encoder config succeeded */
    bool                          started;                                                   /*!< Stream started */
    bool                          running;                                                   /*!< Pipeline running */
} stream_proc_bis_leg_t;
#endif  /* STREAM_PROC_DUAL_BIS */

typedef struct {
    size_t                        playlist_cur_index;
    esp_gmf_task_handle_t         bt2codec_task;
    esp_gmf_pipeline_handle_t     bt2codec_pipe;
    esp_gmf_task_handle_t         codec2bt_task;
    esp_gmf_pipeline_handle_t     codec2bt_pipe;
    esp_gmf_task_handle_t         local2bt_task;
    esp_gmf_pipeline_handle_t     local2bt_pipe;
    esp_bt_audio_stream_handle_t  local2bt_stream;
    QueueHandle_t                 cmd_queue;
    float                         bt2codec_asrc_weight[STREAM_PROC_ASRC_MAX_WEIGHT_LEN];
    float                         codec2bt_input_asrc_weight[STREAM_PROC_ASRC_MAX_WEIGHT_LEN];
    float                         codec2bt_output_asrc_weight[STREAM_PROC_ASRC_MAX_WEIGHT_LEN];
    float                         local2bt_asrc_weight[STREAM_PROC_ASRC_MAX_WEIGHT_LEN];
#if STREAM_PROC_DUAL_BIS
    esp_gmf_pipeline_handle_t     local2bt_head_pipe;
    esp_gmf_task_handle_t         local2bt_head_task;
    stream_proc_bis_leg_t         local2bt_bis_legs[STREAM_PROC_DUAL_BIS_NUM];
    uint8_t                       local2bt_bis_ready;
    uint8_t                       local2bt_bis_configured;
    uint8_t                       local2bt_bis_started;
    bool                          local2bt_head_running;
#endif  /* STREAM_PROC_DUAL_BIS */
#if CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM
    esp_bt_audio_le_playback_sync_handle_t playback_sync;
    esp_bt_audio_le_clk_sync_handle_t      clk_sync;
    QueueHandle_t                          clk_sync_monitor_queue;
    TaskHandle_t                           clk_sync_monitor_task;
    volatile bool                          clk_sync_monitor_task_running;
#endif  /* CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM */
} stream_proc_ctx_t;

static stream_proc_ctx_t *s_stream_proc;

static inline esp_audio_type_t stream_proc_get_audio_type(esp_bt_audio_stream_codec_type_t codec_type)
{
    switch (codec_type) {
        case ESP_BT_AUDIO_STREAM_CODEC_SBC:
            return ESP_AUDIO_TYPE_SBC;
        case ESP_BT_AUDIO_STREAM_CODEC_AAC:
            return ESP_AUDIO_TYPE_AAC;
        case ESP_BT_AUDIO_STREAM_CODEC_LC3:
            return ESP_AUDIO_TYPE_LC3;
        default:
            return ESP_AUDIO_TYPE_UNSUPPORT;
    }
}

static inline bool stream_proc_post_cmd(const stream_proc_cmd_t *cmd, TickType_t wait)
{
    if (s_stream_proc == NULL || s_stream_proc->cmd_queue == NULL) {
        ESP_LOGE(TAG, "Stream processor task is not initialized");
        return false;
    }
    if (xQueueSend(s_stream_proc->cmd_queue, cmd, wait) != pdTRUE) {
        ESP_LOGW(TAG, "Stream processor command queue full, drop action %d", cmd->action);
        return false;
    }
    return true;
}

static inline bool stream_proc_post_pipeline_action(esp_gmf_pipeline_handle_t pipe,
                                                    stream_proc_pipeline_action_t action,
                                                    TickType_t wait)
{
    stream_proc_cmd_t cmd = {
        .action = action,
        .pipe = pipe,
    };
    return stream_proc_post_cmd(&cmd, wait);
}

static esp_gmf_element_handle_t stream_proc_get_asrc(esp_gmf_pipeline_handle_t pipe, uint8_t index)
{
    const void *iterator = NULL;
    esp_gmf_element_handle_t cur_el = NULL;
    uint8_t asrc_count = 0;
    while (esp_gmf_pipeline_iterate_element(pipe, &iterator, &cur_el) == ESP_GMF_ERR_OK) {
        char *el_tag = NULL;
        esp_gmf_obj_get_tag((esp_gmf_obj_handle_t)cur_el, &el_tag);
        if (el_tag && strcasecmp(el_tag, "aud_asrc") == 0) {
            if (asrc_count == index) {
                return cur_el;
            }
            asrc_count++;
        }
    }
    ESP_LOGE(TAG, "No aud_asrc[%d] found in pipeline %p", index, pipe);
    return NULL;
}

static void stream_proc_fill_asrc_weight(float *weight, uint32_t weight_cap, uint8_t src_ch, uint8_t dest_ch)
{
    uint32_t weight_len = src_ch * dest_ch;
    if (weight == NULL || weight_len > weight_cap) {
        return;
    }
    memset(weight, 0, weight_len * sizeof(weight[0]));
    for (uint8_t dest = 0; dest < dest_ch; dest++) {
        if (src_ch == dest_ch) {
            weight[dest * src_ch + dest] = 1.0f;
        } else if (src_ch == 1) {
            weight[dest] = 1.0f;
        } else if (dest_ch == 1) {
            weight[dest * src_ch] = 1.0f / src_ch;
            for (uint8_t src = 1; src < src_ch; src++) {
                weight[dest * src_ch + src] = 1.0f / src_ch;
            }
        } else {
            weight[dest * src_ch + (dest < src_ch ? dest : src_ch - 1)] = 1.0f;
        }
    }
}

static void stream_proc_set_asrc_dest(esp_gmf_pipeline_handle_t pipe, uint8_t index, uint32_t sample_rate,
                                      uint8_t src_ch, uint8_t dest_ch, float *weight, uint32_t weight_cap)
{
    esp_gmf_element_handle_t asrc = stream_proc_get_asrc(pipe, index);
    if (asrc == NULL) {
        return;
    }
    if (src_ch == 0 || dest_ch == 0 || src_ch > STREAM_PROC_ASRC_MAX_CH || dest_ch > STREAM_PROC_ASRC_MAX_CH) {
        ESP_LOGE(TAG, "Invalid ASRC channel config, src: %d, dest: %d", src_ch, dest_ch);
        return;
    }
    esp_gmf_asrc_set_dest_rate(asrc, sample_rate);
    esp_gmf_asrc_set_dest_ch(asrc, dest_ch);
    stream_proc_fill_asrc_weight(weight, weight_cap, src_ch, dest_ch);
    esp_asrc_cfg_t *cfg = (esp_asrc_cfg_t *)OBJ_GET_CFG(asrc);
    if (cfg) {
        cfg->weight = weight;
        cfg->weight_len = src_ch * dest_ch;
    }
}

#if STREAM_PROC_DUAL_BIS
static stream_proc_bis_leg_t *stream_proc_find_bis_leg(esp_bt_audio_stream_handle_t stream)
{
    for (int i = 0; i < STREAM_PROC_DUAL_BIS_NUM; i++) {
        if (s_stream_proc->local2bt_bis_legs[i].stream == stream) {
            return &s_stream_proc->local2bt_bis_legs[i];
        }
    }
    return NULL;
}

static void stream_proc_reset_bis_leg_rb(esp_gmf_pipeline_handle_t pipe)
{
    /* Pipeline reset does not clear the ring buffer's done flag or residual data */
    for (int i = 0; i < STREAM_PROC_DUAL_BIS_NUM; i++) {
        if (s_stream_proc->local2bt_bis_legs[i].pipe == pipe && s_stream_proc->local2bt_bis_legs[i].rb) {
            esp_gmf_db_reset(s_stream_proc->local2bt_bis_legs[i].rb);
            return;
        }
    }
}

static esp_gmf_err_t stream_proc_dual_bis_prev_stop(void *event_ctx)
{
    stream_proc_ctx_t *stream_ctx = (stream_proc_ctx_t *)event_ctx;
    /* The resampler reads and the copier writes with ESP_GMF_MAX_DELAY, so a task blocked on
     * the ring buffer never observes the stop request until the buffer is aborted. Pipeline
     * reset clears the abort flag again before the next run. */
    for (int i = 0; i < STREAM_PROC_DUAL_BIS_NUM; i++) {
        if (stream_ctx->local2bt_bis_legs[i].rb) {
            esp_gmf_db_abort(stream_ctx->local2bt_bis_legs[i].rb);
        }
    }
    return ESP_GMF_ERR_OK;
}

static bool stream_proc_post_bis_leg_prepare(stream_proc_bis_leg_t *leg)
{
    /* The leg head element stays uninitialized until it receives sound info, and an
     * uninitialized element registers no job, so its pipeline would never run. The
     * decoded PCM format reported over the copier link later reopens the resampler. */
    stream_proc_cmd_t cmd = {
        .action = STREAM_PROC_PIPELINE_PREPARE,
        .pipe = leg->pipe,
        .report_src_info = true,
        .src_info = leg->src_info,
    };
    return stream_proc_post_cmd(&cmd, STREAM_PROC_CMD_WAIT_TICKS);
}

static void stream_proc_set_bis_leg_format(stream_proc_bis_leg_t *leg, uint32_t rate, int slot)
{
    memset(leg->weight, 0, sizeof(leg->weight));
    leg->weight[slot == 0 ? 0 : 1] = 1.0f;
    leg->src_info.sample_rates = (int)rate;
    leg->src_info.channels = STREAM_PROC_DUAL_BIS_SRC_CH;
    leg->src_info.bits = STREAM_PROC_DUAL_BIS_SRC_BITS;
}

static esp_err_t stream_proc_configure_bis_leg(stream_proc_bis_leg_t *leg, int slot)
{
    esp_gmf_element_handle_t asrc = stream_proc_get_asrc(leg->pipe, 0);
    if (asrc == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_gmf_err_t ret = esp_gmf_asrc_set_dest_rate(asrc, leg->src_info.sample_rates);
    if (ret != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to set BIS[%d] ASRC destination rate: %d", slot, ret);
        return ESP_FAIL;
    }
    ret = esp_gmf_asrc_set_dest_ch(asrc, 1);
    if (ret != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to set BIS[%d] ASRC destination channels: %d", slot, ret);
        return ESP_FAIL;
    }
    esp_asrc_cfg_t *cfg = (esp_asrc_cfg_t *)OBJ_GET_CFG(asrc);
    if (cfg == NULL) {
        ESP_LOGE(TAG, "BIS[%d] ASRC configuration is unavailable", slot);
        return ESP_ERR_INVALID_STATE;
    }
    cfg->weight = leg->weight;
    cfg->weight_len = STREAM_PROC_DUAL_BIS_WEIGHT_LEN;
    return ESP_OK;
}

static void stream_proc_apply_pipeline_stop_reset(esp_gmf_pipeline_handle_t pipe)
{
    if (pipe == NULL) {
        return;
    }
    ESP_LOGI(TAG, "Reset pipeline %p", pipe);
    esp_gmf_pipeline_stop(pipe);
    esp_gmf_pipeline_reset(pipe);
    stream_proc_reset_bis_leg_rb(pipe);
}

static void stream_proc_request_pipeline_stop_reset(esp_gmf_pipeline_handle_t pipe)
{
    if (stream_proc_post_pipeline_action(pipe, STREAM_PROC_PIPELINE_STOP_RESET, 0)) {
        return;
    }
    /* The stream-processor task cannot wait on its own full queue. */
    ESP_LOGW(TAG, "Stream processor queue full, stop pipeline %p inline", pipe);
    stream_proc_apply_pipeline_stop_reset(pipe);
}

static void stream_proc_dual_bis_stop(void)
{
    if (s_stream_proc->local2bt_head_running) {
        stream_proc_request_pipeline_stop_reset(s_stream_proc->local2bt_head_pipe);
        s_stream_proc->local2bt_head_running = false;
    }
    for (int i = 0; i < STREAM_PROC_DUAL_BIS_NUM; i++) {
        if (s_stream_proc->local2bt_bis_legs[i].running) {
            stream_proc_request_pipeline_stop_reset(s_stream_proc->local2bt_bis_legs[i].pipe);
            s_stream_proc->local2bt_bis_legs[i].running = false;
        }
        s_stream_proc->local2bt_bis_legs[i].ready = false;
        s_stream_proc->local2bt_bis_legs[i].configured = false;
        s_stream_proc->local2bt_bis_legs[i].started = false;
        s_stream_proc->local2bt_bis_legs[i].stream = NULL;
    }
    s_stream_proc->local2bt_bis_ready = 0;
    s_stream_proc->local2bt_bis_configured = 0;
    s_stream_proc->local2bt_bis_started = 0;
}

static bool stream_proc_dual_bis_run(void)
{
    for (int i = 0; i < STREAM_PROC_DUAL_BIS_NUM; i++) {
        if (!stream_proc_post_bis_leg_prepare(&s_stream_proc->local2bt_bis_legs[i]) ||
            !stream_proc_post_pipeline_action(s_stream_proc->local2bt_bis_legs[i].pipe, STREAM_PROC_PIPELINE_RUN,
                                              STREAM_PROC_CMD_WAIT_TICKS)) {
            ESP_LOGE(TAG, "Failed to queue dual BIS[%d] start", i);
            stream_proc_dual_bis_stop();
            return false;
        }
        s_stream_proc->local2bt_bis_legs[i].running = true;
    }
    if (!stream_proc_post_pipeline_action(s_stream_proc->local2bt_head_pipe, STREAM_PROC_PIPELINE_RUN,
                                          STREAM_PROC_CMD_WAIT_TICKS)) {
        ESP_LOGE(TAG, "Failed to queue dual BIS head start");
        stream_proc_dual_bis_stop();
        return false;
    }
    s_stream_proc->local2bt_head_running = true;
    return true;
}

static void stream_proc_dual_bis_try_run(void)
{
    if (s_stream_proc->local2bt_bis_ready < STREAM_PROC_DUAL_BIS_NUM ||
        s_stream_proc->local2bt_bis_configured < STREAM_PROC_DUAL_BIS_NUM ||
        s_stream_proc->local2bt_bis_started < STREAM_PROC_DUAL_BIS_NUM ||
        s_stream_proc->local2bt_head_running) {
        return;
    }
    stream_proc_cmd_t cmd = {
        .action = STREAM_PROC_PIPELINE_PREPARE,
        .pipe = s_stream_proc->local2bt_head_pipe,
        .uri = playlist[s_stream_proc->playlist_cur_index],
    };
    if (!stream_proc_post_cmd(&cmd, STREAM_PROC_CMD_WAIT_TICKS)) {
        ESP_LOGE(TAG, "Failed to queue dual BIS head prepare");
        return;
    }
    stream_proc_dual_bis_run();
}

static void stream_proc_dual_bis_config_leg(int slot)
{
    stream_proc_bis_leg_t *leg = &s_stream_proc->local2bt_bis_legs[slot];
    if (leg->stream == NULL) {
        ESP_LOGW(TAG, "BIS[%d] released before its configuration was applied", slot);
        return;
    }
    esp_gmf_err_t gmf_err = esp_gmf_io_bt_set_stream(ESP_GMF_PIPELINE_GET_OUT_INSTANCE(leg->pipe), leg->stream);
    if (gmf_err != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to bind BIS[%d] stream to Bluetooth I/O: %d", slot, gmf_err);
        goto fail;
    }
    if (stream_proc_configure_bis_leg(leg, slot) != ESP_OK) {
        goto fail;
    }
    esp_bt_audio_stream_codec_info_t info = {0};
    esp_err_t err = esp_bt_audio_stream_get_codec_info(leg->stream, &info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get BIS[%d] codec info: %s", slot, esp_err_to_name(err));
        goto fail;
    }
    esp_audio_enc_config_t enc_cfg = {
        .type = stream_proc_get_audio_type(info.codec_type),
        .cfg = info.codec_cfg,
        .cfg_sz = info.cfg_size,
    };
    esp_gmf_element_handle_t encoder = NULL;
    if (esp_gmf_pipeline_get_el_by_name(leg->pipe, "aud_enc", &encoder) != ESP_GMF_ERR_OK || encoder == NULL) {
        ESP_LOGE(TAG, "aud_enc missing on BIS leg %d", slot);
        goto fail;
    }
    gmf_err = esp_gmf_audio_enc_reconfig(encoder, &enc_cfg);
    if (gmf_err != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to configure BIS[%d] encoder: %d", slot, gmf_err);
        goto fail;
    }
    if (slot == 0) {
        esp_audio_simple_dec_cfg_t dec_cfg = {
            .dec_type = ESP_AUDIO_TYPE_MP3,
            .dec_cfg = NULL,
            .cfg_size = 0,
        };
        gmf_err = esp_gmf_audio_dec_reconfig(s_stream_proc->local2bt_head_pipe->head_el, &dec_cfg);
        if (gmf_err != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to configure dual BIS media decoder: %d", gmf_err);
            goto fail;
        }
    }
    if (!leg->configured) {
        leg->configured = true;
        s_stream_proc->local2bt_bis_configured++;
    }
    stream_proc_dual_bis_try_run();
    return;

fail:
    stream_proc_dual_bis_stop();
}

static esp_err_t stream_proc_dual_bis_prepare(esp_bt_audio_stream_handle_t stream, stream_user_data_t *user_d)
{
    int slot = -1;
    for (int i = 0; i < STREAM_PROC_DUAL_BIS_NUM; i++) {
        if (s_stream_proc->local2bt_bis_legs[i].stream == NULL) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        ESP_LOGE(TAG, "No free dual BIS slot");
        return ESP_ERR_INVALID_STATE;
    }

    esp_bt_audio_stream_codec_info_t info = {0};
    esp_err_t err = esp_bt_audio_stream_get_codec_info(stream, &info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get dual BIS codec info: %s", esp_err_to_name(err));
        return err;
    }

    stream_proc_bis_leg_t *leg = &s_stream_proc->local2bt_bis_legs[slot];
    stream_proc_set_bis_leg_format(leg, info.sample_rate, slot);
    leg->stream = stream;
    user_d->pipe = leg->pipe;
    /* Element configuration must observe the reset that a preceding stop queued, so it runs
     * on the stream processor task rather than in this Bluetooth callback context. */
    stream_proc_cmd_t cmd = {
        .action = STREAM_PROC_BIS_LEG_CONFIG,
        .leg = slot,
    };
    if (!stream_proc_post_cmd(&cmd, 0)) {
        leg->stream = NULL;
        return ESP_FAIL;
    }

    if (slot == 0) {
        s_stream_proc->local2bt_stream = stream;
        ESP_LOGI(TAG, "Set dual BIS media file: %s (index %d)", playlist[s_stream_proc->playlist_cur_index],
                 s_stream_proc->playlist_cur_index);
    }
    leg->ready = true;
    s_stream_proc->local2bt_bis_ready++;
    ESP_LOGI(TAG, "Dual BIS[%d] prepared, 1 BIG / %d BIS, rate=%lu", slot, STREAM_PROC_DUAL_BIS_NUM,
             (unsigned long)info.sample_rate);
    return ESP_OK;
}

static esp_gmf_err_t local2bt_head_pipe_event_cb(esp_gmf_event_pkt_t *pkt, void *event_ctx)
{
    stream_proc_ctx_t *stream_ctx = (stream_proc_ctx_t *)event_ctx;
    if (pkt == NULL) {
        return ESP_GMF_ERR_OK;
    }
    if (pkt->type == ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        ESP_LOGI(TAG, "[local to dual BIS pipeline] state => %s(%d)", gmf_state_to_str(pkt->sub), pkt->sub);
        if (pkt->sub == ESP_GMF_EVENT_STATE_FINISHED) {
            ESP_LOGI(TAG, "Local to dual BIS media finished");
            if (stream_ctx->local2bt_stream) {
                /* This runs in the head GMF task thread; stopping and re-running that same
                 * pipeline from here would stall until esp_gmf_task_run() times out. */
                stream_proc_post_pipeline_action(NULL, STREAM_PROC_PIPELINE_PLAY_NEXT, 0);
            }
        } else if (pkt->sub == ESP_GMF_EVENT_STATE_ERROR) {
            ESP_LOGE(TAG, "Local to dual BIS media error");
        }
    }
    return ESP_GMF_ERR_OK;
}

static void cleanup_pipeline_local2bt_dual_bis(void)
{
    if (s_stream_proc->local2bt_head_task) {
        esp_gmf_task_deinit(s_stream_proc->local2bt_head_task);
        s_stream_proc->local2bt_head_task = NULL;
    }
    for (int i = 0; i < STREAM_PROC_DUAL_BIS_NUM; i++) {
        if (s_stream_proc->local2bt_bis_legs[i].task) {
            esp_gmf_task_deinit(s_stream_proc->local2bt_bis_legs[i].task);
            s_stream_proc->local2bt_bis_legs[i].task = NULL;
        }
    }
    if (s_stream_proc->local2bt_head_pipe) {
        esp_gmf_pipeline_destroy(s_stream_proc->local2bt_head_pipe);
        s_stream_proc->local2bt_head_pipe = NULL;
    }
    for (int i = 0; i < STREAM_PROC_DUAL_BIS_NUM; i++) {
        if (s_stream_proc->local2bt_bis_legs[i].pipe) {
            esp_gmf_pipeline_destroy(s_stream_proc->local2bt_bis_legs[i].pipe);
            s_stream_proc->local2bt_bis_legs[i].pipe = NULL;
        }
        if (s_stream_proc->local2bt_bis_legs[i].rb) {
            esp_gmf_db_deinit(s_stream_proc->local2bt_bis_legs[i].rb);
            s_stream_proc->local2bt_bis_legs[i].rb = NULL;
        }
    }
}

static void setup_pipeline_local2bt_dual_bis(esp_gmf_pool_handle_t pool)
{
    const char *head_els[] = {"aud_dec", "copier"};
    if (esp_gmf_pool_new_pipeline(pool, "io_file", head_els, sizeof(head_els) / sizeof(head_els[0]),
                                  NULL, &s_stream_proc->local2bt_head_pipe) != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to create dual BIS head pipeline");
        goto fail;
    }
    esp_gmf_pipeline_set_event(s_stream_proc->local2bt_head_pipe, local2bt_head_pipe_event_cb, s_stream_proc);

    for (int i = 0; i < STREAM_PROC_DUAL_BIS_NUM; i++) {
        const char *leg_els[] = {"aud_asrc", "aud_enc"};
        if (esp_gmf_pool_new_pipeline(pool, NULL, leg_els, sizeof(leg_els) / sizeof(leg_els[0]),
                                      "io_bt", &s_stream_proc->local2bt_bis_legs[i].pipe) != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to create dual BIS leg %d", i);
            goto fail;
        }
        if (esp_gmf_db_new_ringbuf(STREAM_PROC_DUAL_BIS_RB_BLOCKS, STREAM_PROC_DUAL_BIS_RB_SIZE,
                                   &s_stream_proc->local2bt_bis_legs[i].rb) != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to create dual BIS ringbuf %d", i);
            goto fail;
        }
        esp_gmf_port_handle_t out_port =
            NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write,
                                      NULL, s_stream_proc->local2bt_bis_legs[i].rb,
                                      STREAM_PROC_DUAL_BIS_PORT_SIZE, ESP_GMF_MAX_DELAY);
        esp_gmf_port_handle_t in_port =
            NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read,
                                     NULL, s_stream_proc->local2bt_bis_legs[i].rb,
                                     STREAM_PROC_DUAL_BIS_PORT_SIZE, 1);
        if (out_port == NULL || in_port == NULL) {
            ESP_LOGE(TAG, "Failed to create dual BIS ports %d", i);
            if (out_port) {
                esp_gmf_port_deinit(out_port);
            }
            if (in_port) {
                esp_gmf_port_deinit(in_port);
            }
            goto fail;
        }
        if (esp_gmf_pipeline_connect_pipe(s_stream_proc->local2bt_head_pipe, "copier", out_port,
                                          s_stream_proc->local2bt_bis_legs[i].pipe,
                                          "aud_asrc", in_port) != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to connect dual BIS leg %d", i);
            goto fail;
        }
        esp_gmf_pipeline_set_prev_stop_cb(s_stream_proc->local2bt_bis_legs[i].pipe,
                                          stream_proc_dual_bis_prev_stop, s_stream_proc);
    }
    esp_gmf_pipeline_set_prev_stop_cb(s_stream_proc->local2bt_head_pipe,
                                      stream_proc_dual_bis_prev_stop, s_stream_proc);

    esp_gmf_task_cfg_t head_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    head_cfg.thread.core = 0;
    head_cfg.thread.stack = 6144;
    head_cfg.thread.prio = 14;
    head_cfg.thread.stack_in_ext = true;
    head_cfg.name = "local2bt_head";
    if (esp_gmf_task_init(&head_cfg, &s_stream_proc->local2bt_head_task) != ESP_GMF_ERR_OK ||
        esp_gmf_pipeline_bind_task(s_stream_proc->local2bt_head_pipe,
                                   s_stream_proc->local2bt_head_task) != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to create or bind dual BIS head task");
        goto fail;
    }

    for (int i = 0; i < STREAM_PROC_DUAL_BIS_NUM; i++) {
        esp_gmf_task_cfg_t leg_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
        leg_cfg.name = (i == 0) ? "bis_l" : "bis_r";
        leg_cfg.thread.core = (i == 0) ? 1 : 0;
        leg_cfg.thread.stack = 6144;
        leg_cfg.thread.prio = 15;
        leg_cfg.thread.stack_in_ext = true;
        if (esp_gmf_task_init(&leg_cfg, &s_stream_proc->local2bt_bis_legs[i].task) != ESP_GMF_ERR_OK ||
            esp_gmf_pipeline_bind_task(s_stream_proc->local2bt_bis_legs[i].pipe,
                                       s_stream_proc->local2bt_bis_legs[i].task) != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to create or bind dual BIS leg task %d", i);
            goto fail;
        }
    }
    ESP_LOGI(TAG, "Dual BIS local-to-BT pipelines ready, 1 BIG / %d BIS", STREAM_PROC_DUAL_BIS_NUM);
    return;

fail:
    cleanup_pipeline_local2bt_dual_bis();
}
#endif  /* STREAM_PROC_DUAL_BIS */

#if CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM
#define STREAM_PROC_CLK_SYNC_DIFF_THRESHOLD  2
#define STREAM_PROC_CLK_SYNC_MON_QUEUE_SIZE  10
#define STREAM_PROC_CLK_SYNC_MON_TASK_STACK  4096
#define STREAM_PROC_CLK_SYNC_MON_TASK_PRIO   5
#define STREAM_PROC_CLK_SYNC_MON_TASK_CORE_ID  0

static inline esp_err_t stream_proc_open_dac(dev_audio_codec_handles_t *dac_handle)
{
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = CODEC_DAC_SAMPLE_RATE,
        .bits_per_sample = CODEC_DAC_BITS_PER_SAMPLE,
        .channel = CODEC_DAC_CHANNELS,
    };
    return esp_codec_dev_open(dac_handle->codec_dev, &fs);
}

static i2s_chan_handle_t get_i2s_chan_handle(const char *name)
{
    i2s_chan_handle_t ch = NULL;
    dev_audio_codec_config_t *codec_config = NULL;
    esp_err_t ret = esp_board_manager_get_device_config(name, (void **)&codec_config);
    ESP_RETURN_ON_FALSE(ret == ESP_OK, NULL, TAG, "get device config failed");
    ret = esp_board_periph_get_handle(codec_config->i2s_cfg.name, (void **)&ch);
    ESP_RETURN_ON_FALSE(ret == ESP_OK, NULL, TAG, "get i2s chan handle failed");
    ESP_LOGD(TAG, "get i2s[%s:%s] handle %p", name, codec_config->i2s_cfg.name, ch);
    return ch;
}

static void stream_proc_clk_sync_monitor_task(void *arg)
{
    stream_proc_ctx_t *stream_ctx = (stream_proc_ctx_t *)arg;
    esp_bt_audio_le_clk_sync_msg_t msg = {0};

    while (stream_ctx->clk_sync_monitor_task_running) {
        if (xQueueReceive(stream_ctx->clk_sync_monitor_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!stream_ctx->clk_sync_monitor_task_running) {
            break;
        }
        ESP_LOGW(TAG, "Clock sync monitor: diff=%ld, fifo=%" PRIu32 ", bck=%" PRIu32,
                 (long)msg.diff, msg.fifo_cnt, msg.bck_cnt);
    }
    stream_ctx->clk_sync_monitor_task = NULL;
    vTaskDeleteWithCaps(NULL);
}

static esp_err_t stream_proc_ensure_clk_sync_monitor(void)
{
    if (!s_stream_proc->clk_sync_monitor_queue) {
        s_stream_proc->clk_sync_monitor_queue = xQueueCreate(STREAM_PROC_CLK_SYNC_MON_QUEUE_SIZE,
                                                             sizeof(esp_bt_audio_le_clk_sync_msg_t));
        ESP_RETURN_ON_FALSE(s_stream_proc->clk_sync_monitor_queue, ESP_ERR_NO_MEM, TAG,
                            "Create clock sync monitor queue failed");
    }

    if (!s_stream_proc->clk_sync_monitor_task) {
        s_stream_proc->clk_sync_monitor_task_running = true;
        BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(stream_proc_clk_sync_monitor_task, "clk_sync_mon",
                                                         STREAM_PROC_CLK_SYNC_MON_TASK_STACK, s_stream_proc,
                                                         STREAM_PROC_CLK_SYNC_MON_TASK_PRIO,
                                                         &s_stream_proc->clk_sync_monitor_task,
                                                         STREAM_PROC_CLK_SYNC_MON_TASK_CORE_ID,
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ret != pdPASS) {
            s_stream_proc->clk_sync_monitor_task_running = false;
            ESP_LOGE(TAG, "Create clock sync monitor task failed");
            return ESP_ERR_NO_MEM;
        }
    }

    xQueueReset(s_stream_proc->clk_sync_monitor_queue);
    return ESP_OK;
}

static void stream_proc_deinit_clk_sync_monitor(void)
{
    if (s_stream_proc->clk_sync_monitor_task) {
        esp_bt_audio_le_clk_sync_msg_t msg = {0};
        s_stream_proc->clk_sync_monitor_task_running = false;
        if (s_stream_proc->clk_sync_monitor_queue) {
            xQueueSend(s_stream_proc->clk_sync_monitor_queue, &msg, 0);
        }
        for (uint8_t i = 0; i < 10 && s_stream_proc->clk_sync_monitor_task; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (s_stream_proc->clk_sync_monitor_task) {
            vTaskDeleteWithCaps(s_stream_proc->clk_sync_monitor_task);
            s_stream_proc->clk_sync_monitor_task = NULL;
        }
    }

    if (s_stream_proc->clk_sync_monitor_queue) {
        vQueueDelete(s_stream_proc->clk_sync_monitor_queue);
        s_stream_proc->clk_sync_monitor_queue = NULL;
    }
    s_stream_proc->clk_sync_monitor_task_running = false;
}

static void stream_proc_deinit_playback_sync(void)
{
    if (!s_stream_proc->playback_sync) {
        return;
    }

    esp_err_t ret = esp_bt_audio_le_playback_sync_disable(s_stream_proc->playback_sync);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Playback sync disable failed: %s", esp_err_to_name(ret));
    }
    ret = esp_bt_audio_le_playback_sync_deinit(s_stream_proc->playback_sync);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Playback sync deinit failed: %s", esp_err_to_name(ret));
    }
    s_stream_proc->playback_sync = NULL;
}

static void stream_proc_deinit_clk_sync(void)
{
    if (!s_stream_proc->clk_sync) {
        return;
    }

    esp_err_t ret = esp_bt_audio_le_clk_sync_disable(s_stream_proc->clk_sync);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Clock sync disable failed: %s", esp_err_to_name(ret));
    }
    ret = esp_bt_audio_le_clk_sync_deinit(s_stream_proc->clk_sync);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Clock sync deinit failed: %s", esp_err_to_name(ret));
    }
    s_stream_proc->clk_sync = NULL;
    stream_proc_deinit_clk_sync_monitor();
}

static void stream_proc_prepare_clk_sync()
{
    if (s_stream_proc->clk_sync) {
        return;
    }
    esp_err_t ret = ESP_OK;
    i2s_chan_handle_t tx_handle = get_i2s_chan_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
    if (!tx_handle) {
        ESP_LOGE(TAG, "Get I2S TX handle failed");
        return;
    }

    ret = stream_proc_ensure_clk_sync_monitor();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Prepare clock sync monitor failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_audio_le_clk_sync_init(tx_handle, s_stream_proc->clk_sync_monitor_queue,
                                        &s_stream_proc->clk_sync);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Clock sync init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Clock sync initialized, clk_sync=%p", s_stream_proc->clk_sync);
}

static void stream_proc_enable_clk_sync(esp_bt_audio_stream_handle_t stream)
{
    if (!s_stream_proc->clk_sync) {
        ESP_LOGW(TAG, "Clock sync is not initialized, continue without clock sync");
        return;
    }

    uint32_t iso_interval = 0;
    esp_err_t ret = esp_bt_audio_stream_get_iso_interval(stream, &iso_interval);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Get ISO interval failed: %s", esp_err_to_name(ret));
        return;
    }

    uint64_t ideal_count = ((uint64_t)CODEC_DAC_SAMPLE_RATE * CODEC_DAC_CHANNELS * iso_interval + 500000U) /
                           1000000U;
    if (ideal_count == 0 || ideal_count > UINT32_MAX) {
        ESP_LOGW(TAG, "Invalid clock sync ideal count: %" PRIu64, ideal_count);
        return;
    }

    ret = esp_bt_audio_le_clk_sync_enable(s_stream_proc->clk_sync, (uint32_t)ideal_count,
                                          STREAM_PROC_CLK_SYNC_DIFF_THRESHOLD);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Clock sync enable failed: %s", esp_err_to_name(ret));
    }
}

static void stream_proc_prepare_playback_sync(void)
{
    if (s_stream_proc->playback_sync) {
        return;
    }

    dev_audio_codec_handles_t *dac_handle = NULL;
    esp_err_t ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&dac_handle);
    if (ret != ESP_OK || !dac_handle) {
        ESP_LOGE(TAG, "get audio dac handle failed");
        return;
    }

    ret = esp_codec_dev_close(dac_handle->codec_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "close audio dac failed: %s", esp_err_to_name(ret));
        return;
    }

    i2s_chan_handle_t tx_handle = get_i2s_chan_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
    if (!tx_handle) {
        ESP_LOGE(TAG, "get i2s tx handle failed");
        stream_proc_open_dac(dac_handle);
        return;
    }
    ret = esp_bt_audio_le_playback_sync_init(tx_handle, &s_stream_proc->playback_sync);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Playback sync init failed: %s", esp_err_to_name(ret));
        stream_proc_open_dac(dac_handle);
        return;
    }

    ret = esp_bt_audio_le_playback_sync_enable(s_stream_proc->playback_sync);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Playback sync enable failed: %s", esp_err_to_name(ret));
        esp_bt_audio_le_playback_sync_deinit(s_stream_proc->playback_sync);
        s_stream_proc->playback_sync = NULL;
        stream_proc_open_dac(dac_handle);
        return;
    }

    ret = stream_proc_open_dac(dac_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "open audio dac failed: %s", esp_err_to_name(ret));
        stream_proc_deinit_playback_sync();
    }
}
#else
static void stream_proc_prepare_playback_sync(void)
{
}

static void stream_proc_prepare_clk_sync()
{
}

static void stream_proc_enable_clk_sync(esp_bt_audio_stream_handle_t stream)
{
}

static void stream_proc_deinit_playback_sync(void)
{
}

static void stream_proc_deinit_clk_sync(void)
{
}

static void stream_proc_deinit_clk_sync_monitor(void)
{
}
#endif  /* CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM */

static const char *gmf_state_to_str(int state)
{
    switch (state) {
        case ESP_GMF_EVENT_STATE_NONE:
            return "NONE";
        case ESP_GMF_EVENT_STATE_INITIALIZED:
            return "INITIALIZED";
        case ESP_GMF_EVENT_STATE_OPENING:
            return "OPENING";
        case ESP_GMF_EVENT_STATE_RUNNING:
            return "RUNNING";
        case ESP_GMF_EVENT_STATE_PAUSED:
            return "PAUSED";
        case ESP_GMF_EVENT_STATE_STOPPED:
            return "STOPPED";
        case ESP_GMF_EVENT_STATE_FINISHED:
            return "FINISHED";
        case ESP_GMF_EVENT_STATE_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

static void local2bt_play(const char *uri)
{
#if STREAM_PROC_DUAL_BIS
    if (s_stream_proc->local2bt_head_pipe &&
        s_stream_proc->local2bt_bis_ready == STREAM_PROC_DUAL_BIS_NUM &&
        s_stream_proc->local2bt_bis_configured == STREAM_PROC_DUAL_BIS_NUM) {
        if (s_stream_proc->local2bt_head_running) {
            stream_proc_request_pipeline_stop_reset(s_stream_proc->local2bt_head_pipe);
            s_stream_proc->local2bt_head_running = false;
        }
        for (int i = 0; i < STREAM_PROC_DUAL_BIS_NUM; i++) {
            if (s_stream_proc->local2bt_bis_legs[i].running) {
                stream_proc_request_pipeline_stop_reset(s_stream_proc->local2bt_bis_legs[i].pipe);
                s_stream_proc->local2bt_bis_legs[i].running = false;
            }
        }
        stream_proc_cmd_t cmd = {
            .action = STREAM_PROC_PIPELINE_PREPARE,
            .pipe = s_stream_proc->local2bt_head_pipe,
            .uri = uri,
        };
        if (!stream_proc_post_cmd(&cmd, STREAM_PROC_CMD_WAIT_TICKS) || !stream_proc_dual_bis_run()) {
            ESP_LOGE(TAG, "Failed to restart dual BIS media");
            return;
        }
        ESP_LOGI(TAG, "Local to dual BIS media play: %s (index %d)", uri, s_stream_proc->playlist_cur_index);
        return;
    }
#endif  /* STREAM_PROC_DUAL_BIS */
    if (s_stream_proc->local2bt_pipe == NULL) {
        ESP_LOGE(TAG, "Local to BT pipeline is not initialized");
        return;
    }
    if (s_stream_proc->local2bt_stream == NULL) {
        ESP_LOGE(TAG, "Local to BT stream is not initialized");
        return;
    }
    esp_gmf_pipeline_stop(s_stream_proc->local2bt_pipe);
    esp_gmf_pipeline_reset(s_stream_proc->local2bt_pipe);
    esp_gmf_io_bt_set_stream(ESP_GMF_PIPELINE_GET_OUT_INSTANCE(s_stream_proc->local2bt_pipe),
                             s_stream_proc->local2bt_stream);
    esp_gmf_pipeline_set_in_uri(s_stream_proc->local2bt_pipe, uri);
    ESP_LOGI(TAG, "Local to BT media play: %s (index %d)", uri, s_stream_proc->playlist_cur_index);
    esp_gmf_pipeline_loading_jobs(s_stream_proc->local2bt_pipe);
    esp_gmf_pipeline_run(s_stream_proc->local2bt_pipe);
}

void local2bt_play_next(void)
{
    if (s_stream_proc == NULL || s_stream_proc->local2bt_stream == NULL) {
        ESP_LOGE(TAG, "Local to BT stream is not initialized");
        return;
    }
    s_stream_proc->playlist_cur_index = (s_stream_proc->playlist_cur_index + 1) % playlist_len;
    local2bt_play(playlist[s_stream_proc->playlist_cur_index]);
}

void local2bt_play_prev(void)
{
    if (s_stream_proc == NULL || s_stream_proc->local2bt_stream == NULL) {
        ESP_LOGE(TAG, "Local to BT stream is not initialized");
        return;
    }
    s_stream_proc->playlist_cur_index = (s_stream_proc->playlist_cur_index + playlist_len - 1) % playlist_len;
    local2bt_play(playlist[s_stream_proc->playlist_cur_index]);
}

static void stream_proc_destroy(stream_user_data_t *user_d)
{
    ESP_LOGI(TAG, "stream_user_data_destroy %p", user_d);
    if (user_d) {
        free(user_d);
    }
}

static void stream_proc_prepare(esp_bt_audio_stream_handle_t stream, stream_user_data_t **out)
{
    stream_user_data_t *user_d = heap_caps_calloc(1, sizeof(stream_user_data_t), MALLOC_CAP_SPIRAM);
    if (user_d == NULL) {
        ESP_LOGE(TAG, "calloc user data failed");
        *out = NULL;
        return;
    }
    esp_bt_audio_stream_codec_info_t codec_info = {0};
    esp_bt_audio_stream_get_codec_info(stream, &codec_info);
    ESP_LOGI(TAG, "Codec Info: type=%d, bits=%d, channels=0x%x (%d ch), sample_rate=%d, cfg_size=%d, codec_cfg=%p",
             codec_info.codec_type,
             codec_info.bits,
             codec_info.channels,
             __builtin_popcount(codec_info.channels),
             codec_info.sample_rate,
             codec_info.cfg_size,
             codec_info.codec_cfg);

    esp_bt_audio_stream_dir_t dir = ESP_BT_AUDIO_STREAM_DIR_UNKNOWN;
    if (esp_bt_audio_stream_get_dir(stream, &dir) != ESP_OK) {
        ESP_LOGE(TAG, "Get stream dir failed, stream=%p", stream);
        free(user_d);
        *out = NULL;
        return;
    }

    if (dir == ESP_BT_AUDIO_STREAM_DIR_SINK) {
        ESP_LOGI(TAG, "Prepare bt to codec pipeline");
        user_d->pipe = s_stream_proc->bt2codec_pipe;
        esp_gmf_io_bt_set_stream(ESP_GMF_PIPELINE_GET_IN_INSTANCE(user_d->pipe), stream);
        esp_audio_simple_dec_cfg_t simple_dec_cfg = {
            .dec_type = stream_proc_get_audio_type(codec_info.codec_type),
            .dec_cfg = codec_info.codec_cfg,
            .cfg_size = codec_info.cfg_size,
            .use_frame_dec = true,
        };
        esp_gmf_audio_dec_reconfig(user_d->pipe->head_el, &simple_dec_cfg);

        stream_proc_set_asrc_dest(user_d->pipe, 0, CODEC_DAC_SAMPLE_RATE, __builtin_popcount(codec_info.channels),
                                  CODEC_DAC_CHANNELS, s_stream_proc->bt2codec_asrc_weight,
                                  STREAM_PROC_ASRC_MAX_WEIGHT_LEN);

        /* Playback sync is only needed for LE Audio streams */
        esp_bt_audio_stream_profile_t profile = ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN;
        esp_bt_audio_stream_get_profile(stream, &profile);
        if (profile == ESP_BT_AUDIO_STREAM_PROFILE_LE_UNICAST ||
            profile == ESP_BT_AUDIO_STREAM_PROFILE_LE_BROADCAST) {
            stream_proc_prepare_playback_sync();
        }
        stream_proc_post_pipeline_action(user_d->pipe, STREAM_PROC_PIPELINE_PREPARE, 0);
    } else {
        uint8_t output_asrc_index = 0;
        uint32_t context = 0;
        bool report_src_info = false;
        esp_gmf_info_sound_t src_info = {0};
        const char *uri = NULL;
        if (esp_bt_audio_stream_get_context(stream, &context) != ESP_OK) {
            ESP_LOGE(TAG, "Get stream context failed, stream=%p", stream);
        }
#if STREAM_PROC_DUAL_BIS
        {
            esp_bt_audio_stream_profile_t profile = ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN;
            esp_bt_audio_stream_get_profile(stream, &profile);
            if (profile == ESP_BT_AUDIO_STREAM_PROFILE_LE_BROADCAST) {
                if (stream_proc_dual_bis_prepare(stream, user_d) != ESP_OK) {
                    free(user_d);
                    *out = NULL;
                    return;
                }
                *out = user_d;
                return;
            }
        }
#endif  /* STREAM_PROC_DUAL_BIS */
        if (context == ESP_BT_AUDIO_STREAM_CONTEXT_MEDIA) {
            ESP_LOGI(TAG, "Prepare local to bt pipeline");
            user_d->pipe = s_stream_proc->local2bt_pipe;
            s_stream_proc->local2bt_stream = stream;

            esp_audio_simple_dec_cfg_t simple_dec_cfg = {
                .dec_type = ESP_AUDIO_TYPE_MP3,
                .dec_cfg = NULL,
                .cfg_size = 0,
            };
            esp_gmf_audio_dec_reconfig(user_d->pipe->head_el, &simple_dec_cfg);
            uri = playlist[s_stream_proc->playlist_cur_index];
            ESP_LOGI(TAG, "Set media file: %s (index %d)", playlist[s_stream_proc->playlist_cur_index],
                     s_stream_proc->playlist_cur_index);
        } else {
            ESP_LOGI(TAG, "Prepare codec to bt pipeline");
            user_d->pipe = s_stream_proc->codec2bt_pipe;
            output_asrc_index = 1;
            report_src_info = true;
            src_info.sample_rates = CODEC_ADC_SAMPLE_RATE;
            src_info.channels = CODEC_ADC_CHANNELS;
            src_info.bits = CODEC_ADC_BITS_PER_SAMPLE;
        }
        esp_gmf_io_bt_set_stream(ESP_GMF_PIPELINE_GET_OUT_INSTANCE(user_d->pipe), stream);
        uint8_t output_src_ch = context == ESP_BT_AUDIO_STREAM_CONTEXT_MEDIA ? 2 : 1;
        float *asrc_weight = context == ESP_BT_AUDIO_STREAM_CONTEXT_MEDIA ?
                                 s_stream_proc->local2bt_asrc_weight :
                                 s_stream_proc->codec2bt_output_asrc_weight;
        stream_proc_set_asrc_dest(user_d->pipe, output_asrc_index, codec_info.sample_rate, output_src_ch,
                                  __builtin_popcount(codec_info.channels), asrc_weight,
                                  STREAM_PROC_ASRC_MAX_WEIGHT_LEN);

        esp_audio_enc_config_t enc_cfg = {
            .type = stream_proc_get_audio_type(codec_info.codec_type),
            .cfg = codec_info.codec_cfg,
            .cfg_sz = codec_info.cfg_size,
        };
        esp_gmf_audio_enc_reconfig(user_d->pipe->last_el, &enc_cfg);
        stream_proc_cmd_t cmd = {
            .action = STREAM_PROC_PIPELINE_PREPARE,
            .pipe = user_d->pipe,
            .uri = uri,
            .report_src_info = report_src_info,
            .src_info = src_info,
        };
        stream_proc_post_cmd(&cmd, 0);
    }
    *out = user_d;
}

void stream_proc_state_chg(esp_bt_audio_stream_handle_t stream, esp_bt_audio_stream_state_t state)
{
    const char *state_str[] = {"ALLOCATED", "STARTED", "STOPPED", "RELEASED"};
    esp_bt_audio_stream_dir_t dir = ESP_BT_AUDIO_STREAM_DIR_UNKNOWN;
    esp_bt_audio_stream_get_dir(stream, &dir);
    ESP_LOGI(TAG, "Stream state changed: stream %p, dir %d, state %s", stream, dir, state_str[state]);
    switch (state) {
        case ESP_BT_AUDIO_STREAM_STATE_ALLOCATED: {
            stream_user_data_t *user_dat = NULL;
            esp_bt_audio_stream_get_local_data(stream, (void **)&user_dat);
            if (user_dat) {
                stream_proc_destroy(user_dat);
                esp_bt_audio_stream_set_local_data(stream, NULL);
                user_dat = NULL;
            }
            stream_proc_prepare(stream, &user_dat);
            esp_bt_audio_stream_set_local_data(stream, user_dat);
            break;
        }
        case ESP_BT_AUDIO_STREAM_STATE_STARTED: {
            stream_user_data_t *user_d = NULL;
            esp_bt_audio_stream_get_local_data(stream, (void **)&user_d);
            if (user_d && user_d->pipe) {
                if (dir == ESP_BT_AUDIO_STREAM_DIR_SINK) {
                    esp_bt_audio_stream_profile_t profile = ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN;
                    esp_bt_audio_stream_get_profile(stream, &profile);
                    if (profile == ESP_BT_AUDIO_STREAM_PROFILE_LE_UNICAST ||
                        profile == ESP_BT_AUDIO_STREAM_PROFILE_LE_BROADCAST) {
                        stream_proc_prepare_clk_sync();
                        stream_proc_enable_clk_sync(stream);
                    }
                }
#if STREAM_PROC_DUAL_BIS
                if (dir == ESP_BT_AUDIO_STREAM_DIR_SOURCE) {
                    esp_bt_audio_stream_profile_t profile = ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN;
                    esp_bt_audio_stream_get_profile(stream, &profile);
                    if (profile == ESP_BT_AUDIO_STREAM_PROFILE_LE_BROADCAST) {
                        stream_proc_bis_leg_t *leg = stream_proc_find_bis_leg(stream);
                        if (leg && !leg->started) {
                            leg->started = true;
                            s_stream_proc->local2bt_bis_started++;
                        }
                        stream_proc_dual_bis_try_run();
                        break;
                    }
                }
#endif  /* STREAM_PROC_DUAL_BIS */
                stream_proc_post_pipeline_action(user_d->pipe, STREAM_PROC_PIPELINE_RUN, 0);
            } else {
                ESP_LOGE(TAG, "Stream user data not prepared for stream %p", stream);
            }
            break;
        }
        case ESP_BT_AUDIO_STREAM_STATE_STOPPED: {
#if STREAM_PROC_DUAL_BIS
            if (dir == ESP_BT_AUDIO_STREAM_DIR_SOURCE) {
                esp_bt_audio_stream_profile_t profile = ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN;
                esp_bt_audio_stream_get_profile(stream, &profile);
                if (profile == ESP_BT_AUDIO_STREAM_PROFILE_LE_BROADCAST) {
                    stream_proc_dual_bis_stop();
                    break;
                }
            }
#endif  /* STREAM_PROC_DUAL_BIS */
            stream_user_data_t *user_d = NULL;
            esp_bt_audio_stream_get_local_data(stream, (void **)&user_d);
            if (user_d && user_d->pipe) {
                ESP_LOGI(TAG, "Schedule reset pipeline %p", user_d->pipe);
                stream_proc_post_pipeline_action(user_d->pipe, STREAM_PROC_PIPELINE_STOP_RESET, 0);
            }
            if (dir == ESP_BT_AUDIO_STREAM_DIR_SINK) {
                esp_bt_audio_stream_profile_t profile = ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN;
                esp_bt_audio_stream_get_profile(stream, &profile);
                if (profile == ESP_BT_AUDIO_STREAM_PROFILE_LE_UNICAST ||
                    profile == ESP_BT_AUDIO_STREAM_PROFILE_LE_BROADCAST) {
                    stream_proc_deinit_clk_sync();
                    stream_proc_deinit_playback_sync();
                }
            }
            break;
        }
        case ESP_BT_AUDIO_STREAM_STATE_RELEASED: {
            stream_user_data_t *user_d = NULL;
            esp_bt_audio_stream_get_local_data(stream, (void **)&user_d);
            if (dir == ESP_BT_AUDIO_STREAM_DIR_SINK) {
                esp_bt_audio_stream_profile_t profile = ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN;
                esp_bt_audio_stream_get_profile(stream, &profile);
                if (profile == ESP_BT_AUDIO_STREAM_PROFILE_LE_UNICAST ||
                    profile == ESP_BT_AUDIO_STREAM_PROFILE_LE_BROADCAST) {
                    stream_proc_deinit_clk_sync();
                    stream_proc_deinit_playback_sync();
                }
            }
#if STREAM_PROC_DUAL_BIS
            if (dir == ESP_BT_AUDIO_STREAM_DIR_SOURCE) {
                esp_bt_audio_stream_profile_t profile = ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN;
                esp_bt_audio_stream_get_profile(stream, &profile);
                if (profile == ESP_BT_AUDIO_STREAM_PROFILE_LE_BROADCAST) {
                    stream_proc_dual_bis_stop();
                }
            }
#endif  /* STREAM_PROC_DUAL_BIS */
            if (user_d) {
                stream_proc_destroy(user_d);
                esp_bt_audio_stream_set_local_data(stream, NULL);
            }
            if (s_stream_proc->local2bt_stream == stream) {
                s_stream_proc->local2bt_stream = NULL;
            }
            break;
        }
        default:
            break;
    }
}

static esp_gmf_err_t bt2codec_pipe_event_cb(esp_gmf_event_pkt_t *pkt, void *event_ctx)
{
    (void)event_ctx;
    if (pkt == NULL) {
        return ESP_GMF_ERR_OK;
    }
    if (pkt->type == ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        ESP_LOGI(TAG, "[bt2codec pipeline] state => %s(%d)", gmf_state_to_str(pkt->sub), pkt->sub);
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t codec2bt_pipe_event_cb(esp_gmf_event_pkt_t *pkt, void *event_ctx)
{
    (void)event_ctx;
    if (pkt == NULL) {
        return ESP_GMF_ERR_OK;
    }
    if (pkt->type == ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        ESP_LOGI(TAG, "[codec2bt pipeline] state => %s(%d)", gmf_state_to_str(pkt->sub), pkt->sub);
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t local2bt_pipe_event_cb(esp_gmf_event_pkt_t *pkt, void *event_ctx)
{
    stream_proc_ctx_t *stream_ctx = (stream_proc_ctx_t *)event_ctx;
    if (pkt == NULL) {
        return ESP_GMF_ERR_OK;
    }
    if (pkt->type == ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        ESP_LOGI(TAG, "[local to bt media pipeline] state => %s(%d)", gmf_state_to_str(pkt->sub), pkt->sub);
        if (pkt->sub == ESP_GMF_EVENT_STATE_FINISHED) {
            ESP_LOGI(TAG, "Local to BT media finished");
            if (stream_ctx->local2bt_stream) {
                stream_proc_post_pipeline_action(NULL, STREAM_PROC_PIPELINE_PLAY_NEXT, 0);
            }
        } else if (pkt->sub == ESP_GMF_EVENT_STATE_ERROR) {
            ESP_LOGE(TAG, "Local to BT media error");
#if CONFIG_BT_CLASSIC_ENABLED && defined(CONFIG_GMF_EXAMPLE_A2DP_SOURCE)
            esp_bt_audio_media_stop(ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC);
#endif  /* CONFIG_BT_CLASSIC_ENABLED && defined(CONFIG_GMF_EXAMPLE_A2DP_SOURCE) */
        }
    }
    return ESP_GMF_ERR_OK;
}

static void setup_pipeline_bt2codec(esp_gmf_pool_handle_t pool)
{
    const char *name[] = {"aud_dec", "aud_asrc"};
    esp_gmf_pool_new_pipeline(pool, "io_bt", name, sizeof(name) / sizeof(char *), "io_codec_dev",
                              &s_stream_proc->bt2codec_pipe);
    esp_gmf_pipeline_set_event(s_stream_proc->bt2codec_pipe, bt2codec_pipe_event_cb, s_stream_proc);

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.thread.core = 0;
    cfg.thread.stack = 5120;
    cfg.thread.prio = 15;
    cfg.thread.stack_in_ext = true;
    cfg.name = "bt2codec_task";
    esp_gmf_task_init(&cfg, &s_stream_proc->bt2codec_task);

    esp_gmf_pipeline_bind_task(s_stream_proc->bt2codec_pipe, s_stream_proc->bt2codec_task);
}

static void setup_pipeline_codec2bt(esp_gmf_pool_handle_t pool)
{
    const char *name[] = {"aud_asrc", "ai_aec", "aud_asrc", "aud_enc"};
    esp_gmf_pool_new_pipeline(pool, "io_codec_dev", name, sizeof(name) / sizeof(char *), "io_bt",
                              &s_stream_proc->codec2bt_pipe);
    esp_gmf_pipeline_set_event(s_stream_proc->codec2bt_pipe, codec2bt_pipe_event_cb, s_stream_proc);

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.thread.core = 1;
    cfg.thread.stack = 5120;
    cfg.thread.prio = 15;
    cfg.thread.stack_in_ext = true;
    cfg.name = "codec2bt_task";
    esp_gmf_task_init(&cfg, &s_stream_proc->codec2bt_task);

    stream_proc_set_asrc_dest(s_stream_proc->codec2bt_pipe, 0, 8000, CODEC_ADC_CHANNELS, CODEC_ADC_CHANNELS,
                              s_stream_proc->codec2bt_input_asrc_weight, STREAM_PROC_ASRC_MAX_WEIGHT_LEN);

    esp_gmf_pipeline_bind_task(s_stream_proc->codec2bt_pipe, s_stream_proc->codec2bt_task);
}

static void setup_pipeline_local2bt(esp_gmf_pool_handle_t pool)
{
    const char *name[] = {"aud_dec", "aud_asrc", "aud_enc"};
    esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), "io_bt",
                              &s_stream_proc->local2bt_pipe);
    esp_gmf_pipeline_set_event(s_stream_proc->local2bt_pipe, local2bt_pipe_event_cb, s_stream_proc);

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.thread.core = 1;
    cfg.thread.stack = 5120;
    cfg.thread.prio = 15;
    cfg.thread.stack_in_ext = true;
    cfg.name = "local2bt_task";
    esp_gmf_task_init(&cfg, &s_stream_proc->local2bt_task);

    esp_gmf_pipeline_bind_task(s_stream_proc->local2bt_pipe, s_stream_proc->local2bt_task);
}

static void stream_proc_task(void *arg)
{
    stream_proc_ctx_t *stream_ctx = (stream_proc_ctx_t *)arg;
    stream_proc_cmd_t cmd = {0};

    while (true) {
        if (xQueueReceive(stream_ctx->cmd_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (cmd.action) {
            case STREAM_PROC_PIPELINE_PREPARE: {
                if (cmd.report_src_info) {
                    esp_gmf_pipeline_report_info(cmd.pipe, ESP_GMF_INFO_SOUND, &cmd.src_info, sizeof(cmd.src_info));
                }
                if (cmd.uri) {
                    esp_gmf_pipeline_set_in_uri(cmd.pipe, cmd.uri);
                }
#if STREAM_PROC_DUAL_BIS
                stream_proc_reset_bis_leg_rb(cmd.pipe);
#endif  /* STREAM_PROC_DUAL_BIS */
                esp_gmf_pipeline_loading_jobs(cmd.pipe);
                break;
            }
            case STREAM_PROC_PIPELINE_RUN:
                esp_gmf_pipeline_run(cmd.pipe);
                break;
            case STREAM_PROC_PIPELINE_STOP_RESET:
#if STREAM_PROC_DUAL_BIS
                stream_proc_apply_pipeline_stop_reset(cmd.pipe);
#else
                ESP_LOGI(TAG, "Reset pipeline %p", cmd.pipe);
                esp_gmf_pipeline_stop(cmd.pipe);
                esp_gmf_pipeline_reset(cmd.pipe);
#endif  /* STREAM_PROC_DUAL_BIS */
                break;
            case STREAM_PROC_PIPELINE_PLAY_NEXT:
                local2bt_play_next();
                break;
#if STREAM_PROC_DUAL_BIS
            case STREAM_PROC_BIS_LEG_CONFIG:
                stream_proc_dual_bis_config_leg(cmd.leg);
                break;
#endif  /* STREAM_PROC_DUAL_BIS */
            default:
                ESP_LOGW(TAG, "Unknown stream processor action %d", cmd.action);
                break;
        }
    }
}

static void setup_stream_proc_task(void)
{
    if (s_stream_proc->cmd_queue) {
        return;
    }

    s_stream_proc->cmd_queue = xQueueCreate(STREAM_PROC_CMD_QUEUE_SIZE, sizeof(stream_proc_cmd_t));
    if (s_stream_proc->cmd_queue == NULL) {
        ESP_LOGE(TAG, "Create stream processor command queue failed");
        return;
    }

    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(stream_proc_task, "stream_proc_task",
                                                     STREAM_PROC_TASK_STACK_SIZE, s_stream_proc,
                                                     STREAM_PROC_TASK_PRIO,
                                                     NULL, STREAM_PROC_TASK_CORE_ID,
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Create stream processor task failed");
        vQueueDelete(s_stream_proc->cmd_queue);
        s_stream_proc->cmd_queue = NULL;
    }
}

void stream_proc_init(esp_gmf_pool_handle_t pool)
{
    if (s_stream_proc) {
        return;
    }
    s_stream_proc = heap_caps_calloc(1, sizeof(*s_stream_proc), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_stream_proc == NULL) {
        ESP_LOGE(TAG, "Allocate stream processor context from PSRAM failed");
        return;
    }

    setup_pipeline_bt2codec(pool);
    setup_pipeline_codec2bt(pool);
    setup_pipeline_local2bt(pool);
#if STREAM_PROC_DUAL_BIS
    setup_pipeline_local2bt_dual_bis(pool);
#endif  /* STREAM_PROC_DUAL_BIS */
    setup_stream_proc_task();
}
