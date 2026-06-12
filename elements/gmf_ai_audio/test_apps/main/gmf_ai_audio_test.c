/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_bit_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_gmf_element.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_obj.h"
#include "esp_gmf_port.h"
#include "esp_gmf_job.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"

#include "esp_gmf_wn.h"
#include "esp_gmf_vad.h"
#include "esp_gmf_ns.h"
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include "esp_gmf_doa.h"
#endif  /* DOA supported targets */
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
#include "esp_afe_config.h"
#include "esp_gmf_afe_manager.h"
#include "esp_gmf_afe.h"
#include "esp_gmf_aec.h"
#include "esp_dsp.h"
#endif  /* defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4) */
#include "esp_gmf_app_setup_peripheral.h"

#define DEBUG2FILE  (false)
#define FS          16000
#define DURATION    1
#define SIGNAL_LEN  (FS * DURATION)
#define STEREO_LEN  (SIGNAL_LEN * 2)
#define DELAY       0
#define ATTENUATION 0.1

#define WAKEUP_DETECTED (BIT0)
#define VAD_DETECTED    (BIT1)
#define VCMD_FOUND      (BIT2)
#ifdef CONFIG_IDF_TARGET_ESP32
#define AEC_ENABLE    (false)
#define MN_ENABLE     (false)
#define EVENTS_2_WAIT (WAKEUP_DETECTED | VAD_DETECTED)
#elif defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
#define AEC_ENABLE    (true)
#define MN_ENABLE     (true)
#define EVENTS_2_WAIT (WAKEUP_DETECTED | VAD_DETECTED | VCMD_FOUND)
#else
#define AEC_ENABLE    (false)
#define MN_ENABLE     (false)
#define EVENTS_2_WAIT (WAKEUP_DETECTED)
#endif  /* CONFIG_IDF_TARGET_ESP32 */

/* WebRTC NS runs ~10 ms/frame on device; keep unit-test PCM short (200 ms, frame-aligned). */
#define NS_TEST_FRAME_BYTES   (16000 * 10 / 1000 * (int)sizeof(int16_t))
#define NS_TEST_PCM_MAX_BYTES (NS_TEST_FRAME_BYTES * 20)

static const char        *TAG           = "AI_AUDIO_TEST";
static uint32_t           out_count     = 0;
static EventGroupHandle_t g_event_group = NULL;
extern const uint8_t      ns_input_sample_pcm_start[] asm("_binary_ns_input_sample_pcm_start");
extern const uint8_t      ns_input_sample_pcm_end[] asm("_binary_ns_input_sample_pcm_end");
extern const uint8_t      hi_lexin_pcm_start[] asm("_binary_hi_lexin_pcm_start");
extern const uint8_t      hi_lexin_pcm_end[] asm("_binary_hi_lexin_pcm_end");
#if defined(CONFIG_IDF_TARGET_ESP32S3)
extern const uint8_t      doa_pcm_start[] asm("_binary_doa_pcm_start");
extern const uint8_t      doa_pcm_end[] asm("_binary_doa_pcm_end");
#endif  /* CONFIG_IDF_TARGET_ESP32S3 */


#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
void generate_reference_signal(int16_t *signal, int len)
{
    for (int i = 0; i < len; i++) {
        double t = (double)i / FS;
        double value = sin(2 * M_PI * 1000 * t);
        signal[i] = (int16_t)(value * 32767);
    }
}

void generate_noise(int16_t *noise_buffer, int len)
{
    for (int i = 0; i < len; i++) {
        double t = (double)i / FS;
        double value = sin(2 * M_PI * 2000 * t);
        noise_buffer[i] = (int16_t)(value * 32767 * ATTENUATION);
    }
}

void generate_echo_signal(int16_t *reference, int16_t *noise_buffer, int16_t *echo, int len)
{
    for (int i = 0; i < len; i++) {
        int idx = (i - DELAY) >= 0 ? (i - DELAY) : 0;
        double echo_sample = reference[idx] * ATTENUATION;
        echo_sample += noise_buffer[i];
        if (echo_sample > 32767) {
            echo_sample = 32767;
        }
        if (echo_sample < -32768) {
            echo_sample = -32768;
        }

        echo[i] = (int16_t)echo_sample;
    }
}

void create_stereo_pcm(int16_t *reference, int16_t *echo, int length, int16_t *stereo)
{
    for (int i = 0; i < length; i++) {
        stereo[2 * i] = reference[i];
        stereo[2 * i + 1] = echo[i];
    }
}

