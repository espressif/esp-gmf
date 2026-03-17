/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_io.h"
#include "esp_gmf_port.h"
#include "esp_embed_tone.h"
#include "esp_gmf_io_embed_flash.h"
#include "esp_gmf_audio_helper.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_fade.h"
#include "gmf_loader_setup_defaults.h"
#include "esp_gmf_io_codec_dev.h"

#include "audio_multi_source_player.h"
#include "audio_config.h"

static const char *TAG = "AUDIO_MULTI_SRC_PLAYER";

typedef struct {
    esp_gmf_pipeline_handle_t  main_pipeline;
    esp_gmf_pipeline_handle_t  flash_pipeline;
    esp_gmf_pool_handle_t      pool;
    esp_gmf_task_handle_t      main_task;
    esp_gmf_task_handle_t      flash_task;
    EventGroupHandle_t         main_sync_evt;
    EventGroupHandle_t         flash_sync_evt;
    void                      *playback_handle;

    audio_source_t  current_source;
    audio_state_t   current_state;
    audio_source_t  saved_source;
    audio_state_t   saved_state;
    bool            flash_playing;
    bool            initialized;
} audio_multi_source_player_ctx_t;

static audio_multi_source_player_ctx_t g_audio_ctx = {0};

static esp_gmf_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx);
static esp_gmf_err_t _flash_pipeline_event(esp_gmf_event_pkt_t *event, void *ctx);
static audio_ms_player_err_t switch_audio_source(audio_source_t new_source);
static audio_ms_player_err_t play_flash_tone(int tone_index);
static audio_ms_player_err_t restore_original_playback(void);
static esp_gmf_err_t configure_flash_pipeline_for_tone(esp_gmf_pipeline_handle_t pipeline, int tone_index);

static const char *source_names[] = {
    [AUDIO_SRC_HTTP]   = "HTTP",
    [AUDIO_SRC_SDCARD] = "SD card",
    [AUDIO_SRC_FLASH]  = "Flash"};

static const char *state_names[] = {
    [AUDIO_STATE_IDLE]    = "Idle",
    [AUDIO_STATE_PLAYING] = "Playing",
    [AUDIO_STATE_PAUSED]  = "Paused",
    [AUDIO_STATE_STOPPED] = "Stopped",
    [AUDIO_STATE_ERROR]   = "Error"};

static esp_gmf_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGD(TAG, "RECV Pipeline EVT: type: %x, sub: %s",
             event->type, esp_gmf_event_get_state_str(event->sub));

    if (event->type == ESP_GMF_EVT_TYPE_REPORT_INFO && event->sub == ESP_GMF_INFO_SOUND) {
        esp_gmf_info_sound_t *info = (esp_gmf_info_sound_t *)event->payload;
        if (info && g_audio_ctx.main_pipeline) {
            ESP_LOGI(TAG, "Music info: sample_rates=%d, bits=%d, ch=%d",
                     info->sample_rates, info->bits, info->channels);
        }
    }

    if ((event->sub == ESP_GMF_EVENT_STATE_STOPPED) ||
        (event->sub == ESP_GMF_EVENT_STATE_FINISHED) ||
        (event->sub == ESP_GMF_EVENT_STATE_ERROR)) {
        if (ctx) {
            xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_BIT);
        }
        g_audio_ctx.current_state = AUDIO_STATE_STOPPED;
    } else if (event->sub == ESP_GMF_EVENT_STATE_RUNNING) {
        g_audio_ctx.current_state = AUDIO_STATE_PLAYING;
    } else if (event->sub == ESP_GMF_EVENT_STATE_PAUSED) {
        g_audio_ctx.current_state = AUDIO_STATE_PAUSED;
    }

    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _flash_pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGD(TAG, "RECV Flash Pipeline EVT: type: %x, sub: %s",
             event->type, esp_gmf_event_get_state_str(event->sub));

    if ((event->sub == ESP_GMF_EVENT_STATE_STOPPED) ||
        (event->sub == ESP_GMF_EVENT_STATE_FINISHED) ||
        (event->sub == ESP_GMF_EVENT_STATE_ERROR)) {

        if (ctx) {
            xEventGroupSetBits((EventGroupHandle_t)ctx, FLASH_PIPELINE_BLOCK_BIT);
        }
        g_audio_ctx.flash_playing = false;

        if (event->sub == ESP_GMF_EVENT_STATE_FINISHED ||
            event->sub == ESP_GMF_EVENT_STATE_STOPPED) {
            ESP_LOGI(TAG, "Flash playback finished, restoring original playback");
            restore_original_playback();
        }
    }

    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t configure_flash_pipeline_for_tone(esp_gmf_pipeline_handle_t pipeline, int tone_index)
{
    esp_gmf_err_t ret = esp_gmf_pipeline_set_in_uri(pipeline, esp_embed_tone_url[tone_index]);
    if (ret != ESP_GMF_ERR_OK) {
        return ret;
    }
    esp_gmf_io_handle_t in_io = NULL;
    esp_gmf_pipeline_get_in(pipeline, &in_io);
    esp_gmf_io_embed_flash_set_context(in_io, (embed_item_info_t *)&g_esp_embed_tone[0], ESP_EMBED_TONE_URL_MAX);

    esp_gmf_element_handle_t dec_el = NULL;
    ret = esp_gmf_pipeline_get_el_by_name(pipeline, "aud_dec", &dec_el);
    if (ret != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to get flash decoder element: %d", ret);
        return ret;
    }
    esp_gmf_info_sound_t info = {0};
    ret = esp_gmf_audio_helper_get_audio_type_by_uri(esp_embed_tone_url[tone_index], &info.format_id);
    if (ret != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to get flash audio type: %d", ret);
        return ret;
    }
    ret = esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info);
    if (ret != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure flash decoder: %d", ret);
        return ret;
    }
    return ESP_GMF_ERR_OK;
}

