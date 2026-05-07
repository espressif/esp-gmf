/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_extractor.h"
#include "esp_extractor_defaults.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "settings.h"
#include "video_player_view.h"
#include "video_render_sys.h"
#include "esp_video_render_proc.h"
#include "esp_gmf_oal_sys.h"

#define TAG                        "VIDEO_PLAYER"
#define PLAYER_FRAME_QUEUE_LEN     (6)
#define PLAYER_WORKER_STACK_SIZE   (40 * 1024)
#define PLAYER_EVT_EXTRACT_EXITED  BIT0
#define PLAYER_EVT_DECODE_EXITED   BIT1
#define PLAYER_EVT_WORKERS_EXITED  (PLAYER_EVT_EXTRACT_EXITED | PLAYER_EVT_DECODE_EXITED)

typedef enum {
    PLAYER_CMD_NONE = 0,
    PLAYER_CMD_PLAY,
    PLAYER_CMD_NEXT,
    PLAYER_CMD_PREV,
    PLAYER_CMD_PAUSE,
    PLAYER_CMD_RESUME,
    PLAYER_CMD_STOP,
    PLAYER_CMD_SEEK,
    PLAYER_CMD_SET_VOL,
    PLAYER_CMD_MUTE,
    PLAYER_CMD_CTRL,
    PLAYER_CMD_EXIT,
} player_cmd_id_t;

typedef struct {
    player_cmd_id_t  id;
    int              value;
} player_cmd_t;

typedef struct {
    char  path[PLAYER_MAX_PLAYLIST][PLAYER_MAX_PATH_LEN];
    int   count;
} playlist_t;

typedef struct {
    playlist_t                        playlist;
    int                               cur;
    bool                              running;
    bool                              paused;
    bool                              muted;
    int                               volume;
    int                               duration_ms;
    int                               pos_ms;
    int                               decoded_frames;
    int                               recv_decoded_frames;
    uint32_t                          last_ui_ms;
    esp_video_render_handle_t         render;
    esp_video_render_stream_handle_t  stream;
    esp_video_render_stream_handle_t  overlay_stream;
    esp_video_render_stream_info_t    stream_info;
    esp_extractor_handle_t            extractor;
    esp_extractor_config_t           *extract_cfg;
    video_player_view_t              *view;
    QueueHandle_t                     cmd_q;
    QueueHandle_t                     frame_q;
    SemaphoreHandle_t                 media_lock;
    TaskHandle_t                      extract_task;
    TaskHandle_t                      decode_task;
    volatile bool                     worker_should_exit;
    EventGroupHandle_t                worker_events;
    bool                              with_ui;
    bool                              fullscreen;
    uint8_t                           full_speed_decode;
    esp_video_render_proc_handle_t    dec_proc;
} player_ctx_t;

typedef struct {
    FILE    *fp;
    uint8_t *io_cache;
} file_src_t;

typedef struct {
    esp_extractor_handle_t      extractor;
    esp_extractor_frame_info_t  frame;
} frame_msg_t;

static void flush_frame_queue_locked(player_ctx_t *ctx);
static void video_extract_task(void *arg);
static void video_decode_task(void *arg);
static esp_err_t player_console_init(QueueHandle_t cmd_q);

static QueueHandle_t s_console_cmd_q;
static bool s_console_inited;

