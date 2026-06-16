/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
#include "esp_audio_render.h"
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */

#include "player_sync.h"
#include "player_events.h"
#include "player_stream.h"

typedef struct {
    player_sync_config_t  config;
    float                 speed;                /*!< Playback speed multiplier (default 1.0) */
    uint64_t              audio_render_pts_ms;  /*!< Audio render PTS (ms) */
    uint64_t              video_render_pts_ms;  /*!< Video render PTS (ms) */
    uint64_t              audio_decode_pts_ms;  /*!< Audio decode PTS (ms) */
    uint64_t              video_decode_pts_ms;  /*!< Video decode PTS (ms) */
    uint64_t              seek_target_ms;       /*!< Target PTS (ms) set by CMD_SEEK; read by decoders to detect seek completion */
    // FPS synchronization related fields
    float                 video_fps;            /*!< Video frame rate */
    uint64_t              frame_interval_ms;    /*!< Frame interval time in milliseconds */
    bool                  fps_sync_enabled;     /*!< Whether FPS sync is enabled */
    bool                  is_audio_paused;      /*!< Whether audio is paused */
    bool                  is_video_paused;      /*!< Whether video is paused */
    bool                  seek_in_progress;     /*!< CMD_SEEK is running; drop every render frame and never update render pts */
    uint64_t              sys_anchor_pts_ms;
    uint64_t              sys_anchor_wall_ms;
} player_sync_internal_t;

static const char *TAG = "esp_player_sync";

#define MASTER_CLOCK_DROP_LATE_MIN_MS  30
#define MASTER_CLOCK_REANCHOR_LATE_MS  1000
#define GET_CURRENT_TIME()             (pdTICKS_TO_MS(xTaskGetTickCount()))

static inline bool sync_validate_handle(player_sync_handle_t handle, player_sync_internal_t **sync)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "Invalid handle: %p", handle);
        return false;
    }
    *sync = (player_sync_internal_t *)handle;
    return true;
}

static inline void sync_master_clock_reset(player_sync_internal_t *sync)
{
    sync->sys_anchor_pts_ms = 0;
    sync->sys_anchor_wall_ms = 0;
}

static inline int64_t sync_master_clock_drop_late_ms(const player_sync_internal_t *sync)
{
    int64_t t = (int64_t)sync->frame_interval_ms;
    return (t < MASTER_CLOCK_DROP_LATE_MIN_MS) ? MASTER_CLOCK_DROP_LATE_MIN_MS : t;
}

static inline void sync_handle_audio_pause(player_sync_internal_t *sync)
{
    if (sync->seek_in_progress) {
        return;
    }
    if (sync->is_audio_paused == true) {
        if (player_wait_events(sync->config.bit_ctx, sync->config.audio_resume_bit, UINT32_MAX) != ESP_PLAYER_ERR_OK) {
            return;
        }
        sync->is_audio_paused = false;
    }
}

static inline void sync_handle_video_pause(player_sync_internal_t *sync)
{
    if (sync->seek_in_progress) {
        return;
    }
    if (sync->is_video_paused == true) {
        if (player_wait_events(sync->config.bit_ctx, sync->config.video_resume_bit, UINT32_MAX) != ESP_PLAYER_ERR_OK) {
            return;
        }
        sync->is_video_paused = false;
    }
}

static inline bool sync_handle_system_mode(player_sync_internal_t *sync, uint64_t *pts_ptr)
{
    (void)sync;
    if (*pts_ptr == 0) {
        *pts_ptr = GET_CURRENT_TIME();
        return true;
    }
    return false;
}

static inline bool sync_check_delay(player_sync_internal_t *sync, uint64_t current_pts_ms,
                                    uint64_t last_pts_ms, float speed, int64_t threshold, const char *pts_name)
{
    int64_t delay = (int64_t)current_pts_ms - (int64_t)last_pts_ms;
    delay = (int64_t)(delay * speed);
    if (delay > threshold) {
        ESP_LOGD(TAG, "%s frame delayed: pts_ms=%" PRIu64 ", last_pts_ms=%" PRIu64 ", delay=%lld",
                 pts_name, current_pts_ms, last_pts_ms, delay);
        return false;
    }
    return true;
}