audio_ms_player_err_t audio_multi_source_player_init(const audio_ms_player_config_t *config)
{
    if (config == NULL) {
        return AUDIO_MS_PLAYER_ERR_INVALID_ARG;
    }
    if (g_audio_ctx.initialized) {
        ESP_LOGW(TAG, "Audio manager already initialized");
        return AUDIO_MS_PLAYER_OK;
    }

    ESP_LOGI(TAG, "Initializing audio manager");

    memset(&g_audio_ctx, 0, sizeof(audio_multi_source_player_ctx_t));
    g_audio_ctx.playback_handle = config->playback_handle;

    esp_gmf_err_t ret = esp_gmf_pool_init(&g_audio_ctx.pool);
    if (ret != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to initialize GMF pool: %d", ret);
        return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
    }

    gmf_loader_setup_io_default(g_audio_ctx.pool);
    gmf_loader_setup_audio_codec_default(g_audio_ctx.pool);
    gmf_loader_setup_audio_effects_default(g_audio_ctx.pool);

    const char *pipeline_elements[] = {"aud_dec", "aud_fade", "aud_bit_cvt", "aud_rate_cvt", "aud_ch_cvt"};
    ret = esp_gmf_pool_new_pipeline(g_audio_ctx.pool, "io_http", pipeline_elements,
                                    sizeof(pipeline_elements) / sizeof(char *),
                                    "io_codec_dev", &g_audio_ctx.main_pipeline);
    if (ret != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to create main pipeline: %d", ret);
        goto cleanup_pool;
    }

    esp_gmf_io_codec_dev_set_dev(ESP_GMF_PIPELINE_GET_OUT_INSTANCE(g_audio_ctx.main_pipeline),
                                 g_audio_ctx.playback_handle);

    esp_gmf_task_cfg_t task_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    task_cfg.name = "audio_main_task";
    task_cfg.thread.stack_in_ext = true;
    ret = esp_gmf_task_init(&task_cfg, &g_audio_ctx.main_task);
    if (ret != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to create main task: %d", ret);
        goto cleanup_pipeline;
    }

    esp_gmf_task_set_timeout(g_audio_ctx.main_task, PIPELINE_TASK_TIMEOUT_MS);
    esp_gmf_pipeline_bind_task(g_audio_ctx.main_pipeline, g_audio_ctx.main_task);

    g_audio_ctx.main_sync_evt = xEventGroupCreate();
    if (!g_audio_ctx.main_sync_evt) {
        ESP_LOGE(TAG, "Failed to create main sync event group");
        goto cleanup_task;
    }

    esp_gmf_pipeline_set_event(g_audio_ctx.main_pipeline, _pipeline_event, g_audio_ctx.main_sync_evt);

    g_audio_ctx.current_source = AUDIO_SRC_HTTP;
    g_audio_ctx.current_state = AUDIO_STATE_IDLE;
    g_audio_ctx.saved_source = AUDIO_SRC_HTTP;
    g_audio_ctx.saved_state = AUDIO_STATE_IDLE;
    g_audio_ctx.flash_playing = false;
    g_audio_ctx.initialized = true;

    ESP_LOGI(TAG, "Audio manager initialized successfully");
    return AUDIO_MS_PLAYER_OK;

cleanup_task:
    esp_gmf_task_deinit(g_audio_ctx.main_task);
    g_audio_ctx.main_task = NULL;

cleanup_pipeline:
    esp_gmf_pipeline_destroy(g_audio_ctx.main_pipeline);
    g_audio_ctx.main_pipeline = NULL;

cleanup_pool:
    if (g_audio_ctx.pool) {
        gmf_loader_teardown_audio_effects_default(g_audio_ctx.pool);
        gmf_loader_teardown_audio_codec_default(g_audio_ctx.pool);
        gmf_loader_teardown_io_default(g_audio_ctx.pool);
        esp_gmf_pool_deinit(g_audio_ctx.pool);
        g_audio_ctx.pool = NULL;
    }
    return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
}

