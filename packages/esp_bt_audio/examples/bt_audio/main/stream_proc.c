/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_gmf_task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_media.h"
#include "esp_bt_audio_stream.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_element.h"
#include "esp_gmf_err.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_io_bt.h"
#include "esp_gmf_asrc.h"
#if CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM
#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#include "esp_board_periph.h"
#include "esp_codec_dev.h"
#include "dev_audio_codec.h"
#include "esp_bt_audio_le_playback_sync.h"
#endif  /* CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM */

#include "stream_proc.h"
#include "codec_defs.h"

#define STREAM_PROC_ASRC_MAX_CH          4
#define STREAM_PROC_ASRC_MAX_WEIGHT_LEN  (STREAM_PROC_ASRC_MAX_CH * STREAM_PROC_ASRC_MAX_CH)
#define STREAM_PROC_CMD_QUEUE_SIZE       12
#define STREAM_PROC_TASK_STACK_SIZE      4096
#define STREAM_PROC_TASK_PRIO            20

typedef enum {
    STREAM_PROC_PIPELINE_PREPARE,
    STREAM_PROC_PIPELINE_RUN,
    STREAM_PROC_PIPELINE_STOP_RESET,
} stream_proc_pipeline_action_t;

typedef struct {
    stream_proc_pipeline_action_t  action;
    esp_gmf_pipeline_handle_t      pipe;
    const char                    *uri;
    bool                           report_codec_input_info;
} stream_proc_cmd_t;

static const char *TAG = "STREAM_PROC";

/* Playlist configuration */
static const char *playlist[] = {
    "file://sdcard/media0.mp3",
    "file://sdcard/media1.mp3",
    "file://sdcard/media2.mp3",
};
static const size_t playlist_len = sizeof(playlist) / sizeof(playlist[0]);
static size_t playlist_cur_index = 0;

/* Pipeline handles */
static esp_gmf_task_handle_t bt2codec_task = NULL;
static esp_gmf_pipeline_handle_t bt2codec_pipe = NULL;

static esp_gmf_task_handle_t codec2bt_task = NULL;
static esp_gmf_pipeline_handle_t codec2bt_pipe = NULL;

static esp_gmf_task_handle_t local2bt_task = NULL;
static esp_gmf_pipeline_handle_t local2bt_pipe = NULL;
static esp_bt_audio_stream_handle_t local2bt_stream = NULL;

static QueueHandle_t stream_proc_cmd_queue = NULL;

static float bt2codec_asrc_weight[STREAM_PROC_ASRC_MAX_WEIGHT_LEN];
static float codec2bt_input_asrc_weight[STREAM_PROC_ASRC_MAX_WEIGHT_LEN];
static float codec2bt_output_asrc_weight[STREAM_PROC_ASRC_MAX_WEIGHT_LEN];
static float local2bt_asrc_weight[STREAM_PROC_ASRC_MAX_WEIGHT_LEN];