void analyze_frequency(int16_t *pcm_data, uint32_t n_samples)
{
    float *real = (float *)esp_gmf_oal_malloc_align(16, n_samples * 2 * sizeof(float));
    float *hanning_win = (float *)esp_gmf_oal_malloc_align(16, n_samples * sizeof(float));
    TEST_ASSERT_NOT_NULL(real);
    TEST_ASSERT_NOT_NULL(hanning_win);

    // Perform FFT
    TEST_ASSERT_EQUAL(ESP_OK, dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE));
    dsps_wind_hann_f32(hanning_win, n_samples);
    for (int i = 0; i < n_samples; i++) {
        real[i * 2 + 0] = ((float)pcm_data[i] / 32768.0f) * hanning_win[i];
        real[i * 2 + 1] = 0;
    }

    dsps_fft2r_fc32(real, n_samples);
    dsps_bit_rev_fc32(real, n_samples);
    dsps_cplx2reC_fc32(real, n_samples);

    for (int i = 0; i < n_samples >> 1; i++) {
        real[i] = 10 * log10f((real[i * 2 + 0] * real[i * 2 + 0] + real[i * 2 + 1] * real[i * 2 + 1]) / n_samples);
    }

    // dsps_view(real, n_samples / 2, 64, 10,  -60, 40, '|');
    float max_energy = -INFINITY;
    int max_index = 0;
    for (int i = 0; i < n_samples >> 1; i++) {
        if (real[i] > max_energy) {
            max_energy = real[i];
            max_index = i;
        }
    }
    float max_frequency = (float)max_index * FS / n_samples;
    if (max_energy > -20) {
        TEST_ASSERT_EQUAL(2000, (int)max_frequency);
    }

    esp_gmf_oal_free(real);
    esp_gmf_oal_free(hanning_win);
    dsps_fft2r_deinit_fc32();
}

static esp_gmf_err_io_t aec_acquire_read(void *handle, esp_gmf_payload_t *load, int wanted_size, int block_ticks)
{
    uint8_t *src = (uint8_t *)handle;
    static int count = 0;
    if (load->buf == NULL) {
        return ESP_GMF_IO_FAIL;
    }
    esp_gmf_err_io_t ret = ESP_GMF_IO_OK;
    if (count < STEREO_LEN * 2) {
        if (count + wanted_size > STEREO_LEN * 2) {
            wanted_size = STEREO_LEN * 2 - count;
        }
        memcpy(load->buf, &src[count], wanted_size);
        count += wanted_size;
        load->valid_size = wanted_size;

        if (count == STEREO_LEN * 2) {
            load->is_done = true;
        }
    } else {
        load->valid_size = 0;
        load->is_done = true;
    }
    return ret;
}