audio_ms_player_err_t audio_multi_source_player_deinit(void)
{
    if (!g_audio_ctx.initialized) {
        ESP_LOGW(TAG, "Audio manager not initialized");
        return AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED;
    }

    ESP_LOGI(TAG, "Deinitializing audio manager");

    audio_multi_source_player_stop();

    if (g_audio_ctx.flash_pipeline) {
        esp_gmf_pipeline_stop(g_audio_ctx.flash_pipeline);
        esp_gmf_pipeline_destroy(g_audio_ctx.flash_pipeline);
    }
    if (g_audio_ctx.flash_task) {
        esp_gmf_task_deinit(g_audio_ctx.flash_task);
    }
    if (g_audio_ctx.flash_sync_evt) {
        vEventGroupDelete(g_audio_ctx.flash_sync_evt);
    }
    if (g_audio_ctx.main_pipeline) {
        esp_gmf_pipeline_destroy(g_audio_ctx.main_pipeline);
    }
    if (g_audio_ctx.main_task) {
        esp_gmf_task_deinit(g_audio_ctx.main_task);
    }
    if (g_audio_ctx.main_sync_evt) {
        vEventGroupDelete(g_audio_ctx.main_sync_evt);
    }
    if (g_audio_ctx.pool) {
        gmf_loader_teardown_audio_effects_default(g_audio_ctx.pool);
        gmf_loader_teardown_audio_codec_default(g_audio_ctx.pool);
        gmf_loader_teardown_io_default(g_audio_ctx.pool);
        esp_gmf_pool_deinit(g_audio_ctx.pool);
        g_audio_ctx.pool = NULL;
    }
    memset(&g_audio_ctx, 0, sizeof(audio_multi_source_player_ctx_t));

    ESP_LOGI(TAG, "Audio manager deinitialized successfully");
    return AUDIO_MS_PLAYER_OK;
}