static bool create_task(TaskFunction_t main_func, const char *name, uint32_t stack, void *arg, UBaseType_t prio,
                        int core_id, TaskHandle_t *task_handle)
{
    return pdPASS == xTaskCreatePinnedToCoreWithCaps(main_func, name, stack, arg, prio, task_handle,
                                                     core_id, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static int clamp_i(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int file_read_cb(void *buffer, uint32_t size, void *ctx)
{
    file_src_t *s = (file_src_t *)ctx;
    return (int)fread(buffer, 1, size, s->fp);
}

static int file_seek_cb(uint32_t pos, void *ctx)
{
    file_src_t *s = (file_src_t *)ctx;
    return fseek(s->fp, pos, SEEK_SET);
}

static uint32_t file_size_cb(void *ctx)
{
    file_src_t *s = (file_src_t *)ctx;
    long old = ftell(s->fp);
    fseek(s->fp, 0, SEEK_END);
    long end = ftell(s->fp);
    fseek(s->fp, old, SEEK_SET);
    return end > 0 ? (uint32_t)end : 0;
}

static bool has_video_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL) {
        return false;
    }
    dot++;
    char ext[8] = {0};
    int n = 0;
    while (*dot && n < (int)sizeof(ext) - 1) {
        ext[n++] = (char)tolower((int)*dot);
        dot++;
    }
    return strcmp(ext, "mp4") == 0 || strcmp(ext, "avi") == 0 || strcmp(ext, "ts") == 0;
}

static void playlist_add(playlist_t *list, const char *path)
{
    if (list->count >= PLAYER_MAX_PLAYLIST) {
        return;
    }
    ESP_LOGI(TAG, "Adding media: %s", path);
    snprintf(list->path[list->count], PLAYER_MAX_PATH_LEN, "%s", path);
    list->count++;
}

static void build_playlist(const char *url, playlist_t *list)
{
    memset(list, 0, sizeof(*list));
    if (url == NULL) {
        return;
    }
    DIR *d = opendir(url);
    if (d == NULL) {
        if (has_video_ext(url)) {
            playlist_add(list, url);
        }
        return;
    }
    struct dirent *ent = NULL;
    int dir_len = strlen(url);
    while ((ent = readdir(d)) != NULL && list->count < PLAYER_MAX_PLAYLIST) {
        if (ent->d_type == DT_DIR) {
            continue;
        }
        if (!has_video_ext(ent->d_name)) {
            continue;
        }
        char full[PLAYER_MAX_PATH_LEN] = {0};
        int file_len = strlen(ent->d_name);
        if (dir_len + 2 + file_len >= PLAYER_MAX_PATH_LEN) {
            continue;
        }
        memcpy(full, url, dir_len);
        full[dir_len] = '/';
        memcpy(full + dir_len + 1, ent->d_name, file_len + 1);
        playlist_add(list, full);
    }
    closedir(d);
}

static uint8_t *allocate_io_cache(uint32_t size)
{
    uint32_t caps = MALLOC_CAP_CACHE_ALIGNED;
#if CONFIG_SPIRAM_BOOT_INIT && SOC_SDMMC_PSRAM_DMA_CAPABLE
    caps |= MALLOC_CAP_SPIRAM;
#else
    caps |= MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
#endif  /* CONFIG_SPIRAM_BOOT_INIT && SOC_SDMMC_PSRAM_DMA_CAPABLE */
    return heap_caps_malloc(size, caps);
}

static esp_extractor_config_t *alloc_file_cfg(const char *path, uint32_t out_pool_size)
{
    file_src_t *src = (file_src_t *)calloc(1, sizeof(file_src_t));
    esp_extractor_config_t *cfg = (esp_extractor_config_t *)calloc(1, sizeof(esp_extractor_config_t));
    if (src == NULL || cfg == NULL) {
        free(src);
        free(cfg);
        return NULL;
    }
    src->fp = fopen(path, "rb");
    if (src->fp == NULL) {
        free(src);
        free(cfg);
        return NULL;
    }
    src->io_cache = allocate_io_cache(CONFIG_EXTRACTOR_HELPER_FILE_IO_CACHE_SIZE);
    if (src->io_cache) {
        setvbuf(src->fp, (char *)src->io_cache, _IOFBF, CONFIG_EXTRACTOR_HELPER_FILE_IO_CACHE_SIZE);
    }
    cfg->extract_mask = ESP_EXTRACT_MASK_VIDEO;
    cfg->in_ctx = src;
    cfg->out_pool_size = out_pool_size;
    cfg->out_align = 64;
    cfg->in_read_cb = file_read_cb;
    cfg->in_seek_cb = file_seek_cb;
    cfg->in_size_cb = file_size_cb;
    return cfg;
}

static void free_file_cfg(esp_extractor_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    if (cfg->in_ctx) {
        file_src_t *src = (file_src_t *)cfg->in_ctx;
        if (src->fp) {
            fclose(src->fp);
        }
        if (src->io_cache) {
            heap_caps_free(src->io_cache);
            src->io_cache = NULL;
        }
        free(cfg->in_ctx);
    }
    free(cfg);
}

static esp_video_render_format_t map_format(esp_extractor_format_t f)
{
    if (f == ESP_EXTRACTOR_VIDEO_FORMAT_MJPEG) {
        return ESP_VIDEO_RENDER_FORMAT_MJPEG;
    }
    if (f == ESP_EXTRACTOR_VIDEO_FORMAT_H264) {
        return ESP_VIDEO_RENDER_FORMAT_H264;
    }
    return ESP_VIDEO_RENDER_FORMAT_NONE;
}

static void close_media(player_ctx_t *ctx)
{
    uint8_t wait_bits = 0;
    if (xSemaphoreTake(ctx->media_lock, pdMS_TO_TICKS(500)) == pdTRUE) {
        ctx->worker_should_exit = true;
        if (ctx->extract_task) {
            wait_bits |= PLAYER_EVT_EXTRACT_EXITED;
        }
        if (ctx->decode_task) {
            wait_bits |= PLAYER_EVT_DECODE_EXITED;
        }
        xSemaphoreGive(ctx->media_lock);
    }
    if (ctx->dec_proc) {
        // Close proc so that can let reader and writer quit
        esp_video_render_proc_close(ctx->dec_proc);
        ctx->dec_proc = NULL;
    }
    if (ctx->worker_events && wait_bits) {
        (void)xEventGroupWaitBits(ctx->worker_events, wait_bits, pdFALSE, pdTRUE, pdMS_TO_TICKS(1500));
        xEventGroupClearBits(ctx->worker_events, wait_bits);
    }
    if (xSemaphoreTake(ctx->media_lock, pdMS_TO_TICKS(500)) == pdTRUE) {
        flush_frame_queue_locked(ctx);
        if (ctx->view) {
            video_player_view_deinit(ctx->view);
            ctx->view = NULL;
        }
        if (ctx->stream) {
            esp_video_render_stream_close(ctx->stream);
            ctx->stream = NULL;
        }
        if (ctx->overlay_stream) {
            esp_video_render_stream_close(ctx->overlay_stream);
            ctx->overlay_stream = NULL;
        }
        if (ctx->extractor) {
            esp_extractor_close(ctx->extractor);
            ctx->extractor = NULL;
        }
        if (ctx->extract_cfg) {
            free_file_cfg(ctx->extract_cfg);
            ctx->extract_cfg = NULL;
        }
        xSemaphoreGive(ctx->media_lock);
    }
}

static inline const char *get_file_name(const char *path)
{
    char *name = strrchr(path, '/');
    return name ? name + 1 : path;
}

static esp_video_render_err_t open_video_stream(player_ctx_t *ctx)
{
    esp_video_render_err_t ret = esp_video_render_stream_open(ctx->render, &ctx->stream_info, &ctx->stream);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open video stream");
        return ret;
    }
    if (ctx->fullscreen) {
        esp_video_render_disp_info_t disp = {};
        esp_video_render_get_display_info(ctx->render, &disp);
        esp_video_render_rect_t rect = {.x = 0, .y = 0, .width = disp.width, .height = disp.height};
        esp_video_render_stream_set_disp_rect(ctx->stream, &rect);
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

static int open_view(player_ctx_t *ctx)
{
    if (ctx->with_ui == false) {
        return 0;
    }
    // Overlay is full screen
    esp_video_render_disp_info_t disp = {};
    esp_video_render_get_display_info(ctx->render, &disp);
    esp_video_render_stream_info_t overlay_info = {
        .info = {
            .format = disp.format,
            .width = disp.width,
            .height = disp.height,
        },
    };
    esp_video_render_stream_handle_t overlay_stream = ctx->stream;
    if (ctx->full_speed_decode != 1) {
        if (esp_video_render_stream_open(ctx->render, &overlay_info, &ctx->overlay_stream) != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to open overlay stream");
            return -1;
        }
        esp_video_render_stream_set_zorder(ctx->overlay_stream, 1);
        overlay_stream = ctx->overlay_stream;
    }

    video_player_view_cfg_t vcfg = {
        .stream = overlay_stream,
        .width = disp.width,
        .height = disp.height,
        .file_name = get_file_name(ctx->playlist.path[ctx->cur]),
        .initial_duration_sec = ctx->duration_ms > 0 ? ctx->duration_ms / 1000 : 1,
        .hide_timeout_ms = 5000,
        .status_bar_alpha = 128,
        .fps_alpha = 200,
        .format = disp.format,
    };
    // If not fullscreen, use the stream size
    if (ctx->fullscreen == false) {
        vcfg.width = ctx->stream_info.info.width;
        vcfg.height = ctx->stream_info.info.height;
    }
    // Get font data
    extern const uint8_t dejavu_sans_ttf_start[] asm("_binary_DejaVuSans_ttf_start");
    extern const uint8_t dejavu_sans_ttf_end[] asm("_binary_DejaVuSans_ttf_end");
    vcfg.font_data = dejavu_sans_ttf_start;
    vcfg.font_data_size = (int)(dejavu_sans_ttf_end - dejavu_sans_ttf_start);
    if (video_player_view_init(&vcfg, &ctx->view) != 0) {
        ESP_LOGE(TAG, "Failed to initialize view");
        return -1;
    }
    video_player_view_set_playing(ctx->view, !ctx->paused);
    video_player_view_set_mute(ctx->view, ctx->muted);
    video_player_view_set_volume(ctx->view, ctx->volume);
    return 0;
}

static int on_decoded_frame(esp_video_render_frame_t *frame, void *arg)
{
    player_ctx_t *ctx = (player_ctx_t *)arg;
    if (ctx->recv_decoded_frames == 0) {
        // Delay to open stream when decoded ok
        esp_video_render_proc_get_out_frame_info(ctx->dec_proc, &ctx->stream_info.info);
        ctx->stream_info.info.fps = 0;  // No fps control
        ctx->stream_info.cached = false;
        if (open_video_stream(ctx) != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to open stream");
            return -1;
        }
        if (ctx->full_speed_decode == 1 && open_view(ctx) != 0) {
            ESP_LOGE(TAG, "Failed to open view for fullspeed decode");
            return -1;
        }
    }
    ctx->recv_decoded_frames++;
    // Only render when fullspeed not 2
    if (ctx->full_speed_decode != 2) {
        (void)esp_video_render_stream_write(ctx->stream, frame);
    }
    return 0;
}

static int open_media(player_ctx_t *ctx, int index)
{
    close_media(ctx);
    if (index < 0 || index >= ctx->playlist.count) {
        return -1;
    }
    ctx->cur = index;
    ctx->extract_cfg = alloc_file_cfg(ctx->playlist.path[index], PLAYER_EXTRACT_POOL_SIZE);
    if (ctx->extract_cfg == NULL) {
        ESP_LOGE(TAG, "Failed to allocate extract config");
        return -1;
    }
    do {
        if (esp_extractor_open(ctx->extract_cfg, &ctx->extractor) != ESP_EXTRACTOR_ERR_OK) {
            ESP_LOGE(TAG, "Failed to open extractor");
            break;
        }
        if (esp_extractor_parse_stream(ctx->extractor) != ESP_EXTRACTOR_ERR_OK) {
            ESP_LOGE(TAG, "Failed to parse stream");
            break;
        }

        uint16_t video_num = 0;
        if (esp_extractor_get_stream_num(ctx->extractor, ESP_EXTRACTOR_STREAM_TYPE_VIDEO, &video_num) != ESP_EXTRACTOR_ERR_OK || video_num == 0) {
            ESP_LOGE(TAG, "No video stream in %s", ctx->playlist.path[index]);
            break;
        }
        esp_extractor_stream_info_t st = {};
        esp_extractor_get_stream_info(ctx->extractor, ESP_EXTRACTOR_STREAM_TYPE_VIDEO, 0, &st);
        esp_video_render_format_t fmt = map_format(st.video_info.format);
        if (fmt == ESP_VIDEO_RENDER_FORMAT_NONE) {
            ESP_LOGE(TAG, "Unsupported video format");
            break;
        }
        // esp_video_render_clr_t bg_clr = { .r = 0, .g = 0, .b = 0 };
        // esp_video_render_set_bg_color(ctx->render, &bg_clr);

        esp_video_render_disp_info_t disp = {};
        esp_video_render_get_display_info(ctx->render, &disp);
        ctx->duration_ms = (int)st.duration;
        ctx->pos_ms = 0;
        ctx->stream_info.info.format = fmt;
        ctx->stream_info.info.width = st.video_info.width;
        ctx->stream_info.info.height = st.video_info.height;
        ctx->recv_decoded_frames = 0;
        if (ctx->full_speed_decode == 0) {
            ctx->stream_info.info.fps = 0;  // No fps control
            ctx->stream_info.cached = false;
            if (open_video_stream(ctx) != ESP_VIDEO_RENDER_ERR_OK) {
                ESP_LOGE(TAG, "Failed to open stream");
                break;
            }
            esp_video_render_stream_render_async(ctx->stream);
        } else {
            void *pool = NULL;
            if (esp_video_render_get_pool(ctx->render, &pool) != ESP_VIDEO_RENDER_ERR_OK) {
                ESP_LOGE(TAG, "Failed to get render pool");
                break;
            }
            esp_video_render_proc_cfg_t dec_cfg = {
                .pool = pool,
                .in_frame_info = {
                    .format = fmt,
                    .width = st.video_info.width,
                    .height = st.video_info.height,
                },
                .out_cfg = {
                    .on_frame = on_decoded_frame,
                    .out_ctx = ctx,
                },
            };
            dec_cfg.out_frame_info = dec_cfg.in_frame_info;
            dec_cfg.out_frame_info.format = ESP_VIDEO_RENDER_FORMAT_NONE;
            if (fmt == ESP_VIDEO_RENDER_FORMAT_MJPEG) {
                dec_cfg.out_frame_info.format = disp.format;
            }
            if (esp_video_render_proc_open(&dec_cfg, &ctx->dec_proc) != ESP_VIDEO_RENDER_ERR_OK) {
                ESP_LOGE(TAG, "Failed to open decoder proc");
                break;
            }
            // Output worker to receive docoded data and render to stream
            esp_video_render_proc_out_worker_cfg_t out_worker_cfg = {
                .task_cfg = {
                    .stack_size = PLAYER_WORKER_STACK_SIZE,
                    .priority = 5,
                    .core_id = 0,
                },
                .out_cache_size = PLAYER_DEC_OUT_CACHE_SIZE,
            };
            if (esp_video_render_proc_set_out_worker(ctx->dec_proc, &out_worker_cfg) != ESP_VIDEO_RENDER_ERR_OK) {
                ESP_LOGE(TAG, "Failed to set out worker");
                break;
            }
        }
        if (ctx->full_speed_decode != 1 && open_view(ctx) != 0) {
            ESP_LOGE(TAG, "Failed to open view");
            break;
        }
        ctx->worker_should_exit = false;
        if (ctx->worker_events) {
            xEventGroupClearBits(ctx->worker_events, PLAYER_EVT_WORKERS_EXITED);
        }
        if (!create_task(video_extract_task, "player_extract", PLAYER_WORKER_STACK_SIZE, ctx, 5, 1,
                         &ctx->extract_task) ||
            !create_task(video_decode_task, "player_decode", PLAYER_WORKER_STACK_SIZE, ctx, 5, 1,
                         &ctx->decode_task)) {
            ESP_LOGE(TAG, "Failed to create worker tasks");
            break;
        }
        return 0;
    } while (0);
    close_media(ctx);
    return -1;
}

static int enqueue_cmd(player_cmd_id_t id, int value)
{
    if (s_console_cmd_q == NULL) {
        return 1;
    }
    player_cmd_t cmd = {
        .id = id,
        .value = value,
    };
    return (xQueueSend(s_console_cmd_q, &cmd, 0) == pdTRUE) ? 0 : 1;
}

static int cmd_play(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return enqueue_cmd(PLAYER_CMD_PLAY, 0);
}
static int cmd_next(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return enqueue_cmd(PLAYER_CMD_NEXT, 0);
}
static int cmd_prev(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return enqueue_cmd(PLAYER_CMD_PREV, 0);
}
static int cmd_pause(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return enqueue_cmd(PLAYER_CMD_PAUSE, 0);
}
static int cmd_resume(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return enqueue_cmd(PLAYER_CMD_RESUME, 0);
}
static int cmd_stop(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return enqueue_cmd(PLAYER_CMD_STOP, 0);
}
static int cmd_exit(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return enqueue_cmd(PLAYER_CMD_EXIT, 0);
}

#define BUILD_CTRL(with_ui, full_screen, fullspeed)  (((with_ui) << 16) + ((full_screen) << 8) + (fullspeed))
#define GET_CTRL_WITH_UI(ctrl)                       (((ctrl) >> 16) & 0xFF)
#define GET_CTRL_FULL_SCREEN(ctrl)                   (((ctrl) >> 8) & 0xFF)
#define GET_CTRL_FULLSPEED(ctrl)                     (((ctrl) & 0xFF))

static int cmd_ctrl(int argc, char **argv)
{
    int v = -1;
    if (argc >= 4) {
        v = BUILD_CTRL(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));
    } else {
        printf("Usage: ctrl <with_ui> <full_screen> <fullspeed>\n");
    }
    return enqueue_cmd(PLAYER_CMD_CTRL, v);
}

static int cmd_seek(int argc, char **argv)
{
    if (argc < 2) {
        return 1;
    }
    return enqueue_cmd(PLAYER_CMD_SEEK, atoi(argv[1]));
}

static int cmd_vol(int argc, char **argv)
{
    if (argc < 2) {
        return 1;
    }
    return enqueue_cmd(PLAYER_CMD_SET_VOL, atoi(argv[1]));
}

static int cmd_mute(int argc, char **argv)
{
    if (argc < 2) {
        return 1;
    }
    return enqueue_cmd(PLAYER_CMD_MUTE, atoi(argv[1]));
}

static int cmd_sys(int argc, char **argv)
{
    esp_gmf_oal_sys_get_real_time_stats(1000, false);
    return 0;
}

static int cmd_measure(int argc, char **argv)
{
    esp_video_render_measure_enable(true);
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_video_render_measure_enable(false);
    return 0;
}

static int cmd_assert(int argc, char **argv)
{
    *(int *)0 = 0;
    return 0;
}

static esp_err_t player_console_init(QueueHandle_t cmd_q)
{
    if (s_console_inited) {
        s_console_cmd_q = cmd_q;
        return ESP_OK;
    }
    s_console_cmd_q = cmd_q;
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "player>";
#if CONFIG_ESP_CONSOLE_UART
    esp_console_dev_uart_config_t dev_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&dev_cfg, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t dev_cfg = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&dev_cfg, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t dev_cfg = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&dev_cfg, &repl_config, &repl));
