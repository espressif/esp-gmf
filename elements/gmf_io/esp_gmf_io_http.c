/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdbool.h>
#include <errno.h>
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "gzip_miniz.h"

#include "esp_gmf_new_databus.h"
#include "esp_gmf_io_http.h"
#include "esp_gmf_oal_mem.h"

#define HTTP_MAX_CONNECT_TIMES  (5)

/**
 * @brief  Http io context in GMF
 */
typedef struct http_io {
    esp_gmf_io_t              base;           /*!< The GMF http io handle */
    bool                      is_open;        /*!< The flag of whether opened */
    esp_http_client_handle_t  client;         /*!< The http client handle */
    int                       _errno;         /*!< Errno code for http */
    int                       connect_times;  /*!< Max reconnect times */
    bool                      gzip_encoding;  /*!< Content is encoded */
    gzip_miniz_handle_t       gzip;           /*!< GZIP instance */
} http_stream_t;

static const char *TAG = "ESP_GMF_HTTP";

static esp_gmf_err_t _http_destroy(esp_gmf_io_handle_t self);

static int _gzip_read_data(uint8_t *data, int size, void *ctx)
{
    http_stream_t *http = (http_stream_t *)ctx;
    return esp_http_client_read(http->client, (char *)data, size);
}

static esp_gmf_err_t _http_event_handle(esp_http_client_event_t *evt)
{
    http_stream_t *http = (http_stream_t *)evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_HEADER) {
        return ESP_GMF_ERR_OK;
    }
    if (strcasecmp(evt->header_key, "Content-Encoding") == 0) {
        http->gzip_encoding = true;
        if (strcasecmp(evt->header_value, "gzip") == 0) {
            gzip_miniz_cfg_t cfg = {
                .chunk_size = 1024,
                .ctx = http,
                .read_cb = _gzip_read_data,
            };
            http->gzip = gzip_miniz_init(&cfg);
        }
        if (http->gzip == NULL) {
            ESP_LOGE(TAG, "Content-Encoding %s not supported", evt->header_value);
            return ESP_GMF_ERR_FAIL;
        }
    }
    return ESP_GMF_ERR_OK;
}

static int dispatch_hook(esp_gmf_io_handle_t self, http_stream_event_id_t type, void *buffer, int buffer_len)
{
    http_stream_t *http_stream = (http_stream_t *)self;
    http_io_cfg_t *http_io_cfg = (http_io_cfg_t *)OBJ_GET_CFG(http_stream);
    http_stream_event_msg_t msg;
    msg.event_id = type;
    msg.http_client = http_stream->client;
    msg.user_data = http_io_cfg->user_data;
    msg.buffer = buffer;
    msg.buffer_len = buffer_len;
    if (http_io_cfg->event_handle) {
        return http_io_cfg->event_handle(&msg);
    }
    return ESP_GMF_ERR_OK;
}

static int _http_read_data(http_stream_t *http, char *buffer, int len)
{
    if (http->gzip_encoding == false) {
        return esp_http_client_read(http->client, buffer, len);
    }
    // use gzip to uncompress data
    return gzip_miniz_read(http->gzip, (uint8_t *)buffer, len);
}

static esp_gmf_err_t _http_new(void *cfg, esp_gmf_obj_handle_t *io)
{
    return esp_gmf_io_http_init(cfg, io);
}

