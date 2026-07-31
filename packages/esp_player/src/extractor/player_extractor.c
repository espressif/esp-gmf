/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <inttypes.h>
#include <limits.h>

#include "esp_extractor_ctrl.h"
#include "esp_extractor_id3_parser.h"
#include "impl/esp_raw_extractor.h"

#include "player_extractor.h"
#include "player_stream.h"

/* Chunk size the RAW extractor reads per frame for headerless inputs (e.g. PCM).
 * Must stay within the extractor output pool (DEFAULT_EXTRACTOR_AUDIO_POOL_SIZE). */
#define PLAYER_RAW_MAX_FRAME_SIZE  (2048)

#define PLAYER_EXTRACTOR_INFO_TO_VIDEO_STREAM_INFO(stream_info, extractor_info)  do {  \
    (stream_info)->track_type         = ESP_PLAYER_TRACK_TYPE_VIDEO;                   \
    (stream_info)->video_info.format  = (extractor_info)->video_info.format;           \
    (stream_info)->video_info.width   = (extractor_info)->video_info.width;            \
    (stream_info)->video_info.height  = (extractor_info)->video_info.height;           \
    (stream_info)->video_info.fps     = (extractor_info)->video_info.fps;              \
    (stream_info)->video_info.bitrate = (extractor_info)->bitrate;                     \
} while (0)

#define PLAYER_EXTRACTOR_INFO_TO_AUDIO_STREAM_INFO(stream_info, extractor_info)  do {          \
    (stream_info)->track_type                 = ESP_PLAYER_TRACK_TYPE_AUDIO;                   \
    (stream_info)->audio_info.format          = (extractor_info)->audio_info.format;           \
    (stream_info)->audio_info.sample_rate     = (extractor_info)->audio_info.sample_rate;      \
    (stream_info)->audio_info.channels        = (extractor_info)->audio_info.channel;          \
    (stream_info)->audio_info.bits_per_sample = (extractor_info)->audio_info.bits_per_sample;  \
    (stream_info)->audio_info.spec_info_len   = (extractor_info)->spec_info_len;               \
    (stream_info)->audio_info.spec_info       = (extractor_info)->spec_info;                   \
    (stream_info)->audio_info.bitrate         = (extractor_info)->bitrate;                     \
} while (0)

static const char *TAG = "ESP_PLAYER_EXTRACTOR";

typedef struct {
    esp_gmf_element_t       parent;                 /*!< Parent element */
    esp_extractor_handle_t  extractor_handle;       /*!< Extractor handle */
    uint8_t                 extract_mask;           /*!< Extract mask */
    int8_t                  audio_selected_idx;     /*!< Audio stream index */
    int8_t                  video_selected_idx;     /*!< Video stream index */
    int8_t                  audio_current_idx;      /*!< Current audio stream index */
    int8_t                  video_current_idx;      /*!< Current video stream index */
    uint16_t                audio_stream_num;       /*!< Audio stream number */
    uint16_t                video_stream_num;       /*!< Video stream number */
    bool                    is_parsed;              /*!< Parsing status */
    bool                    is_notify_info;         /*!< Is notify info */
    uint8_t                 eos_mask;               /*!< EOS mask */
    uint8_t                 wait_for_output_count;  /*!< Wait for output count */
    uint64_t                last_pts;               /*!< Last PTS (ms) */
    uint64_t                delta_pts;              /*!< Delta PTS (ms) */
    uint32_t                raw_sample_rate;        /*!< RAW PCM sample rate (Hz); 0 = not a raw source */
    uint8_t                 raw_channels;           /*!< RAW PCM channel count */
    uint8_t                 raw_bits_per_sample;    /*!< RAW PCM bits per sample */
    uint64_t                seek_pos_ms;            /*!< Last requested seek position, re-applied after the demuxer is recreated */
    bool                    seek_pending;           /*!< seek_pos_ms still has to be applied once the new demuxer has parsed */
} esp_player_extractor_t;

static bool player_extractor_has_output_stream(const esp_player_extractor_t *extractor)
{
    if ((extractor->extract_mask & ESP_EXTRACT_MASK_AUDIO) && extractor->audio_selected_idx >= 0) {
        return true;
    }
    if ((extractor->extract_mask & ESP_EXTRACT_MASK_VIDEO) && extractor->video_selected_idx >= 0) {
        return true;
    }
    return false;
}

static esp_gmf_job_err_t extractor_send_eos(esp_gmf_port_handle_t out_port)
{
    if (out_port == NULL) {
        return ESP_GMF_JOB_ERR_OK;
    }
    esp_gmf_payload_t *out_load = NULL;
    esp_gmf_err_io_t io_ret = esp_gmf_port_acquire_out(out_port, &out_load, 0, ESP_GMF_MAX_DELAY);
    if (io_ret != ESP_GMF_IO_OK || out_load == NULL) {
        return ESP_GMF_JOB_ERR_FAIL;
    }
    out_load->is_done = true;
    out_load->buf = NULL;
    out_load->valid_size = 0;
    out_load->pts = 0;
    out_load->meta_flag = 0;
    io_ret = esp_gmf_port_release_out(out_port, out_load, ESP_GMF_MAX_DELAY);
    return (io_ret == ESP_GMF_IO_OK) ? ESP_GMF_JOB_ERR_OK : ESP_GMF_JOB_ERR_FAIL;
}