static audio_ms_player_err_t switch_audio_source(audio_source_t new_source)
{
    if (!g_audio_ctx.initialized) {
        return AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED;
    }

    if (new_source >= AUDIO_SRC_MAX) {
        return AUDIO_MS_PLAYER_ERR_UNKNOWN_SOURCE;
    }

    if (new_source == g_audio_ctx.current_source &&
        g_audio_ctx.current_state == AUDIO_STATE_PLAYING) {
        return AUDIO_MS_PLAYER_OK;
    }

    esp_gmf_err_t ret = ESP_GMF_ERR_OK;

    if (g_audio_ctx.current_state == AUDIO_STATE_PLAYING && g_audio_ctx.main_pipeline) {
        esp_gmf_pipeline_stop(g_audio_ctx.main_pipeline);
    }

    if (g_audio_ctx.main_pipeline) {
        esp_gmf_pipeline_reset(g_audio_ctx.main_pipeline);
    }

    esp_gmf_io_handle_t in_io = NULL;
    esp_gmf_pipeline_get_in(g_audio_ctx.main_pipeline, &in_io);

    const char *in_str = NULL;
    const char *uri = NULL;

    switch (new_source) {
        case AUDIO_SRC_HTTP:
            in_str = "io_http";
            uri = HTTP_AUDIO_URL;
            break;
        case AUDIO_SRC_SDCARD:
            in_str = "io_file";
            uri = SDCARD_AUDIO_PATH;
            break;
        default:
            ESP_LOGE(TAG, "Unknown audio source: %d", new_source);
            return AUDIO_MS_PLAYER_ERR_UNKNOWN_SOURCE;
    }

    if (in_str && ((in_io == NULL) || (strcasecmp(OBJ_GET_TAG(in_io), in_str) != 0))) {
        esp_gmf_io_handle_t new_io = NULL;
        ret = esp_gmf_pool_new_io(g_audio_ctx.pool, in_str, ESP_GMF_IO_DIR_READER, &new_io);
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to create IN IO instance: %d", ret);
            return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
        }

        if (in_io) {
            esp_gmf_element_unregister_in_port(g_audio_ctx.main_pipeline->head_el, NULL);
            esp_gmf_obj_delete(in_io);
        }

        ret = esp_gmf_pipeline_replace_in(g_audio_ctx.main_pipeline, new_io);
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to replace input IO: %d", ret);
            return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
        }

        esp_gmf_io_type_t io_type = 0;
        esp_gmf_io_get_type(new_io, &io_type);
        esp_gmf_port_handle_t in_port = NULL;

        if (io_type == ESP_GMF_IO_TYPE_BYTE) {
            in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_io_acquire_read, esp_gmf_io_release_read, NULL, new_io,
                                               (ESP_GMF_ELEMENT_GET(g_audio_ctx.main_pipeline->head_el)->in_attr.data_size), ESP_GMF_MAX_DELAY);
        } else if (io_type == ESP_GMF_IO_TYPE_BLOCK) {
            in_port = NEW_ESP_GMF_PORT_IN_BLOCK(esp_gmf_io_acquire_read, esp_gmf_io_release_read, NULL, new_io,
                                                (ESP_GMF_ELEMENT_GET(g_audio_ctx.main_pipeline->head_el)->in_attr.data_size), ESP_GMF_MAX_DELAY);
        } else {
            ESP_LOGE(TAG, "Incorrect IN type: %d", io_type);
            return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
        }

        if (!in_port) {
            ESP_LOGE(TAG, "Failed to create input port");
            return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
        }

        ret = esp_gmf_element_register_in_port(g_audio_ctx.main_pipeline->head_el, in_port);
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to register input port: %d", ret);
            return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
        }
    }

    if (uri) {
        ret = esp_gmf_pipeline_set_in_uri(g_audio_ctx.main_pipeline, uri);
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to set input URI: %d", ret);
            return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
        }
    }

    esp_gmf_element_handle_t dec_el = NULL;
    ret = esp_gmf_pipeline_get_el_by_name(g_audio_ctx.main_pipeline, "aud_dec", &dec_el);
    if (ret != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to get decoder element: %d", ret);
        return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
    }

    esp_gmf_info_sound_t info = {0};
    ret = esp_gmf_audio_helper_get_audio_type_by_uri(uri, &info.format_id);
    if (ret != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to get audio type: %d", ret);
        return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
    }

    ret = esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info);
    if (ret != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure decoder: %d", ret);
        return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
    }

    ret = esp_gmf_pipeline_loading_jobs(g_audio_ctx.main_pipeline);
    if (ret != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to load pipeline jobs: %d", ret);
        return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
    }

    g_audio_ctx.current_source = new_source;

    ret = esp_gmf_pipeline_run(g_audio_ctx.main_pipeline);
    if (ret == ESP_GMF_ERR_OK) {
        g_audio_ctx.current_state = AUDIO_STATE_PLAYING;
        ESP_LOGI(TAG, "Successfully switched to source %d", new_source);
    } else {
        ESP_LOGE(TAG, "Failed to start pipeline for source %d: %x", new_source, ret);
        g_audio_ctx.current_state = AUDIO_STATE_ERROR;
        return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
    }

    return AUDIO_MS_PLAYER_OK;
}

