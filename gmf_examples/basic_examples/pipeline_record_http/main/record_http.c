/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_board_manager_includes.h"
#include "esp_codec_dev.h"
#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_io_codec_dev.h"
#include "esp_gmf_io_http.h"
#include "gmf_loader_setup_defaults.h"
#include "esp_fourcc.h"

#define DEFAULT_RECORD_SAMPLE_RATE  (16000)
#define DEFAULT_RECORD_BITS         (16)
#define DEFAULT_RECORD_CHANNEL      (1)
#define DEFAULT_RECORD_DURATION_MS  (15000)
#define DEFAULT_MICROPHONE_GAIN     (32)
#define DEFAULT_AUDIO_TYPE          (ESP_FOURCC_PCM)

static const char *TAG = "REC_HTTP";

esp_gmf_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGI(TAG, "CB: RECV Pipeline EVT: el:%s-%p, type:%d, sub:%s, payload:%p, size:%d,%p",
             OBJ_GET_TAG(event->from), event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    return ESP_GMF_ERR_OK;
}

static int _http_write_chunk(esp_http_client_handle_t http, const char *buffer, int len)
{
    char header_chunk_buffer[16] = {0};
    int header_chunk_len = snprintf(header_chunk_buffer, sizeof(header_chunk_buffer), "%x\r\n", len);
    int write_len = esp_http_client_write(http, header_chunk_buffer, header_chunk_len);
    if (write_len <= 0) {
        ESP_LOGE(TAG, "Error write chunked header, write_len=%d", write_len);
        return ESP_GMF_ERR_FAIL;
    }
    write_len = esp_http_client_write(http, buffer, len);
    if (write_len <= 0) {
        ESP_LOGE(TAG, "Error write chunked content, write_len=%d", write_len);
        return ESP_GMF_ERR_FAIL;
    }
    write_len = esp_http_client_write(http, "\r\n", 2);
    if (write_len <= 0) {
        ESP_LOGE(TAG, "Error write chunked tail, write_len=%d", write_len);
        return ESP_GMF_ERR_FAIL;
    }
    return write_len;
}