static void player_extractor_reconcile_id3_parser(esp_player_extractor_t *extractor, esp_extractor_config_t *cfg)
{
    if (extractor == NULL || cfg == NULL || cfg->in_ctx == NULL) {
        return;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)cfg->in_ctx;
    bool want = (cfg->type == ESP_EXTRACTOR_TYPE_MP3) && stream->id3.enable && !stream->id3.finalize_done;
    if (!want) {
        return;
    }
    if (stream->id3.parser != NULL) {
        esp_extractor_id3_parser_close(stream->id3.parser);
        stream->id3.parser = NULL;
    }
    esp_extractor_err_t ret = esp_extractor_id3_parser_open(extractor->extractor_handle, &stream->id3.parser);
    if (ret != ESP_EXTRACTOR_ERR_OK) {
        ESP_LOGW(TAG, "Open ID3 parser failed, ret: %d (playback continues without ID3)", ret);
        stream->id3.parser = NULL;
    }
}

static void player_extractor_finalize_id3(esp_gmf_element_handle_t self)
{
    esp_extractor_config_t *cfg = (esp_extractor_config_t *)OBJ_GET_CFG(self);
    if (cfg == NULL || cfg->in_ctx == NULL) {
        return;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)cfg->in_ctx;
    if (stream->id3.parser != NULL) {
        stream->id3.finalize_done = true;
    }
}