static esp_gmf_err_io_t aec_release_read(void *handle, esp_gmf_payload_t *load, int block_ticks)
{
    load->valid_size = 0;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t aec_acquire_write(void *handle, esp_gmf_payload_t *load, int wanted_size, int block_ticks)
{
    if (load->buf == NULL) {
        return ESP_FAIL;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t aec_release_write(void *handle, esp_gmf_payload_t *load, int block_ticks)
{
    uint8_t *dest = (uint8_t *)handle;
#ifdef CONFIG_IDF_TARGET_ESP32S3
    analyze_frequency((int16_t *)load->buf, load->valid_size / 2);
#endif /* CONFIG_IDF_TARGET_ESP32S3 */
    if (out_count < SIGNAL_LEN * 2) {
        if (out_count + load->valid_size > SIGNAL_LEN * 2) {
            load->valid_size = SIGNAL_LEN * 2 - out_count;
        }
        memcpy(&dest[out_count], load->buf, load->valid_size);
        out_count += load->valid_size;
        if (out_count == SIGNAL_LEN * 2) {
            load->is_done = true;
        }
    } else {
        load->valid_size = 0;
        load->is_done = true;
    }
    return ESP_GMF_IO_OK;
}

TEST_CASE("Test gmf aec process", "[ESP_GMF_AEC][leaks=1400]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);

    printf("\r\n///////////////////// AEC /////////////////////\r\n");
#if DEBUG2FILE == true
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
#endif  /* DEBUG2FILE == true */
    int16_t *reference_signal = (int16_t *)esp_gmf_oal_malloc_align(16, SIGNAL_LEN * sizeof(int16_t));
    TEST_ASSERT_NOT_NULL(reference_signal);
    int16_t *echo_signal = (int16_t *)esp_gmf_oal_malloc_align(16, SIGNAL_LEN * sizeof(int16_t));
    TEST_ASSERT_NOT_NULL(echo_signal);
    int16_t *stereo_signal = (int16_t *)esp_gmf_oal_malloc_align(16, STEREO_LEN * sizeof(int16_t));
    TEST_ASSERT_NOT_NULL(stereo_signal);
    int16_t *output_signal = (int16_t *)esp_gmf_oal_malloc_align(16, SIGNAL_LEN * sizeof(int16_t));
    TEST_ASSERT_NOT_NULL(output_signal);
    int16_t *noise_buffer = (int16_t *)esp_gmf_oal_malloc_align(16, SIGNAL_LEN * sizeof(int16_t));
    TEST_ASSERT_NOT_NULL(noise_buffer);
    generate_reference_signal(reference_signal, SIGNAL_LEN);
    generate_noise(noise_buffer, SIGNAL_LEN);
    generate_echo_signal(reference_signal, noise_buffer, echo_signal, SIGNAL_LEN);
    create_stereo_pcm(reference_signal, echo_signal, SIGNAL_LEN, stereo_signal);

    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(aec_acquire_read, aec_release_read, NULL, stereo_signal, 1024, 100);
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(aec_acquire_write, aec_release_write, NULL, output_signal, 1024, 100);

    esp_gmf_element_handle_t gmf_aec_handle = NULL;
    esp_gmf_aec_cfg_t gmf_aec_cfg = {
        .filter_len = 4,
        .type = AFE_TYPE_VC,
        .mode = AFE_MODE_HIGH_PERF,
        .input_format = (char *)"RM",
    };
    esp_gmf_aec_init(&gmf_aec_cfg, &gmf_aec_handle);
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t *out_caps = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_get_caps(gmf_aec_handle, (const esp_gmf_cap_t **)&caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_AEC, &out_caps));

    esp_gmf_element_register_in_port(gmf_aec_handle, in_port);
    esp_gmf_element_register_out_port(gmf_aec_handle, out_port);
    esp_gmf_element_process_open(gmf_aec_handle, NULL);
    esp_gmf_job_err_t ret = ESP_GMF_JOB_ERR_OK;
    do {
        ret = esp_gmf_element_process_running(gmf_aec_handle, NULL);
        if (ret == ESP_GMF_JOB_ERR_FAIL) {
            ESP_LOGE(TAG, "AEC process failed");
            break;
        } else if (ret == ESP_GMF_JOB_ERR_DONE) {
            ESP_LOGI(TAG, "AEC process done");
            break;
        }
    } while (true);
    esp_gmf_element_process_close(gmf_aec_handle, NULL);
    esp_gmf_obj_delete(gmf_aec_handle);

#if DEBUG2FILE == true
    FILE *fp = fopen("/sdcard/reference_signal.pcm", "wb");
    if (fp) {
        fwrite(reference_signal, sizeof(int16_t), SIGNAL_LEN, fp);
        fclose(fp);
    } else {
        ESP_LOGE(TAG, "Failed to open reference_signal.pcm for writing");
    }
    fp = fopen("/sdcard/echo_signal.pcm", "wb");
    if (fp) {
        fwrite(echo_signal, sizeof(int16_t), SIGNAL_LEN, fp);
        fclose(fp);
    } else {
        ESP_LOGE(TAG, "Failed to open echo_signal.pcm for writing");
    }
    fp = fopen("/sdcard/output_signal.pcm", "wb");
    if (fp) {
        fwrite(output_signal, 1, out_count, fp);
        fclose(fp);
    } else {
        ESP_LOGE(TAG, "Failed to open output_signal.pcm for writing");
    }
    fp = fopen("/sdcard/stereo_signal.pcm", "wb");
    if (fp) {
        fwrite(stereo_signal, sizeof(int16_t), STEREO_LEN, fp);
        fclose(fp);
    } else {
        ESP_LOGE(TAG, "Failed to open stereo_signal.pcm for writing");
    }
    fp = fopen("/sdcard/noise_buffer.pcm", "wb");
    if (fp) {
        fwrite(noise_buffer, sizeof(int16_t), SIGNAL_LEN, fp);
        fclose(fp);
    } else {
        ESP_LOGE(TAG, "Failed to open noise_buffer.pcm for writing");
    }
    esp_gmf_app_teardown_sdcard(sdcard_handle);
#endif  /* DEBUG2FILE == true */
    out_count = 0;
    esp_gmf_oal_free(noise_buffer);
    esp_gmf_oal_free(reference_signal);
    esp_gmf_oal_free(echo_signal);
    esp_gmf_oal_free(stereo_signal);
    esp_gmf_oal_free(output_signal);
}

static void afe_manager_dummy_result_cb(afe_fetch_result_t *result, void *user_ctx)
{
    (void)result;
    (void)user_ctx;
}

TEST_CASE("Test gmf afe manager create", "[ESP_GMF_AFE_MANAGER][leaks=1400]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);

    printf("\r\n///////////////////// AFE MANAGER CREATE /////////////////////\r\n");

    esp_gmf_afe_manager_handle_t afe_manager = NULL;
    srmodel_list_t *models = esp_srmodel_init("model");
    const char *ch_format = "MR";
    afe_config_t *afe_cfg = afe_config_init(ch_format, models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    afe_cfg->vad_init = true;
    afe_cfg->vad_mode = VAD_MODE_2;
    afe_cfg->vad_min_speech_ms = 64;
    afe_cfg->vad_min_noise_ms = 100;
    afe_cfg->wakenet_init = true;
    afe_cfg->aec_init = AEC_ENABLE;
    esp_gmf_afe_manager_cfg_t afe_manager_cfg = DEFAULT_GMF_AFE_MANAGER_CFG(afe_cfg, NULL, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_manager_create(&afe_manager_cfg, &afe_manager));

    esp_gmf_afe_manager_features_t feat = {0};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_manager_get_features(afe_manager, &feat));
    TEST_ASSERT_TRUE(feat.wakeup);
    TEST_ASSERT_TRUE(feat.vad);
    TEST_ASSERT_EQUAL(AEC_ENABLE, feat.aec);

    size_t chunk_size = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_manager_get_chunk_size(afe_manager, &chunk_size));
    TEST_ASSERT_TRUE(chunk_size > 0);

    uint8_t ch_num = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_manager_get_input_ch_num(afe_manager, &ch_num));
    TEST_ASSERT_EQUAL(2, ch_num);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_manager_suspend(afe_manager, true));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_manager_suspend(afe_manager, false));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_manager_set_result_cb(afe_manager, afe_manager_dummy_result_cb, NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_manager_set_result_cb(afe_manager, NULL, NULL));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_manager_destroy(afe_manager));
    afe_manager = NULL;
    afe_config_free(afe_cfg);
    afe_cfg = NULL;
    esp_srmodel_deinit(models);
}