static audio_ms_player_err_t play_flash_tone(int tone_index)
{
    if (!g_audio_ctx.initialized) {
        return AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED;
    }

    if (g_audio_ctx.flash_playing) {
        ESP_LOGW(TAG, "Flash tone is already playing");
        return AUDIO_MS_PLAYER_OK;
    }

    ESP_LOGI(TAG, "Starting flash tone playback");

    g_audio_ctx.saved_source = g_audio_ctx.current_source;
    g_audio_ctx.saved_state = g_audio_ctx.current_state;
    g_audio_ctx.flash_playing = true;

    if (g_audio_ctx.current_state == AUDIO_STATE_PLAYING && g_audio_ctx.main_pipeline) {
        ESP_LOGI(TAG, "Pausing current playback");
        esp_gmf_pipeline_pause(g_audio_ctx.main_pipeline);
    }

    esp_gmf_err_t ret = ESP_GMF_ERR_OK;

    if (g_audio_ctx.flash_pipeline == NULL) {
        const char *flash_elements[] = {"aud_dec", "aud_bit_cvt", "aud_rate_cvt", "aud_ch_cvt"};
        ret = esp_gmf_pool_new_pipeline(g_audio_ctx.pool, "io_embed_flash", flash_elements,
                                        sizeof(flash_elements) / sizeof(char *),
                                        "io_codec_dev", &g_audio_ctx.flash_pipeline);
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to create flash pipeline: %d", ret);
            goto flash_error;
        }

        esp_gmf_io_codec_dev_set_dev(ESP_GMF_PIPELINE_GET_OUT_INSTANCE(g_audio_ctx.flash_pipeline),
                                     g_audio_ctx.playback_handle);

        ret = configure_flash_pipeline_for_tone(g_audio_ctx.flash_pipeline, tone_index);
        if (ret != ESP_GMF_ERR_OK) {
            goto flash_error;
        }

        esp_gmf_task_cfg_t flash_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
        flash_cfg.name = "audio_flash_task";
        ret = esp_gmf_task_init(&flash_cfg, &g_audio_ctx.flash_task);
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to create flash task: %d", ret);
            goto flash_error;
        }

        esp_gmf_pipeline_bind_task(g_audio_ctx.flash_pipeline, g_audio_ctx.flash_task);
        esp_gmf_pipeline_loading_jobs(g_audio_ctx.flash_pipeline);

        g_audio_ctx.flash_sync_evt = xEventGroupCreate();
        if (!g_audio_ctx.flash_sync_evt) {
            ESP_LOGE(TAG, "Failed to create flash sync event group");
            goto flash_error;
        }

        esp_gmf_pipeline_set_event(g_audio_ctx.flash_pipeline, _flash_pipeline_event, g_audio_ctx.flash_sync_evt);
    } else {
        esp_gmf_pipeline_reset(g_audio_ctx.flash_pipeline);
        ret = configure_flash_pipeline_for_tone(g_audio_ctx.flash_pipeline, tone_index);
        if (ret != ESP_GMF_ERR_OK) {
            goto flash_error;
        }
        ret = esp_gmf_pipeline_loading_jobs(g_audio_ctx.flash_pipeline);
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to load flash pipeline jobs: %d", ret);
            goto flash_error;
        }
    }

    xEventGroupClearBits(g_audio_ctx.flash_sync_evt, FLASH_PIPELINE_BLOCK_BIT);

    ret = esp_gmf_pipeline_run(g_audio_ctx.flash_pipeline);
    if (ret == ESP_GMF_ERR_OK) {
        ESP_LOGI(TAG, "Flash tone playback started");
    } else {
        ESP_LOGE(TAG, "Failed to start flash pipeline: %x", ret);
        goto flash_error;
    }

    return AUDIO_MS_PLAYER_OK;