static esp_gmf_job_err_t player_extractor_open(esp_gmf_element_handle_t self, void *para)
{
    esp_extractor_err_t extractor_ret = ESP_EXTRACTOR_ERR_OK;
    esp_player_extractor_t *extractor = (esp_player_extractor_t *)self;
    esp_extractor_config_t *cfg = (esp_extractor_config_t *)OBJ_GET_CFG(self);
    if (extractor->extractor_handle != NULL) {
        esp_extractor_close(extractor->extractor_handle);
        extractor->extractor_handle = NULL;
    }
    extractor_ret = esp_extractor_open(cfg, &extractor->extractor_handle);
    if (extractor_ret != ESP_EXTRACTOR_ERR_OK) {
        ESP_LOGE(TAG, "Open extractor error, ret: %d", extractor_ret);
        return ESP_GMF_JOB_ERR_FAIL;
    }
    extractor->is_parsed = false;
    extractor->audio_current_idx = extractor->audio_selected_idx = -1;
    extractor->video_current_idx = extractor->video_selected_idx = -1;
    bool frame_across_pes = true;
    esp_extractor_err_t ctrl_ret = esp_extractor_ctrl(extractor->extractor_handle,
                                                      ESP_EXTRACTOR_CTRL_TYPE_SET_FRAME_ACROSS_PES,
                                                      &frame_across_pes, sizeof(frame_across_pes));
    if (ctrl_ret != ESP_EXTRACTOR_ERR_OK) {
        ESP_LOGE(TAG, "Set frame_across_pes failed, ret: %d", ctrl_ret);
        esp_extractor_close(extractor->extractor_handle);
        extractor->extractor_handle = NULL;
        return ESP_GMF_JOB_ERR_FAIL;
    }
    /* Headerless raw sources (e.g. PCM) cannot be probed; the caller selects RAW
     * and supplies stream parameters via the URL query string. */
    if (cfg->type == ESP_EXTRACTOR_TYPE_RAW) {
        if (extractor->raw_sample_rate == 0 || extractor->raw_channels == 0
            || extractor->raw_bits_per_sample == 0) {
            ESP_LOGE(TAG, "RAW source missing PCM params (sr=%" PRIu32 " ch=%u bits=%u)",
                     extractor->raw_sample_rate, extractor->raw_channels, extractor->raw_bits_per_sample);
            esp_extractor_close(extractor->extractor_handle);
            extractor->extractor_handle = NULL;
            return ESP_GMF_JOB_ERR_FAIL;
        }
        if (esp_raw_extractor_register() != ESP_EXTRACTOR_ERR_OK) {
            ESP_LOGE(TAG, "Register raw extractor failed");
            esp_extractor_close(extractor->extractor_handle);
            extractor->extractor_handle = NULL;
            return ESP_GMF_JOB_ERR_FAIL;
        }
        esp_extractor_stream_info_t stream_info = {
            .stream_type = ESP_EXTRACTOR_STREAM_TYPE_AUDIO,
            .audio_info = {
                .format = ESP_EXTRACTOR_AUDIO_FORMAT_PCM,
                .sample_rate = extractor->raw_sample_rate,
                .channel = extractor->raw_channels,
                .bits_per_sample = extractor->raw_bits_per_sample,
            },
        };
        ctrl_ret = esp_extractor_ctrl(extractor->extractor_handle,
                                      ESP_EXTRACTOR_CTRL_TYPE_SET_STREAM_INFO,
                                      &stream_info, sizeof(stream_info));
        if (ctrl_ret != ESP_EXTRACTOR_ERR_OK) {
            ESP_LOGE(TAG, "Set RAW stream info failed, ret: %d", ctrl_ret);
            esp_extractor_close(extractor->extractor_handle);
            extractor->extractor_handle = NULL;
            return ESP_GMF_JOB_ERR_FAIL;
        }
        uint32_t raw_max_frame = PLAYER_RAW_MAX_FRAME_SIZE;
        ctrl_ret = esp_extractor_ctrl(extractor->extractor_handle,
                                      ESP_EXTRACTOR_CTRL_TYPE_SET_MAX_FRAME_SIZE,
                                      &raw_max_frame, sizeof(raw_max_frame));
        if (ctrl_ret != ESP_EXTRACTOR_ERR_OK) {
            ESP_LOGE(TAG, "Set RAW max frame size failed, ret: %d", ctrl_ret);
            esp_extractor_close(extractor->extractor_handle);
            extractor->extractor_handle = NULL;
            return ESP_GMF_JOB_ERR_FAIL;
        }
    }
    /* The caller applies the seek target before running the pipeline, i.e. to the instance that
     * was just replaced. Seeking cannot happen here because a freshly opened demuxer has no
     * stream information yet, so defer it until the parse in the process job. */
    extractor->seek_pending = (extractor->seek_pos_ms != 0);
    player_extractor_reconcile_id3_parser(extractor, cfg);
    extractor->is_notify_info = false;
    extractor->extract_mask = cfg->extract_mask;
    extractor->eos_mask = 0;
    extractor->wait_for_output_count = 0;
    extractor->last_pts = 0;
    extractor->delta_pts = 0;
    ESP_LOGD(TAG, "Open extractor, extract_mask: %d", extractor->extract_mask);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t player_extractor_ops_close(esp_gmf_element_handle_t self, void *para)
{
    esp_player_extractor_t *extractor = (esp_player_extractor_t *)self;
    extractor->is_parsed = false;
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t player_extractor_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_job_err_t job_ret = ESP_GMF_JOB_ERR_OK;
    esp_extractor_err_t extractor_ret = ESP_EXTRACTOR_ERR_OK;
    esp_player_extractor_t *extractor = (esp_player_extractor_t *)self;
    esp_gmf_err_io_t io_ret = ESP_GMF_IO_OK;
    esp_gmf_port_handle_t out_port = NULL;
    esp_gmf_payload_t *out_load = NULL;

    if (extractor->is_parsed == false) {
        extractor_ret = esp_extractor_parse_stream(extractor->extractor_handle);
        if (extractor_ret != ESP_EXTRACTOR_ERR_OK) {
            ESP_LOGE(TAG, "Parse stream error, ret: %d", extractor_ret);
            return ESP_GMF_JOB_ERR_FAIL;
        }
        extractor->audio_stream_num = 0;
        extractor->video_stream_num = 0;
        extractor_ret = esp_extractor_get_stream_num(extractor->extractor_handle, ESP_EXTRACTOR_STREAM_TYPE_AUDIO, &extractor->audio_stream_num);
        if (extractor_ret != ESP_EXTRACTOR_ERR_OK) {
            if (extractor_ret == ESP_EXTRACTOR_ERR_NOT_FOUND) {
                extractor->audio_stream_num = 0;
            } else {
                ESP_LOGE(TAG, "Get audio stream num error, ret: %d, line: %d", extractor_ret, __LINE__);
                return ESP_GMF_JOB_ERR_FAIL;
            }
        }
        extractor_ret = esp_extractor_get_stream_num(extractor->extractor_handle, ESP_EXTRACTOR_STREAM_TYPE_VIDEO, &extractor->video_stream_num);
        if (extractor_ret != ESP_EXTRACTOR_ERR_OK) {
            if (extractor_ret == ESP_EXTRACTOR_ERR_NOT_FOUND) {
                extractor->video_stream_num = 0;
            } else {
                ESP_LOGE(TAG, "Get video stream num error, ret: %d, line: %d", extractor_ret, __LINE__);
                return ESP_GMF_JOB_ERR_FAIL;
            }
        }
        if (extractor->audio_stream_num > 0 && (extractor->extract_mask & ESP_EXTRACT_MASK_AUDIO)) {
            extractor->audio_selected_idx = extractor->audio_current_idx = 0;
            extractor->eos_mask |= ESP_EXTRACT_MASK_AUDIO;
        }
        if (extractor->video_stream_num > 0 && (extractor->extract_mask & ESP_EXTRACT_MASK_VIDEO)) {
            extractor->video_selected_idx = extractor->video_current_idx = 0;
            extractor->eos_mask |= ESP_EXTRACT_MASK_VIDEO;
        }
        extractor->is_parsed = true;
        player_extractor_finalize_id3(self);
        if (extractor->seek_pending) {
            extractor->seek_pending = false;
            if (player_extractor_seek(self, extractor->seek_pos_ms) != ESP_GMF_ERR_OK) {
                return ESP_GMF_JOB_ERR_FAIL;
            }
        }
    }
    if (extractor->is_notify_info == false) {
        if (esp_gmf_element_notify_vid_info(self, NULL) != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Notify video info error");
            return ESP_GMF_JOB_ERR_FAIL;
        }
        extractor->is_notify_info = true;
    }
    if (extractor->audio_selected_idx != extractor->audio_current_idx) {
        bool is_enable = extractor->audio_selected_idx >= 0;
        /* Enable applies to the requested track; disable applies to the previously active track. */
        int8_t audio_idx_arg = is_enable ? extractor->audio_selected_idx : extractor->audio_current_idx;
        extractor_ret = esp_extractor_enable_stream(extractor->extractor_handle, ESP_EXTRACTOR_STREAM_TYPE_AUDIO,
                                                    (uint16_t)audio_idx_arg, is_enable);
        if (extractor_ret != ESP_EXTRACTOR_ERR_OK) {
            ESP_LOGE(TAG, "Trans stream error, ret: %d, idx: %d, is_enable: %d", extractor_ret, (int)audio_idx_arg, is_enable);
            return ESP_GMF_JOB_ERR_FAIL;
        }
        extractor->audio_current_idx = extractor->audio_selected_idx;
        if (is_enable) {
            extractor->eos_mask |= ESP_EXTRACT_MASK_AUDIO;
        } else {
            extractor->eos_mask &= ~ESP_EXTRACT_MASK_AUDIO;
        }
    }
    if (extractor->video_selected_idx != extractor->video_current_idx) {
        bool is_enable = extractor->video_selected_idx >= 0;
        int8_t video_idx_arg = is_enable ? extractor->video_selected_idx : extractor->video_current_idx;
        extractor_ret = esp_extractor_enable_stream(extractor->extractor_handle, ESP_EXTRACTOR_STREAM_TYPE_VIDEO,
                                                    (uint16_t)video_idx_arg, is_enable);
        if (extractor_ret != ESP_EXTRACTOR_ERR_OK) {
            ESP_LOGE(TAG, "Trans stream error, ret: %d, idx: %d, is_enable: %d", extractor_ret, (int)video_idx_arg, is_enable);
            return ESP_GMF_JOB_ERR_FAIL;
        }
        extractor->video_current_idx = extractor->video_selected_idx;
        if (is_enable) {
            extractor->eos_mask |= ESP_EXTRACT_MASK_VIDEO;
        } else {
            extractor->eos_mask &= ~ESP_EXTRACT_MASK_VIDEO;
        }
    }

    /* Decoder/render error may disable all output tracks; do not treat that as demux failure. */
    if (!player_extractor_has_output_stream(extractor)) {
        ESP_LOGD(TAG, "No enabled output stream, yield until stop or re-enable");
        vTaskDelay(pdMS_TO_TICKS(10));
        return ESP_GMF_JOB_ERR_CONTINUE;
    }

    // read frame
    esp_extractor_frame_info_t frame_info = {0};
    extractor_ret = esp_extractor_read_frame(extractor->extractor_handle, &frame_info);
    if (extractor_ret == ESP_EXTRACTOR_ERR_ABORTED) {
        ESP_LOGD(TAG, "Extractor is aborted, line: %d", __LINE__);
        return ESP_GMF_JOB_ERR_ABORT;
    }
    if (extractor_ret == ESP_EXTRACTOR_ERR_WAITING_OUTPUT) {
        ESP_LOGD(TAG, "Extractor is waiting output, line: %d", __LINE__);
        vTaskDelay(pdMS_TO_TICKS(10));
        return ESP_GMF_JOB_ERR_CONTINUE;
    }
    if (extractor_ret != ESP_EXTRACTOR_ERR_OK && extractor_ret != ESP_EXTRACTOR_ERR_EOS) {
        ESP_LOGE(TAG, "Read frame error, ret: %d", extractor_ret);
        return ESP_GMF_JOB_ERR_FAIL;
    }
    /* RAW extractor read_frame() fills buffer/size only; stream_type and pts stay zero
     * without this fix downstream decoder/render stall. */
    if (extractor->raw_sample_rate != 0 && frame_info.frame_size > 0
        && frame_info.stream_type == ESP_EXTRACTOR_STREAM_TYPE_NONE) {
        frame_info.stream_type = ESP_EXTRACTOR_STREAM_TYPE_AUDIO;
        frame_info.pts = extractor->last_pts;
    }
    extractor->wait_for_output_count = 0;
    extractor->delta_pts = frame_info.pts - extractor->last_pts;
    extractor->last_pts = frame_info.pts;
    if (extractor->raw_sample_rate != 0 && frame_info.frame_size > 0) {
        uint32_t byte_rate = extractor->raw_sample_rate * extractor->raw_channels
                             * ((uint32_t)extractor->raw_bits_per_sample / 8U);
        if (byte_rate > 0) {
            extractor->last_pts += ((uint64_t)frame_info.frame_size * 1000ULL) / byte_rate;
        }
    }
    if (extractor_ret == ESP_EXTRACTOR_ERR_EOS) {
        ESP_LOGI(TAG, "Extractor is EOS, line: %d", __LINE__);
        job_ret = ESP_GMF_JOB_ERR_DONE;
        if (frame_info.frame_size == 0) {
            if ((extractor->extract_mask & ESP_EXTRACT_MASK_AUDIO) && extractor->audio_selected_idx >= 0) {
                esp_gmf_port_handle_t out_head = ESP_GMF_ELEMENT_GET(self)->out;
                if (out_head != NULL) {
                    extractor_send_eos(out_head);
                }
            }
            return job_ret;
        }
    }
    // NOTE: If both Audio and Video exist, the first port is audio, the second is video
    if (frame_info.stream_type == ESP_EXTRACTOR_STREAM_TYPE_AUDIO && (extractor->extract_mask & ESP_EXTRACT_MASK_AUDIO) && extractor->audio_selected_idx >= 0) {
        // Audio: return buffer address and size to decoder
        out_port = ESP_GMF_ELEMENT_GET(self)->out;
    } else if (frame_info.stream_type == ESP_EXTRACTOR_STREAM_TYPE_VIDEO && (extractor->extract_mask & ESP_EXTRACT_MASK_VIDEO) && extractor->video_selected_idx >= 0) {
        // Video: return buffer address and size to decoder
        out_port = ESP_GMF_ELEMENT_GET(self)->out;
        if (extractor->extract_mask == ESP_EXTRACT_MASK_AV) {
            out_port = out_port->next;
            if (out_port == NULL) {
                ESP_LOGE(TAG, "No video port");
                return ESP_GMF_JOB_ERR_FAIL;
            }
        }
    }
    if (out_port) {
        io_ret = esp_gmf_port_acquire_out(out_port, &out_load, frame_info.frame_size, ESP_GMF_MAX_DELAY);
        ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, io_ret, job_ret, goto __extractor_release);
        out_load->buf = frame_info.frame_buffer;
        out_load->buf_length = frame_info.frame_size;
        out_load->valid_size = frame_info.frame_size;
        out_load->pts = frame_info.pts;
        out_load->is_done = EXTRACTOR_IS_EOS(frame_info.frame_flag);
        if (extractor_ret == ESP_EXTRACTOR_ERR_EOS) {
            out_load->is_done = true;
        }

        if (out_load->is_done == true) {
            if (extractor->eos_mask & ESP_EXTRACT_MASK_AUDIO && frame_info.stream_type == ESP_EXTRACTOR_STREAM_TYPE_AUDIO) {
                extractor->eos_mask &= ~ESP_EXTRACT_MASK_AUDIO;
            }
            if (extractor->eos_mask & ESP_EXTRACT_MASK_VIDEO && frame_info.stream_type == ESP_EXTRACTOR_STREAM_TYPE_VIDEO) {
                extractor->eos_mask &= ~ESP_EXTRACT_MASK_VIDEO;
            }
            if (extractor->eos_mask == 0) {
                ESP_LOGI(TAG, "Extractor is done, line: %d", __LINE__);
                job_ret = ESP_GMF_JOB_ERR_DONE;
            }
        }
    }
    if (job_ret == ESP_GMF_JOB_ERR_DONE && extractor->eos_mask != 0) {
        esp_gmf_port_handle_t out_head = ESP_GMF_ELEMENT_GET(self)->out;
        if (extractor->extract_mask == ESP_EXTRACT_MASK_AV) {
            if (extractor->eos_mask & ESP_EXTRACT_MASK_AUDIO) {
                if (extractor_send_eos(out_head) != ESP_GMF_JOB_ERR_OK) {
                    return ESP_GMF_JOB_ERR_FAIL;
                }
            }
            if ((extractor->eos_mask & ESP_EXTRACT_MASK_VIDEO) && out_head && out_head->next) {
                if (extractor_send_eos(out_head->next) != ESP_GMF_JOB_ERR_OK) {
                    return ESP_GMF_JOB_ERR_FAIL;
                }
            }
        } else if (extractor->extract_mask == ESP_EXTRACT_MASK_AUDIO) {
            if (extractor->eos_mask & ESP_EXTRACT_MASK_AUDIO) {
                if (extractor_send_eos(out_head) != ESP_GMF_JOB_ERR_OK) {
                    return ESP_GMF_JOB_ERR_FAIL;
                }
            }
        } else if (extractor->extract_mask == ESP_EXTRACT_MASK_VIDEO) {
            if (extractor->eos_mask & ESP_EXTRACT_MASK_VIDEO) {
                if (extractor_send_eos(out_head) != ESP_GMF_JOB_ERR_OK) {
                    return ESP_GMF_JOB_ERR_FAIL;
                }
            }
        }
        extractor->eos_mask = 0;
        return ESP_GMF_JOB_ERR_DONE;
    }
__extractor_release:
    if (out_load != NULL) {
        io_ret = esp_gmf_port_release_out(out_port, out_load, ESP_GMF_MAX_DELAY);
        ESP_GMF_PORT_RELEASE_OUT_CHECK(TAG, io_ret, job_ret, return job_ret);
    }
    return job_ret;
}

