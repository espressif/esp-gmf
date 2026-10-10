/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"

#include "esp_gmf_ai_audio_helper.h"

#define ADC_LABEL_TOKEN_LEN  (2)

static const char *TAG = "GMF_AI_HELPER";

typedef struct {
    char  token[ADC_LABEL_TOKEN_LEN + 1];
    char  sr;
} adc_label_sr_map_t;

/* Same token set as audio_codec_adc_label_parse(). Microphone positions become 'M'. */
static const adc_label_sr_map_t s_label_map[] = {
    {"FC", 'M'},
    {"FL", 'M'},
    {"FR", 'M'},
    {"SL", 'M'},
    {"SR", 'M'},
    {"BL", 'M'},
    {"BR", 'M'},
    {"RE", 'R'},
    {"NA", 'N'},
};

static bool label_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool adc_label_token_to_sr(const char *token, size_t token_len, char *sr)
{
    if (token_len != ADC_LABEL_TOKEN_LEN) {
        return false;
    }
    for (size_t i = 0; i < sizeof(s_label_map) / sizeof(s_label_map[0]); i++) {
        if (memcmp(token, s_label_map[i].token, ADC_LABEL_TOKEN_LEN) == 0) {
            *sr = s_label_map[i].sr;
            return true;
        }
    }
    return false;
}

esp_gmf_err_t esp_gmf_ai_audio_ch_layout_to_sr_format(const char *ch_layout, char *sr_format, size_t sr_format_size)
{
    if (ch_layout == NULL || ch_layout[0] == '\0' || sr_format == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }

    char converted[ESP_GMF_AI_AUDIO_SR_FORMAT_MAX_CH + 1];
    size_t count = 0;
    const char *cursor = ch_layout;

    while (true) {
        if (count >= ESP_GMF_AI_AUDIO_SR_FORMAT_MAX_CH) {
            ESP_LOGE(TAG, "ADC layout label exceeds %d channels", ESP_GMF_AI_AUDIO_SR_FORMAT_MAX_CH);
            return ESP_GMF_ERR_INVALID_ARG;
        }

        const char *segment_end = cursor;
        while (*segment_end != '\0' && *segment_end != ',') {
            segment_end++;
        }

        const char *token = cursor;
        const char *token_end = segment_end;
        while (token < token_end && label_is_space(*token)) {
            token++;
        }
        while (token_end > token && label_is_space(*(token_end - 1))) {
            token_end--;
        }

        size_t token_len = (size_t)(token_end - token);
        char sr = '\0';
        if (adc_label_token_to_sr(token, token_len, &sr) == false) {
            ESP_LOGE(TAG, "Unsupported ADC layout label token: %.*s", (int)token_len, token);
            return ESP_GMF_ERR_INVALID_ARG;
        }
        converted[count++] = sr;

        if (*segment_end == '\0') {
            break;
        }
        cursor = segment_end + 1;
        if (*cursor == '\0') {
            ESP_LOGE(TAG, "ADC layout label has a trailing comma");
            return ESP_GMF_ERR_INVALID_ARG;
        }
    }

    if (count + 1 > sr_format_size) {
        return ESP_GMF_ERR_NOT_ENOUGH;
    }
    memcpy(sr_format, converted, count);
    sr_format[count] = '\0';
    return ESP_GMF_ERR_OK;
}
