
/**
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_capture_sync.h"
#include "esp_capture_types.h"
#include "esp_timer.h"
#include "capture_os.h"
#include <stdbool.h>
#include <stdlib.h>

#define ELAPSE(cur, last) (cur > last ? cur - last : cur + (0xFFFFFFFF - last))
#define CUR()             (uint32_t)(esp_timer_get_time() / 1000)

typedef struct {
    esp_capture_sync_mode_t  mode;
    uint32_t                 last_update_time;
    uint32_t                 last_update_pts;
    uint32_t                 last_audio_pts;
    bool                     started;
} sync_t;

esp_capture_err_t esp_capture_sync_create(esp_capture_sync_mode_t mode, esp_capture_sync_handle_t *handle)
{
    sync_t *sync = (sync_t *)capture_calloc(1, sizeof(sync_t));
    if (sync == NULL) {
        return ESP_CAPTURE_ERR_NO_MEM;
    }
    sync->mode = mode;
    *handle = sync;
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t esp_capture_sync_audio_update(esp_capture_sync_handle_t handle, uint32_t aud_pts)
{
    sync_t *sync = (sync_t *)handle;
    if (sync->mode == ESP_CAPTURE_SYNC_MODE_AUDIO) {
        sync->last_update_time = CUR();
        sync->last_update_pts = sync->last_audio_pts = aud_pts;
    }
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t esp_capture_sync_on(esp_capture_sync_handle_t handle)
{
    if (handle == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    sync_t *sync = (sync_t *)handle;
    sync->started = true;
    sync->last_update_pts = 0;
    sync->last_update_time = CUR();
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t esp_capture_sync_off(esp_capture_sync_handle_t handle)
{
    if (handle == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    sync_t *sync = (sync_t *)handle;
    sync->started = false;
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_sync_mode_t esp_capture_sync_get_mode(esp_capture_sync_handle_t handle)
{
    if (handle == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    sync_t *sync = (sync_t *)handle;
    return sync->mode;
}

esp_capture_err_t esp_capture_sync_get_current(esp_capture_sync_handle_t handle, uint32_t *pts)
{
    if (handle == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    sync_t *sync = (sync_t *)handle;
    if (sync->started == false) {
        *pts = sync->last_update_pts;
        return ESP_CAPTURE_ERR_OK;
    }
    uint32_t cur = CUR();
    uint32_t elapse = ELAPSE(cur, sync->last_update_time);
    *pts = sync->last_update_pts + elapse;
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t esp_capture_sync_destroy(esp_capture_sync_handle_t handle)
{
    if (handle == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    capture_free(handle);
    return ESP_CAPTURE_ERR_OK;
}