static esp_gmf_err_t player_extractor_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return player_extractor_init((esp_extractor_config_t *)cfg, handle);
}

static esp_gmf_err_t player_extractor_destroy(esp_gmf_element_handle_t self)
{
    if (self == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    ESP_LOGD(TAG, "Destroyed, %p", self);
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }

    bool close_failed = false;
    esp_player_extractor_t *extractor = (esp_player_extractor_t *)self;
    if (extractor->extractor_handle) {
        esp_extractor_err_t extractor_ret = esp_extractor_close(extractor->extractor_handle);
        if (extractor_ret != ESP_EXTRACTOR_ERR_OK) {
            ESP_LOGE(TAG, "Close extractor error, ret: %d", extractor_ret);
            close_failed = true;
        }
        extractor->extractor_handle = NULL;
    }
    esp_gmf_element_deinit(self);
    esp_gmf_oal_free(self);
    return close_failed ? ESP_GMF_ERR_FAIL : ESP_GMF_ERR_OK;
}

esp_gmf_err_t player_extractor_init(esp_extractor_config_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    *handle = NULL;

    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_player_extractor_t *extractor = NULL;
    esp_extractor_config_t *cfg = NULL;

    // Allocate extractor
    extractor = esp_gmf_oal_calloc(1, sizeof(esp_player_extractor_t));
    ESP_GMF_MEM_VERIFY(TAG, extractor, {return ESP_GMF_ERR_MEMORY_LACK;}, "extractor", sizeof(esp_extractor_config_t));

    // Allocate configuration
    cfg = esp_gmf_oal_calloc(1, sizeof(esp_extractor_config_t));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto EXTRACTOR_INIT_FAIL;}, "extractor configuration", sizeof(esp_extractor_config_t));

    // Initialize fields
    extractor->audio_selected_idx = 0;
    extractor->video_selected_idx = 0;

    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)extractor;
    obj->new_obj = player_extractor_new;
    obj->del_obj = player_extractor_destroy;
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_extractor_config_t));
    if (config) {
        memcpy(cfg, config, sizeof(esp_extractor_config_t));
    } else {
        esp_extractor_config_t dcfg = DEFAULT_PLAYER_EXTRACTOR_CONFIG();
        memcpy(cfg, &dcfg, sizeof(esp_extractor_config_t));
    }
    ret = esp_gmf_obj_set_tag(obj, "extractor");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto EXTRACTOR_INIT_FAIL, "Failed to set obj tag");
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_MULTI, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = false;
    ret = esp_gmf_element_init(extractor, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto EXTRACTOR_INIT_FAIL, "Failed to initialize extractor element");
    ESP_GMF_ELEMENT_GET(extractor)->ops.open = player_extractor_open;
    ESP_GMF_ELEMENT_GET(extractor)->ops.process = player_extractor_process;
    ESP_GMF_ELEMENT_GET(extractor)->ops.close = player_extractor_ops_close;
    *handle = (esp_gmf_element_handle_t)obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;