static bool sync_master_clock_pace(player_sync_internal_t *sync, uint64_t pts_ms)
{
    if (pts_ms == 0) {
        return true;
    }
    float speed = sync->speed;
    if (speed <= 0.f) {
        speed = 1.f;
    }
    if (sync->sys_anchor_pts_ms == 0) {
        sync->sys_anchor_pts_ms = pts_ms;
        sync->sys_anchor_wall_ms = GET_CURRENT_TIME();
        sync->video_render_pts_ms = pts_ms;
        return true;
    }
    int64_t elapsed_pts_ms = (int64_t)pts_ms - (int64_t)sync->sys_anchor_pts_ms;
    if (elapsed_pts_ms < 0) {
        /* PTS went backwards (seek backwards, loop, new track): re-anchor silently. */
        sync->sys_anchor_pts_ms = pts_ms;
        sync->sys_anchor_wall_ms = GET_CURRENT_TIME();
        sync->video_render_pts_ms = pts_ms;
        return true;
    }
    int64_t target_wall_ms = (int64_t)sync->sys_anchor_wall_ms
                             + (int64_t)((double)elapsed_pts_ms / (double)speed);
    int64_t now_wall_ms = (int64_t)GET_CURRENT_TIME();
    int64_t diff_ms = target_wall_ms - now_wall_ms;
    if (diff_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(diff_ms));
    } else if (-diff_ms > MASTER_CLOCK_REANCHOR_LATE_MS) {
        sync->sys_anchor_pts_ms = pts_ms;
        sync->sys_anchor_wall_ms = GET_CURRENT_TIME();
    } else if (speed > 1.0f && -diff_ms > sync_master_clock_drop_late_ms(sync)) {
        sync->video_render_pts_ms = pts_ms;
        return false;
    }
    sync->video_render_pts_ms = pts_ms;
    return true;
}