static volatile uint32_t s_afe_manager_cfg_result_hits;

static void afe_manager_config_result_cb(afe_fetch_result_t *result, void *user_ctx)
{
    (void)result;
    (void)user_ctx;
    s_afe_manager_cfg_result_hits++;
}

static int32_t afe_manager_config_read_cb(void *buffer, int buf_sz, void *user_ctx, uint32_t ticks)
{
    (void)user_ctx;
    (void)ticks;
    memset(buffer, 0, (size_t)buf_sz);
    return buf_sz;
}

TEST_CASE("Test gmf afe manager result_cb from create config", "[ESP_GMF_AFE_MANAGER][leaks=1400]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);

    printf("\r\n///////////////////// AFE MANAGER CONFIG RESULT_CB /////////////////////\r\n");

    s_afe_manager_cfg_result_hits = 0;

    esp_gmf_afe_manager_handle_t afe_manager = NULL;
    srmodel_list_t *models = esp_srmodel_init("model");
    const char *ch_format = "MR";
    afe_config_t *afe_cfg = afe_config_init(ch_format, models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    afe_cfg->vad_init = true;
    afe_cfg->vad_mode = VAD_MODE_2;
    afe_cfg->vad_min_speech_ms = 64;
    afe_cfg->vad_min_noise_ms = 100;
    afe_cfg->wakenet_init = true;
    afe_cfg->aec_init = AEC_ENABLE;
    esp_gmf_afe_manager_cfg_t afe_manager_cfg = DEFAULT_GMF_AFE_MANAGER_CFG(
        afe_cfg,
        afe_manager_config_read_cb,
        NULL,
        afe_manager_config_result_cb,
        NULL);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_manager_create(&afe_manager_cfg, &afe_manager));

    const int wait_step_ms = 50;
    const int wait_total_ms = 5000;
    int waited_ms = 0;
    while (s_afe_manager_cfg_result_hits == 0 && waited_ms < wait_total_ms) {
        vTaskDelay(pdMS_TO_TICKS(wait_step_ms));
        waited_ms += wait_step_ms;
    }
    TEST_ASSERT_GREATER_THAN(0U, s_afe_manager_cfg_result_hits);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_manager_destroy(afe_manager));
    afe_manager = NULL;
    afe_config_free(afe_cfg);
    afe_cfg = NULL;
    esp_srmodel_deinit(models);
}