static bool stream_proc_post_cmd(const stream_proc_cmd_t *cmd)
{
    if (stream_proc_cmd_queue == NULL) {
        ESP_LOGE(TAG, "Stream processor task is not initialized");
        return false;
    }
    if (xQueueSend(stream_proc_cmd_queue, cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Stream processor command queue full, drop action %d", cmd->action);
        return false;
    }
    return true;
}

static void stream_proc_post_pipeline_action(esp_gmf_pipeline_handle_t pipe, stream_proc_pipeline_action_t action)
{
    stream_proc_cmd_t cmd = {
        .action = action,
        .pipe = pipe,
    };
    stream_proc_post_cmd(&cmd);
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

#if CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM
#define STREAM_PROC_CLK_SYNC_DIFF_THRESHOLD  2
#define STREAM_PROC_CLK_SYNC_MON_QUEUE_SIZE  10
#define STREAM_PROC_CLK_SYNC_MON_TASK_STACK  4096
#define STREAM_PROC_CLK_SYNC_MON_TASK_PRIO   5

static esp_bt_audio_le_playback_sync_handle_t playback_sync = NULL;
static esp_bt_audio_le_clk_sync_handle_t clk_sync = NULL;
static QueueHandle_t clk_sync_monitor_queue = NULL;
static TaskHandle_t clk_sync_monitor_task = NULL;
static volatile bool clk_sync_monitor_task_running = false;

static esp_err_t stream_proc_open_dac(dev_audio_codec_handles_t *dac_handle)
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
    ESP_LOGI(TAG, "get i2s[%s:%s] handle %p", name, codec_config->i2s_cfg.name, ch);
    return ch;
}

static void stream_proc_clk_sync_monitor_task(void *arg)
{
    (void)arg;
    esp_bt_audio_le_clk_sync_msg_t msg = {0};

    while (clk_sync_monitor_task_running) {
        if (xQueueReceive(clk_sync_monitor_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!clk_sync_monitor_task_running) {
            break;
        }
        ESP_LOGW(TAG, "Clock sync monitor: diff=%ld, fifo=%" PRIu32 ", bck=%" PRIu32,
                 (long)msg.diff, msg.fifo_cnt, msg.bck_cnt);
    }
    clk_sync_monitor_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t stream_proc_ensure_clk_sync_monitor(void)
{
    if (!clk_sync_monitor_queue) {
        clk_sync_monitor_queue = xQueueCreate(STREAM_PROC_CLK_SYNC_MON_QUEUE_SIZE,
                                              sizeof(esp_bt_audio_le_clk_sync_msg_t));
        ESP_RETURN_ON_FALSE(clk_sync_monitor_queue, ESP_ERR_NO_MEM, TAG, "Create clock sync monitor queue failed");
    }

    if (!clk_sync_monitor_task) {
        clk_sync_monitor_task_running = true;
        BaseType_t ret = xTaskCreate(stream_proc_clk_sync_monitor_task, "clk_sync_mon",
                                     STREAM_PROC_CLK_SYNC_MON_TASK_STACK, NULL,
                                     STREAM_PROC_CLK_SYNC_MON_TASK_PRIO, &clk_sync_monitor_task);
        if (ret != pdPASS) {
            clk_sync_monitor_task_running = false;
            ESP_LOGE(TAG, "Create clock sync monitor task failed");
            return ESP_ERR_NO_MEM;
        }
    }

    xQueueReset(clk_sync_monitor_queue);
    return ESP_OK;
}

static void stream_proc_deinit_clk_sync_monitor(void)
{
    if (clk_sync_monitor_task) {
        esp_bt_audio_le_clk_sync_msg_t msg = {0};
        clk_sync_monitor_task_running = false;
        if (clk_sync_monitor_queue) {
            xQueueSend(clk_sync_monitor_queue, &msg, 0);
        }
        for (uint8_t i = 0; i < 10 && clk_sync_monitor_task; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (clk_sync_monitor_task) {
            vTaskDelete(clk_sync_monitor_task);
            clk_sync_monitor_task = NULL;
        }
    }

    if (clk_sync_monitor_queue) {
        vQueueDelete(clk_sync_monitor_queue);
        clk_sync_monitor_queue = NULL;
    }
    clk_sync_monitor_task_running = false;
}

static void stream_proc_deinit_playback_sync(void)
{
    if (!playback_sync) {
        return;
    }

    esp_err_t ret = esp_bt_audio_le_playback_sync_disable(playback_sync);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Playback sync disable failed: %s", esp_err_to_name(ret));
    }
    ret = esp_bt_audio_le_playback_sync_deinit(playback_sync);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Playback sync deinit failed: %s", esp_err_to_name(ret));
    }
    playback_sync = NULL;
}

static void stream_proc_deinit_clk_sync(void)
{
    if (!clk_sync) {
        return;
    }

    esp_err_t ret = esp_bt_audio_le_clk_sync_disable(clk_sync);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Clock sync disable failed: %s", esp_err_to_name(ret));
    }
    ret = esp_bt_audio_le_clk_sync_deinit(clk_sync);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Clock sync deinit failed: %s", esp_err_to_name(ret));
    }
    clk_sync = NULL;
    stream_proc_deinit_clk_sync_monitor();
}