static esp_gmf_err_t _http_set_content_type_by_format(esp_http_client_handle_t http, uint32_t format)
{
    switch (format) {
        case ESP_FOURCC_AAC:
            esp_http_client_set_header(http, "Content-Type", "audio/aac");
            break;
        case ESP_FOURCC_ALAW:
            esp_http_client_set_header(http, "Content-Type", "audio/g711-alaw");
            break;
        case ESP_FOURCC_ULAW:
            esp_http_client_set_header(http, "Content-Type", "audio/g711-ulaw");
            break;
        case ESP_FOURCC_AMRNB:
            esp_http_client_set_header(http, "Content-Type", "audio/amr");
            break;
        case ESP_FOURCC_AMRWB:
            esp_http_client_set_header(http, "Content-Type", "audio/amr-wb");
            break;
        case ESP_FOURCC_OPUS:
            esp_http_client_set_header(http, "Content-Type", "audio/opus");
            break;
        case ESP_FOURCC_ADPCM:
            esp_http_client_set_header(http, "Content-Type", "audio/adpcm");
            break;
        case ESP_FOURCC_LC3:
            esp_http_client_set_header(http, "Content-Type", "audio/lc3");
            break;
        case ESP_FOURCC_SBC:
            esp_http_client_set_header(http, "Content-Type", "audio/sbc");
            break;
        case ESP_FOURCC_PCM:
            esp_http_client_set_header(http, "Content-Type", "audio/pcm");
            break;
        default:
            return ESP_GMF_ERR_FAIL;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _http_stream_event_handle(http_stream_event_msg_t *msg)
{
    esp_http_client_handle_t http = (esp_http_client_handle_t)msg->http_client;
    switch (msg->event_id) {
        case HTTP_STREAM_PRE_REQUEST:
            // set header
            ESP_LOGW(TAG, "[ + ] HTTP client HTTP_STREAM_PRE_REQUEST");
            esp_http_client_set_method(http, HTTP_METHOD_POST);
            char dat[10] = {0};
            snprintf(dat, sizeof(dat), "%d", DEFAULT_RECORD_SAMPLE_RATE);
            esp_http_client_set_header(http, "x-audio-sample-rates", dat);
            uint32_t fmt = *((uint32_t *)msg->user_data);
            esp_gmf_err_t ret = _http_set_content_type_by_format(http, fmt);
            if (ret != ESP_GMF_ERR_OK) {
                ESP_LOGE(TAG, "Failed to set content type for format: %lx", fmt);
                return ret;
            }
            snprintf(dat, sizeof(dat), "%d", DEFAULT_RECORD_BITS);
            esp_http_client_set_header(http, "x-audio-bits", dat);
            snprintf(dat, sizeof(dat), "%d", DEFAULT_RECORD_CHANNEL);
            esp_http_client_set_header(http, "x-audio-channel", dat);
            return ESP_GMF_ERR_OK;

        case HTTP_STREAM_ON_REQUEST:
            ESP_LOGD(TAG, "[ + ] HTTP client HTTP_STREAM_ON_REQUEST, write chunk");
            return _http_write_chunk(http, msg->buffer, msg->buffer_len);

        case HTTP_STREAM_POST_REQUEST:
            ESP_LOGW(TAG, "[ + ] HTTP client HTTP_STREAM_POST_REQUEST, write end chunked marker");
            if (esp_http_client_write(http, "0\r\n\r\n", 5) <= 0) {
                return ESP_GMF_ERR_FAIL;
            }
            return ESP_GMF_ERR_OK;

        case HTTP_STREAM_FINISH_REQUEST:
            ESP_LOGW(TAG, "[ + ] HTTP client HTTP_STREAM_FINISH_REQUEST");
            char buf[64] = {0};
            int read_len = esp_http_client_read(http, buf, sizeof(buf) - 1);
            if (read_len <= 0) {
                return ESP_GMF_ERR_FAIL;
            }
            buf[read_len] = 0;
            ESP_LOGI(TAG, "Got HTTP Response = %s", (char *)buf);
            return ESP_GMF_ERR_OK;

        default:
            ESP_LOGD(TAG, "Unknown event: %d", msg->event_id);
            return ESP_GMF_ERR_OK;
    }
}

static int record_peripheral_init(esp_codec_dev_handle_t *out_handle)
{
    int ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init audio ADC");
        return ret;
    }
    dev_audio_codec_handles_t *rec_dev_handle = NULL;
    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&rec_dev_handle);
    if (rec_dev_handle == NULL || rec_dev_handle->codec_dev == NULL) {
        ESP_LOGE(TAG, "Failed to get record handle");
        return ESP_ERR_NOT_FOUND;
    }
    *out_handle = rec_dev_handle->codec_dev;
    ret = esp_codec_dev_set_in_gain(*out_handle, DEFAULT_MICROPHONE_GAIN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set input gain");
        return ret;
    }
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = DEFAULT_RECORD_SAMPLE_RATE,
        .channel = DEFAULT_RECORD_CHANNEL,
        .bits_per_sample = DEFAULT_RECORD_BITS,
    };
#ifdef CONFIG_BOARD_LYRAT_MINI_V1_1
    if (fs.channel == 1) {
        fs.channel = 2;
        fs.channel_mask = 0x02;
    }
#endif  /* CONFIG_BOARD_LYRAT_MINI_V1_1 */
    ret = esp_codec_dev_open(*out_handle, &fs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open record codec");
        return ret;
    }
    return ESP_OK;
}

static void record_peripheral_deinit(esp_codec_dev_handle_t handle)
{
    if (handle != NULL) {
        esp_codec_dev_close(handle);
    }
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
}