EXTRACTOR_INIT_FAIL:
    player_extractor_destroy((esp_gmf_obj_t *)extractor);
    return ret;
}

esp_gmf_err_t player_extractor_seek(esp_gmf_element_handle_t handle, uint64_t time_pos)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    esp_player_extractor_t *extractor = (esp_player_extractor_t *)handle;
    extractor->seek_pos_ms = time_pos;

    if (extractor->extractor_handle) {
        /* A demuxer that has not parsed its stream yet rejects seeking; let the process job
         * apply the target once the parse completes. */
        if (extractor->is_parsed == false) {
            extractor->seek_pending = (time_pos != 0);
            return ESP_GMF_ERR_OK;
        }
        /* The underlying esp_extractor_seek only accepts uint32_t ms (~49 days); this
         * truncation is safe for any realistic media asset. */
        esp_extractor_err_t extractor_ret = esp_extractor_seek(extractor->extractor_handle, (uint32_t)time_pos);
        if (extractor_ret != ESP_EXTRACTOR_ERR_OK) {
            ESP_LOGE(TAG, "Seek to %" PRIu64 " ms error, ret: %d", time_pos, extractor_ret);
            return ESP_GMF_ERR_FAIL;
        }
        return ESP_GMF_ERR_OK;
    }

    ESP_LOGI(TAG, "Extractor handle not initialized, line: %d", __LINE__);
    return ESP_GMF_ERR_INVALID_ARG;
}