esp_player_err_t player_sync_create(player_sync_config_t *config, player_sync_handle_t *handle)
{
    if (handle == NULL || config == NULL) {
        ESP_LOGE(TAG, "Invalid argument. config %p, handle %p, line %d", config, handle, __LINE__);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    player_sync_internal_t *sync = (player_sync_internal_t *)calloc(1, sizeof(player_sync_internal_t));
    if (sync == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory. line %d", __LINE__);
        return ESP_PLAYER_ERR_NO_MEM;
    }
    memcpy(&sync->config, config, sizeof(player_sync_config_t));
    sync->speed = 1.0f;
    sync->video_fps = 0.0f;
    sync->fps_sync_enabled = false;

    sync->audio_render_pts_ms = 0;
    sync->video_render_pts_ms = 0;
    sync->audio_decode_pts_ms = 0;
    sync->video_decode_pts_ms = 0;

    sync->is_audio_paused = false;
    sync->is_video_paused = false;
    *handle = (player_sync_handle_t)sync;
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_sync_set_mode(player_sync_handle_t handle, esp_player_sync_mode_t sync_mode)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    if (sync_mode >= ESP_PLAYER_SYNC_MODE_MAX) {
        ESP_LOGE(TAG, "Invalid sync_mode: %d", (int)sync_mode);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    sync->config.sync_mode = sync_mode;
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_sync_destroy(player_sync_handle_t handle)
{
    if (handle == NULL) {
        return ESP_PLAYER_ERR_OK;
    }
    player_sync_internal_t *sync = (player_sync_internal_t *)handle;
    free(sync);
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_sync_set_speed(player_sync_handle_t handle, float speed)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    sync->speed = speed;
    sync_master_clock_reset(sync);
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_sync_set_audio_delay_threshold(player_sync_handle_t handle, int64_t audio_delay_threshold)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    sync->config.audio_delay_threshold = audio_delay_threshold;
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_sync_set_video_delay_threshold(player_sync_handle_t handle, int64_t video_delay_threshold)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    sync->config.video_delay_threshold = video_delay_threshold;
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_sync_set_video_fps(player_sync_handle_t handle, float fps)
{
    if (handle == NULL || fps <= 0.0f) {
        ESP_LOGE(TAG, "Invalid parameters: handle=%p, fps=%.2f", handle, fps);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }

    player_sync_internal_t *sync = (player_sync_internal_t *)handle;
    sync->video_fps = fps;
    sync->frame_interval_ms = (uint64_t)(1000.0f / fps);  // Calculate frame interval in milliseconds
    ESP_LOGI(TAG, "Video FPS set to %.2f, frame interval: %" PRIu64 " ms", fps, sync->frame_interval_ms);
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_sync_enable_video_fps_sync(player_sync_handle_t handle, bool enable)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    sync->fps_sync_enabled = enable;
    sync->video_render_pts_ms = 0;
    sync->video_decode_pts_ms = 0;
    return ESP_PLAYER_ERR_OK;
}

static esp_player_err_t player_sync_wait_for_next_frame(player_sync_handle_t handle, uint64_t timeout_ms)
{
    player_sync_internal_t *sync = (player_sync_internal_t *)handle;

    float speed = sync->speed;
    if (speed <= 0.f) {
        speed = 1.f;
    }
    uint64_t effective_interval_ms = (uint64_t)((double)sync->frame_interval_ms / (double)speed);
    if (effective_interval_ms == 0) {
        effective_interval_ms = sync->frame_interval_ms;
    }
    uint64_t effective_timeout_ms = timeout_ms;
    if (timeout_ms > 0) {
        effective_timeout_ms = (uint64_t)((double)timeout_ms / (double)speed);
        if (effective_timeout_ms == 0) {
            effective_timeout_ms = timeout_ms;
        }
    }

    uint64_t cur_interval_ms = GET_CURRENT_TIME() - sync->video_render_pts_ms;

    // If time has already arrived, return directly
    if (cur_interval_ms * 10 >= effective_interval_ms * 7) {
        sync->video_render_pts_ms = GET_CURRENT_TIME();
        return ESP_PLAYER_ERR_OK;
    }

    // Calculate wait time needed
    uint64_t wait_time_ms = effective_interval_ms - cur_interval_ms;

    // If timeout is set, take the smaller value
    bool timeout_reached = false;
    if (effective_timeout_ms > 0 && wait_time_ms > effective_timeout_ms) {
        wait_time_ms = effective_timeout_ms;
        timeout_reached = true;
    }
    // Use vTaskDelay to wait
    vTaskDelay(pdMS_TO_TICKS(wait_time_ms));

    // Update frame time
    sync->video_render_pts_ms = GET_CURRENT_TIME();

    ESP_LOGD(TAG, "FPS sync wait completed, waited %" PRIu64 " ms", wait_time_ms);
    return timeout_reached ? ESP_PLAYER_ERR_TIMEOUT : ESP_PLAYER_ERR_OK;
}

bool player_sync_audio_render_frame(player_sync_handle_t handle, uint64_t pts_ms)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return false;
    }
    /* Seek gate: drop frames during CMD_SEEK without touching the render clock. */
    if (sync->seek_in_progress) {
        return false;
    }
    sync_handle_audio_pause(sync);
    /* Re-check after possible unblock from sync_handle_audio_pause() */
    if (sync->seek_in_progress) {
        return false;
    }
    if (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_NONE) {
        if (pts_ms > 0) {
            sync->audio_render_pts_ms = pts_ms;
        }
        return true;
    }
    uint64_t last_pts_ms = sync->audio_render_pts_ms;
    if (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_AUDIO) {
        if (last_pts_ms == 0) {
            sync->audio_render_pts_ms = pts_ms;
            return true;
        }
        if (!sync_check_delay(sync, pts_ms, last_pts_ms,
                              sync->speed, sync->config.audio_delay_threshold, "Audio")) {
            return false;
        }
        sync->audio_render_pts_ms = pts_ms;
        return true;
    }
    // System time sync mode - sync after decode
    if (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_SYSTEM) {
        if (sync_handle_system_mode(sync, &sync->audio_render_pts_ms)) {
            return true;
        }
        uint64_t current_pts_ms = GET_CURRENT_TIME();
        if (!sync_check_delay(sync, current_pts_ms, last_pts_ms,
                              sync->speed, sync->config.audio_delay_threshold, "Audio")) {
            return false;
        }
        sync->audio_render_pts_ms = current_pts_ms;
        return true;
    }
    last_pts_ms = sync->audio_render_pts_ms;
    if (last_pts_ms == 0) {
        return true;
    }
    if (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_VIDEO) {
        sync->audio_render_pts_ms = pts_ms;
    }
    if (!sync_check_delay(sync, sync->audio_render_pts_ms, last_pts_ms,
                          sync->speed, sync->config.audio_delay_threshold, "Audio")) {
        return false;
    }
    return true;
}

bool player_sync_video_render_frame(player_sync_handle_t handle, uint64_t pts_ms)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return false;
    }
    if (sync->seek_in_progress) {
        return false;
    }
    sync_handle_video_pause(sync);
    if (sync->seek_in_progress) {
        return false;
    }
    if (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_NONE) {
        if (pts_ms > 0) {
            sync->video_render_pts_ms = pts_ms;
        }
        return true;
    }
    uint64_t last_pts_ms = sync->video_render_pts_ms;

    /* AUDIO master: drop video that is too far behind the audio render clock (uses render-side PTS). */
    if (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_AUDIO && pts_ms > 0) {
        uint64_t aud_ref = sync->audio_render_pts_ms;
        if (aud_ref > 0) {
            int64_t late_by_ms = (int64_t)aud_ref - (int64_t)pts_ms;
            if (late_by_ms > sync->config.video_delay_threshold) {
                ESP_LOGD(TAG, "Video render drop (late vs audio): vid_pts=%" PRIu64 " aud_pts=%" PRIu64
                              " late_ms=%lld thr=%lld",
                         pts_ms, aud_ref, (long long)late_by_ms, (long long)sync->config.video_delay_threshold);
                return false;
            }
        }
    }

    bool master_clock_pace = (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_SYSTEM
                              || (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_AUDIO
                                  && sync->audio_render_pts_ms == 0))
                             && sync->config.audio_render_stream != NULL;
    if (master_clock_pace) {
        return sync_master_clock_pace(sync, pts_ms);
    }

    /* FPS pacing is handled by player_sync_video_fps_sync() in the render element; accept frame here. */
    if (sync->fps_sync_enabled) {
        if (pts_ms > 0) {
            sync->video_render_pts_ms = pts_ms;
        }
        return true;
    }

    // Video sync mode
    if (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_VIDEO) {
        sync->video_render_pts_ms = pts_ms;
        return true;
    }
    if (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_AUDIO) {
        if (last_pts_ms == 0) {
            sync->video_render_pts_ms = pts_ms;
            return true;
        }
        if (!sync_check_delay(sync, pts_ms, last_pts_ms,
                              sync->speed, sync->config.video_delay_threshold, "Video")) {
            return false;
        }
        sync->video_render_pts_ms = pts_ms;
        return true;
    }
    if (last_pts_ms == 0) {
        return true;
    }
    if (!sync_check_delay(sync, sync->video_render_pts_ms, last_pts_ms,
                          sync->speed, sync->config.video_delay_threshold, "Video")) {
        return false;
    }
    return true;
}

