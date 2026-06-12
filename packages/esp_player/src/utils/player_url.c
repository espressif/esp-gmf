/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_gmf_uri_parser.h"
#include "sdkconfig.h"

#if CONFIG_ESP_PLAYER_ENABLE_FILE_IO
#include "esp_gmf_io_file.h"
#endif  /* CONFIG_ESP_PLAYER_ENABLE_FILE_IO */
#if CONFIG_ESP_PLAYER_ENABLE_HTTP_IO
#include "esp_gmf_io_http.h"
#endif  /* CONFIG_ESP_PLAYER_ENABLE_HTTP_IO */
#if CONFIG_ESP_PLAYER_ENABLE_HLS_IO
#include "esp_hls_io.h"
#endif  /* CONFIG_ESP_PLAYER_ENABLE_HLS_IO */

#include "player_url.h"
#include "player_internal.h"
#include "player_submit_frame.h"
#include "player_helper.h"
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
#include "player_adec_defaults_cfg.h"
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */

typedef enum {
    PLAYER_URL_SCHEME_UNKNOWN = 0,
    PLAYER_URL_SCHEME_FILE,
    PLAYER_URL_SCHEME_HTTP,
    PLAYER_URL_SCHEME_HTTPS,
    PLAYER_URL_SCHEME_HLS,
} player_url_scheme_t;

typedef struct {
    player_url_scheme_t  scheme;
    char                *raw;
    char                *path;
    char                *query;
    char                *fragment;
} player_url_parsed_t;

static void player_url_parsed_free(player_url_parsed_t *parsed)
{
    if (parsed == NULL) {
        return;
    }
    free(parsed->raw);
    free(parsed->path);
    free(parsed->query);
    free(parsed->fragment);
    parsed->raw = NULL;
    parsed->path = NULL;
    parsed->query = NULL;
    parsed->fragment = NULL;
}

static bool player_path_has_ext(const char *path, const char *ext)
{
    if (path == NULL || ext == NULL) {
        return false;
    }
    size_t pl = strlen(path);
    size_t el = strlen(ext);
    return pl >= el && strcasecmp(path + pl - el, ext) == 0;
}

static bool path_looks_like_hls(const char *path, const char *full)
{
    if (path && strcasestr(path, ".m3u8") != NULL) {
        return true;
    }
    return full && strcasestr(full, ".m3u8") != NULL;
}

static bool player_query_get_ulong(const char *query, const char *key, unsigned long *out)
{
    if (query == NULL || key == NULL || out == NULL) {
        return false;
    }
    size_t klen = strlen(key);
    const char *p = query;
    while (*p != '\0') {
        const char *eq = strchr(p, '=');
        if (eq == NULL) {
            break;
        }
        if ((size_t)(eq - p) == klen && strncmp(p, key, klen) == 0) {
            char *end = NULL;
            unsigned long v = strtoul(eq + 1, &end, 10);
            if (end > eq + 1 && (*end == '&' || *end == '\0')) {
                *out = v;
                return true;
            }
        }
        const char *amp = strchr(eq + 1, '&');
        if (amp == NULL) {
            break;
        }
        p = amp + 1;
    }
    return false;
}

static bool player_query_get_bool(const char *query, const char *key, bool *out)
{
    unsigned long v = 0;
    if (!player_query_get_ulong(query, key, &v)) {
        return false;
    }
    *out = (v != 0);
    return true;
}