esp_gmf_err_t player_extractor_release_frame(esp_gmf_element_handle_t handle, esp_extractor_frame_info_t *frame_info)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    ESP_GMF_NULL_CHECK(TAG, frame_info, return ESP_GMF_ERR_INVALID_ARG;);
    esp_player_extractor_t *extractor = (esp_player_extractor_t *)handle;
    if (extractor->extractor_handle) {
        esp_extractor_err_t extractor_ret = esp_extractor_release_frame(extractor->extractor_handle, frame_info);
        if (extractor_ret != ESP_EXTRACTOR_ERR_OK) {
            ESP_LOGE(TAG, "Release frame buffer error, ret: %d", extractor_ret);
            return ESP_GMF_ERR_FAIL;
        }
        return ESP_GMF_ERR_OK;
    }
    ESP_LOGI(TAG, "Extractor handle not initialized, line: %d", __LINE__);
    return ESP_GMF_ERR_INVALID_ARG;
}

esp_gmf_err_t player_extractor_get_stream_num(esp_gmf_element_handle_t handle, esp_extractor_stream_type_t type, uint16_t *stream_num)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    ESP_GMF_NULL_CHECK(TAG, stream_num, return ESP_GMF_ERR_INVALID_ARG;);
    esp_player_extractor_t *extractor = (esp_player_extractor_t *)handle;
    if (extractor->extractor_handle) {
        esp_extractor_err_t extractor_ret = esp_extractor_get_stream_num(extractor->extractor_handle, type, stream_num);
        if (extractor_ret != ESP_EXTRACTOR_ERR_OK) {
            if (extractor_ret == ESP_EXTRACTOR_ERR_NOT_FOUND) {
                *stream_num = 0;
            } else {
                ESP_LOGE(TAG, "Get stream num error, ret: %d, line: %d", extractor_ret, __LINE__);
                return ESP_GMF_ERR_FAIL;
            }
        }
        return ESP_GMF_ERR_OK;
    }
    ESP_LOGI(TAG, "Extractor handle not initialized, line: %d", __LINE__);
    return ESP_GMF_ERR_INVALID_ARG;
}