static esp_gmf_err_t _http_get_score(esp_gmf_io_handle_t handle, const char *url, int *score)
{
    *score = ESP_GMF_IO_SCORE_NONE;
    if (strncasecmp(url, "http://", 7) == 0 || strncasecmp(url, "https://", 8) == 0) {
        *score = ESP_GMF_IO_SCORE_STANDARD;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _http_open(esp_gmf_io_handle_t self)
{
    http_stream_t *http = (http_stream_t *)self;
    esp_gmf_err_t err;
    if (http->is_open) {
        ESP_LOGW(TAG, "The HTTP already opened, %p", http);
        return ESP_GMF_ERR_OK;
    }
    http->_errno = 0;
    esp_gmf_info_file_t info = {0};
    esp_gmf_io_get_info((esp_gmf_io_handle_t)http, &info);
    char *uri = (char *)info.uri;
    if (uri == NULL) {
        ESP_LOGE(TAG, "Error open connection, uri = NULL");
        return ESP_GMF_ERR_FAIL;
    }

    http_io_cfg_t *http_io_cfg = (http_io_cfg_t *)OBJ_GET_CFG(http);
    ESP_LOGI(TAG, "HTTP Open, URI = %s", uri);
    if (http->client == NULL) {
        esp_http_client_config_t http_cfg = {
            .url = uri,
            .event_handler = _http_event_handle,
            .user_data = self,
            .timeout_ms = 30 * 1000,
            .buffer_size = HTTP_STREAM_BUFFER_SIZE,
            .buffer_size_tx = 1024,
            .cert_pem = http_io_cfg->cert_pem,
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
            .crt_bundle_attach = http_io_cfg->crt_bundle_attach,
#endif  /* CONFIG_MBEDTLS_CERTIFICATE_BUNDLE */
        };
        http->client = esp_http_client_init(&http_cfg);
        ESP_GMF_CHECK(TAG, http->client, return ESP_GMF_ERR_MEMORY_LACK, "Failed to initialize http client");
    } else {
        esp_http_client_set_url(http->client, uri);
    }

    if (info.pos) {
        char rang_header[32];
        snprintf(rang_header, 32, "bytes=%d-", (int)info.pos);
        esp_http_client_set_header(http->client, "Range", rang_header);
    } else {
        esp_http_client_delete_header(http->client, "Range");
    }

    if (dispatch_hook(self, HTTP_STREAM_PRE_REQUEST, NULL, 0) != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to process user callback:%d", __LINE__);
        return ESP_GMF_ERR_FAIL;
    }

    if (http_io_cfg->dir == ESP_GMF_IO_DIR_WRITER) {
        err = esp_http_client_open(http->client, -1);
        if (err == ESP_GMF_ERR_OK) {
            http->is_open = true;
        }
        return err;
    }

    char *buffer = NULL;
    int post_len = esp_http_client_get_post_field(http->client, &buffer);
_stream_redirect:
    if (http->gzip_encoding) {
        gzip_miniz_deinit(http->gzip);
        http->gzip = NULL;
        http->gzip_encoding = false;
    }
    if ((err = esp_http_client_open(http->client, post_len)) != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open http stream");
        return err;
    }

    int wrlen = dispatch_hook(self, HTTP_STREAM_ON_REQUEST, buffer, post_len);
    if (wrlen < 0) {
        ESP_LOGE(TAG, "Failed to process user callback:%d", __LINE__);
        return ESP_GMF_ERR_FAIL;
    }
    if (post_len && buffer && wrlen == 0) {
        if (esp_http_client_write(http->client, buffer, post_len) <= 0) {
            ESP_LOGE(TAG, "Failed to write data to http stream");
            return ESP_GMF_ERR_FAIL;
        }
        ESP_LOGD(TAG, "len=%d, data=%s", post_len, buffer);
    }

    if (dispatch_hook(self, HTTP_STREAM_POST_REQUEST, NULL, 0) < 0) {
        esp_http_client_close(http->client);
        return ESP_GMF_ERR_FAIL;
    }
    /**
     * Due to the total byte of content has been changed after seek, set info.total_bytes at beginning only.
     */
    int64_t cur_pos = esp_http_client_fetch_headers(http->client);
    esp_gmf_io_get_info((esp_gmf_io_handle_t)http, &info);
    if (info.pos <= 0) {
        info.size = cur_pos;
    }

    ESP_LOGI(TAG, "The total size is %d bytes", (int)info.size);
    int status_code = esp_http_client_get_status_code(http->client);
    if (status_code == 301 || status_code == 302) {
        esp_http_client_set_redirection(http->client);
        goto _stream_redirect;
    }
    if (status_code != 200 && (esp_http_client_get_status_code(http->client) != 206)) {
        ESP_LOGE(TAG, "Invalid HTTP stream, status code = %d", status_code);
        return ESP_GMF_ERR_FAIL;
    }
    esp_gmf_io_set_size(self, info.size);
    http->is_open = true;

    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _http_prev_close(esp_gmf_io_handle_t self)
{
    http_stream_t *http = (http_stream_t *)self;
    if (http->is_open && (http->base.dir == ESP_GMF_IO_DIR_WRITER)) {
        do {
            if (dispatch_hook(self, HTTP_STREAM_POST_REQUEST, NULL, 0) < 0) {
                break;
            }
            if (esp_http_client_fetch_headers(http->client) < 0) {
                break;
            }
            if (dispatch_hook(self, HTTP_STREAM_FINISH_REQUEST, NULL, 0) < 0) {
                break;
            }
        } while (0);
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 3)
    if (http->client) {
        int fd = esp_http_client_get_socket(http->client);
        if (fd) {
            shutdown(fd, SHUT_RDWR);
        }
    }
#endif  /* ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 3) */
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _http_close(esp_gmf_io_handle_t self)
{
    http_stream_t *http = (http_stream_t *)self;
    ESP_LOGD(TAG, "_http_close, %p", http);
    http->is_open = false;
    if (http->gzip) {
        gzip_miniz_deinit(http->gzip);
        http->gzip = NULL;
    }
    if (http->client) {
        esp_http_client_close(http->client);
        esp_http_client_cleanup(http->client);
        http->client = NULL;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _http_reconnect(esp_gmf_io_handle_t self)
{
    esp_gmf_err_t err = ESP_GMF_ERR_OK;
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_FAIL);
    err |= _http_close(self);
    err |= _http_open(self);
    return err;
}

static int _http_read(esp_gmf_io_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    http_stream_t *http = (http_stream_t *)self;
    int wrlen = dispatch_hook(self, HTTP_STREAM_ON_RESPONSE, buffer, len);
    int rlen = wrlen;
    if (rlen == 0) {
        rlen = _http_read_data(http, buffer, len);
    }
    if (rlen <= 0) {
        http->_errno = esp_http_client_get_errno(http->client);
        ESP_LOGW(TAG, "No more data, errno: %d, rlen = %d", http->_errno, rlen);
        if (http->_errno != 0) {  // Error occuered, reset connection
            ESP_LOGE(TAG, "Got %d errno(%s)", http->_errno, strerror(http->_errno));
        }
    }
    ESP_LOGD(TAG, "req length = %d, read = %d", len, rlen);
    return rlen;
}

static int _http_write(esp_gmf_io_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    http_stream_t *http = (http_stream_t *)self;
    int wrlen = dispatch_hook(self, HTTP_STREAM_ON_REQUEST, buffer, len);
    if (wrlen < 0) {
        ESP_LOGE(TAG, "Failed to process user callback:%d", __LINE__);
        return ESP_GMF_ERR_FAIL;
    }
    if (wrlen > 0) {
        return wrlen;
    }
    if ((wrlen = esp_http_client_write(http->client, buffer, len)) <= 0) {
        http->_errno = esp_http_client_get_errno(http->client);
        ESP_LOGE(TAG, "Failed to write data to http stream, wrlen = %d, errno = %d(%s)", wrlen, http->_errno, strerror(http->_errno));
    }
    return wrlen;
}

static esp_gmf_err_t _http_destroy(esp_gmf_io_handle_t self)
{
    http_stream_t *http = (http_stream_t *)self;
    ESP_LOGD(TAG, "%s-%p", __FUNCTION__, http);
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_io_deinit(self);
    esp_gmf_oal_free(http);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _http_seek(esp_gmf_io_handle_t handle, uint64_t pos)
{
    http_stream_t *http = (http_stream_t *)handle;
    ESP_LOGD(TAG, "HTTP Seek to: %lld, %p", pos, http);
    _http_close(handle);
    _http_open(handle);

    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_io_t _http_acquire_read(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int block_ticks)
{
    http_stream_t *http = (http_stream_t *)handle;
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    int rlen = _http_read(handle, (char *)pload->buf, wanted_size, block_ticks, NULL);
    ESP_LOGD(TAG, "Read len: %d-%ld", rlen, wanted_size);
    if (rlen > 0) {
        pload->valid_size = rlen;
        http->connect_times = 0;
    } else if (rlen == 0) {
        pload->valid_size = 0;
        pload->is_done = true;
        ESP_LOGI(TAG, "No more data, ret: %d", rlen);
    } else {
        pload->valid_size = 0;
        ESP_LOGE(TAG, "The error is happened in reading data, return value: %d, error msg: %s", rlen, strerror(http->_errno));
        if (http->connect_times > HTTP_MAX_CONNECT_TIMES) {
            ESP_LOGE(TAG, "Reconnect times more than %d, disconnect http stream", http->connect_times);
            return ESP_GMF_IO_FAIL;
        }
        http->connect_times++;
        esp_gmf_err_t ret = _http_reconnect(handle);
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to reset http connection");
            return ESP_GMF_IO_FAIL;
        } else {
            ESP_LOGW(TAG, "Reconnect to peer successful");
        }
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t _http_release_read(esp_gmf_io_handle_t handle, void *payload, int block_ticks)
{
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t _http_acquire_write(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int block_ticks)
{
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t _http_release_write(esp_gmf_io_handle_t handle, void *payload, int block_ticks)
{
    http_stream_t *http = (http_stream_t *)handle;
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    size_t wlen_total = _http_write(handle, (char *)pload->buf, pload->valid_size, block_ticks, NULL);
    if (wlen_total <= 0) {
        ESP_LOGE(TAG, "The error is happened in writing data, error msg:%s", strerror(http->_errno));
        return ESP_GMF_IO_FAIL;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_t _http_reload(esp_gmf_io_handle_t handle, const char *new_uri)
{
    http_stream_t *http = (http_stream_t *)handle;
    http->is_open = false;
    esp_gmf_io_set_uri(handle, new_uri);
    return _http_open(handle);
}

esp_gmf_err_t esp_gmf_io_http_reset(esp_gmf_io_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_io_http_set_server_cert(esp_gmf_io_handle_t handle, const char *cert)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    ESP_GMF_NULL_CHECK(TAG, cert, return ESP_GMF_ERR_INVALID_ARG);
    http_io_cfg_t *http_io_cfg = (http_io_cfg_t *)OBJ_GET_CFG(handle);
    http_io_cfg->cert_pem = cert;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_io_http_set_event_callback(esp_gmf_io_handle_t handle, http_io_event_handle_t event_callback)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    http_io_cfg_t *http_io_cfg = (http_io_cfg_t *)OBJ_GET_CFG(handle);
    http_io_cfg->event_handle = event_callback;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_io_http_init(http_io_cfg_t *config, esp_gmf_io_handle_t *io)
{
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, io, return ESP_GMF_ERR_INVALID_ARG);
    *io = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    http_stream_t *http = esp_gmf_oal_calloc(1, sizeof(http_stream_t));
    ESP_GMF_MEM_VERIFY(TAG, http, return ESP_GMF_ERR_MEMORY_LACK,
                       "http stream", sizeof(http_stream_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)http;
    obj->new_obj = _http_new;
    obj->del_obj = _http_destroy;
    http_io_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto _http_init_fail;}, "http stream configuration", sizeof(*config));
    memcpy(cfg, config, sizeof(*config));
    esp_gmf_obj_set_config(obj, cfg, sizeof(*config));
    ret = esp_gmf_obj_set_tag(obj, "io_http");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto _http_init_fail, "Failed to set obj tag");
    http->base.dir = config->dir;
    http->base.type = ESP_GMF_IO_TYPE_BLOCK;
    http->base.get_score = _http_get_score;
    http->base.open = _http_open;
    http->base.seek = _http_seek;
    http->base.prev_close = _http_prev_close;
    http->base.close = _http_close;
    http->base.reset = esp_gmf_io_http_reset;
    http->base.reload = _http_reload;
    if (config->dir == ESP_GMF_IO_DIR_WRITER) {
        http->base.acquire_write = _http_acquire_write;
        http->base.release_write = _http_release_write;
    } else if (config->dir == ESP_GMF_IO_DIR_READER) {
        http->base.acquire_read = _http_acquire_read;
        http->base.release_read = _http_release_read;
    } else {
        ESP_LOGE(TAG, "Does not set read or write function");
        ret = ESP_GMF_ERR_NOT_SUPPORT;
        goto _http_init_fail;
    }
    esp_gmf_io_cfg_t io_cfg = {
        .thread = {
            .stack = config->io_cfg.thread.stack,
            .prio = config->io_cfg.thread.prio,
            .core = config->io_cfg.thread.core,
            .stack_in_ext = config->io_cfg.thread.stack_in_ext,
        },
        .buffer_cfg = {
            .io_size = config->io_cfg.buffer_cfg.io_size,
            .buffer_size = config->io_cfg.buffer_cfg.buffer_size,
        },
        .enable_speed_monitor = config->io_cfg.enable_speed_monitor,
    };
    ret = esp_gmf_io_init(&http->base, &io_cfg);
    if (ret != ESP_GMF_ERR_OK) {
        goto _http_init_fail;
    }
    *io = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(http), http);
    return ESP_GMF_ERR_OK;
_http_init_fail:
    esp_gmf_obj_delete(obj);
    return ret;
}