static void stream_proc_prepare_clk_sync(esp_bt_audio_stream_handle_t stream)
{
    if (clk_sync) {
        return;
    }

    uint16_t iso_interval = 0;
    esp_err_t ret = esp_bt_audio_stream_get_iso_interval(stream, &iso_interval);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Get ISO interval failed: %s", esp_err_to_name(ret));
        return;
    }

    uint64_t ideal_count = ((uint64_t)CODEC_DAC_SAMPLE_RATE * CODEC_DAC_CHANNELS * iso_interval + 500000U) / 1000000U;
    if (ideal_count == 0 || ideal_count > UINT32_MAX) {
        ESP_LOGW(TAG, "Invalid clock sync ideal count: %" PRIu64, ideal_count);
        return;
    }

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

    ret = esp_bt_audio_le_clk_sync_init(tx_handle, (uint32_t)ideal_count, STREAM_PROC_CLK_SYNC_DIFF_THRESHOLD,
                                        true, clk_sync_monitor_queue, &clk_sync);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Clock sync init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_bt_audio_le_clk_sync_enable(clk_sync);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Clock sync enable failed: %s", esp_err_to_name(ret));
        esp_bt_audio_le_clk_sync_deinit(clk_sync);
        clk_sync = NULL;
        return;
    }

    ESP_LOGI(TAG, "Clock sync enabled, iso_interval=%u(us), ideal_count=%lu",
             iso_interval, (uint32_t)ideal_count);
}

