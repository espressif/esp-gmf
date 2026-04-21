
/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_thread.h"
#include "video_render.h"
#include "settings.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "progress.h"

#define TAG  "VID_RENDER"

// Player configuration
typedef struct {
    const char              *path;
    int                      fps;
    bool                     cached;
    bool                     with_progress;
    esp_video_render_rect_t  video_rect;
    esp_video_render_clr_t   progress_bar_color;
} player_config_t;

// Extended player structure with all common resources
typedef struct {
    // File handling
    FILE                             *fp;
    bool                              eos;
    int                               last_size;
    long                              file_size;
    // Video rendering
    esp_video_render_frame_t          frame;
    esp_video_render_stream_handle_t  stream;
    esp_video_render_handle_t         video_render;
    // Progress bar
    progress_bar_handle_t             progress_bar;
    uint32_t                          last_progress_update;
    // Configuration
    int                               fps;
    bool                              cached;
    bool                              running;
    bool                              exited;
} player_t;

// Dual video configuration
typedef struct {
    const char             *paths[2];
    int                     fps;
    bool                    with_progress;
    esp_video_render_clr_t  progress_bar_colors[2];
} dual_video_config_t;

static int calc_scale_ratio(int display_width, int display_height,
                            int video_width, int video_height, int num_videos)
{
    int required_width = video_width * num_videos;
    int required_height = video_height;
    if (display_width >= required_width && display_height >= required_height) {
        return 1;  // No scaling needed
    }
    int ratio_w = (required_width + display_width - 1) / display_width;
    int ratio_h = (required_height + display_height - 1) / display_height;
    int ratio = (ratio_w > ratio_h) ? ratio_w : ratio_h;
    if (ratio <= 1) {
        return 1;
    } else if (ratio <= 2) {
        return 2;
    } else if (ratio <= 4) {
        return 4;
    } else {
        return 8;  // Maximum scale down to 1/8
    }
}

static int get_frame_end(uint8_t *data, int size, bool eof)
{
    for (int i = 0; i < size - 1; i++) {
        if (data[i] == 0xFF && data[i + 1] == 0xD9) {
            if (i + 3 < size) {
                if (data[i + 2] == 0xFF && data[i + 3] == 0xD8) {
                    return i + 2;
                }
            }
            if (eof && i == size - 2) {
                return i + 2;
            }
        }
    }
    return -1;
}

static int player_read_mjpeg_frame(player_t *player)
{
    esp_video_render_frame_t *frame = &player->frame;
    if (frame->data == NULL) {
        frame->data = esp_gmf_oal_malloc_align(64, MAX_FRAME_SIZE);
        if (frame->data == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for frame buffer");
            return -1;
        }
    }
    frame->format = ESP_VIDEO_RENDER_FORMAT_MJPEG;
    int filled = 0;
    if (player->last_size) {
        memmove(frame->data, frame->data + frame->size, player->last_size);
        filled = player->last_size;
        player->last_size = 0;
    } else if (player->eos) {
        return 1;
    }
    int frame_end = -1;
    do {
        frame_end = get_frame_end(frame->data, filled, player->eos);
        if (frame_end > 0) {
            break;
        }
        int left = MAX_FRAME_SIZE - filled;
        if (left <= 0) {
            return -1;
        }
        int rd = (int)fread(frame->data + filled, 1, left, player->fp);
        if (rd < 0) {
            return -1;
        }
        filled += rd;
        player->eos = feof(player->fp);
        if (rd) {
            frame_end = get_frame_end(frame->data, filled, player->eos);
        }
    } while (0);
    if (frame_end <= 0) {
        ESP_LOGE(TAG, "Not find in filled %d", filled);
        return -1;
    }
    frame->size = frame_end;
    player->last_size = filled - frame->size;
    return 0;
}

static void player_reset(player_t *player)
{
    fseek(player->fp, 0, SEEK_SET);
    player->last_size = 0;
    player->eos = false;
    if (player->progress_bar) {
        progress_bar_update(player->progress_bar, 0);
        player->last_progress_update = 0;
    }
}