flash_error:
    if (g_audio_ctx.flash_sync_evt) {
        vEventGroupDelete(g_audio_ctx.flash_sync_evt);
        g_audio_ctx.flash_sync_evt = NULL;
    }
    if (g_audio_ctx.flash_task) {
        esp_gmf_task_deinit(g_audio_ctx.flash_task);
        g_audio_ctx.flash_task = NULL;
    }
    if (g_audio_ctx.flash_pipeline) {
        esp_gmf_pipeline_destroy(g_audio_ctx.flash_pipeline);
        g_audio_ctx.flash_pipeline = NULL;
    }
    g_audio_ctx.flash_playing = false;
    restore_original_playback();
    return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
}

static audio_ms_player_err_t restore_original_playback(void)
{
    if (!g_audio_ctx.initialized) {
        return AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED;
    }

    ESP_LOGI(TAG, "Restoring original playback: source=%d, was_playing=%d",
             g_audio_ctx.saved_source, g_audio_ctx.saved_state);

    if (g_audio_ctx.flash_pipeline) {
        bool need_wait_stop = true;
        if (g_audio_ctx.flash_sync_evt) {
            EventBits_t bits = xEventGroupGetBits(g_audio_ctx.flash_sync_evt);
            need_wait_stop = ((bits & FLASH_PIPELINE_BLOCK_BIT) == 0);
        }

        if (need_wait_stop) {
            esp_gmf_pipeline_stop(g_audio_ctx.flash_pipeline);
        }
        esp_gmf_pipeline_reset(g_audio_ctx.flash_pipeline);
    }

    if (g_audio_ctx.saved_state == AUDIO_STATE_PLAYING) {
        if (g_audio_ctx.main_pipeline && g_audio_ctx.current_source == g_audio_ctx.saved_source) {
            ESP_LOGI(TAG, "Resuming original playback");
            esp_gmf_element_handle_t fade_el = NULL;
            esp_gmf_pipeline_get_el_by_name(g_audio_ctx.main_pipeline, "aud_fade", &fade_el);
            if (fade_el) {
                esp_gmf_fade_reset_weight(fade_el);
            }
            esp_gmf_pipeline_resume(g_audio_ctx.main_pipeline);
            g_audio_ctx.current_state = AUDIO_STATE_PLAYING;
        } else {
            ESP_LOGI(TAG, "Switching back to original source and starting playback");
            g_audio_ctx.current_source = g_audio_ctx.saved_source;
            switch_audio_source(g_audio_ctx.saved_source);
        }
    }

    g_audio_ctx.flash_playing = false;
    return AUDIO_MS_PLAYER_OK;
}

audio_ms_player_err_t audio_multi_source_player_play(audio_source_t source)
{
    if (!g_audio_ctx.initialized) {
        return AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED;
    }

    if (g_audio_ctx.current_state == AUDIO_STATE_PLAYING &&
        g_audio_ctx.current_source == source) {
        return AUDIO_MS_PLAYER_ERR_ALREADY_PLAYING;
    }

    return switch_audio_source(source);
}

audio_ms_player_err_t audio_multi_source_player_pause(void)
{
    if (!g_audio_ctx.initialized) {
        return AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED;
    }

    if (g_audio_ctx.current_state != AUDIO_STATE_PLAYING) {
        return AUDIO_MS_PLAYER_ERR_NOT_PLAYING;
    }

    if (!g_audio_ctx.main_pipeline) {
        return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
    }

    esp_gmf_err_t ret = esp_gmf_pipeline_pause(g_audio_ctx.main_pipeline);
    if (ret == ESP_GMF_ERR_OK) {
        g_audio_ctx.current_state = AUDIO_STATE_PAUSED;
    } else {
        ESP_LOGE(TAG, "Failed to pause playback: %d", ret);
        return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
    }

    return AUDIO_MS_PLAYER_OK;
}

audio_ms_player_err_t audio_multi_source_player_resume(void)
{
    if (!g_audio_ctx.initialized) {
        return AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED;
    }

    if (g_audio_ctx.current_state != AUDIO_STATE_PAUSED) {
        return AUDIO_MS_PLAYER_ERR_NOT_PLAYING;
    }

    if (!g_audio_ctx.main_pipeline) {
        return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
    }

    esp_gmf_element_handle_t fade_el = NULL;
    esp_gmf_pipeline_get_el_by_name(g_audio_ctx.main_pipeline, "aud_fade", &fade_el);
    if (fade_el) {
        esp_gmf_fade_reset_weight(fade_el);
    }

    esp_gmf_err_t ret = esp_gmf_pipeline_resume(g_audio_ctx.main_pipeline);
    if (ret == ESP_GMF_ERR_OK) {
        g_audio_ctx.current_state = AUDIO_STATE_PLAYING;
    } else {
        ESP_LOGE(TAG, "Failed to resume playback: %d", ret);
        return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
    }

    return AUDIO_MS_PLAYER_OK;
}

