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

static const char        *TAG           = "AI_AUDIO_TEST";
static uint32_t           out_count     = 0;
static EventGroupHandle_t g_event_group = NULL;
extern const uint8_t      hi_lexin_pcm_start[] asm("_binary_hi_lexin_pcm_start");
extern const uint8_t      hi_lexin_pcm_end[] asm("_binary_hi_lexin_pcm_end");

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

TEST_CASE("Test gmf aec process", "[ESP_GMF_AEC]")
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

TEST_CASE("Test gmf afe manager create", "[ESP_GMF_AFE_MANAGER]")
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
    afe_cfg->aec_init = true;
    esp_gmf_afe_manager_cfg_t afe_manager_cfg = DEFAULT_GMF_AFE_MANAGER_CFG(afe_cfg, NULL, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_manager_create(&afe_manager_cfg, &afe_manager));
    afe_config_free(afe_cfg);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_manager_destroy(afe_manager));
    esp_srmodel_deinit(models);
}

static esp_gmf_err_io_t afe_acquire_read(void *handle, esp_gmf_payload_t *load, int wanted_size, int block_ticks)
{
    static int offset = 0;
    const uint8_t *src = hi_lexin_pcm_start;
    int total_size = hi_lexin_pcm_end - hi_lexin_pcm_start;

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

TEST_CASE("Test gmf afe process", "[ESP_GMF_AFE]")
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

static esp_gmf_err_io_t wn_acquire_read(void *handle, esp_gmf_payload_t *load, int wanted_size, int block_ticks)
{
    static int offset = 0;
    const uint8_t *src = hi_lexin_pcm_start;
    int total_size = hi_lexin_pcm_end - hi_lexin_pcm_start;

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

TEST_CASE("Test gmf wakenet process", "[ESP_GMF_WN]")
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