esp_player_err_t player_sync_video_fps_sync(player_sync_handle_t handle)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    /* Skip when master clock is active in player_sync_video_render_frame()
     * to avoid double-pacing. Condition mirrors the gate there. */
    bool master_clock_active =
        (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_SYSTEM
         || (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_AUDIO
             && sync->audio_render_pts_ms == 0))
        && sync->config.audio_render_stream != NULL;
    if (master_clock_active) {
        return ESP_PLAYER_ERR_OK;
    }
    if (sync->fps_sync_enabled) {
        // Use blocking wait mechanism to ensure precise FPS control
        // Timeout set to frame interval time (1000/fps milliseconds)
        ESP_LOGD(TAG, "FPS sync enabled: fps=%.2f, sync->video_render_pts_ms=%" PRIu64, sync->video_fps, sync->video_render_pts_ms);
        if (sync->video_render_pts_ms == 0) {
            sync->video_render_pts_ms = GET_CURRENT_TIME();
            return ESP_PLAYER_ERR_OK;
        }
        esp_player_err_t ret = player_sync_wait_for_next_frame(handle, sync->frame_interval_ms);
        if (ret == ESP_PLAYER_ERR_OK) {
            ESP_LOGD(TAG, "Video frame rendered at FPS sync: pts_ms=%" PRIu64, sync->video_render_pts_ms);
            return ESP_PLAYER_ERR_OK;
        } else if (ret == ESP_PLAYER_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "FPS sync wait timeout, rendering frame anyway: pts_ms=%" PRIu64, sync->video_render_pts_ms);
            return ESP_PLAYER_ERR_TIMEOUT;  // Play even on timeout to avoid blocking
        } else {
            ESP_LOGW(TAG, "FPS sync wait failed: %d, pts_ms=%" PRIu64, ret, sync->video_render_pts_ms);
            return ESP_PLAYER_ERR_FAIL;
        }
    }
    return ESP_PLAYER_ERR_OK;
}