#else
#error "No ESP console backend enabled"
#endif  /* CONFIG_ESP_CONSOLE_UART */

    static const esp_console_cmd_t cmds[] = {
        {.command = "play", .help = "Play current file", .hint = NULL, .func = cmd_play},
        {.command = "next", .help = "Play next file", .hint = NULL, .func = cmd_next},
        {.command = "prev", .help = "Play previous file", .hint = NULL, .func = cmd_prev},
        {.command = "pause", .help = "Pause playback", .hint = NULL, .func = cmd_pause},
        {.command = "resume", .help = "Resume playback", .hint = NULL, .func = cmd_resume},
        {.command = "stop", .help = "Stop current file", .hint = NULL, .func = cmd_stop},
        {.command = "seek", .help = "Seek by second: seek <sec>", .hint = NULL, .func = cmd_seek},
        {.command = "vol", .help = "Set volume: vol <0-100>", .hint = NULL, .func = cmd_vol},
        {.command = "mute", .help = "Set mute: mute <0|1>", .hint = NULL, .func = cmd_mute},
        {.command = "ctrl", .help = "Set control: ctrl <with_ui> <full_screen> <fullspeed>", .hint = NULL, .func = cmd_ctrl},
        {.command = "exit", .help = "Exit player demo", .hint = NULL, .func = cmd_exit},
        {.command = "assert", .help = "Assert system", .hint = NULL, .func = cmd_assert},
        {.command = "i", .help = "System query", .hint = NULL, .func = cmd_sys},
        {.command = "m", .help = "Measure for details", .hint = NULL, .func = cmd_measure},
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    s_console_inited = true;
    return ESP_OK;
}

