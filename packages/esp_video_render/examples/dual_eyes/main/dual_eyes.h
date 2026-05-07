/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_video_render.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define BREAK_ON_FAIL(sta)  {                                             \
    int _ret = sta;                                                       \
    if (_ret) {                                                           \
        ESP_LOGE(TAG, "Fail at %s:%d ret %d", __func__, __LINE__, _ret);  \
        break;                                                            \
    }                                                                     \
}

/**
 * @brief  Video render information
 */
typedef struct {
    uint16_t  width;   /*!< Width of video render */
    uint16_t  height;  /*!< Height of video render */
} video_render_info_t;

/**
 * @brief  Enable or disable LVGL backend usage
 *
 * @param[in]  use  True to use LVGL backend, false to use default backend
 *
 * @return
 *       - None
 */
void video_render_use_lvgl(bool use);

/**
 * @brief  Create video render instance
 *
 * @param[in]  fps  Frame rate (frames per second)
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int create_video_render(uint8_t fps);

/**
 * @brief  Get video render information
 *
 * @param[in]   idx   Index of video render
 * @param[out]  info  Video render information
 */
void get_video_render_info(uint8_t idx, video_render_info_t *info);

/**
 * @brief  Get video render handle
 *
 * @return
 *       - Video  render handle on success
 *       - NULL   on failure
 */
esp_video_render_handle_t get_video_render(uint8_t idx);

/**
 * @brief  Destroy video render instance
 */
void destroy_video_render(void);

/**
 * @brief  Set repeat count
 *
 * @param[in]  repeat_count  Repeat count
 */
void dual_eyes_set_repeat_count(int repeat_count);

/**
 * @brief  Play a single video file
 *
 * @param[in]  mjpeg_path   Path to MJPEG video file
 * @param[in]  sync_render  True for synchronous rendering, false for asynchronous
 * @param[in]  fps          Frame rate (frames per second)
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int dual_eyes_on_one_display(char *left_mjpeg, char *right_mjpeg, int fps, bool use_lvgl);

/**
 * @brief  Play a single video file with progress bar
 *
 * @param[in]  mjpeg_path  Path to MJPEG video file
 * @param[in]  fps         Frame rate (frames per second)
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int dual_eyes_on_dual_display(char *left_mjpeg, char *right_mjpeg, int fps, bool use_lvgl);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