static void stream_proc_prepare_playback_sync(void)
{
    if (playback_sync) {
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
    ret = esp_bt_audio_le_playback_sync_init(tx_handle, &playback_sync);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Playback sync init failed: %s", esp_err_to_name(ret));
        stream_proc_open_dac(dac_handle);
        return;
    }

    ret = esp_bt_audio_le_playback_sync_enable(playback_sync);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Playback sync enable failed: %s", esp_err_to_name(ret));
        esp_bt_audio_le_playback_sync_deinit(playback_sync);
        playback_sync = NULL;
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

static void stream_proc_prepare_clk_sync(esp_bt_audio_stream_handle_t stream)
{
    (void)stream;
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
#endif  /* CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM */

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
    if (local2bt_pipe == NULL) {
        ESP_LOGE(TAG, "Local to BT pipeline is not initialized");
        return;
    }
    if (local2bt_stream == NULL) {
        ESP_LOGE(TAG, "Local to BT stream is not initialized");
        return;
    }
    esp_gmf_pipeline_stop(local2bt_pipe);
    esp_gmf_pipeline_reset(local2bt_pipe);
    esp_gmf_io_bt_set_stream(ESP_GMF_PIPELINE_GET_OUT_INSTANCE(local2bt_pipe), local2bt_stream);
    esp_gmf_pipeline_set_in_uri(local2bt_pipe, uri);
    ESP_LOGI(TAG, "A2DP Source play: %s (index %d)", uri, playlist_cur_index);
    esp_gmf_pipeline_loading_jobs(local2bt_pipe);
    esp_gmf_pipeline_run(local2bt_pipe);
}

void local2bt_play_next(void)
{
    if (local2bt_stream == NULL) {
        ESP_LOGE(TAG, "Local to BT stream is not initialized");
        return;
    }
    playlist_cur_index = (playlist_cur_index + 1) % playlist_len;
    local2bt_play(playlist[playlist_cur_index]);
}

void local2bt_play_prev(void)
{
    if (local2bt_stream == NULL) {
        ESP_LOGE(TAG, "Local to BT stream is not initialized");
        return;
    }
    playlist_cur_index = (playlist_cur_index + playlist_len - 1) % playlist_len;
    local2bt_play(playlist[playlist_cur_index]);
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
    ESP_LOGI(TAG, "Codec Info: type=%d, bits=%d, channels=%d, sample_rate=%d, cfg_size=%d, codec_cfg=%p",
             codec_info.codec_type,
             codec_info.bits,
             codec_info.channels,
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
        user_d->pipe = bt2codec_pipe;
        esp_gmf_io_bt_set_stream(ESP_GMF_PIPELINE_GET_IN_INSTANCE(user_d->pipe), stream);
        esp_audio_simple_dec_cfg_t simple_dec_cfg = {
            .dec_type = codec_info.codec_type == ESP_BT_AUDIO_STREAM_CODEC_SBC ? ESP_AUDIO_TYPE_SBC : ESP_AUDIO_TYPE_LC3,
            .dec_cfg = codec_info.codec_cfg,
            .cfg_size = codec_info.cfg_size,
        };
        esp_gmf_audio_dec_reconfig(user_d->pipe->head_el, &simple_dec_cfg);

        stream_proc_set_asrc_dest(user_d->pipe, 0, CODEC_DAC_SAMPLE_RATE, __builtin_popcount(codec_info.channels),
                                  CODEC_DAC_CHANNELS, bt2codec_asrc_weight, STREAM_PROC_ASRC_MAX_WEIGHT_LEN);

        stream_proc_prepare_playback_sync();
        stream_proc_post_pipeline_action(user_d->pipe, STREAM_PROC_PIPELINE_PREPARE);
    } else {
        uint8_t output_asrc_index = 0;
        uint32_t context = 0;
        bool report_codec_input_info = false;
        const char *uri = NULL;
        if (esp_bt_audio_stream_get_context(stream, &context) != ESP_OK) {
            ESP_LOGE(TAG, "Get stream context failed, stream=%p", stream);
        }
        if (context == ESP_BT_AUDIO_STREAM_CONTEXT_MEDIA) {
            ESP_LOGI(TAG, "Prepare local to bt pipeline");
            user_d->pipe = local2bt_pipe;
            local2bt_stream = stream;

            esp_audio_simple_dec_cfg_t simple_dec_cfg = {
                .dec_type = ESP_AUDIO_TYPE_MP3,
                .dec_cfg = NULL,
                .cfg_size = 0,
            };
            esp_gmf_audio_dec_reconfig(user_d->pipe->head_el, &simple_dec_cfg);
            uri = playlist[playlist_cur_index];
            ESP_LOGI(TAG, "Set media file: %s (index %d)", playlist[playlist_cur_index], playlist_cur_index);
        } else {
            ESP_LOGI(TAG, "Prepare codec to bt pipeline");
            user_d->pipe = codec2bt_pipe;
            output_asrc_index = 1;
            report_codec_input_info = true;
        }
        esp_gmf_io_bt_set_stream(ESP_GMF_PIPELINE_GET_OUT_INSTANCE(user_d->pipe), stream);
        uint8_t output_src_ch = context == ESP_BT_AUDIO_STREAM_CONTEXT_MEDIA ? 2 : 1;
        float *asrc_weight = context == ESP_BT_AUDIO_STREAM_CONTEXT_MEDIA ? local2bt_asrc_weight :
                             codec2bt_output_asrc_weight;
        stream_proc_set_asrc_dest(user_d->pipe, output_asrc_index, codec_info.sample_rate, output_src_ch,
                                  __builtin_popcount(codec_info.channels), asrc_weight,
                                  STREAM_PROC_ASRC_MAX_WEIGHT_LEN);

        esp_audio_enc_config_t enc_cfg = {
            .type = codec_info.codec_type == ESP_BT_AUDIO_STREAM_CODEC_SBC ? ESP_AUDIO_TYPE_SBC : ESP_AUDIO_TYPE_LC3,
            .cfg = codec_info.codec_cfg,
            .cfg_sz = codec_info.cfg_size,
        };
        esp_gmf_audio_enc_reconfig(user_d->pipe->last_el, &enc_cfg);
        stream_proc_cmd_t cmd = {
            .action = STREAM_PROC_PIPELINE_PREPARE,
            .pipe = user_d->pipe,
            .uri = uri,
            .report_codec_input_info = report_codec_input_info,
        };
        stream_proc_post_cmd(&cmd);
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
                    stream_proc_prepare_clk_sync(stream);
                }
                stream_proc_post_pipeline_action(user_d->pipe, STREAM_PROC_PIPELINE_RUN);
            } else {
                ESP_LOGE(TAG, "Stream user data not prepared for stream %p", stream);
            }
            break;
        }
        case ESP_BT_AUDIO_STREAM_STATE_STOPPED: {
            stream_user_data_t *user_d = NULL;
            esp_bt_audio_stream_get_local_data(stream, (void **)&user_d);
            if (user_d && user_d->pipe) {
                ESP_LOGI(TAG, "Schedule reset pipeline %p", user_d->pipe);
                stream_proc_post_pipeline_action(user_d->pipe, STREAM_PROC_PIPELINE_STOP_RESET);
            }
            if (dir == ESP_BT_AUDIO_STREAM_DIR_SINK) {
                stream_proc_deinit_clk_sync();
                stream_proc_deinit_playback_sync();
            }
            break;
        }
        case ESP_BT_AUDIO_STREAM_STATE_RELEASED: {
            stream_user_data_t *user_d = NULL;
            esp_bt_audio_stream_get_local_data(stream, (void **)&user_d);
            if (dir == ESP_BT_AUDIO_STREAM_DIR_SINK) {
                stream_proc_deinit_clk_sync();
                stream_proc_deinit_playback_sync();
            }
            if (user_d) {
                stream_proc_destroy(user_d);
                esp_bt_audio_stream_set_local_data(stream, NULL);
            }
            if (local2bt_stream == stream) {
                local2bt_stream = NULL;
            }
            break;
        }
        default:
            break;
    }
}