static esp_gmf_err_io_t afe_acquire_read(void *handle, esp_gmf_payload_t *load, int wanted_size, int block_ticks)
{
    static int offset = 0;
    const uint8_t *src = hi_lexin_pcm_start;
    int total_size = (int)(hi_lexin_pcm_end - hi_lexin_pcm_start);

    if (offset < total_size) {
        if (offset + wanted_size > total_size) {
            wanted_size = total_size - offset;
        }
        memcpy(load->buf, &src[offset], wanted_size);
        offset += wanted_size;
        load->valid_size = wanted_size;

        if (offset == total_size) {
            offset = 0;
            load->is_done = true;
        }
    } else {
        load->valid_size = 0;
        load->is_done = true;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t afe_release_read(void *handle, esp_gmf_payload_t *load, int block_ticks)
{
    load->valid_size = 0;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t afe_acquire_write(void *handle, esp_gmf_payload_t *load, int wanted_size, int block_ticks)
{
    if (load->buf == NULL) {
        return ESP_GMF_ERR_FAIL;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t afe_release_write(void *handle, esp_gmf_payload_t *load, int block_ticks)
{
    return ESP_GMF_IO_OK;
}

void esp_gmf_afe_event_cb(esp_gmf_obj_handle_t obj, esp_gmf_afe_evt_t *event, void *user_data)
{
    switch (event->type) {
        case ESP_GMF_AFE_EVT_WAKEUP_START: {
            xEventGroupSetBits(g_event_group, WAKEUP_DETECTED);
#if MN_ENABLE == true
            esp_gmf_afe_vcmd_detection_cancel(obj);
            esp_gmf_afe_vcmd_detection_begin(obj);
#endif  /* MN_ENABLE == true */
            esp_gmf_afe_wakeup_info_t *info = event->event_data;
            ESP_LOGI(TAG, "WAKEUP_START [%d : %d]", info->wake_word_index, info->wakenet_model_index);
            break;
        }
        case ESP_GMF_AFE_EVT_WAKEUP_END: {
#if MN_ENABLE == true
            esp_gmf_afe_vcmd_detection_cancel(obj);
#endif  /* MN_ENABLE == true */
            ESP_LOGI(TAG, "WAKEUP_END");
            break;
        }
        case ESP_GMF_AFE_EVT_VAD_START: {
            xEventGroupSetBits(g_event_group, VAD_DETECTED);
            ESP_LOGI(TAG, "VAD_START");
            break;
        }
        case ESP_GMF_AFE_EVT_VAD_END: {
            ESP_LOGI(TAG, "VAD_END");
            break;
        }
        case ESP_GMF_AFE_EVT_VCMD_DECT_TIMEOUT: {
            ESP_LOGI(TAG, "VCMD_DECT_TIMEOUT");
            break;
        }
        default: {
            esp_gmf_afe_vcmd_info_t *info = event->event_data;
            ESP_LOGW(TAG, "Command %d, phrase_id %d, prob %f, str: %s",
                     event->type, info->phrase_id, info->prob, info->str);
            if (event->type == 25 || event->type == 216) {
                // 25: "da kai kong tiao"
                // 216: "kai kong tiao"
                xEventGroupSetBits(g_event_group, VCMD_FOUND);
            }
            break;
        }
    }
}

TEST_CASE("Test gmf afe process", "[ESP_GMF_AFE][leaks=1400]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);

    printf("\r\n///////////////////// AFE /////////////////////\r\n");
    g_event_group = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL(g_event_group);

    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(afe_acquire_read, afe_release_read, NULL, NULL, 1024, 100);
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(afe_acquire_write, afe_release_write, NULL, NULL, 1024, 100);

    esp_gmf_afe_manager_handle_t afe_manager = NULL;
    srmodel_list_t *models = esp_srmodel_init("model");
    const char *ch_format = "MR";
    afe_config_t *afe_cfg = afe_config_init(ch_format, models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    afe_cfg->vad_init = true;
    afe_cfg->vad_mode = VAD_MODE_2;
    afe_cfg->vad_min_speech_ms = 64;
    afe_cfg->vad_min_noise_ms = 100;
    afe_cfg->wakenet_init = true;
    afe_cfg->aec_init = AEC_ENABLE;
    esp_gmf_afe_manager_cfg_t afe_manager_cfg = DEFAULT_GMF_AFE_MANAGER_CFG(afe_cfg, NULL, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_manager_create(&afe_manager_cfg, &afe_manager));
    esp_gmf_element_handle_t gmf_afe = NULL;
    esp_gmf_afe_cfg_t gmf_afe_cfg = DEFAULT_GMF_AFE_CFG(afe_manager, esp_gmf_afe_event_cb, NULL, models);
    gmf_afe_cfg.vcmd_detect_en = MN_ENABLE;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_init(&gmf_afe_cfg, &gmf_afe));
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t *out_caps = {0};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_get_caps(gmf_afe, (const esp_gmf_cap_t **)&caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_AEC, &out_caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_NS, &out_caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_AGC, &out_caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_VAD, &out_caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_WWE, &out_caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_VCMD, &out_caps));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_in_port(gmf_afe, in_port));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_out_port(gmf_afe, out_port));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_process_open(gmf_afe, NULL));
    esp_gmf_job_err_t ret = ESP_GMF_JOB_ERR_OK;
    do {
        ret = esp_gmf_element_process_running(gmf_afe, NULL);
        if (ret == ESP_GMF_JOB_ERR_FAIL) {
            ESP_LOGE(TAG, "AFE process failed");
            break;
        } else if (ret == ESP_GMF_JOB_ERR_DONE) {
            ESP_LOGI(TAG, "AFE process done");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    } while (true);
    esp_gmf_element_process_close(gmf_afe, NULL);
    esp_gmf_obj_delete(gmf_afe);
    afe_config_free(afe_cfg);
    esp_gmf_afe_manager_destroy(afe_manager);
    esp_srmodel_deinit(models);
    TEST_ASSERT_EQUAL(EVENTS_2_WAIT, xEventGroupWaitBits(g_event_group, EVENTS_2_WAIT, pdTRUE, pdTRUE, pdMS_TO_TICKS(50 * 1000)));
    vEventGroupDelete(g_event_group);
    g_event_group = NULL;
}
#endif  /* CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4 */

typedef struct {
    const uint8_t *pcm;
    int            total_size;
    int            offset;
} mono_pcm_port_ctx_t;

typedef struct {
    const uint8_t *input_ptr;
    size_t         input_remaining;
    uint32_t       input_bytes;
    uint32_t       output_bytes;
} ns_pcm_port_ctx_t;

#if defined(CONFIG_IDF_TARGET_ESP32S3)
typedef struct {
    const uint8_t *pcm;
    int            total_bytes;
    int            offset;
} doa_pcm_port_ctx_t;
#endif  /* CONFIG_IDF_TARGET_ESP32S3 */

static esp_gmf_err_io_t mono_pcm_acquire_read(void *handle, esp_gmf_payload_t *load, int wanted_size, int block_ticks)
{
    mono_pcm_port_ctx_t *ctx = (mono_pcm_port_ctx_t *)handle;
    if ((ctx == NULL) || (load->buf == NULL)) {
        return ESP_GMF_IO_FAIL;
    }
    if (ctx->offset < ctx->total_size) {
        if (ctx->offset + wanted_size > ctx->total_size) {
            wanted_size = ctx->total_size - ctx->offset;
        }
        memcpy(load->buf, &ctx->pcm[ctx->offset], wanted_size);
        ctx->offset += wanted_size;
        load->valid_size = wanted_size;
        if (ctx->offset == ctx->total_size) {
            load->is_done = true;
        }
    } else {
        load->valid_size = 0;
        load->is_done = true;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t mono_pcm_release_read(void *handle, esp_gmf_payload_t *load, int block_ticks)
{
    (void)handle;
    (void)block_ticks;
    load->valid_size = 0;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t mono_pcm_acquire_write(void *handle, esp_gmf_payload_t *load, int wanted_size, int block_ticks)
{
    (void)handle;
    (void)wanted_size;
    (void)block_ticks;
    if (load->buf == NULL) {
        return ESP_GMF_IO_FAIL;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t mono_pcm_release_write(void *handle, esp_gmf_payload_t *load, int block_ticks)
{
    (void)handle;
    (void)block_ticks;
    return ESP_GMF_IO_OK;
}

static esp_gmf_job_err_t gmf_element_process_until_done(esp_gmf_element_handle_t handle)
{
    esp_gmf_job_err_t ret = ESP_GMF_JOB_ERR_OK;
    do {
        ret = esp_gmf_element_process_running(handle, NULL);
        if ((ret != ESP_GMF_JOB_ERR_OK) && (ret != ESP_GMF_JOB_ERR_CONTINUE) && (ret != ESP_GMF_JOB_ERR_TRUNCATE)) {
            break;
        }
    } while (ret != ESP_GMF_JOB_ERR_DONE);
    return ret;
}

static volatile int s_vad_cb_count = 0;

static void vad_test_result_cb(vad_state_t state, void *ctx)
{
    (void)ctx;
    s_vad_cb_count++;
    ESP_LOGI(TAG, "VAD callback state: %d", (int)state);
}

TEST_CASE("Test gmf vad process", "[ESP_GMF_VAD]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);

    printf("\r\n///////////////////// VAD /////////////////////\r\n");
    s_vad_cb_count = 0;

    const int vad_pcm_size = (int)(ns_input_sample_pcm_end - ns_input_sample_pcm_start);
    mono_pcm_port_ctx_t port_ctx = {
        .pcm = ns_input_sample_pcm_start,
        .total_size = vad_pcm_size,
        .offset = 0,
    };

    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(mono_pcm_acquire_read, mono_pcm_release_read,
                                                             NULL, &port_ctx, 1024, 100);
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(mono_pcm_acquire_write, mono_pcm_release_write,
                                                               NULL, NULL, 1024, 100);

    esp_gmf_vad_cfg_t vad_cfg = ESP_GMF_VAD_CFG_DEFAULT();
    vad_cfg.result_callback = vad_test_result_cb;
    esp_gmf_element_handle_t gmf_vad = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_vad_init(&vad_cfg, &gmf_vad));

    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t *out_caps = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_get_caps(gmf_vad, (const esp_gmf_cap_t **)&caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_VAD, &out_caps));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_in_port(gmf_vad, in_port));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_out_port(gmf_vad, out_port));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_process_open(gmf_vad, NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_JOB_ERR_DONE, gmf_element_process_until_done(gmf_vad));
    esp_gmf_element_process_close(gmf_vad, NULL);
    esp_gmf_obj_delete(gmf_vad);
    TEST_ASSERT_EQUAL(vad_pcm_size, port_ctx.offset);
    TEST_ASSERT_GREATER_THAN(0, s_vad_cb_count);
}

static esp_gmf_err_io_t ns_pcm_acquire_read(void *handle, esp_gmf_payload_t *load, int wanted_size, int block_ticks)
{
    ns_pcm_port_ctx_t *ctx = (ns_pcm_port_ctx_t *)handle;
    if ((ctx == NULL) || (load == NULL) || (load->buf == NULL)) {
        return ESP_GMF_IO_FAIL;
    }

    if (wanted_size <= 0) {
        load->valid_size = 0;
        load->is_done = true;
        return ESP_GMF_IO_OK;
    }

    size_t read_size = (ctx->input_remaining < (size_t)wanted_size) ? ctx->input_remaining : (size_t)wanted_size;
    if (read_size == 0) {
        load->valid_size = 0;
        load->is_done = true;
        return ESP_GMF_IO_OK;
    }

    memcpy(load->buf, ctx->input_ptr, read_size);
    ctx->input_ptr += read_size;
    ctx->input_remaining -= read_size;
    ctx->input_bytes += read_size;

    load->valid_size = read_size;
    load->is_done = (ctx->input_remaining == 0);
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t ns_pcm_release_read(void *handle, esp_gmf_payload_t *load, int block_ticks)
{
    (void)handle;
    (void)block_ticks;
    if (load) {
        load->valid_size = 0;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t ns_pcm_acquire_write(void *handle, esp_gmf_payload_t *load, int wanted_size, int block_ticks)
{
    (void)handle;
    (void)wanted_size;
    (void)block_ticks;
    if ((load == NULL) || (load->buf == NULL)) {
        return ESP_GMF_IO_FAIL;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t ns_pcm_release_write(void *handle, esp_gmf_payload_t *load, int block_ticks)
{
    ns_pcm_port_ctx_t *ctx = (ns_pcm_port_ctx_t *)handle;
    (void)block_ticks;
    if ((ctx == NULL) || (load == NULL) || (load->buf == NULL)) {
        return ESP_GMF_IO_FAIL;
    }
    if (load->valid_size > 0) {
        ctx->output_bytes += load->valid_size;
    }
    return ESP_GMF_IO_OK;
}

TEST_CASE("Test gmf ns process", "[ESP_GMF_NS]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PORT", ESP_LOG_WARN);
    esp_log_level_set("GMF_CACHE", ESP_LOG_WARN);
    ESP_GMF_MEM_SHOW(TAG);

    printf("\r\n///////////////////// NS /////////////////////\r\n");

    const size_t ns_pcm_embedded = (size_t)(ns_input_sample_pcm_end - ns_input_sample_pcm_start);
    const size_t ns_pcm_size = (ns_pcm_embedded < NS_TEST_PCM_MAX_BYTES) ? ns_pcm_embedded : NS_TEST_PCM_MAX_BYTES;
    ns_pcm_port_ctx_t port_ctx = {
        .input_ptr = ns_input_sample_pcm_start,
        .input_remaining = ns_pcm_size,
    };

    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(ns_pcm_acquire_read, ns_pcm_release_read,
                                                             NULL, &port_ctx, NS_TEST_FRAME_BYTES, 0);
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ns_pcm_acquire_write, ns_pcm_release_write,
                                                               NULL, &port_ctx, NS_TEST_FRAME_BYTES, 0);

    esp_gmf_ns_cfg_t ns_cfg = ESP_GMF_NS_CFG_DEFAULT();
#if CONFIG_SR_NSN_NSNET2
    ns_cfg.model_name = "nsnet2";
#endif
    ns_cfg.sample_rate = 16000;
    ns_cfg.channel = 1;
    esp_gmf_element_handle_t gmf_ns = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_ns_init(&ns_cfg, &gmf_ns));

    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t *out_caps = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_get_caps(gmf_ns, (const esp_gmf_cap_t **)&caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_NS, &out_caps));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_in_port(gmf_ns, in_port));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_out_port(gmf_ns, out_port));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_process_open(gmf_ns, NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_JOB_ERR_DONE, gmf_element_process_until_done(gmf_ns));
    esp_gmf_element_process_close(gmf_ns, NULL);
    esp_gmf_obj_delete(gmf_ns);

    TEST_ASSERT_EQUAL(ns_pcm_size, port_ctx.input_bytes);
    TEST_ASSERT_GREATER_THAN(0, port_ctx.output_bytes);
    TEST_ASSERT_GREATER_OR_EQUAL(ns_pcm_size - NS_TEST_FRAME_BYTES, port_ctx.output_bytes);
    ESP_LOGI(TAG, "NS test summary: input_bytes=%lu, output_bytes=%lu (cap=%u, embedded=%u)",
             (unsigned long)port_ctx.input_bytes, (unsigned long)port_ctx.output_bytes,
             (unsigned)NS_TEST_PCM_MAX_BYTES, (unsigned)ns_pcm_embedded);
}

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define DOA_TEST_FRAME_MS    32
#define DOA_TEST_CH_NUM      2  /* matches input_format "MM" */
#define DOA_TEST_INPUT_BYTES (DOA_TEST_FRAME_MS * FS / 1000 * (int)sizeof(int16_t) * DOA_TEST_CH_NUM)

static esp_gmf_err_io_t doa_pcm_acquire_read(void *handle, esp_gmf_payload_t *load, int wanted_size, int block_ticks)
{
    doa_pcm_port_ctx_t *ctx = (doa_pcm_port_ctx_t *)handle;
    (void)block_ticks;
    if ((ctx == NULL) || (load == NULL) || (load->buf == NULL)) {
        return ESP_GMF_IO_FAIL;
    }
    if (wanted_size <= 0) {
        load->valid_size = 0;
        load->is_done = true;
        return ESP_GMF_IO_OK;
    }
    if (ctx->offset < ctx->total_bytes) {
        if (ctx->offset + wanted_size > ctx->total_bytes) {
            wanted_size = ctx->total_bytes - ctx->offset;
        }
        memcpy(load->buf, &ctx->pcm[ctx->offset], wanted_size);
        ctx->offset += wanted_size;
        load->valid_size = wanted_size;
        load->is_done = (ctx->offset >= ctx->total_bytes);
    } else {
        load->valid_size = 0;
        load->is_done = true;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t doa_pcm_release_read(void *handle, esp_gmf_payload_t *load, int block_ticks)
{
    (void)handle;
    (void)block_ticks;
    if (load) {
        load->valid_size = 0;
    }
    return ESP_GMF_IO_OK;
}

static volatile int s_doa_cb_count = 0;
static float s_doa_last_result = -1.0f;

static void doa_test_result_cb(float doa_result, void *ctx)
{
    (void)ctx;
    s_doa_cb_count++;
    s_doa_last_result = doa_result;
    ESP_LOGI(TAG, "DOA callback result: %f", doa_result);
}

TEST_CASE("Test gmf doa process", "[ESP_GMF_DOA]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PORT", ESP_LOG_WARN);
    esp_log_level_set("GMF_CACHE", ESP_LOG_WARN);
    ESP_GMF_MEM_SHOW(TAG);

    printf("\r\n///////////////////// DOA /////////////////////\r\n");
    s_doa_cb_count = 0;
    s_doa_last_result = -1.0f;

    const size_t doa_pcm_embedded = (size_t)(doa_pcm_end - doa_pcm_start);
    TEST_ASSERT_GREATER_OR_EQUAL(DOA_TEST_INPUT_BYTES, doa_pcm_embedded);

    doa_pcm_port_ctx_t port_ctx = {
        .pcm = doa_pcm_start,
        .total_bytes = DOA_TEST_INPUT_BYTES,
        .offset = 0,
    };

    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(doa_pcm_acquire_read, doa_pcm_release_read,
                                                             NULL, &port_ctx, DOA_TEST_INPUT_BYTES, 0);

    esp_gmf_doa_cfg_t doa_cfg = {
        .sample_rate = FS,
        .resolution = 10,
        .d_mics = 0.08f,
        .frame_ms = DOA_TEST_FRAME_MS,
        .input_format = "MM",
        .result_callback = NULL,
        .ctx = NULL,
    };
    esp_gmf_element_handle_t gmf_doa = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_doa_init(&doa_cfg, &gmf_doa));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_doa_set_result_cb(gmf_doa, doa_test_result_cb, NULL));

    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t *out_caps = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_get_caps(gmf_doa, (const esp_gmf_cap_t **)&caps));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_cap_fetch_node(caps, ESP_GMF_CAPS_AUDIO_DOA, &out_caps));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_in_port(gmf_doa, in_port));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_process_open(gmf_doa, NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_JOB_ERR_DONE, gmf_element_process_until_done(gmf_doa));
    esp_gmf_element_process_close(gmf_doa, NULL);
    esp_gmf_obj_delete(gmf_doa);

    TEST_ASSERT_GREATER_THAN(0, s_doa_cb_count);
    TEST_ASSERT_TRUE(s_doa_last_result >= 0.0f);
    TEST_ASSERT_TRUE(s_doa_last_result <= 180.0f);
    TEST_ASSERT_EQUAL(DOA_TEST_INPUT_BYTES, port_ctx.offset);
    ESP_LOGI(TAG, "DOA test summary: input_bytes=%d, embedded=%u, result=%f, callbacks=%d",
             port_ctx.offset, (unsigned)doa_pcm_embedded, s_doa_last_result, s_doa_cb_count);
}
#endif  /* CONFIG_IDF_TARGET_ESP32S3 */