static void player_update_progress(player_t *player)
{
    if (!player->progress_bar || player->file_size <= 0) {
        return;
    }
    long current_pos = ftell(player->fp) - player->last_size;
    if (current_pos < 0) {
        current_pos = 0;
    }
    uint8_t percent = (uint8_t)(((uint64_t)current_pos * 100) / player->file_size);
    if (percent > 100) {
        percent = 100;
    }
    progress_bar_update(player->progress_bar, percent);
}

static void cleanup_player(player_t *player)
{
    if (player->progress_bar) {
        progress_bar_destroy(player->progress_bar);
        player->progress_bar = NULL;
    }
    if (player->stream) {
        esp_video_render_stream_close(player->stream);
        player->stream = NULL;
    }
    if (player->fp) {
        fclose(player->fp);
        player->fp = NULL;
    }
    if (player->frame.data) {
        esp_gmf_oal_free(player->frame.data);
        player->frame.data = NULL;
    }
}

static int calc_player_rect(bool dual, int player_idx, player_config_t *config,
                            esp_video_render_handle_t video_render)
{
    esp_video_render_disp_info_t disp_info = {};
    int ret = esp_video_render_get_display_info(video_render, &disp_info);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to get display info: %d", ret);
        return -1;
    }
    if (dual == false) {
        config->video_rect.x = (disp_info.width - VIDEO_WIDTH) / 2;
        config->video_rect.y = (disp_info.height - VIDEO_HEIGHT) / 2;
        config->video_rect.width = VIDEO_WIDTH;
        config->video_rect.height = VIDEO_HEIGHT;
    } else {
        // Calculate scale ratio for side-by-side layout
        int video_width = VIDEO_WIDTH;
        int video_height = VIDEO_HEIGHT;
        int scale_ratio = calc_scale_ratio(disp_info.width, disp_info.height,
                                           video_width, video_height, 2);
        // Calculate scaled dimensions
        int scaled_width = video_width / scale_ratio;
        int scaled_height = video_height / scale_ratio;
        int total_width = scaled_width * 2;
        int start_x = (disp_info.width - total_width) / 2;
        int start_y = (disp_info.height - scaled_height) / 2;
        config->video_rect.x = start_x + player_idx * scaled_width;
        config->video_rect.y = start_y;
        config->video_rect.width = scaled_width;
        config->video_rect.height = scaled_height;
    }
    return 0;
}

static int prepare_player(player_t *player, const player_config_t *config,
                          esp_video_render_handle_t video_render)
{
    int ret = 0;
    memset(player, 0, sizeof(player_t));
    do {
        // Open file
        player->fp = fopen(config->path, "rb");
        if (player->fp == NULL) {
            ESP_LOGE(TAG, "Failed to open file %s", config->path);
            ret = -1;
            break;
        }
        // Get file size if progress bar is needed
        if (config->with_progress) {
            fseek(player->fp, 0, SEEK_END);
            player->file_size = ftell(player->fp);
            fseek(player->fp, 0, SEEK_SET);
            if (player->file_size <= 0) {
                ESP_LOGE(TAG, "Invalid file size: %ld", player->file_size);
                ret = -1;
                break;
            }
        }
        player->video_render = video_render;
        player->fps = config->fps;
        player->cached = config->cached;
        // Open stream
        esp_video_render_stream_info_t stream_info = {
            .info = {
                .format = ESP_VIDEO_RENDER_FORMAT_MJPEG,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = config->fps},
            .cached = config->cached,
        };
        ret = esp_video_render_stream_open(video_render, &stream_info, &player->stream);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to open stream ret %d", ret);
            break;
        }
        // Remove const qualifier for API call
        esp_video_render_rect_t video_rect = config->video_rect;
        ret = esp_video_render_stream_set_disp_rect(player->stream, &video_rect);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to set display rect ret %d", ret);
            break;
        }

        // Create progress bar if needed
        if (config->with_progress) {
            progress_bar_cfg_t progress_cfg = {
                .stream = player->stream,
                .bg_color = {.r = 40, .g = 40, .b = 40},
                .bar_color = config->progress_bar_color,
                .height = 8,
                .padding = 4,
                .bottom_margin = 10,
            };
            esp_video_render_disp_info_t disp_info = {};
            esp_video_render_get_display_info(video_render, &disp_info);
            progress_cfg.format = disp_info.format;
            player->progress_bar = progress_bar_create(&config->video_rect, &progress_cfg);
            if (player->progress_bar == NULL) {
                ESP_LOGW(TAG, "Failed to create progress bar, continuing without it");
            }
        }
        // Start async rendering if cached
        if (config->cached) {
            ret = esp_video_render_stream_render_async(player->stream);
            if (ret != 0) {
                break;
            }
        }
    } while (0);

    // Cleanup on error
    if (ret != 0) {
        cleanup_player(player);
    }

    return ret;
}