static esp_gmf_err_t bt2codec_pipe_event_cb(esp_gmf_event_pkt_t *pkt, void *ctx)
{
    if (pkt == NULL) {
        return ESP_GMF_ERR_OK;
    }
    if (pkt->type == ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        ESP_LOGI(TAG, "[bt2codec pipeline] state => %s(%d)", gmf_state_to_str(pkt->sub), pkt->sub);
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t codec2bt_pipe_event_cb(esp_gmf_event_pkt_t *pkt, void *ctx)
{
    if (pkt == NULL) {
        return ESP_GMF_ERR_OK;
    }
    if (pkt->type == ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        ESP_LOGI(TAG, "[codec2bt pipeline] state => %s(%d)", gmf_state_to_str(pkt->sub), pkt->sub);
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t local2bt_pipe_event_cb(esp_gmf_event_pkt_t *pkt, void *ctx)
{
    if (pkt == NULL) {
        return ESP_GMF_ERR_OK;
    }
    if (pkt->type == ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        ESP_LOGI(TAG, "[a2dp source pipeline] state => %s(%d)", gmf_state_to_str(pkt->sub), pkt->sub);
        if (pkt->sub == ESP_GMF_EVENT_STATE_FINISHED) {
            ESP_LOGI(TAG, "A2DP Source finished");
            if (local2bt_stream) {
                local2bt_play_next();
            }
        } else if (pkt->sub == ESP_GMF_EVENT_STATE_ERROR) {
            ESP_LOGE(TAG, "A2DP Source error");
            esp_bt_audio_media_stop(ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC);
        }
    }
    return ESP_GMF_ERR_OK;
}

static void setup_pipeline_bt2codec(esp_gmf_pool_handle_t pool)
{
    const char *name[] = {"aud_dec", "aud_asrc"};
    esp_gmf_pool_new_pipeline(pool, "io_bt", name, sizeof(name) / sizeof(char *), "io_codec_dev", &bt2codec_pipe);
    esp_gmf_pipeline_set_event(bt2codec_pipe, bt2codec_pipe_event_cb, NULL);

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.thread.core = 0;
    cfg.thread.stack = 5120;
    cfg.thread.prio = 15;
    cfg.thread.stack_in_ext = true;
    cfg.name = "bt2codec_task";
    esp_gmf_task_init(&cfg, &bt2codec_task);

    esp_gmf_pipeline_bind_task(bt2codec_pipe, bt2codec_task);
}

static void setup_pipeline_codec2bt(esp_gmf_pool_handle_t pool)
{
    const char *name[] = {"aud_asrc", "ai_aec", "aud_asrc", "aud_enc"};
    esp_gmf_pool_new_pipeline(pool, "io_codec_dev", name, sizeof(name) / sizeof(char *), "io_bt", &codec2bt_pipe);
    esp_gmf_pipeline_set_event(codec2bt_pipe, codec2bt_pipe_event_cb, NULL);

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.thread.core = 1;
    cfg.thread.stack = 5120;
    cfg.thread.prio = 15;
    cfg.thread.stack_in_ext = true;
    cfg.name = "codec2bt_task";
    esp_gmf_task_init(&cfg, &codec2bt_task);

    stream_proc_set_asrc_dest(codec2bt_pipe, 0, 8000, CODEC_ADC_CHANNELS, CODEC_ADC_CHANNELS,
                              codec2bt_input_asrc_weight, STREAM_PROC_ASRC_MAX_WEIGHT_LEN);

    esp_gmf_pipeline_bind_task(codec2bt_pipe, codec2bt_task);
}

static void setup_pipeline_local2bt(esp_gmf_pool_handle_t pool)
{
    const char *name[] = {"aud_dec", "aud_asrc", "aud_enc"};
    esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), "io_bt", &local2bt_pipe);
    esp_gmf_pipeline_set_event(local2bt_pipe, local2bt_pipe_event_cb, NULL);

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.thread.core = 1;
    cfg.thread.stack = 5120;
    cfg.thread.prio = 15;
    cfg.thread.stack_in_ext = true;
    cfg.name = "local2bt_task";
    esp_gmf_task_init(&cfg, &local2bt_task);

    esp_gmf_pipeline_bind_task(local2bt_pipe, local2bt_task);
}

static void stream_proc_task(void *arg)
{
    (void)arg;
    stream_proc_cmd_t cmd = {0};

    while (true) {
        if (xQueueReceive(stream_proc_cmd_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (cmd.action) {
            case STREAM_PROC_PIPELINE_PREPARE: {
                if (cmd.report_codec_input_info) {
                    esp_gmf_info_sound_t info = {
                        .sample_rates = CODEC_ADC_SAMPLE_RATE,
                        .channels = CODEC_ADC_CHANNELS,
                        .bits = CODEC_ADC_BITS_PER_SAMPLE,
                    };
                    esp_gmf_pipeline_report_info(cmd.pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));
                }
                if (cmd.uri) {
                    esp_gmf_pipeline_set_in_uri(cmd.pipe, cmd.uri);
                }
                esp_gmf_pipeline_loading_jobs(cmd.pipe);
                break;
            }
            case STREAM_PROC_PIPELINE_RUN:
                esp_gmf_pipeline_run(cmd.pipe);
                break;
            case STREAM_PROC_PIPELINE_STOP_RESET:
                ESP_LOGI(TAG, "Reset pipeline %p", cmd.pipe);
                esp_gmf_pipeline_stop(cmd.pipe);
                esp_gmf_pipeline_reset(cmd.pipe);
                break;
            default:
                ESP_LOGW(TAG, "Unknown stream processor action %d", cmd.action);
                break;
        }
    }
}

static void setup_stream_proc_task(void)
{
    if (stream_proc_cmd_queue) {
        return;
    }

    stream_proc_cmd_queue = xQueueCreate(STREAM_PROC_CMD_QUEUE_SIZE, sizeof(stream_proc_cmd_t));
    if (stream_proc_cmd_queue == NULL) {
        ESP_LOGE(TAG, "Create stream processor command queue failed");
        return;
    }

    BaseType_t ret = xTaskCreate(stream_proc_task, "stream_proc_task", STREAM_PROC_TASK_STACK_SIZE, NULL,
                                 STREAM_PROC_TASK_PRIO, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Create stream processor task failed");
        vQueueDelete(stream_proc_cmd_queue);
        stream_proc_cmd_queue = NULL;
    }
}

void stream_proc_init(esp_gmf_pool_handle_t pool)
{
    setup_pipeline_bt2codec(pool);
    setup_pipeline_codec2bt(pool);
    setup_pipeline_local2bt(pool);
    setup_stream_proc_task();
}