static esp_player_err_t file_vfs_path_dup(const char *url, char **path_out)
{
    if (url == NULL || path_out == NULL) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    *path_out = NULL;

    const char *p;
    if (url[0] == '/') {
        p = url;
    } else {
        const char *scheme = strstr(url, "://");
        if (scheme == NULL || strncasecmp(url, "file:", 5) != 0) {
            return ESP_PLAYER_ERR_INVALID_ARG;
        }
        p = scheme + 2;
        /* Non-RFC double-slash form: file://sdcard/... → treat as file:///sdcard/...
         * RFC 3986 would parse "sdcard" as the authority, but we accept it for compatibility. */
        if (p[0] == '/' && p[1] == '/') {
            p++;
        }
    }
    if (p[0] == '\0') {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    if (p[0] != '/') {
        size_t len = 1 + strlen(p) + 1;
        char *path = malloc(len);
        if (path == NULL) {
            return ESP_PLAYER_ERR_NO_MEM;
        }
        path[0] = '/';
        strcpy(path + 1, p);
        *path_out = path;
        return ESP_PLAYER_ERR_OK;
    }
    *path_out = strdup(p);
    if (*path_out == NULL) {
        return ESP_PLAYER_ERR_NO_MEM;
    }
    return ESP_PLAYER_ERR_OK;
}

static esp_player_err_t normalize_legacy_uri_dup(const char *url, char **norm_out)
{
    if (url == NULL || norm_out == NULL || url[0] == '\0') {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    *norm_out = NULL;

    if (url[0] == '/' && strstr(url, "://") == NULL) {
        *norm_out = strdup(url);
        return (*norm_out != NULL) ? ESP_PLAYER_ERR_OK : ESP_PLAYER_ERR_NO_MEM;
    }
    if (strncasecmp(url, "file:", 5) == 0) {
        return file_vfs_path_dup(url, norm_out);
    }
    if (strstr(url, "://") == NULL) {
        if (asprintf(norm_out, "file://%s", url) < 0 || *norm_out == NULL) {
            return ESP_PLAYER_ERR_NO_MEM;
        }
        return ESP_PLAYER_ERR_OK;
    }
    *norm_out = strdup(url);
    return (*norm_out != NULL) ? ESP_PLAYER_ERR_OK : ESP_PLAYER_ERR_NO_MEM;
}

static player_url_scheme_t scheme_from_string(const char *schema, const char *path, const char *full)
{
    if (schema == NULL || schema[0] == '\0') {
        return PLAYER_URL_SCHEME_UNKNOWN;
    }
    if (strcasecmp(schema, "file") == 0) {
#if CONFIG_ESP_PLAYER_ENABLE_FILE_IO
        return PLAYER_URL_SCHEME_FILE;
#else
        return PLAYER_URL_SCHEME_UNKNOWN;
#endif  /* CONFIG_ESP_PLAYER_ENABLE_FILE_IO */
    }
    if (strcasecmp(schema, "http") == 0) {
#if CONFIG_ESP_PLAYER_ENABLE_HLS_IO
        if (path_looks_like_hls(path, full)) {
            return PLAYER_URL_SCHEME_HLS;
        }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_HLS_IO */
#if CONFIG_ESP_PLAYER_ENABLE_HTTP_IO
        return PLAYER_URL_SCHEME_HTTP;
#else
        return PLAYER_URL_SCHEME_UNKNOWN;
#endif  /* CONFIG_ESP_PLAYER_ENABLE_HTTP_IO */
    }
    if (strcasecmp(schema, "https") == 0) {
#if CONFIG_ESP_PLAYER_ENABLE_HLS_IO
        if (path_looks_like_hls(path, full)) {
            return PLAYER_URL_SCHEME_HLS;
        }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_HLS_IO */
#if CONFIG_ESP_PLAYER_ENABLE_HTTP_IO
        return PLAYER_URL_SCHEME_HTTPS;
#else
        return PLAYER_URL_SCHEME_UNKNOWN;
#endif  /* CONFIG_ESP_PLAYER_ENABLE_HTTP_IO */
    }
    return PLAYER_URL_SCHEME_UNKNOWN;
}

static esp_player_err_t player_url_parse(const char *url, player_url_parsed_t *out)
{
    if (url == NULL || out == NULL) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    char *norm = NULL;
    esp_player_err_t ret = normalize_legacy_uri_dup(url, &norm);
    if (ret != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(ESP_PLAYER_TAG, "URI invalid: %s", url);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }

#if CONFIG_ESP_PLAYER_ENABLE_FILE_IO
    if (norm[0] == '/' && strstr(norm, "://") == NULL) {
        out->scheme = PLAYER_URL_SCHEME_FILE;
        const char *query_pos = strchr(norm, '?');
        if (query_pos != NULL) {
            out->query = strdup(query_pos + 1);
            if (out->query == NULL) {
                free(norm);
                return ESP_PLAYER_ERR_NO_MEM;
            }
            char *path_only = strndup(norm, (size_t)(query_pos - norm));
            if (path_only == NULL) {
                player_url_parsed_free(out);
                free(norm);
                return ESP_PLAYER_ERR_NO_MEM;
            }
            ret = file_vfs_path_dup(path_only, &out->path);
            free(path_only);
        } else {
            ret = file_vfs_path_dup(norm, &out->path);
        }
        if (ret != ESP_PLAYER_ERR_OK) {
            player_url_parsed_free(out);
            free(norm);
            return ret;
        }
        out->raw = strdup(out->path);
        free(norm);
        if (out->raw == NULL) {
            player_url_parsed_free(out);
            return ESP_PLAYER_ERR_NO_MEM;
        }
        ESP_LOGI(ESP_PLAYER_TAG, "parsed scheme=%d path=\"%s\"", (int)out->scheme, out->path);
        return ESP_PLAYER_ERR_OK;
    }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_FILE_IO */

    esp_gmf_uri_t *uri = NULL;
    if (esp_gmf_uri_parse(norm, &uri) != 0) {
        ESP_LOGE(ESP_PLAYER_TAG, "esp_gmf_uri_parse failed for \"%s\"", norm);
        free(norm);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }

    out->scheme = scheme_from_string(uri->scheme, uri->path, norm);
    if (out->scheme == PLAYER_URL_SCHEME_UNKNOWN) {
        ESP_LOGE(ESP_PLAYER_TAG, "Unsupported URI schema \"%s\"", uri->scheme ? uri->scheme : "(null)");
        esp_gmf_uri_free(uri);
        free(norm);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }

    out->raw = strdup(norm);
    if (out->raw == NULL) {
        esp_gmf_uri_free(uri);
        free(norm);
        return ESP_PLAYER_ERR_NO_MEM;
    }

    if (uri->fragment != NULL) {
        out->fragment = strdup(uri->fragment);
        if (out->fragment == NULL) {
            player_url_parsed_free(out);
            esp_gmf_uri_free(uri);
            free(norm);
            return ESP_PLAYER_ERR_NO_MEM;
        }
    }
    if (uri->query != NULL) {
        out->query = strdup(uri->query);
        if (out->query == NULL) {
            player_url_parsed_free(out);
            esp_gmf_uri_free(uri);
            free(norm);
            return ESP_PLAYER_ERR_NO_MEM;
        }
    }

    switch (out->scheme) {
        case PLAYER_URL_SCHEME_FILE: {
            free(out->raw);
            out->raw = NULL;
            free(out->path);
            out->path = NULL;
            ret = file_vfs_path_dup(norm, &out->path);
            if (ret != ESP_PLAYER_ERR_OK) {
                player_url_parsed_free(out);
                esp_gmf_uri_free(uri);
                free(norm);
                return ret;
            }
            out->raw = strdup(out->path);
            if (out->raw == NULL) {
                player_url_parsed_free(out);
                esp_gmf_uri_free(uri);
                free(norm);
                return ESP_PLAYER_ERR_NO_MEM;
            }
            break;
        }
        case PLAYER_URL_SCHEME_HTTP:
        case PLAYER_URL_SCHEME_HTTPS:
        case PLAYER_URL_SCHEME_HLS:
            if (uri->path != NULL && uri->path[0] != '\0') {
                out->path = strdup(uri->path);
                if (out->path == NULL) {
                    player_url_parsed_free(out);
                    esp_gmf_uri_free(uri);
                    free(norm);
                    return ESP_PLAYER_ERR_NO_MEM;
                }
            }
            break;
        default:
            player_url_parsed_free(out);
            esp_gmf_uri_free(uri);
            free(norm);
            return ESP_PLAYER_ERR_INVALID_ARG;
    }

    esp_gmf_uri_free(uri);
    free(norm);

    ESP_LOGI(ESP_PLAYER_TAG, "parsed scheme=%d path=\"%s\"",
             (int)out->scheme, out->path ? out->path : "");
    return ESP_PLAYER_ERR_OK;
}

#if CONFIG_ESP_PLAYER_ENABLE_HLS_IO
static esp_player_err_t player_hls_init(esp_player_stream_t *stream)
{
    if (stream->io_pool == NULL) {
        esp_gmf_err_t ret = esp_gmf_pool_init(&stream->io_pool);
        if (ret != ESP_GMF_ERR_OK) {
            stream->io_pool = NULL;
            ESP_LOGE(ESP_PLAYER_TAG, "HLS pool init failed, ret %d", ret);
            return ESP_PLAYER_ERR_FAIL;
        }
        http_io_cfg_t cfg = HTTP_STREAM_CFG_DEFAULT();
        cfg.io_cfg.buffer_cfg.buffer_size = player_cfg_http_read_buf_size(stream);
        cfg.dir = ESP_GMF_IO_DIR_READER;
        esp_gmf_io_handle_t http_io = NULL;
        if (esp_gmf_io_http_init(&cfg, &http_io) != ESP_GMF_ERR_OK) {
            esp_gmf_pool_deinit(stream->io_pool);
            stream->io_pool = NULL;
            return ESP_PLAYER_ERR_FAIL;
        }
        ret = esp_gmf_pool_register_io(stream->io_pool, http_io, NULL);
        if (ret != ESP_GMF_ERR_OK) {
            esp_gmf_io_deinit(http_io);
            esp_gmf_obj_delete(http_io);
            esp_gmf_pool_deinit(stream->io_pool);
            stream->io_pool = NULL;
            return ESP_PLAYER_ERR_FAIL;
        }
    }

    esp_hls_io_cfg_t hls_cfg = {0};
    hls_cfg.name = "io_hls";
    hls_cfg.pool = stream->io_pool;
    if (esp_gmf_io_hls_init(&hls_cfg, &stream->input_handle) != ESP_GMF_ERR_OK) {
        esp_gmf_pool_deinit(stream->io_pool);
        stream->io_pool = NULL;
        return ESP_PLAYER_ERR_FAIL;
    }
    return ESP_PLAYER_ERR_OK;
}
#endif  /* CONFIG_ESP_PLAYER_ENABLE_HLS_IO */

#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
/* Headerless PCM parameters come from the URL query only:
 * /path/foo.pcm?sr=<Hz>&ch=<n>&bits=<n>
 * https://host/foo.pcm?sr=<Hz>&ch=<n>&bits=<n> */
static esp_player_err_t player_apply_raw_pcm_query(esp_player_stream_t *stream, const char *path,
                                                   const char *query)
{
    if (!player_path_has_ext(path, ".pcm")) {
        return ESP_PLAYER_ERR_OK;
    }
    if (stream->audio_side == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "PCM playback requires audio enabled in av_mask");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    unsigned long sr = 0;
    unsigned long ch = 0;
    unsigned long bits = 0;
    if (!player_query_get_ulong(query, "sr", &sr) || sr == 0 || sr > UINT32_MAX) {
        ESP_LOGE(ESP_PLAYER_TAG, "PCM requires ?sr= in URL");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    if (!player_query_get_ulong(query, "ch", &ch) || ch == 0 || ch > UINT8_MAX) {
        ESP_LOGE(ESP_PLAYER_TAG, "PCM requires ?ch= in URL");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    if (!player_query_get_ulong(query, "bits", &bits) || bits == 0 || bits > UINT8_MAX) {
        ESP_LOGE(ESP_PLAYER_TAG, "PCM requires ?bits= in URL");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }

    esp_player_audio_stream_info_t *ai = &stream->audio_side->track_info.audio_info;
    ai->format = ESP_AUDIO_SIMPLE_DEC_TYPE_PCM;
    ai->sample_rate = (uint32_t)sr;
    ai->channels = (uint8_t)ch;
    ai->bits_per_sample = (uint8_t)bits;

    stream->dec_cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_PCM;
    if (player_prepare_dec_cfg(stream, ESP_AUDIO_SIMPLE_DEC_TYPE_PCM) != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to prepare PCM decoder config");
        return ESP_PLAYER_ERR_FAIL;
    }
    esp_pcm_dec_cfg_t *pcm_cfg = (esp_pcm_dec_cfg_t *)stream->dec_cfg.dec_cfg;
    if (pcm_cfg != NULL) {
        pcm_cfg->sample_rate = ai->sample_rate;
        pcm_cfg->channel = ai->channels;
        pcm_cfg->bits_per_sample = ai->bits_per_sample;
    }

    ESP_LOGI(ESP_PLAYER_TAG, "RAW PCM source: sr=%lu ch=%lu bits=%lu", sr, ch, bits);
    return ESP_PLAYER_ERR_OK;
}
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */

static esp_player_err_t player_get_input_io(esp_player_stream_t *stream, const char *url)
{
    if (stream == NULL || url == NULL || url[0] == '\0') {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, stream: %p, url: %s", stream, url ? url : "(null)");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }

    player_url_parsed_t parsed = {0};
    esp_player_err_t ret = player_url_parse(url, &parsed);
    if (ret != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid media URI: %s", url);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    ESP_LOGI(ESP_PLAYER_TAG, "url: %s (scheme=%d path=%s)", url, (int)parsed.scheme,
             parsed.path ? parsed.path : "");

#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
    if (parsed.scheme == PLAYER_URL_SCHEME_FILE
        || parsed.scheme == PLAYER_URL_SCHEME_HTTP
        || parsed.scheme == PLAYER_URL_SCHEME_HTTPS) {
        ret = player_apply_raw_pcm_query(stream, parsed.path, parsed.query);
        if (ret != ESP_PLAYER_ERR_OK) {
            player_url_parsed_free(&parsed);
            return ret;
        }
    }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */

    switch (parsed.scheme) {
        case PLAYER_URL_SCHEME_HLS:
#if CONFIG_ESP_PLAYER_ENABLE_HLS_IO
            ret = player_hls_init(stream);
#endif  /* CONFIG_ESP_PLAYER_ENABLE_HLS_IO */
            break;
        case PLAYER_URL_SCHEME_HTTP:
        case PLAYER_URL_SCHEME_HTTPS: {
#if CONFIG_ESP_PLAYER_ENABLE_HTTP_IO
            http_io_cfg_t cfg = HTTP_STREAM_CFG_DEFAULT();
            cfg.io_cfg.buffer_cfg.buffer_size = player_cfg_http_read_buf_size(stream);
            cfg.dir = ESP_GMF_IO_DIR_READER;
            ret = (esp_gmf_io_http_init(&cfg, &stream->input_handle) == ESP_GMF_ERR_OK) ? ESP_PLAYER_ERR_OK : ESP_PLAYER_ERR_FAIL;
#endif  /* CONFIG_ESP_PLAYER_ENABLE_HTTP_IO */
            break;
        }
        case PLAYER_URL_SCHEME_FILE: {
#if CONFIG_ESP_PLAYER_ENABLE_FILE_IO
            file_io_cfg_t cfg = {
                .dir = ESP_GMF_IO_DIR_READER,
            };
            ret = (esp_gmf_io_file_init(&cfg, &stream->input_handle) == ESP_GMF_ERR_OK) ? ESP_PLAYER_ERR_OK : ESP_PLAYER_ERR_FAIL;
#endif  /* CONFIG_ESP_PLAYER_ENABLE_FILE_IO */
            break;
        }
        default:
            ret = ESP_PLAYER_ERR_INVALID_ARG;
            break;
    }

    if (ret != ESP_PLAYER_ERR_OK) {
        player_url_parsed_free(&parsed);
        return ret;
    }

    if (esp_gmf_io_set_uri(stream->input_handle, parsed.raw) != ESP_GMF_ERR_OK) {
        player_url_parsed_free(&parsed);
        return ESP_PLAYER_ERR_FAIL;
    }
    player_url_parsed_free(&parsed);
    return ESP_PLAYER_ERR_OK;
}

static void player_free_frame_url_mem(esp_player_stream_t *stream)
{
    free(stream->frame_url);
    stream->frame_url = NULL;
}

static bool player_url_is_frame_mode(const char *url, esp_player_dec_frame_mode_t *mode_out)
{
    if (url == NULL || mode_out == NULL) {
        return false;
    }
    if (strncasecmp(url, "fill:", 5) == 0) {
        *mode_out = ESP_PLAYER_DEC_FRAME_MODE_FILL;
        return true;
    }
    if (strncasecmp(url, "block:", 6) == 0) {
        *mode_out = ESP_PLAYER_DEC_FRAME_MODE_BLOCK;
        return true;
    }
    return false;
}

static esp_player_err_t normalize_frame_url_dup(const char *url, char **norm_out)
{
    if (url == NULL || norm_out == NULL || url[0] == '\0') {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    *norm_out = NULL;

    size_t scheme_len = 0;
    if (strncasecmp(url, "fill:", 5) == 0) {
        scheme_len = 4;
    } else if (strncasecmp(url, "block:", 6) == 0) {
        scheme_len = 5;
    } else {
        *norm_out = strdup(url);
        return (*norm_out != NULL) ? ESP_PLAYER_ERR_OK : ESP_PLAYER_ERR_NO_MEM;
    }

    /* fill:// (bare) → fill:/// */
    if (strcmp(url + scheme_len + 1, "//") == 0) {
        if (asprintf(norm_out, "%.*s:///", (int)scheme_len, url) < 0 || *norm_out == NULL) {
            return ESP_PLAYER_ERR_NO_MEM;
        }
        return ESP_PLAYER_ERR_OK;
    }

    *norm_out = strdup(url);
    return (*norm_out != NULL) ? ESP_PLAYER_ERR_OK : ESP_PLAYER_ERR_NO_MEM;
}

#define PLAYER_FRAME_CODEC_MAX_LEN  (32)

static esp_player_err_t frame_url_parse_codec(const esp_gmf_uri_t *uri, char *codec, size_t codec_len)
{
    if (uri == NULL || codec == NULL || codec_len == 0) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    codec[0] = '\0';

    if (uri->path == NULL || uri->path[0] == '\0') {
        return ESP_PLAYER_ERR_OK;
    }

    /* Only the file extension identifies the codec; the filename prefix is arbitrary. */
    const char *dot = strrchr(uri->path, '.');
    if (dot == NULL || dot[1] == '\0') {
        return ESP_PLAYER_ERR_OK;
    }
    const char *ext = dot + 1;

    size_t n = strlen(ext);
    if (n >= codec_len) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    memcpy(codec, ext, n);
    codec[n] = '\0';
    return ESP_PLAYER_ERR_OK;
}

#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
static void player_apply_frame_url_params(esp_audio_simple_dec_type_t dec_type, void *buf,
                                          const esp_gmf_uri_t *uri)
{
    if (buf == NULL || uri == NULL) {
        return;
    }

    const char *q = uri->query;
    unsigned long v = 0;
    bool bv = false;

    /* All parameters come from the URL query string: ?sr=<Hz>&ch=<n>&bits=<n>&... */
    uint32_t sr = 0;
    uint8_t ch = 0;
    uint8_t bits = 0;

    if (q != NULL && player_query_get_ulong(q, "sr", &v) && v > 0) {
        sr = (uint32_t)v;
    }
    if (q != NULL && player_query_get_ulong(q, "ch", &v) && v > 0) {
        ch = (uint8_t)v;
    }
    if (q != NULL && player_query_get_ulong(q, "bits", &v) && v > 0) {
        bits = (uint8_t)v;
    }

    switch (dec_type) {
        case ESP_AUDIO_SIMPLE_DEC_TYPE_AAC: {
            esp_aac_dec_cfg_t *cfg = buf;
            if (sr) {
                cfg->sample_rate = sr;
            }
            if (ch) {
                cfg->channel = ch;
            }
            if (bits) {
                cfg->bits_per_sample = bits;
            }
            if (q && player_query_get_bool(q, "no_adts", &bv)) {
                cfg->no_adts_header = bv;
            }
            if (q && player_query_get_bool(q, "aac_plus", &bv)) {
                cfg->aac_plus_enable = bv;
            }
            break;
        }
        case ESP_AUDIO_SIMPLE_DEC_TYPE_RAW_OPUS: {
            esp_opus_dec_cfg_t *cfg = buf;
            if (sr) {
                cfg->sample_rate = sr;
            }
            if (ch) {
                cfg->channel = ch;
            }
            if (q && player_query_get_ulong(q, "frame_dms", &v)) {
                cfg->frame_duration = (uint8_t)v;
            }
            if (q && player_query_get_bool(q, "self_del", &bv)) {
                cfg->self_delimited = bv;
            }
            break;
        }
        case ESP_AUDIO_SIMPLE_DEC_TYPE_PCM: {
            esp_pcm_dec_cfg_t *cfg = buf;
            if (sr) {
                cfg->sample_rate = sr;
            }
            if (ch) {
                cfg->channel = ch;
            }
            if (bits) {
                cfg->bits_per_sample = bits;
            }
            break;
        }
        case ESP_AUDIO_SIMPLE_DEC_TYPE_G711A:
        case ESP_AUDIO_SIMPLE_DEC_TYPE_G711U: {
            esp_g711_dec_cfg_t *cfg = buf;
            if (ch) {
                cfg->channel = ch;
            }
            break;
        }
        case ESP_AUDIO_SIMPLE_DEC_TYPE_ADPCM: {
            esp_adpcm_dec_cfg_t *cfg = buf;
            if (sr) {
                cfg->sample_rate = sr;
            }
            if (ch) {
                cfg->channel = ch;
            }
            if (bits) {
                cfg->bits_per_sample = bits;
            }
            break;
        }
        case ESP_AUDIO_SIMPLE_DEC_TYPE_SBC: {
            esp_sbc_dec_cfg_t *cfg = buf;
            if (ch) {
                cfg->ch_num = ch;
            }
            if (q && player_query_get_bool(q, "plc", &bv)) {
                cfg->enable_plc = bv;
            }
            break;
        }
        case ESP_AUDIO_SIMPLE_DEC_TYPE_LC3: {
            esp_lc3_dec_cfg_t *cfg = buf;
            if (sr) {
                cfg->sample_rate = sr;
            }
            if (ch) {
                cfg->channel = ch;
            }
            if (bits) {
                cfg->bits_per_sample = bits;
            }
            if (q && player_query_get_ulong(q, "frame_dms", &v)) {
                cfg->frame_dms = (uint8_t)v;
            }
            if (q && player_query_get_ulong(q, "nbyte", &v)) {
                cfg->nbyte = (uint16_t)v;
            }
            if (q && player_query_get_bool(q, "cbr", &bv)) {
                cfg->is_cbr = bv;
            }
            if (q && player_query_get_bool(q, "len_pre", &bv)) {
                cfg->len_prefixed = bv;
            }
            if (q && player_query_get_bool(q, "plc", &bv)) {
                cfg->enable_plc = bv;
            }
            break;
        }
        default:
            break;
    }
}

static void player_sync_audio_base_info_from_dec_cfg(esp_player_stream_t *stream)
{
    if (stream->audio_side == NULL) {
        return;
    }
    esp_player_audio_stream_info_t *info = &stream->audio_side->track_info.audio_info;
    switch (stream->dec_cfg.dec_type) {
        case ESP_AUDIO_SIMPLE_DEC_TYPE_AAC: {
            esp_aac_dec_cfg_t *cfg = stream->dec_cfg.dec_cfg;
            if (cfg != NULL) {
                info->sample_rate = cfg->sample_rate;
                info->channels = cfg->channel;
                info->bits_per_sample = cfg->bits_per_sample;
            }
            break;
        }
        case ESP_AUDIO_SIMPLE_DEC_TYPE_RAW_OPUS: {
            esp_opus_dec_cfg_t *cfg = stream->dec_cfg.dec_cfg;
            if (cfg != NULL) {
                info->sample_rate = cfg->sample_rate;
                info->channels = cfg->channel;
                info->bits_per_sample = 16;
            }
            break;
        }
        case ESP_AUDIO_SIMPLE_DEC_TYPE_PCM: {
            esp_pcm_dec_cfg_t *cfg = stream->dec_cfg.dec_cfg;
            if (cfg != NULL) {
                info->sample_rate = cfg->sample_rate;
                info->channels = cfg->channel;
                info->bits_per_sample = cfg->bits_per_sample;
            }
            break;
        }
        case ESP_AUDIO_SIMPLE_DEC_TYPE_G711A:
        case ESP_AUDIO_SIMPLE_DEC_TYPE_G711U: {
            esp_g711_dec_cfg_t *cfg = stream->dec_cfg.dec_cfg;
            if (cfg != NULL) {
                info->channels = cfg->channel;
                info->bits_per_sample = 16;
            }
            break;
        }
        case ESP_AUDIO_SIMPLE_DEC_TYPE_ADPCM: {
            esp_adpcm_dec_cfg_t *cfg = stream->dec_cfg.dec_cfg;
            if (cfg != NULL) {
                info->sample_rate = cfg->sample_rate;
                info->channels = cfg->channel;
                info->bits_per_sample = cfg->bits_per_sample;
            }
            break;
        }
        case ESP_AUDIO_SIMPLE_DEC_TYPE_SBC: {
            esp_sbc_dec_cfg_t *cfg = stream->dec_cfg.dec_cfg;
            if (cfg != NULL) {
                info->channels = cfg->ch_num;
                info->bits_per_sample = 16;
            }
            break;
        }
        case ESP_AUDIO_SIMPLE_DEC_TYPE_LC3: {
            esp_lc3_dec_cfg_t *cfg = stream->dec_cfg.dec_cfg;
            if (cfg != NULL) {
                info->sample_rate = cfg->sample_rate;
                info->channels = cfg->channel;
                info->bits_per_sample = cfg->bits_per_sample;
            }
            break;
        }
        default:
            break;
    }
}

static esp_player_err_t player_frame_url_apply_dec_cfg(esp_player_stream_t *stream, const char *url)
{
    char *norm = NULL;
    esp_player_err_t ret = normalize_frame_url_dup(url, &norm);
    if (ret != ESP_PLAYER_ERR_OK) {
        return ret;
    }

    esp_gmf_uri_t *uri = NULL;
    if (esp_gmf_uri_parse(norm, &uri) != 0) {
        ESP_LOGE(ESP_PLAYER_TAG, "esp_gmf_uri_parse failed for frame URL \"%s\"", norm);
        free(norm);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }

    char codec[PLAYER_FRAME_CODEC_MAX_LEN];
    ret = frame_url_parse_codec(uri, codec, sizeof(codec));
    if (ret != ESP_PLAYER_ERR_OK) {
        esp_gmf_uri_free(uri);
        free(norm);
        return ret;
    }
    if (codec[0] == '\0') {
        /* Bare fill:/// — caller must set decoder via esp_player_set_dec_cfg() */
        esp_gmf_uri_free(uri);
        free(norm);
        return ESP_PLAYER_ERR_OK;
    }

    esp_player_format_t format = ESP_PLAYER_FORMAT_NONE;
    ret = player_format_from_codec_name(codec, &format);
    if (ret != ESP_PLAYER_ERR_OK) {
        esp_gmf_uri_free(uri);
        free(norm);
        return ret;
    }

    if (is_simple_format_type(format)) {
        esp_player_format_t dummy = format;
        ret = player_set_dec_cfg_impl(stream, format, &dummy, 0);
    } else {
        uint32_t cfg_sz = 0;
        esp_audio_simple_dec_type_t dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
        if (player_dec_cfg_resolve(format, &cfg_sz, &dec_type, NULL) != ESP_PLAYER_ERR_OK) {
            esp_gmf_uri_free(uri);
            free(norm);
            return ESP_PLAYER_ERR_NOT_SUPPORT;
        }
        void *buf = calloc(1, cfg_sz);
        if (buf == NULL) {
            esp_gmf_uri_free(uri);
            free(norm);
            return ESP_PLAYER_ERR_NO_MEM;
        }
        player_dec_cfg_resolve(format, &cfg_sz, &dec_type, buf);
        player_apply_frame_url_params(dec_type, buf, uri);
        ret = player_set_dec_cfg_impl(stream, format, buf, cfg_sz);
        free(buf);
    }

    if (ret == ESP_PLAYER_ERR_OK) {
        player_sync_audio_base_info_from_dec_cfg(stream);
        ESP_LOGI(ESP_PLAYER_TAG, "frame URL dec cfg: codec=%s query=%s",
                 codec, uri->query ? uri->query : "(none)");
    }

    esp_gmf_uri_free(uri);
    free(norm);
    return ret;
}
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */

static esp_player_err_t player_setup_frame_mode(esp_player_stream_t *stream, const char *url,
                                                esp_player_dec_frame_mode_t mode)
{
    stream->dec_frame_mode = mode;
    if (mode == ESP_PLAYER_DEC_FRAME_MODE_FILL) {
        if (frame_pool_create(&stream->fill_pool) != ESP_PLAYER_ERR_OK) {
            ESP_LOGE(ESP_PLAYER_TAG, "Failed to create fill-mode frame pool");
            stream->dec_frame_mode = ESP_PLAYER_DEC_FRAME_MODE_UNKNOWN;
            return ESP_PLAYER_ERR_NO_MEM;
        }
    }

#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
    esp_player_err_t ret = player_frame_url_apply_dec_cfg(stream, url);
    if (ret != ESP_PLAYER_ERR_OK) {
        if (mode == ESP_PLAYER_DEC_FRAME_MODE_FILL) {
            frame_pool_destroy(stream->fill_pool);
            stream->fill_pool = NULL;
        }
        stream->dec_frame_mode = ESP_PLAYER_DEC_FRAME_MODE_UNKNOWN;
        return ret;
    }
#else
    (void)url;
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */

    player_free_frame_url_mem(stream);
    stream->frame_url = strdup(url);
    if (stream->frame_url == NULL) {
        if (mode == ESP_PLAYER_DEC_FRAME_MODE_FILL) {
            frame_pool_destroy(stream->fill_pool);
            stream->fill_pool = NULL;
        }
        stream->dec_frame_mode = ESP_PLAYER_DEC_FRAME_MODE_UNKNOWN;
        return ESP_PLAYER_ERR_NO_MEM;
    }
    return ESP_PLAYER_ERR_OK;
}

static bool player_is_same_url(esp_player_stream_t *stream, const char *new_url)
{
    if (new_url == NULL) {
        return false;
    }
    switch (stream->dec_frame_mode) {
        case ESP_PLAYER_DEC_FRAME_MODE_FILL:
        case ESP_PLAYER_DEC_FRAME_MODE_BLOCK:
            if (stream->frame_url != NULL && strcmp(stream->frame_url, new_url) == 0) {
                return true;
            }
            return false;
        case ESP_PLAYER_DEC_FRAME_MODE_EXTRACTOR: {
            if (stream->input_handle == NULL) {
                return false;
            }
            char *old_uri = NULL;
            if (esp_gmf_io_get_uri(stream->input_handle, &old_uri) != ESP_GMF_ERR_OK || old_uri == NULL) {
                return false;
            }
            if (strcmp(old_uri, new_url) == 0) {
                return true;
            }
            char *old_path = NULL;
            char *new_path = NULL;
            bool same = false;
            if (file_vfs_path_dup(old_uri, &old_path) == ESP_PLAYER_ERR_OK
                && file_vfs_path_dup(new_url, &new_path) == ESP_PLAYER_ERR_OK) {
                same = (strcmp(old_path, new_path) == 0);
            }
            free(old_path);
            free(new_path);
            return same;
        }
        default:
            return false;
    }
}

bool _player_is_network_source_uri(esp_player_stream_t *stream)
{
    if (stream == NULL || stream->input_handle == NULL
        || stream->dec_frame_mode != ESP_PLAYER_DEC_FRAME_MODE_EXTRACTOR) {
        return false;
    }
    char *uri_str = NULL;
    if (esp_gmf_io_get_uri(stream->input_handle, &uri_str) != ESP_GMF_ERR_OK || uri_str == NULL) {
        return false;
    }
    /* Must stay stack-light: called from player_cmd (4KB) during start_playback. */
    return (strncasecmp(uri_str, "http://", 7) == 0) || (strncasecmp(uri_str, "https://", 8) == 0);
}

esp_player_err_t _player_update_url(esp_player_stream_t *stream, const char *new_url)
{
    bool old_set = (stream->dec_frame_mode != ESP_PLAYER_DEC_FRAME_MODE_UNKNOWN);
    if (!old_set && new_url == NULL) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    if (player_is_same_url(stream, new_url)) {
        return ESP_PLAYER_ERR_OK;
    }
    if (!old_set) {
        player_drop_all_queues(stream);
        player_reset_all_db(stream);
        frame_pool_destroy(stream->fill_pool);
        stream->fill_pool = NULL;
    } else {
        player_destroy_audio_path(stream);
        player_destroy_video_path(stream);
        player_destroy_extractor_path(stream);
        player_destroy_input_io(stream);
        frame_pool_destroy(stream->fill_pool);
        stream->fill_pool = NULL;
        player_free_frame_url_mem(stream);
        stream->dec_frame_mode = ESP_PLAYER_DEC_FRAME_MODE_UNKNOWN;
    }
    if (new_url == NULL) {
        stream->dec_frame_mode = ESP_PLAYER_DEC_FRAME_MODE_UNKNOWN;
        player_free_frame_url_mem(stream);
        return ESP_PLAYER_ERR_OK;
    }
    esp_player_dec_frame_mode_t frame_mode;
    if (player_url_is_frame_mode(new_url, &frame_mode)) {
        return player_setup_frame_mode(stream, new_url, frame_mode);
    }
    stream->dec_frame_mode = ESP_PLAYER_DEC_FRAME_MODE_EXTRACTOR;
    esp_player_err_t ret = player_get_input_io(stream, new_url);
    if (ret != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to get input io, ret: %d", ret);
        stream->dec_frame_mode = ESP_PLAYER_DEC_FRAME_MODE_UNKNOWN;
        return ret;
    }
    stream->input_opened = false;
    return ESP_PLAYER_ERR_OK;
}