static int player_run(player_t *player, uint32_t play_time_ms)
{
    uint32_t start_time = esp_timer_get_time() / 1000;
    uint32_t end_time = start_time + play_time_ms;

    // For single video case, running is not set, so check end_time instead
    bool use_running = player->running;
    int ret = 0;
    while (1) {
        if (use_running && !player->running) {
            break;
        }
        uint32_t cur_time = esp_timer_get_time() / 1000;
        if (cur_time > end_time) {
            break;
        }

        ret = player_read_mjpeg_frame(player);
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to read frame");
            break;
        }

        // Repeat play
        if (ret == 1) {
            player_reset(player);
            continue;
        }

        // Update progress bar (throttle to every 100ms)
        if (player->progress_bar && (cur_time - player->last_progress_update >= 100)) {
            player_update_progress(player);
            player->last_progress_update = cur_time;
        }

        ret = esp_video_render_stream_write(player->stream, &player->frame);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGW(TAG, "Failed to write frame: %d", ret);
            if (ret == ESP_VIDEO_RENDER_ERR_INVALID_ARG || ret == ESP_VIDEO_RENDER_ERR_INVALID_STATE) {
                break;  // Stream closed
            }
        }
    }

    // Set progress to 100% at end
    if (ret == 0 && player->progress_bar) {
        progress_bar_update(player->progress_bar, 100);
    }
    player->exited = true;
    return ret;
}

// Decode thread function for dual video
static void decode_thread_func(void *arg)
{
    player_t *player = (player_t *)arg;

    ESP_LOGI(TAG, "Decode thread started for stream %p", player->stream);
    player_run(player, PLAY_TIME);
    ESP_LOGI(TAG, "Decode thread exiting");
    esp_gmf_oal_thread_delete(NULL);
}

static void simple_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static int run_dual_video_threads(const char *thread_names[2], int stack_size,
                                  uint32_t play_time_ms, player_t players[2])
{
    int ret = 0;
    for (int i = 0; i < 2; i++) {
        players[i].running = true;
        players[i].exited = false;
        // Decode run on core 0 or 1
        ret = esp_gmf_oal_thread_create(NULL, thread_names[i],
                                        decode_thread_func, &players[i],
                                        stack_size, 5, true, i % 2);
        players[i].running &= (ret == ESP_GMF_ERR_OK);
        players[i].exited = !players[i].running;
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to create decode thread %s ret %d", thread_names[i], ret);
            break;
        }
    }
    if (ret == 0) {
        simple_delay_ms(play_time_ms);
    }
    // Stop thread running
    for (int i = 0; i < 2; i++) {
        players[i].running = false;
    }
    // Wait for threads to exit
    int stop_timeout = 10;
    while (stop_timeout > 0) {
        if (players[0].exited && players[1].exited) {
            break;
        }
        simple_delay_ms(100);
        stop_timeout--;
    }
    if (stop_timeout == 0) {
        ESP_LOGE(TAG, "Failed to stop threads");
        ret = -1;
    }
    return ret;
}