static void video_extract_task(void *arg)
{
    player_ctx_t *ctx = (player_ctx_t *)arg;
    while (1) {
        if (ctx->worker_should_exit) {
            break;
        }
        if (!ctx->running || ctx->extractor == NULL) {
            break;
        }
        if (ctx->paused) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (xSemaphoreTake(ctx->media_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        esp_extractor_handle_t extractor = ctx->extractor;
        if (ctx->worker_should_exit || !ctx->running || extractor == NULL) {
            xSemaphoreGive(ctx->media_lock);
            break;
        }
        esp_extractor_frame_info_t frame = {};
        esp_extractor_err_t ret = esp_extractor_read_frame(extractor, &frame);
        if (ret == ESP_EXTRACTOR_ERR_EOS) {
            player_cmd_t cmd = {.id = PLAYER_CMD_NEXT};
            xSemaphoreGive(ctx->media_lock);
            xQueueSend(ctx->cmd_q, &cmd, 0);
            continue;
        }
        if (ret == ESP_EXTRACTOR_ERR_WAITING_OUTPUT || ret == ESP_EXTRACTOR_ERR_SKIPPED) {
            xSemaphoreGive(ctx->media_lock);
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        if (ret != ESP_EXTRACTOR_ERR_OK) {
            ESP_LOGW(TAG, "extract frame ret %d", ret);
            xSemaphoreGive(ctx->media_lock);
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        bool processed = false;

        if (frame.frame_buffer && frame.stream_type == ESP_EXTRACTOR_STREAM_TYPE_VIDEO) {
            frame_msg_t msg = {
                .extractor = extractor,
                .frame = frame,
            };
            processed = true;
            xSemaphoreGive(ctx->media_lock);
            if (xQueueSend(ctx->frame_q, &msg, pdMS_TO_TICKS(500)) == pdTRUE) {
                continue;
            }
            ESP_LOGE(TAG, "Failed to send frame to queue");
        }
        (void)esp_extractor_release_frame(extractor, &frame);
        if (!processed) {
            xSemaphoreGive(ctx->media_lock);
        }
    }
    if (ctx->worker_events) {
        xEventGroupSetBits(ctx->worker_events, PLAYER_EVT_EXTRACT_EXITED);
    }
    ctx->extract_task = NULL;
    vTaskDeleteWithCaps(NULL);
}

static void video_decode_task(void *arg)
{
    player_ctx_t *ctx = (player_ctx_t *)arg;
    int decode_frames = 0;
    uint32_t last_fps_update = esp_timer_get_time() / 1000;
    while (1) {
        if (ctx->worker_should_exit) {
            break;
        }
        if (!ctx->running || (ctx->stream == NULL && ctx->dec_proc == NULL)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        frame_msg_t msg = {};
        if (xQueueReceive(ctx->frame_q, &msg, pdMS_TO_TICKS(20)) != pdTRUE) {
            continue;
        }
        if (msg.extractor == ctx->extractor) {
            esp_video_render_frame_t vr_frame = {
                .format = ctx->stream_info.info.format,
                .width = ctx->stream_info.info.width,
                .height = ctx->stream_info.info.height,
                .data = msg.frame.frame_buffer,
                .size = msg.frame.frame_size,
            };
            if (ctx->full_speed_decode) {
                if (esp_video_render_proc_write(ctx->dec_proc, &vr_frame) != ESP_VIDEO_RENDER_ERR_OK) {
                    ESP_LOGE(TAG, "Failed to write frame to decoder proc");
                    (void)esp_extractor_release_frame(msg.extractor, &msg.frame);
                    break;
                }
            } else {
                (void)esp_video_render_stream_write(ctx->stream, &vr_frame);
            }
            decode_frames++;
            uint32_t current_time = esp_timer_get_time() / 1000;
            if (current_time - last_fps_update >= 2000) {
                ESP_LOGI(TAG, "Decode fps %d", decode_frames * 1000 / (current_time - last_fps_update));
                last_fps_update = current_time;
                decode_frames = 0;
            }
            ctx->decoded_frames++;
            ctx->pos_ms = (int)msg.frame.pts;
        }
        (void)esp_extractor_release_frame(msg.extractor, &msg.frame);
    }
    if (ctx->worker_events) {
        xEventGroupSetBits(ctx->worker_events, PLAYER_EVT_DECODE_EXITED);
    }
    ctx->decode_task = NULL;
    vTaskDeleteWithCaps(NULL);
}

static void flush_frame_queue_locked(player_ctx_t *ctx)
{
    frame_msg_t msg = {};
    while (xQueueReceive(ctx->frame_q, &msg, 0) == pdTRUE) {
        (void)esp_extractor_release_frame(msg.extractor, &msg.frame);
    }
}

static void handle_cmd(player_ctx_t *ctx, const player_cmd_t *cmd)
{
    switch (cmd->id) {
        case PLAYER_CMD_PLAY:
            open_media(ctx, ctx->cur);
            break;
        case PLAYER_CMD_NEXT:
            open_media(ctx, (ctx->cur + 1) % ctx->playlist.count);
            break;
        case PLAYER_CMD_PREV:
            open_media(ctx, (ctx->cur - 1 + ctx->playlist.count) % ctx->playlist.count);
            break;
        case PLAYER_CMD_PAUSE:
            ctx->paused = true;
            if (ctx->view) {
                video_player_view_set_playing(ctx->view, false);
            }
            break;
        case PLAYER_CMD_RESUME:
            ctx->paused = false;
            if (ctx->view) {
                video_player_view_set_playing(ctx->view, true);
            }
            break;
        case PLAYER_CMD_STOP:
            close_media(ctx);
            break;
        case PLAYER_CMD_SEEK:
            if (xSemaphoreTake(ctx->media_lock, portMAX_DELAY) == pdTRUE) {
                int ms = cmd->value < 0 ? 0 : cmd->value * 1000;
                if (ctx->extractor) {
                    (void)esp_extractor_seek(ctx->extractor, (uint32_t)ms);
                    ctx->pos_ms = ms;
                }
                flush_frame_queue_locked(ctx);
                xSemaphoreGive(ctx->media_lock);
            }
            break;
        case PLAYER_CMD_SET_VOL:
            ctx->volume = clamp_i(cmd->value, 0, 100);
            if (ctx->view) {
                video_player_view_set_volume(ctx->view, ctx->volume);
            }
            break;
        case PLAYER_CMD_MUTE:
            ctx->muted = (cmd->value != 0);
            if (ctx->view) {
                video_player_view_set_mute(ctx->view, ctx->muted);
            }
            break;
        case PLAYER_CMD_EXIT:
            ctx->running = false;
            break;
        case PLAYER_CMD_CTRL:
            if (cmd->value == -1) {
                ESP_LOGI(TAG, "Get Ctrl: with_ui=%d full_screen=%d fullspeed=%d",
                         ctx->with_ui, ctx->fullscreen, ctx->full_speed_decode);
            } else {
                if (ctx->extract_cfg) {
                    ESP_LOGW(TAG, "Not allow set fullspeed mode during playing");
                    break;
                }
                ctx->with_ui = GET_CTRL_WITH_UI(cmd->value);
                ctx->fullscreen = GET_CTRL_FULL_SCREEN(cmd->value);
                ctx->full_speed_decode = GET_CTRL_FULLSPEED(cmd->value);
                ESP_LOGI(TAG, "Set Ctrl: with_ui=%d full_screen=%d fullspeed=%d",
                         ctx->with_ui, ctx->fullscreen, ctx->full_speed_decode);
            }
            break;
        default:
            break;
    }
}

static void destroy_player_ctx(player_ctx_t *ctx)
{
    if (ctx->extract_cfg) {
        close_media(ctx);
    }
    if (ctx->worker_events) {
        vEventGroupDelete(ctx->worker_events);
    }
    if (ctx->media_lock) {
        vSemaphoreDelete(ctx->media_lock);
    }
    if (ctx->frame_q) {
        vQueueDelete(ctx->frame_q);
    }
    if (ctx->cmd_q) {
        vQueueDelete(ctx->cmd_q);
    }
    esp_extractor_unregister_default();
    video_render_sys_destroy();
}

static void demo_loop(void *arg)
{
    const char *url = PLAYER_SOURCE_URL;
    esp_extractor_register_default();
    player_ctx_t ctx = {};
    ctx.running = true;
    ctx.volume = 80;
    ctx.full_speed_decode = 1;
    ctx.fullscreen = true;
    ctx.with_ui = true;
    do {
        if (video_render_sys_create(PLAYER_DEFAULT_FPS) != 0) {
            ESP_LOGE(TAG, "Failed to create video render system");
            break;
        }
        ctx.render = video_render_sys_get();
        build_playlist(url, &ctx.playlist);
        if (ctx.playlist.count == 0) {
            ESP_LOGE(TAG, "No media found in %s", url);
            break;
        }
        ctx.cmd_q = xQueueCreate(8, sizeof(player_cmd_t));
        ctx.frame_q = xQueueCreate(PLAYER_FRAME_QUEUE_LEN, sizeof(frame_msg_t));
        ctx.media_lock = xSemaphoreCreateMutex();
        ctx.worker_events = xEventGroupCreate();
        if (ctx.cmd_q == NULL || ctx.frame_q == NULL || ctx.media_lock == NULL || ctx.worker_events == NULL) {
            ESP_LOGE(TAG, "Failed to allocate player queues/mutex");
            break;
        }
        if (player_console_init(ctx.cmd_q) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init player console");
            break;
        }
        ESP_LOGI(TAG, "Commands: next prev pause resume stop seek <sec> vol <0-100> mute <0/1>");

        if (open_media(&ctx, 0) != 0) {
            ESP_LOGE(TAG, "Failed to open media");
            break;
        }
        ctx.last_ui_ms = (uint32_t)(esp_timer_get_time() / 1000);
        while (ctx.running) {
            player_cmd_t cmd = {};
            while (xQueueReceive(ctx.cmd_q, &cmd, 0) == pdTRUE) {
                handle_cmd(&ctx, &cmd);
            }
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
            // Update UI every 800ms
            if (now - ctx.last_ui_ms >= 800 && ctx.view) {
                int fps = ctx.decoded_frames * 1000 / (now - ctx.last_ui_ms);
                video_player_view_set_position(ctx.view, ctx.pos_ms / 1000);
                video_player_view_set_fps(ctx.view, fps);
                ctx.last_ui_ms = now;
                ctx.decoded_frames = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    } while (0);
    destroy_player_ctx(&ctx);
    ESP_LOGI(TAG, "Demo loop exited");
    vTaskDeleteWithCaps(NULL);
}

int video_player_run_demo(void)
{
    return create_task(demo_loop, "demo_loop", 6 * 1024, NULL, 15, 0, NULL);
}