static void gmf_loader_setup_all(esp_gmf_pool_handle_t pool)
{
    gmf_loader_setup_io_default(pool);
    gmf_loader_setup_audio_codec_default(pool);
}

static void gmf_loader_teardown_all(esp_gmf_pool_handle_t pool)
{
    gmf_loader_teardown_audio_codec_default(pool);
    gmf_loader_teardown_io_default(pool);
}

void app_main(void)
{
    esp_codec_dev_handle_t record_handle = NULL;
    ESP_LOGI(TAG, "[ 1 ] Mount peripheral");
    esp_gmf_app_wifi_connect();
    int ret = record_peripheral_init(&record_handle);
    if (ret != ESP_OK) {
        esp_gmf_app_wifi_disconnect();
        return;
    }

    ESP_LOGI(TAG, "[ 2 ] Register all the elements and set audio information to record codec device");
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    gmf_loader_setup_all(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    ESP_LOGI(TAG, "[ 3 ] Create audio pipeline");
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_enc"};
    ret = esp_gmf_pool_new_pipeline(pool, "io_codec_dev", name, sizeof(name) / sizeof(char *), "io_http", &pipe);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to new pipeline");
    esp_gmf_io_codec_dev_set_dev(ESP_GMF_PIPELINE_GET_IN_INSTANCE(pipe), record_handle);

    ESP_LOGI(TAG, "[ 3.1 ] Register the http io of the record pipeline");
    esp_gmf_io_handle_t http_io = NULL;
    esp_gmf_pipeline_get_out(pipe, &http_io);
    http_io_cfg_t *http_cfg = (http_io_cfg_t *)OBJ_GET_CFG(http_io);
    uint32_t audio_type = DEFAULT_AUDIO_TYPE;
    http_cfg->user_data = (void *)&audio_type;
    http_cfg->event_handle = _http_stream_event_handle;

    ESP_LOGI(TAG, "[ 3.2 ] Reconfig audio encoder type and audio information and report information to the record pipeline");
    esp_gmf_element_handle_t enc_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_enc", &enc_el);
    esp_gmf_info_sound_t info = {
        .sample_rates = DEFAULT_RECORD_SAMPLE_RATE,
        .channels = DEFAULT_RECORD_CHANNEL,
        .bits = DEFAULT_RECORD_BITS,
        .format_id = audio_type,
    };
    esp_gmf_audio_enc_reconfig_by_sound_info(enc_el, &info);
    esp_gmf_pipeline_report_info(pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));

    ESP_LOGI(TAG, "[ 3.3 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task");
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    // To support all encoders, especially AMR-NB、AMR-WB、OPUS, the task stack size should be about 40k
    cfg.thread.stack = 40 * 1024;
    esp_gmf_task_handle_t work_task = NULL;
    ret = esp_gmf_task_init(&cfg, &work_task);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to create pipeline task");
    esp_gmf_pipeline_bind_task(pipe, work_task);
    esp_gmf_pipeline_loading_jobs(pipe);

    ESP_LOGI(TAG, "[ 3.4 ] Create envent group and listening event from pipeline");
    esp_gmf_pipeline_set_event(pipe, _pipeline_event, NULL);

    ESP_LOGI(TAG, "[ 4 ] Start audio_pipeline");
    esp_gmf_pipeline_set_out_uri(pipe, CONFIG_EXAMPLE_HTTP_SERVER_URL);
    esp_gmf_pipeline_run(pipe);
    vTaskDelay(DEFAULT_RECORD_DURATION_MS / portTICK_PERIOD_MS);
    esp_gmf_pipeline_stop(pipe);

    ESP_LOGI(TAG, "[ 5 ] Destroy all the resources");
    esp_gmf_task_deinit(work_task);
    esp_gmf_pipeline_destroy(pipe);
    gmf_loader_teardown_all(pool);
    esp_gmf_pool_deinit(pool);
    record_peripheral_deinit(record_handle);
    esp_gmf_app_wifi_disconnect();
}