esp_gmf_err_t player_extractor_enable_stream(esp_gmf_element_handle_t handle, esp_extractor_stream_type_t type, uint16_t stream_idx, bool enable)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    esp_player_extractor_t *extractor = (esp_player_extractor_t *)handle;
    if (extractor->extractor_handle) {
        if (type == ESP_EXTRACTOR_STREAM_TYPE_AUDIO) {
            if (stream_idx >= extractor->audio_stream_num) {
                ESP_LOGE(TAG, "Audio stream index(%d) out of range, max: %d, enable: %d, line: %d", stream_idx, extractor->audio_stream_num, enable, __LINE__);
                return ESP_GMF_ERR_FAIL;
            }
            if (stream_idx > INT8_MAX) {
                ESP_LOGE(TAG, "Audio stream index(%d) exceeds int8 range", stream_idx);
                return ESP_GMF_ERR_FAIL;
            }
            extractor->audio_selected_idx = enable == true ? stream_idx : -1;
        } else if (type == ESP_EXTRACTOR_STREAM_TYPE_VIDEO) {
            if (stream_idx >= extractor->video_stream_num) {
                ESP_LOGE(TAG, "Video stream index(%d) out of range, max: %d, enable: %d, line: %d", stream_idx, extractor->video_stream_num, enable, __LINE__);
                return ESP_GMF_ERR_FAIL;
            }
            if (stream_idx > INT8_MAX) {
                ESP_LOGE(TAG, "Video stream index(%d) exceeds int8 range", stream_idx);
                return ESP_GMF_ERR_FAIL;
            }
            extractor->video_selected_idx = enable == true ? stream_idx : -1;
        }
        return ESP_GMF_ERR_OK;
    }
    ESP_LOGE(TAG, "Extractor handle not initialized, line: %d", __LINE__);
    return ESP_GMF_ERR_INVALID_ARG;
}

esp_gmf_err_t player_extractor_get_stream_info(esp_gmf_element_handle_t handle, esp_extractor_stream_type_t type, uint16_t stream_idx, esp_extractor_stream_info_t *info)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    ESP_GMF_NULL_CHECK(TAG, info, return ESP_GMF_ERR_INVALID_ARG;);
    esp_player_extractor_t *extractor = (esp_player_extractor_t *)handle;
    if (extractor->extractor_handle) {
        if (type == ESP_EXTRACTOR_STREAM_TYPE_AUDIO) {
            if (stream_idx >= extractor->audio_stream_num) {
                ESP_LOGE(TAG, "Stream index out of range, line: %d", __LINE__);
                return ESP_GMF_ERR_FAIL;
            }
        } else if (type == ESP_EXTRACTOR_STREAM_TYPE_VIDEO) {
            if (stream_idx >= extractor->video_stream_num) {
                ESP_LOGE(TAG, "Stream index out of range, line: %d", __LINE__);
                return ESP_GMF_ERR_FAIL;
            }
        } else {
            ESP_LOGE(TAG, "Invalid stream type: %d", type);
            return ESP_GMF_ERR_INVALID_ARG;
        }
        esp_extractor_err_t extractor_ret = esp_extractor_get_stream_info(extractor->extractor_handle, type, stream_idx, info);
        if (extractor_ret != ESP_EXTRACTOR_ERR_OK) {
            ESP_LOGE(TAG, "Get stream info error, ret: %d, line: %d", extractor_ret, __LINE__);
            return ESP_GMF_ERR_FAIL;
        }
        return ESP_GMF_ERR_OK;
    }
    ESP_LOGI(TAG, "Extractor handle not initialized, line: %d", __LINE__);
    return ESP_GMF_ERR_INVALID_ARG;
}

