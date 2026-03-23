/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <strings.h>

#include "esp_err.h"
#include "esp_check.h"
#include "esp_pbac_api.h"
#include "esp_heap_caps.h"

#include "esp_bt_audio_event.h"
#include "esp_bt_audio_pb.h"
#include "bt_audio_evt_dispatcher.h"
#include "bt_audio_ops.h"

/**
 * @brief  PBAC profile context
 */
typedef struct {
    esp_pbac_conn_hdl_t  conn_handle;  /*!< PBAC connection handle */
} pbac_ctx_t;

/**
 * @brief  vCard iterator
 */
typedef struct {
    char   *data;       /*!< vCard data */
    size_t  len;        /*!< vCard data length */
    size_t  pos;        /*!< vCard position */
    char   *vcard_end;  /*!< vCard end */
    char    saved;      /*!< Saved character */
} vcard_iter_t;

static const char *TAG = "BT_AUD_PBAC";
static pbac_ctx_t *pbac_ctx = NULL;

static inline void vcard_unfold(char *str)
{
    char *dst = str;
    while (*str) {
        if ((str[0] == '\r' && str[1] == '\n') || str[0] == '\n') {
            char *next = (str[0] == '\r') ? str + 2 : str + 1;
            if (*next == ' ' || *next == '\t') {
                *dst++ = ' ';
                str = next + 1;
                continue;
            }
        }
        *dst++ = *str++;
    }
    *dst = '\0';
}

static inline char *vcard_line_value(char *line)
{
    char *colon = strchr(line, ':');
    return colon ? colon + 1 : NULL;
}

static inline void vcard_trim_line_end(char *value)
{
    size_t len = strlen(value);
    while (len > 0 && (value[len - 1] == '\r' || value[len - 1] == '\n')) {
        value[--len] = '\0';
    }
}

static inline bool vcard_line_is_tel_property(const char *line, size_t line_len)
{
    while (line_len > 0 && line[line_len - 1] == '\r') {
        line_len--;
    }
    const char *colon = memchr(line, ':', line_len);
    if (!colon) {
        return false;
    }
    const char *value = colon + 1;
    if ((size_t)(value - line) >= line_len || *value == '\0') {
        return false;
    }
    size_t prefix_len = (size_t)(colon - line);
    const char *semi = memchr(line, ';', prefix_len);
    const char *name_end = semi ? semi : colon;
    size_t name_len = (size_t)(name_end - line);
    const char *prop_name = line;
    const char *dot = NULL;
    for (size_t i = 0; i < name_len; i++) {
        if (line[i] == '.') {
            dot = line + i;
        }
    }
    if (dot != NULL) {
        prop_name = dot + 1;
        name_len = name_len - (size_t)(prop_name - line);
    }
    return name_len == 3 && strncasecmp(prop_name, "TEL", 3) == 0;
}

static inline size_t vcard_count_tel_lines(const char *str)
{
    size_t count = 0;
    const char *p = str;
    while (*p) {
        const char *nl = strchr(p, '\n');
        const char *end = nl ? nl : p + strlen(p);
        size_t len = (size_t)(end - p);
        if (len > 0 && vcard_line_is_tel_property(p, len)) {
            count++;
        }
        if (!nl) {
            break;
        }
        p = nl + 1;
    }
    return count;
}

static inline size_t find_vcard_marker(const char *buf, size_t len, const char *marker, size_t marker_len)
{
    for (size_t i = 0; i + marker_len <= len; i++) {
        if (strncasecmp(buf + i, marker, marker_len) == 0) {
            return i;
        }
    }
    return len;
}

static inline void vcard_iter_init(vcard_iter_t *iter, char *data, size_t len)
{
    if (!iter) {
        return;
    }
    iter->data = data;
    iter->len = len;
    iter->pos = 0;
    iter->vcard_end = NULL;
    iter->saved = '\0';
}

static esp_err_t vcard_iter_next(vcard_iter_t *iter, char **vcard_start)
{
    if (!iter || !vcard_start) {
        return ESP_ERR_INVALID_ARG;
    }
    if (iter->vcard_end) {
        *iter->vcard_end = iter->saved;
        iter->vcard_end = NULL;
    }
    if (iter->pos >= iter->len) {
        return ESP_ERR_NOT_FOUND;
    }
    const char *marker_begin = "BEGIN:VCARD";
    const size_t marker_begin_len = 11;
    size_t off = find_vcard_marker(iter->data + iter->pos, iter->len - iter->pos, marker_begin, marker_begin_len);
    if (off >= iter->len - iter->pos) {
        return ESP_ERR_NOT_FOUND;
    }
    char *start = iter->data + iter->pos + off;
    const char *marker_end = "END:VCARD";
    const size_t marker_end_len = 9;
    size_t end_off = find_vcard_marker(start, iter->len - (size_t)(start - iter->data), marker_end, marker_end_len);
    if (end_off >= iter->len - (size_t)(start - iter->data)) {
        return ESP_ERR_NOT_FOUND;
    }
    char *end_line = start + end_off + marker_end_len;
    if (end_line >= iter->data + iter->len) {
        iter->saved = '\0';
        iter->vcard_end = NULL;
        iter->pos = iter->len;
    } else {
        iter->saved = *end_line;
        *end_line = '\0';
        iter->vcard_end = end_line;
        iter->pos = (size_t)(end_line - iter->data) + 1;
        while (iter->pos < iter->len) {
            char c = iter->data[iter->pos];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                break;
            }
            iter->pos++;
        }
    }
    *vcard_start = start;
    return ESP_OK;
}