bool player_sync_audio_decode_frame(player_sync_handle_t handle, uint64_t pts_ms)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return false;
    }
    if (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_NONE) {
        if (pts_ms > 0) {
            sync->audio_decode_pts_ms = pts_ms;
        }
        return true;
    }
    uint64_t last_pts_ms = sync->audio_decode_pts_ms;
    // Audio sync mode
    if (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_AUDIO) {
        sync->audio_decode_pts_ms = pts_ms;
        return true;
    }
    // System time sync mode
    if (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_SYSTEM) {
        if (sync_handle_system_mode(sync, &sync->audio_decode_pts_ms)) {
            return true;
        }
        uint64_t current_pts_ms = GET_CURRENT_TIME();
        if (!sync_check_delay(sync, current_pts_ms, last_pts_ms,
                              sync->speed, sync->config.audio_delay_threshold, "Audio")) {
            return false;
        }
        sync->audio_decode_pts_ms = current_pts_ms;
        return true;
    }
    last_pts_ms = sync->audio_decode_pts_ms;
    if (last_pts_ms == 0) {
        return true;
    }
    if (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_VIDEO) {
        sync->audio_decode_pts_ms = pts_ms;
    }
    if (!sync_check_delay(sync, sync->audio_decode_pts_ms, last_pts_ms,
                          sync->speed, sync->config.audio_delay_threshold, "Audio")) {
        return false;
    }
    return true;
}

bool player_sync_video_decode_frame(player_sync_handle_t handle, uint64_t pts_ms)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return false;
    }

    if (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_NONE) {
        if (pts_ms > 0) {
            sync->video_decode_pts_ms = pts_ms;
        }
        return true;
    }
    uint64_t last_pts_ms = sync->video_decode_pts_ms;
    // Video sync mode
    if (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_VIDEO) {
        sync->video_decode_pts_ms = pts_ms;
        return true;
    }
    // System time sync mode
    if (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_SYSTEM) {
        if (sync_handle_system_mode(sync, &sync->video_decode_pts_ms)) {
            return true;
        }
        uint64_t current_pts_ms = GET_CURRENT_TIME();
        if (!sync_check_delay(sync, current_pts_ms, last_pts_ms,
                              sync->speed, sync->config.video_delay_threshold, "Video")) {
            return false;
        }
        sync->video_decode_pts_ms = current_pts_ms;
        return true;
    }
    last_pts_ms = sync->video_decode_pts_ms;
    if (last_pts_ms == 0) {
        return true;
    }
    if (sync->config.sync_mode == ESP_PLAYER_SYNC_MODE_AUDIO) {
        sync->video_decode_pts_ms = pts_ms;
    }
    if (!sync_check_delay(sync, sync->video_decode_pts_ms, last_pts_ms,
                          sync->speed, sync->config.video_delay_threshold, "Video")) {
        return false;
    }
    return true;
}