esp_gmf_err_t player_extractor_get_track_info(esp_gmf_element_handle_t handle, esp_player_track_type_t type, uint16_t track_idx, esp_player_track_info_t *info)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    ESP_GMF_NULL_CHECK(TAG, info, return ESP_GMF_ERR_INVALID_ARG;);
    if (type != ESP_PLAYER_TRACK_TYPE_AUDIO && type != ESP_PLAYER_TRACK_TYPE_VIDEO) {
        ESP_LOGE(TAG, "Invalid track type: %d", (int)type);
        return ESP_GMF_ERR_INVALID_ARG;
    }

    esp_extractor_stream_type_t extractor_type = (type == ESP_PLAYER_TRACK_TYPE_AUDIO)
                                                     ? ESP_EXTRACTOR_STREAM_TYPE_AUDIO
                                                     : ESP_EXTRACTOR_STREAM_TYPE_VIDEO;
    esp_extractor_stream_info_t extractor_info = {0};
    esp_gmf_err_t ret = player_extractor_get_stream_info(handle, extractor_type, track_idx, &extractor_info);
    if (ret != ESP_GMF_ERR_OK) {
        return ret;
    }
    if (type == ESP_PLAYER_TRACK_TYPE_AUDIO) {
        PLAYER_EXTRACTOR_INFO_TO_AUDIO_STREAM_INFO(info, &extractor_info);
    } else {
        PLAYER_EXTRACTOR_INFO_TO_VIDEO_STREAM_INFO(info, &extractor_info);
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t player_extractor_trans_stream(esp_gmf_element_handle_t handle, esp_extractor_stream_type_t type, uint8_t selected_idx)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    esp_player_extractor_t *extractor = (esp_player_extractor_t *)handle;
    if (selected_idx > INT8_MAX) {
        ESP_LOGE(TAG, "selected_idx(%u) exceeds int8 range", (unsigned)selected_idx);
        return ESP_GMF_ERR_INVALID_ARG;
    }
    if (type == ESP_EXTRACTOR_STREAM_TYPE_AUDIO) {
        extractor->audio_selected_idx = selected_idx;
        if (extractor->audio_current_idx == extractor->audio_selected_idx) {
            return ESP_GMF_ERR_OK;
        }
    } else if (type == ESP_EXTRACTOR_STREAM_TYPE_VIDEO) {
        extractor->video_selected_idx = selected_idx;
        if (extractor->video_current_idx == extractor->video_selected_idx) {
            return ESP_GMF_ERR_OK;
        }
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t player_extractor_track_active(esp_gmf_element_handle_t handle, esp_extractor_stream_type_t type, int8_t *stream_idx)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    ESP_GMF_NULL_CHECK(TAG, stream_idx, return ESP_GMF_ERR_INVALID_ARG;);
    esp_player_extractor_t *extractor = (esp_player_extractor_t *)handle;
    if (extractor->extractor_handle == NULL) {
        ESP_LOGI(TAG, "Extractor handle not initialized, line: %d", __LINE__);
        return ESP_GMF_ERR_FAIL;
    }
    if (type == ESP_EXTRACTOR_STREAM_TYPE_AUDIO) {
        *stream_idx = extractor->audio_selected_idx;
    } else if (type == ESP_EXTRACTOR_STREAM_TYPE_VIDEO) {
        *stream_idx = extractor->video_selected_idx;
    } else {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t player_extractor_get_last_pts(esp_gmf_element_handle_t handle, uint64_t *pts_ms)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    ESP_GMF_NULL_CHECK(TAG, pts_ms, return ESP_GMF_ERR_INVALID_ARG;);
    esp_player_extractor_t *extractor = (esp_player_extractor_t *)handle;
    *pts_ms = extractor->last_pts;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t player_extractor_get_delta_pts(esp_gmf_element_handle_t handle, uint64_t *delta_ms)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    ESP_GMF_NULL_CHECK(TAG, delta_ms, return ESP_GMF_ERR_INVALID_ARG;);
    esp_player_extractor_t *extractor = (esp_player_extractor_t *)handle;
    *delta_ms = extractor->delta_pts;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t player_extractor_set_raw_pcm_info(esp_gmf_element_handle_t handle, uint32_t sample_rate,
                                                uint8_t channels, uint8_t bits_per_sample)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    esp_player_extractor_t *extractor = (esp_player_extractor_t *)handle;
    extractor->raw_sample_rate = sample_rate;
    extractor->raw_channels = channels;
    extractor->raw_bits_per_sample = bits_per_sample;
    return ESP_GMF_ERR_OK;
}

bool player_extractor_is_raw_source(esp_gmf_element_handle_t handle)
{
    if (handle == NULL) {
        return false;
    }
    esp_player_extractor_t *extractor = (esp_player_extractor_t *)handle;
    return extractor->raw_sample_rate != 0;
}