int video_render_play_one_video(char *mjpeg_path, bool sync_render, int fps)
{
    player_t player = {};
    player_config_t config = {
        .path = mjpeg_path,
        .fps = fps,
        .cached = !sync_render,
        .with_progress = false,
    };
    int ret = create_video_render(config.fps);
    if (ret != 0) {
        return ret;
    }
    esp_video_render_handle_t video_render = get_video_render();
    ret = calc_player_rect(false, 0, &config, video_render);
    if (ret == 0) {
        ret = prepare_player(&player, &config, video_render);
    }
    if (ret == 0) {
        ret = player_run(&player, PLAY_TIME);
    }
    cleanup_player(&player);
    destroy_video_render();
    return ret;
}

int video_render_play_one_video_with_progress(char *mjpeg_path, int fps)
{
    player_t player = {};
    player_config_t config = {
        .path = mjpeg_path,
        .fps = fps,
        .cached = true,
        .with_progress = true,
        .progress_bar_color = {.r = 0, .g = 255, .b = 0},  // Green
    };
    int ret = create_video_render(config.fps);
    if (ret != 0) {
        return ret;
    }
    esp_video_render_handle_t video_render = get_video_render();
    calc_player_rect(false, 0, &config, video_render);
    ret = prepare_player(&player, &config, video_render);
    if (ret == 0) {
        ret = player_run(&player, PLAY_TIME);
    }
    cleanup_player(&player);
    destroy_video_render();
    return ret;
}

int video_render_play_dual_video(char *mjpeg_path1, char *mjpeg_path2, int fps)
{
    int ret = create_video_render(fps);
    if (ret != 0) {
        return ret;
    }
    esp_video_render_handle_t video_render = get_video_render();

    // Set background color to gray
    esp_video_render_clr_t bg_color = {.r = 0x40, .g = 0x40, .b = 0x40};
    esp_video_render_set_bg_color(video_render, &bg_color);

    player_config_t config = {
        .fps = fps,
        .cached = true,
        .with_progress = false,
    };
    player_t players[2] = {};
    const char *paths[2] = {mjpeg_path1, mjpeg_path2};

    for (int i = 0; i < 2; i++) {
        config.path = paths[i];
        calc_player_rect(true, i, &config, video_render);
        ret = prepare_player(&players[i], &config, video_render);
        if (ret != 0) {
            // Cleanup already prepared players
            for (int j = 0; j < i; j++) {
                cleanup_player(&players[j]);
            }
            destroy_video_render();
            return ret;
        }
    }

    const char *thread_names[2] = {"Decode1", "Decode2"};
    ret = run_dual_video_threads(thread_names, 30 * 1024, PLAY_TIME, players);

    for (int i = 0; i < 2; i++) {
        cleanup_player(&players[i]);
    }
    destroy_video_render();
    return ret;
}

int video_render_play_dual_video_with_progress(char *mjpeg_path1, char *mjpeg_path2, int fps)
{
    int ret = create_video_render(fps);
    if (ret != 0) {
        return ret;
    }
    esp_video_render_handle_t video_render = get_video_render();

    // Set background color to gray
    esp_video_render_clr_t bg_color = {.r = 0x40, .g = 0x40, .b = 0x40};
    esp_video_render_set_bg_color(video_render, &bg_color);

    player_config_t config = {
        .fps = fps,
        .cached = true,
        .with_progress = true,
    };
    player_t players[2] = {};
    const char *paths[2] = {mjpeg_path1, mjpeg_path2};
    esp_video_render_clr_t progress_colors[2] = {
        {.r = 0, .g = 255, .b = 0},  // Green for left
        {.r = 0, .g = 0, .b = 255},  // Blue for right
    };

    for (int i = 0; i < 2; i++) {
        config.path = paths[i];
        config.progress_bar_color = progress_colors[i];
        calc_player_rect(true, i, &config, video_render);
        ret = prepare_player(&players[i], &config, video_render);
        if (ret != 0) {
            // Cleanup already prepared players
            for (int j = 0; j < i; j++) {
                cleanup_player(&players[j]);
            }
            destroy_video_render();
            return ret;
        }
    }

    const char *thread_names[2] = {"Decode1_Prog", "Decode2_Prog"};
    ret = run_dual_video_threads(thread_names, 20 * 1024, PLAY_TIME, players);

    for (int i = 0; i < 2; i++) {
        cleanup_player(&players[i]);
    }
    destroy_video_render();
    return ret;
}