static esp_err_t vcard_parse(char *str, esp_bt_audio_pb_history_t *history)
{
    ESP_RETURN_ON_FALSE(str && history, ESP_ERR_INVALID_ARG, "BT_AUD_PBAC", "null argument");

    vcard_unfold(str);

    if (strncasecmp(str, "BEGIN:VCARD", 11) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t n_tel = vcard_count_tel_lines(str);
    esp_bt_audio_pb_tel_t *tel_arr = NULL;
    if (n_tel > 0) {
        tel_arr = heap_caps_calloc_prefer(n_tel, sizeof(esp_bt_audio_pb_tel_t), 2,
                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
        ESP_RETURN_ON_FALSE(tel_arr, ESP_ERR_NO_MEM, TAG, "tel list alloc");
    }

    memset(history, 0, sizeof(*history));
    history->entry.tel = tel_arr;
    history->entry.tel_count = n_tel;

    size_t tel_idx = 0;
    char *line = str;
    for (;;) {
        char *eol = strchr(line, '\n');
        if (eol) {
            *eol = '\0';
        }
        char *r = strchr(line, '\r');
        if (r) {
            *r = '\0';
        }

        char *value = vcard_line_value(line);
        if (value && *value) {
            char *name_end = strchr(line, ';');
            if (!name_end) {
                name_end = strchr(line, ':');
            }
            size_t name_len = name_end ? (size_t)(name_end - line) : strlen(line);
            const char *prop_name = line;
            size_t prop_len = name_len;
            char *dot = NULL;
            for (size_t i = 0; i < name_len; i++) {
                if (line[i] == '.') {
                    dot = line + i;
                }
            }
            if (dot != NULL) {
                prop_name = dot + 1;
                prop_len = name_len - (size_t)(prop_name - line);
            }

            if (prop_len == 1 && strncasecmp(prop_name, "N", 1) == 0) {
                char *n = value;
                history->entry.name.last_name = n;
                for (int i = 0; i < 5; i++) {
                    char *semi = strchr(n, ';');
                    if (semi) {
                        *semi = '\0';
                        n = semi + 1;
                        if (i == 0) {
                            history->entry.name.first_name = n;
                        } else if (i == 1) {
                            history->entry.name.middle_name = n;
                        } else if (i == 2) {
                            history->entry.name.prefix = n;
                        } else if (i == 3) {
                            history->entry.name.suffix = n;
                        }
                    } else {
                        if (i == 0) {
                            history->entry.name.first_name = n + strlen(n);
                        } else if (i == 1) {
                            history->entry.name.middle_name = n + strlen(n);
                        } else if (i == 2) {
                            history->entry.name.prefix = n + strlen(n);
                        } else if (i == 3) {
                            history->entry.name.suffix = n + strlen(n);
                        }
                        break;
                    }
                }
            } else if (prop_len == 2 && strncasecmp(prop_name, "FN", 2) == 0) {
                vcard_trim_line_end(value);
                history->entry.fullname = value;
            } else if (prop_len == 3 && strncasecmp(prop_name, "TEL", 3) == 0 && tel_idx < history->entry.tel_count) {
                vcard_trim_line_end(value);
                history->entry.tel[tel_idx].number = value;
                char *params = strchr(line, ';');
                if (params) {
                    char *type_key = strstr(params, "TYPE=");
                    if (type_key) {
                        history->entry.tel[tel_idx].type = type_key + 5;  /* after "TYPE=" */
                        char *t = history->entry.tel[tel_idx].type;
                        char *end = strchr(t, ':');
                        if (!end) {
                            end = strchr(t, ';');
                        }
                        if (end) {
                            *end = '\0';
                        }
                        size_t L = strlen(t);
                        if (L >= 2 && t[0] == '"' && t[L - 1] == '"') {
                            t[L - 1] = '\0';
                            history->entry.tel[tel_idx].type = t + 1;
                        }
                    }
                }
                tel_idx++;
            } else if (prop_len == 20 && strncasecmp(prop_name, "X-IRMC-CALL-DATETIME", 20) == 0) {
                vcard_trim_line_end(value);
                history->timestamp = value;
                char *params = strchr(line, ';');
                if (params) {
                    params++;
                    char *end = strchr(params, ';');
                    if (!end) {
                        end = strchr(params, ':');
                    }
                    if (end) {
                        *end = '\0';
                    }
                    if (strncasecmp(params, "TYPE=", 5) == 0) {
                        params += 5;
                    }
                    if (strncasecmp(params, "RECEIVED", 8) == 0 || strncasecmp(params, "DIALED", 6) == 0 || strncasecmp(params, "MISSED", 6) == 0) {
                        history->property = params;
                    }
                }
            } else if (prop_len == 15 && strncasecmp(prop_name, "X-CALL-DATETIME", 15) == 0) {
                vcard_trim_line_end(value);
                history->timestamp = value;
                char *params = strchr(line, ';');
                if (params) {
                    params++;
                    char *end = strchr(params, ';');
                    if (!end) {
                        end = strchr(params, ':');
                    }
                    if (end) {
                        *end = '\0';
                    }
                    if (strncasecmp(params, "TYPE=", 5) == 0) {
                        params += 5;
                    }
                    if (strncasecmp(params, "RECEIVED", 8) == 0 || strncasecmp(params, "DIALED", 6) == 0 || strncasecmp(params, "MISSED", 6) == 0) {
                        history->property = params;
                    }
                }
            } else if (prop_len >= 2 && strncasecmp(prop_name, "X-", 2) == 0 && !history->property) {
                if (strncasecmp(prop_name, "X-IRMC-CALL-DATETIME", 20) != 0 && strncasecmp(prop_name, "X-CALL-DATETIME", 15) != 0) {
                    vcard_trim_line_end(value);
                    history->property = value;
                }
            }
        }

        if (!eol) {
            break;
        }
        line = eol + 1;
    }

    return ESP_OK;
}

static void bt_app_pbac_cb(esp_pbac_event_t event, esp_pbac_param_t *param)
{
    switch (event) {
        case ESP_PBAC_CONNECTION_STATE_EVT: {
            ESP_LOGI(TAG, "PBAC connection state evt: state %d", param->conn_stat.connected);
            if (param->conn_stat.connected) {
                pbac_ctx->conn_handle = param->conn_stat.handle;
                esp_pbac_pull_phone_book_app_param_t app_param = {0};
                app_param.include_max_list_count = 1;
                app_param.max_list_count = 0;
                esp_pbac_pull_phone_book(pbac_ctx->conn_handle, "telecom/pb.vcf", &app_param);
            } else {
                pbac_ctx->conn_handle = ESP_PBAC_INVALID_HANDLE;
            }
            break;
        }
        case ESP_PBAC_SET_PHONE_BOOK_RESPONSE_EVT: {
            ESP_LOGD(TAG, "PBAC set phone book response evt: result %d", param->set_phone_book_rsp.result);
            break;
        }
        case ESP_PBAC_PULL_PHONE_BOOK_RESPONSE_EVT: {
            if (param->pull_phone_book_rsp.result == ESP_PBAC_SUCCESS) {
                if (param->pull_phone_book_rsp.final) {
                    if (param->pull_phone_book_rsp.include_phone_book_size) {
                        bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR,
                                              ESP_BT_AUDIO_EVENT_PHONEBOOK_COUNT,
                                              (void *)&param->pull_phone_book_rsp.phone_book_size);
                    }
                }
                if (param->pull_phone_book_rsp.data_len > 0) {
                    size_t dlen = param->pull_phone_book_rsp.data_len;
                    char *buf = heap_caps_calloc_prefer(1, dlen + 1, 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
                    if (buf) {
                        memcpy(buf, param->pull_phone_book_rsp.data, dlen);
                        buf[dlen] = '\0';
                        vcard_iter_t it;
                        vcard_iter_init(&it, buf, dlen);
                        char *vcard;
                        while (vcard_iter_next(&it, &vcard) == ESP_OK) {
                            esp_bt_audio_pb_history_t history = {0};
                            if (vcard_parse(vcard, &history) != ESP_OK) {
                                continue;
                            }
                            if (history.timestamp) {
                                bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR,
                                                      ESP_BT_AUDIO_EVENT_PHONEBOOK_HISTORY,
                                                      (void *)&history);
                            } else {
                                bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR,
                                                      ESP_BT_AUDIO_EVENT_PHONEBOOK_ENTRY,
                                                      (void *)&history.entry);
                            }
                            if (history.entry.tel) {
                                heap_caps_free(history.entry.tel);
                            }
                        }
                        heap_caps_free(buf);
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}

esp_err_t bt_audio_pbac_connect(uint8_t *bda)
{
    if (!pbac_ctx) {
        ESP_LOGE(TAG, "PBAC not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (pbac_ctx->conn_handle != ESP_PBAC_INVALID_HANDLE) {
        ESP_LOGE(TAG, "PBAC already connected");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = esp_pbac_connect(bda);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect PBAC: %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "PBAC connect");
    return ESP_OK;
}

esp_err_t bt_audio_pbac_disconnect(uint8_t *bda)
{
    if (!pbac_ctx) {
        ESP_LOGE(TAG, "PBAC not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (pbac_ctx->conn_handle == ESP_PBAC_INVALID_HANDLE) {
        ESP_LOGE(TAG, "PBAC not connected");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = esp_pbac_disconnect(pbac_ctx->conn_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disconnect PBAC: %d", ret);
        return ret;
    }
    pbac_ctx->conn_handle = ESP_PBAC_INVALID_HANDLE;
    ESP_LOGI(TAG, "PBAC disconnect");
    return ESP_OK;
}

esp_err_t bt_audio_pbac_fetch(uint8_t target, uint16_t start_idx, uint16_t count)
{
    if (!pbac_ctx) {
        ESP_LOGE(TAG, "PBAC not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (pbac_ctx->conn_handle == ESP_PBAC_INVALID_HANDLE) {
        ESP_LOGE(TAG, "PBAC not connected");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = ESP_OK;
    esp_pbac_pull_phone_book_app_param_t app_param = {0};
    if (start_idx > 0) {
        app_param.include_list_start_offset = 1;
        app_param.list_start_offset = start_idx;
    }
    if (count > 0) {
        app_param.include_max_list_count = 1;
        app_param.max_list_count = count;
    }
    if (target == ESP_BT_AUDIO_PB_FETCH_TARGET_MAIN_PB) {
        ret = esp_pbac_pull_phone_book(pbac_ctx->conn_handle, "telecom/pb.vcf", &app_param);
    } else if (target == ESP_BT_AUDIO_PB_FETCH_TARGET_INCOMING_HISTORY) {
        ret = esp_pbac_pull_phone_book(pbac_ctx->conn_handle, "telecom/ich.vcf", &app_param);
    } else if (target == ESP_BT_AUDIO_PB_FETCH_TARGET_OUTGOING_HISTORY) {
        ret = esp_pbac_pull_phone_book(pbac_ctx->conn_handle, "telecom/och.vcf", &app_param);
    } else if (target == ESP_BT_AUDIO_PB_FETCH_TARGET_MISSED_HISTORY) {
        ret = esp_pbac_pull_phone_book(pbac_ctx->conn_handle, "telecom/mch.vcf", &app_param);
    } else if (target == ESP_BT_AUDIO_PB_FETCH_TARGET_COMBINED_HISTORY) {
        ret = esp_pbac_pull_phone_book(pbac_ctx->conn_handle, "telecom/cch.vcf", &app_param);
    } else {
        ESP_LOGE(TAG, "Invalid target: %d", target);
        return ESP_ERR_INVALID_ARG;
    }
    return ret;
}

esp_err_t bt_audio_pbac_init(void)
{
    if (pbac_ctx) {
        ESP_LOGI(TAG, "PBAC already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    pbac_ctx = heap_caps_calloc_prefer(1, sizeof(pbac_ctx_t), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(pbac_ctx, ESP_ERR_NO_MEM, TAG, "Failed to malloc space for pbac_ctx");
    ESP_ERROR_CHECK(esp_pbac_register_callback(bt_app_pbac_cb));
    ESP_ERROR_CHECK(esp_pbac_init());
    esp_bt_audio_pb_ops_t pb_ops = {0};
    bt_audio_ops_get_pb(&pb_ops);
    pb_ops.fetch = bt_audio_pbac_fetch;
    bt_audio_ops_set_pb(&pb_ops);
    esp_bt_audio_classic_ops_t classic_ops;
    bt_audio_ops_get_classic(&classic_ops);
    classic_ops.pbac_connect = bt_audio_pbac_connect;
    classic_ops.pbac_disconnect = bt_audio_pbac_disconnect;
    bt_audio_ops_set_classic(&classic_ops);
    ESP_LOGI(TAG, "PBAC init success");
    return ESP_OK;
}

esp_err_t bt_audio_pbac_deinit(void)
{
    if (!pbac_ctx) {
        ESP_LOGI(TAG, "PBAC not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    esp_bt_audio_pb_ops_t pb_ops;
    bt_audio_ops_get_pb(&pb_ops);
    pb_ops.fetch = NULL;
    bt_audio_ops_set_pb(&pb_ops);
    ESP_ERROR_CHECK(esp_pbac_deinit());
    heap_caps_free(pbac_ctx);
    pbac_ctx = NULL;
    ESP_LOGI(TAG, "PBAC deinit success");
    return ESP_OK;
}