static esp_gmf_err_io_t wn_acquire_read(void *handle, esp_gmf_payload_t *load, int wanted_size, int block_ticks)
{
    static int offset = 0;
    const uint8_t *src = hi_lexin_pcm_start;
    int total_size = (int)(hi_lexin_pcm_end - hi_lexin_pcm_start);

    if (offset < total_size) {
        if (offset + wanted_size > total_size) {
            wanted_size = total_size - offset;
        }
        memcpy(load->buf, &src[offset], wanted_size);
        offset += wanted_size;
        load->valid_size = wanted_size;

        if (offset == total_size) {
            offset = 0;
            load->is_done = true;
        }
    } else {
        load->valid_size = 0;
        load->is_done = true;
    }
    return 0;
}

static esp_gmf_err_io_t wn_release_read(void *handle, esp_gmf_payload_t *load, int block_ticks)
{
    load->valid_size = 0;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_io_t wn_acquire_write(void *handle, esp_gmf_payload_t *load, int wanted_size, int block_ticks)
{
    if (load->buf == NULL) {
        return ESP_GMF_ERR_FAIL;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_io_t wn_release_write(void *handle, esp_gmf_payload_t *load, int block_ticks)
{
    return ESP_GMF_ERR_OK;
}

static void esp_gmf_wn_event_cb(esp_gmf_obj_handle_t obj, int32_t trigger_ch, void *user_ctx)
{
    static int32_t cnt = 1;
    ESP_LOGI(TAG, "WWE detected on channel %" PRIu32 ", cnt: %" PRIi32, trigger_ch, cnt++);
    xEventGroupSetBits(g_event_group, WAKEUP_DETECTED);
}

TEST_CASE("Test gmf wakenet process", "[ESP_GMF_WN][leaks=1400]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);

    printf("\r\n///////////////////// Wakenet /////////////////////\r\n");
    g_event_group = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL(g_event_group);

    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(wn_acquire_read, wn_release_read, NULL, NULL, 1024, 100);
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(wn_acquire_write, wn_release_write, NULL, NULL, 1024, 100);

    srmodel_list_t *models = esp_srmodel_init("model");
    const char *ch_format = "MR";
    esp_gmf_wn_cfg_t wn_cfg = {
        .input_format = (char *)ch_format,
        .det_mode = DET_MODE_95,
        .models = models,
        .detect_cb = esp_gmf_wn_event_cb,
        .user_ctx = NULL,
    };
    esp_gmf_element_handle_t gmf_wn = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_wn_init(&wn_cfg, &gmf_wn));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_in_port(gmf_wn, in_port));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_out_port(gmf_wn, out_port));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_process_open(gmf_wn, NULL));
    esp_gmf_job_err_t ret = ESP_GMF_JOB_ERR_OK;
    do {
        ret = esp_gmf_element_process_running(gmf_wn, NULL);
        if (ret == ESP_GMF_JOB_ERR_FAIL) {
            ESP_LOGE(TAG, "WN process failed");
            break;
        } else if (ret == ESP_GMF_JOB_ERR_DONE) {
            ESP_LOGI(TAG, "WN process done");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    } while (true);
    TEST_ASSERT_EQUAL(WAKEUP_DETECTED, xEventGroupWaitBits(g_event_group, WAKEUP_DETECTED, pdTRUE, pdTRUE, pdMS_TO_TICKS(10 * 1000)));
    esp_gmf_element_process_close(gmf_wn, NULL);
    esp_gmf_obj_delete(gmf_wn);
    esp_srmodel_deinit(models);
    vEventGroupDelete(g_event_group);
    g_event_group = NULL;
}