esp_player_err_t player_sync_pause(player_sync_handle_t handle)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    if (sync->config.bit_ctx) {
        player_clear_events((esp_player_stream_t *)sync->config.bit_ctx,
                            sync->config.audio_resume_bit | sync->config.video_resume_bit);
    }
    sync->is_audio_paused = true;
    sync->is_video_paused = true;
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_sync_resume(player_sync_handle_t handle)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    if (sync->is_audio_paused) {
        player_set_events(sync->config.bit_ctx, sync->config.audio_resume_bit);
    }
    if (sync->is_video_paused) {
        player_set_events(sync->config.bit_ctx, sync->config.video_resume_bit);
    }
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_sync_reset(player_sync_handle_t handle)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    sync->audio_render_pts_ms = 0;
    sync->video_render_pts_ms = 0;
    sync->audio_decode_pts_ms = 0;
    sync->video_decode_pts_ms = 0;
    sync->fps_sync_enabled = false;
    sync->video_fps = 0.0f;
    sync->frame_interval_ms = 0;
    sync_master_clock_reset(sync);
    return ESP_PLAYER_ERR_OK;
}

uint64_t player_sync_get_audio_render_pts_ms_with_latency(player_sync_handle_t handle)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return 0;
    }
    uint64_t pts = sync->audio_render_pts_ms;
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
    if (sync->config.audio_render_stream && sync->config.bit_ctx) {
        esp_player_stream_t *stream = (esp_player_stream_t *)sync->config.bit_ctx;
        if (stream->task_status & TASK_STATUS_AUDIO_RENDER_RUNNING) {
            uint32_t latency_ms = 0;
            if (esp_audio_render_stream_get_latency(
                    (esp_audio_render_stream_handle_t)sync->config.audio_render_stream,
                    &latency_ms) == ESP_AUDIO_RENDER_ERR_OK) {
                pts = (pts > latency_ms) ? (pts - latency_ms) : 0;
            }
        }
    }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */
    return pts;
}

uint64_t player_sync_get_audio_render_pts_ms(player_sync_handle_t handle)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return 0;
    }
    return sync->audio_render_pts_ms;
}

uint64_t player_sync_get_audio_decode_pts_ms(player_sync_handle_t handle)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return 0;
    }
    return sync->audio_decode_pts_ms;
}

uint64_t player_sync_get_video_render_pts_ms(player_sync_handle_t handle)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return 0;
    }
    return sync->video_render_pts_ms;
}

esp_player_err_t player_sync_set_render_pts(player_sync_handle_t handle, uint64_t time_ms)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    sync->audio_render_pts_ms = time_ms;
    sync->video_render_pts_ms = time_ms;
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_sync_set_seek_target(player_sync_handle_t handle, uint64_t time_ms)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    sync->seek_target_ms = time_ms;
    return ESP_PLAYER_ERR_OK;
}

uint64_t player_sync_get_seek_target(player_sync_handle_t handle)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return 0;
    }
    return sync->seek_target_ms;
}

esp_player_err_t player_sync_set_seek_in_progress(player_sync_handle_t handle, bool in_progress)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    sync->seek_in_progress = in_progress;
    return ESP_PLAYER_ERR_OK;
}

bool player_sync_get_seek_in_progress(player_sync_handle_t handle)
{
    player_sync_internal_t *sync = NULL;
    if (!sync_validate_handle(handle, &sync)) {
        return false;
    }
    return sync->seek_in_progress;
}