audio_ms_player_err_t audio_multi_source_player_stop(void)
{
    if (!g_audio_ctx.initialized) {
        return AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED;
    }

    if (g_audio_ctx.current_state == AUDIO_STATE_IDLE ||
        g_audio_ctx.current_state == AUDIO_STATE_STOPPED) {
        return AUDIO_MS_PLAYER_OK;
    }

    if (!g_audio_ctx.main_pipeline) {
        return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
    }

    esp_gmf_err_t ret = esp_gmf_pipeline_stop(g_audio_ctx.main_pipeline);
    if (ret == ESP_GMF_ERR_OK) {
        g_audio_ctx.current_state = AUDIO_STATE_STOPPED;
    } else {
        ESP_LOGE(TAG, "Failed to stop playback: %d", ret);
        return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
    }

    return AUDIO_MS_PLAYER_OK;
}

audio_ms_player_err_t audio_multi_source_player_switch_source(audio_source_t source)
{
    return switch_audio_source(source);
}

audio_ms_player_err_t audio_multi_source_player_play_flash_tone(void)
{
    static int play_count = 0;
    int tone_index = (play_count++) % ESP_EMBED_TONE_URL_MAX;
    return play_flash_tone(tone_index);
}

audio_ms_player_err_t audio_multi_source_player_get_current_source(audio_source_t *source)
{
    if (source == NULL) {
        return AUDIO_MS_PLAYER_ERR_INVALID_ARG;
    }
    if (!g_audio_ctx.initialized) {
        return AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED;
    }
    *source = g_audio_ctx.current_source;
    return AUDIO_MS_PLAYER_OK;
}

audio_ms_player_err_t audio_multi_source_player_get_current_state(audio_state_t *state)
{
    if (state == NULL) {
        return AUDIO_MS_PLAYER_ERR_INVALID_ARG;
    }
    if (!g_audio_ctx.initialized) {
        return AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED;
    }
    *state = g_audio_ctx.current_state;
    return AUDIO_MS_PLAYER_OK;
}

bool audio_multi_source_player_is_flash_playing(void)
{
    return g_audio_ctx.flash_playing;
}

audio_ms_player_err_t audio_multi_source_player_set_volume(int volume)
{
    if (volume < 0 || volume > 100) {
        return AUDIO_MS_PLAYER_ERR_INVALID_ARG;
    }

    if (!g_audio_ctx.playback_handle) {
        return AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED;
    }

    esp_codec_dev_handle_t playback_handle = (esp_codec_dev_handle_t)g_audio_ctx.playback_handle;
    int ret = esp_codec_dev_set_out_vol(playback_handle, volume);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to set volume: %d", ret);
        return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
    }

    return AUDIO_MS_PLAYER_OK;
}

audio_ms_player_err_t audio_multi_source_player_get_volume(int *volume)
{
    if (!volume) {
        return AUDIO_MS_PLAYER_ERR_INVALID_ARG;
    }

    if (!g_audio_ctx.playback_handle) {
        return AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED;
    }

    esp_codec_dev_handle_t playback_handle = (esp_codec_dev_handle_t)g_audio_ctx.playback_handle;
    int ret = esp_codec_dev_get_out_vol(playback_handle, volume);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to get volume: %d", ret);
        return AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL;
    }

    return AUDIO_MS_PLAYER_OK;
}

const char *audio_multi_source_player_get_source_name(audio_source_t source)
{
    if (source >= AUDIO_SRC_MAX) {
        return "Unknown";
    }
    return source_names[source];
}

const char *audio_multi_source_player_get_state_name(audio_state_t state)
{
    if (state >= sizeof(state_names) / sizeof(state_names[0])) {
        return "Unknown";
    }
    return state_names[state];
}
