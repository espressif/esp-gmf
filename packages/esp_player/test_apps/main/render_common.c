/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_alc.h"
#include "esp_gmf_sonic.h"
#include "esp_gmf_eq.h"
#include "esp_gmf_fade.h"
#include "esp_board_manager_includes.h"
#include "esp_video_render.h"
#include "esp_video_render_backend.h"
#include "esp_gmf_video_color_convert.h"
#include "esp_gmf_video_scale.h"
#include "esp_gmf_video_crop.h"
#include "esp_video_codec_types.h"
#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31
#include "esp_gmf_video_ppa.h"
#endif  /* CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31 */

#include "render_common.h"

static const char *TAG = "RENDER_COMMON";

#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT
#define VIDEO_UT_WARMUP_DRAW_WAIT_MS  500
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT */

static esp_audio_render_handle_t g_audio_render_handle = NULL;
static esp_gmf_pool_handle_t g_render_pool = NULL;
static esp_gmf_io_handle_t g_codec_dev_io_handle = NULL;
static bool g_audio_render_initialized = false;
static bool g_video_render_initialized = false;
static bool g_video_render_ut_keep_alive = false;
static void *g_video_render_handle = NULL;
static esp_gmf_pool_handle_t g_video_render_pool = NULL;
static uint8_t g_audio_max_stream_num = 1;

static void register_element_to_pool(esp_gmf_pool_handle_t pool, esp_gmf_element_handle_t el, const char *name)
{
    if (el == NULL) {
        ESP_LOGW(TAG, "Skip register %s: element is NULL", name);
        return;
    }
    if (esp_gmf_pool_register_element(pool, el, NULL) != ESP_GMF_ERR_OK) {
        ESP_LOGW(TAG, "Failed to register %s element", name);
        esp_gmf_obj_delete(el);
    }
}

static int audio_render_writer_cb(uint8_t *pcm_data, uint32_t pcm_size, void *ctx)
{
    if (!pcm_data || !ctx) {
        ESP_LOGE(TAG, "Invalid parameters: pcm_data=%p, ctx=%p",
                 pcm_data, ctx);
        return -1;
    }
    // printf("audio_render_writer_cb: %" PRIu32 "\n", pcm_size);
    if (pcm_size == 0) {
        return 0;
    }
    esp_gmf_io_handle_t codec_io_handle = (esp_gmf_io_handle_t)ctx;
    esp_gmf_payload_t payload = {
        .buf = pcm_data,
        .buf_length = pcm_size,
        .valid_size = pcm_size,
        .is_done = false,
        .pts = 0,
        .needs_free = 0,
        .meta_flag = 0};

    esp_gmf_err_io_t ret = esp_gmf_io_acquire_write(codec_io_handle, &payload, pcm_size, ESP_GMF_MAX_DELAY);
    if (ret != ESP_GMF_IO_OK) {
        ESP_LOGE(TAG, "Failed to acquire write: %d", ret);
        return -1;
    }

    ret = esp_gmf_io_release_write(codec_io_handle, &payload, ESP_GMF_MAX_DELAY);
    if (ret != ESP_GMF_IO_OK) {
        ESP_LOGE(TAG, "Failed to release write: %d", ret);
        return -1;
    }

    ESP_LOGD(TAG, "Audio data written: %" PRIu32 " bytes", pcm_size);
    return 0;
}

static esp_player_err_t create_default_pool(esp_gmf_pool_handle_t *pool)
{
    if (!pool) {
        ESP_LOGE(TAG, "Invalid pool pointer");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }

    *pool = NULL;
    if (esp_gmf_pool_init(pool) != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to initialize pool");
        return ESP_PLAYER_ERR_FAIL;
    }

    esp_gmf_element_handle_t el = NULL;
    esp_player_err_t ret = ESP_PLAYER_ERR_OK;

    esp_ae_ch_cvt_cfg_t ch_cvt_cfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    if (esp_gmf_ch_cvt_init(&ch_cvt_cfg, &el) == ESP_GMF_ERR_OK) {
        register_element_to_pool(*pool, el, "ch_cvt");
    } else {
        ESP_LOGW(TAG, "Failed to initialize ch_cvt");
    }

    esp_ae_bit_cvt_cfg_t bit_cvt_cfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
    if (esp_gmf_bit_cvt_init(&bit_cvt_cfg, &el) == ESP_GMF_ERR_OK) {
        register_element_to_pool(*pool, el, "bit_cvt");
    } else {
        ESP_LOGW(TAG, "Failed to initialize bit_cvt");
    }

    esp_ae_rate_cvt_cfg_t rate_cvt_cfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
    if (esp_gmf_rate_cvt_init(&rate_cvt_cfg, &el) == ESP_GMF_ERR_OK) {
        register_element_to_pool(*pool, el, "rate_cvt");
    } else {
        ESP_LOGW(TAG, "Failed to initialize rate_cvt");
    }

    esp_ae_alc_cfg_t alc_cfg = DEFAULT_ESP_GMF_ALC_CONFIG();
    if (esp_gmf_alc_init(&alc_cfg, &el) == ESP_GMF_ERR_OK) {
        register_element_to_pool(*pool, el, "alc");
    } else {
        ESP_LOGW(TAG, "Failed to initialize alc");
    }

    esp_ae_sonic_cfg_t sonic_cfg = DEFAULT_ESP_GMF_SONIC_CONFIG();
    if (esp_gmf_sonic_init(&sonic_cfg, &el) == ESP_GMF_ERR_OK) {
        register_element_to_pool(*pool, el, "sonic");
    } else {
        ESP_LOGW(TAG, "Failed to initialize sonic");
    }

    esp_ae_eq_cfg_t eq_cfg = DEFAULT_ESP_GMF_EQ_CONFIG();
    if (esp_gmf_eq_init(&eq_cfg, &el) == ESP_GMF_ERR_OK) {
        register_element_to_pool(*pool, el, "eq");
    } else {
        ESP_LOGW(TAG, "Failed to initialize eq");
    }

    esp_ae_fade_cfg_t fade_cfg = DEFAULT_ESP_GMF_FADE_CONFIG();
    if (esp_gmf_fade_init(&fade_cfg, &el) == ESP_GMF_ERR_OK) {
        register_element_to_pool(*pool, el, "fade");
    } else {
        ESP_LOGW(TAG, "Failed to initialize fade");
    }

    ESP_LOGI(TAG, "Audio processing pool created successfully");
    return ret;
}

static esp_player_err_t create_video_render_pool(esp_gmf_pool_handle_t *pool)
{
    *pool = NULL;
    if (esp_gmf_pool_init(pool) != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to initialize video render pool");
        return ESP_PLAYER_ERR_FAIL;
    }
    esp_gmf_element_handle_t el = NULL;
    esp_imgfx_color_convert_cfg_t color_convert_cfg = {
        .color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT601};
    if (esp_gmf_video_color_convert_init(&color_convert_cfg, &el) != ESP_GMF_ERR_OK) {
        ESP_LOGW(TAG, "Failed to initialize color_convert element");
    } else {
        register_element_to_pool(*pool, el, "color_convert");
    }
#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31
    if (esp_gmf_video_ppa_init(NULL, &el) != ESP_GMF_ERR_OK) {
        ESP_LOGW(TAG, "Failed to initialize ppa element");
    } else {
        register_element_to_pool(*pool, el, "ppa");
    }
#else
    esp_imgfx_scale_cfg_t scale_cfg = {
        .filter_type = ESP_IMGFX_SCALE_FILTER_TYPE_BILINEAR};
    if (esp_gmf_video_scale_init(&scale_cfg, &el) != ESP_GMF_ERR_OK) {
        ESP_LOGW(TAG, "Failed to initialize scale element");
    } else {
        register_element_to_pool(*pool, el, "scale");
    }
    esp_imgfx_crop_cfg_t crop_cfg = {};
    if (esp_gmf_video_crop_init(&crop_cfg, &el) != ESP_GMF_ERR_OK) {
        ESP_LOGW(TAG, "Failed to initialize crop element");
    } else {
        register_element_to_pool(*pool, el, "crop");
    }
#endif  /* CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31 */
    return ESP_PLAYER_ERR_OK;
}

#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT
static esp_player_err_t fill_lcd_backend_cfg(esp_video_render_lcd_cfg_t *lcd_backend_cfg,
                                             const dev_display_lcd_config_t *lcd_cfg,
                                             const dev_display_lcd_handles_t *lcd_handle)
{
    if (lcd_backend_cfg == NULL || lcd_cfg == NULL || lcd_handle == NULL || lcd_handle->panel_handle == NULL) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }

    lcd_backend_cfg->width = lcd_cfg->lcd_width;
    lcd_backend_cfg->height = lcd_cfg->lcd_height;
    lcd_backend_cfg->fb_num = 1;
    lcd_backend_cfg->lcd_handle = lcd_handle->panel_handle;
    lcd_backend_cfg->io_handle = lcd_handle->io_handle;

    if (strcmp(lcd_cfg->sub_type, "spi") == 0) {
        lcd_backend_cfg->lcd_type = ESP_VIDEO_RENDER_LCD_TYPE_DVP;
        lcd_backend_cfg->out_format = ESP_VIDEO_RENDER_FORMAT_RGB565_BE;
    } else if (strcmp(lcd_cfg->sub_type, "rgb") == 0) {
        lcd_backend_cfg->lcd_type = ESP_VIDEO_RENDER_LCD_TYPE_RGB;
        lcd_backend_cfg->out_format = ESP_VIDEO_RENDER_FORMAT_RGB565;
#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT
        lcd_backend_cfg->fb_num = lcd_cfg->sub_cfg.rgb.panel_config.num_fbs;
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT */
    } else if (strcmp(lcd_cfg->sub_type, "i80") == 0) {
        lcd_backend_cfg->lcd_type = ESP_VIDEO_RENDER_LCD_TYPE_I80;
        lcd_backend_cfg->out_format = ESP_VIDEO_RENDER_FORMAT_RGB565;
    } else if (strcmp(lcd_cfg->sub_type, "dsi") == 0) {
        lcd_backend_cfg->lcd_type = ESP_VIDEO_RENDER_LCD_TYPE_DPI;
        lcd_backend_cfg->out_format = ESP_VIDEO_RENDER_FORMAT_RGB565;
#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT
        lcd_backend_cfg->fb_num = lcd_cfg->sub_cfg.dsi.dpi_config.num_fbs;
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT */
    } else if (strcmp(lcd_cfg->sub_type, "parlio") == 0) {
        lcd_backend_cfg->lcd_type = ESP_VIDEO_RENDER_LCD_TYPE_I80;
        lcd_backend_cfg->out_format = ESP_VIDEO_RENDER_FORMAT_RGB565;
    } else {
        ESP_LOGE(TAG, "Unsupported LCD sub type: %s", lcd_cfg->sub_type);
        return ESP_PLAYER_ERR_FAIL;
    }

    return ESP_PLAYER_ERR_OK;
}

static esp_player_err_t video_render_test_warmup_draw(void)
{
    if (g_video_render_handle == NULL) {
        return ESP_PLAYER_ERR_FAIL;
    }

    esp_video_render_handle_t render = (esp_video_render_handle_t)g_video_render_handle;
    esp_video_render_stream_handle_t stream = NULL;
    esp_video_render_disp_info_t disp = {};

    if (esp_video_render_get_display_info(render, &disp) != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGW(TAG, "Warmup: get_display_info failed");
        return ESP_PLAYER_ERR_FAIL;
    }

    esp_video_render_stream_info_t stream_info = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB888,
            .width = disp.width,
            .height = disp.height,
            .fps = 30,
        },
        .cached = false,
    };
    if (esp_video_render_stream_open(render, &stream_info, &stream) != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGW(TAG, "Warmup: stream_open failed");
        return ESP_PLAYER_ERR_FAIL;
    }

    esp_video_render_rect_t disp_rect = {
        .x = 0,
        .y = 0,
        .width = disp.width,
        .height = disp.height,
    };
    (void)esp_video_render_stream_set_disp_rect(stream, &disp_rect);

    uint32_t frame_size = disp.width * disp.height * 3;
    uint8_t cache_align = esp_gmf_oal_get_spiram_cache_align();
    uint8_t *buf = esp_gmf_oal_malloc_align(cache_align, frame_size);
    if (buf == NULL) {
        esp_video_render_stream_close(stream);
        return ESP_PLAYER_ERR_NO_MEM;
    }
    memset(buf, 0, frame_size);

    esp_video_render_frame_t frame = {
        .format = ESP_VIDEO_RENDER_FORMAT_RGB888,
        .width = disp.width,
        .height = disp.height,
        .data = buf,
        .size = frame_size,
    };
    esp_video_render_err_t wr = esp_video_render_stream_write(stream, &frame);
    esp_gmf_oal_free(buf);
    if (wr != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGW(TAG, "Warmup: stream_write failed, ret=%d", (int)wr);
        esp_video_render_stream_close(stream);
        return ESP_PLAYER_ERR_FAIL;
    }

    vTaskDelay(pdMS_TO_TICKS(VIDEO_UT_WARMUP_DRAW_WAIT_MS));
    esp_video_render_stream_close(stream);
    ESP_LOGI(TAG, "Video render warmup draw done (%" PRIu32 "x%" PRIu32 " RGB888)", disp.width, disp.height);
    return ESP_PLAYER_ERR_OK;
}
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT */

#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT
void video_render_reconfig_lcd(void)
{
    dev_display_lcd_config_t lcd_cfg;
    dev_display_lcd_config_t *origin_cfg = NULL;

    esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&origin_cfg);
    if (origin_cfg == NULL) {
        ESP_LOGE(TAG, "Failed to get display config");
        return;
    }
    memcpy(&lcd_cfg, origin_cfg, sizeof(dev_display_lcd_config_t));
    if (strcmp(origin_cfg->sub_type, "rgb") == 0) {
#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT
        lcd_cfg.sub_cfg.rgb.panel_config.num_fbs = 2;
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT */
    } else if (strcmp(origin_cfg->sub_type, "dsi") == 0) {
#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT
        lcd_cfg.sub_cfg.dsi.dpi_config.num_fbs = 2;
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT */
    }
    esp_board_device_override_config(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, &lcd_cfg, sizeof(dev_display_lcd_config_t));
    ESP_LOGI(TAG, "LCD configuration overridden");
}
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT */

esp_player_err_t audio_render_create_handle(esp_audio_render_stream_handle_t *stream_handle,
                                            uint32_t sample_rate,
                                            uint8_t bits_per_sample,
                                            uint8_t channels,
                                            esp_audio_render_stream_id_t stream_id)
{
    if (!stream_handle) {
        ESP_LOGE(TAG, "Invalid stream handle pointer");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }

    if (g_audio_render_initialized) {
        ESP_LOGW(TAG, "Audio render already initialized, reusing existing instance for stream_id=%d", (int)stream_id);
        if (esp_audio_render_stream_get(g_audio_render_handle, stream_id, stream_handle) != ESP_AUDIO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to get audio render stream");
            return ESP_PLAYER_ERR_FAIL;
        }
        return ESP_PLAYER_ERR_OK;
    }

    esp_player_err_t ret = create_default_pool(&g_render_pool);
    if (ret != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to create audio processing pool");
        return ret;
    }

    esp_gmf_app_codec_info_t codec_info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
    codec_info.play_info.sample_rate = sample_rate;
    codec_info.play_info.channel = channels;
    esp_gmf_app_setup_codec_dev(&codec_info);

    if (g_codec_dev_io_handle == NULL) {
        codec_dev_io_cfg_t codec_io_cfg = ESP_GMF_IO_CODEC_DEV_CFG_DEFAULT();

        codec_io_cfg.dir = ESP_GMF_IO_DIR_WRITER;
        codec_io_cfg.dev = esp_gmf_app_get_playback_handle();
        if (esp_gmf_io_codec_dev_init(&codec_io_cfg, &g_codec_dev_io_handle) != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to initialize codec dev IO");
            esp_gmf_pool_deinit(g_render_pool);
            g_render_pool = NULL;
            return ESP_PLAYER_ERR_FAIL;
        }
    }

    if (esp_gmf_io_open(g_codec_dev_io_handle) != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open codec dev IO");
        esp_gmf_io_deinit(g_codec_dev_io_handle);
        esp_gmf_io_close(g_codec_dev_io_handle);
        esp_gmf_obj_delete(g_codec_dev_io_handle);
        g_codec_dev_io_handle = NULL;
        esp_gmf_pool_deinit(g_render_pool);
        g_render_pool = NULL;
        return ESP_PLAYER_ERR_FAIL;
    }

    esp_audio_render_cfg_t render_cfg = {
        .max_stream_num = g_audio_max_stream_num,
        .out_writer = audio_render_writer_cb,
        .out_ctx = g_codec_dev_io_handle,
        .out_sample_info = {
            .sample_rate = sample_rate,
            .bits_per_sample = bits_per_sample,
            .channel = channels},
        .pool = g_render_pool,
        .process_period = 20,  // 20ms
    };
    printf("render_cfg.max_stream_num: %d\n", render_cfg.max_stream_num);
    if (esp_audio_render_create(&render_cfg, &g_audio_render_handle) != ESP_AUDIO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to create audio render");
        esp_gmf_io_close(g_codec_dev_io_handle);
        esp_gmf_io_deinit(g_codec_dev_io_handle);
        esp_gmf_obj_delete(g_codec_dev_io_handle);
        g_codec_dev_io_handle = NULL;
        esp_gmf_pool_deinit(g_render_pool);
        g_render_pool = NULL;
        return ESP_PLAYER_ERR_FAIL;
    }

    if (esp_audio_render_stream_get(g_audio_render_handle, stream_id, stream_handle) != ESP_AUDIO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to get audio render stream");
        esp_audio_render_destroy(g_audio_render_handle);
        g_audio_render_handle = NULL;
        esp_gmf_io_close(g_codec_dev_io_handle);
        esp_gmf_io_deinit(g_codec_dev_io_handle);
        esp_gmf_obj_delete(g_codec_dev_io_handle);
        g_codec_dev_io_handle = NULL;
        esp_gmf_pool_deinit(g_render_pool);
        g_render_pool = NULL;
        return ESP_PLAYER_ERR_FAIL;
    }

    g_audio_render_initialized = true;
    ESP_LOGI(TAG, "Audio render handle created successfully: sample_rate=%" PRIu32 ", bits_per_sample=%u, channels=%u",
             sample_rate, bits_per_sample, channels);

    return ESP_PLAYER_ERR_OK;
}

void audio_render_set_max_stream_num(uint8_t max_stream_num)
{
    g_audio_max_stream_num = max_stream_num;
}

void audio_render_destroy_handle(void)
{
    if (g_audio_render_handle) {
        esp_audio_render_destroy(g_audio_render_handle);
        g_audio_render_handle = NULL;
        ESP_LOGI(TAG, "Audio render handle destroyed");
    }

    if (g_codec_dev_io_handle) {
        esp_gmf_app_teardown_codec_dev();
        esp_gmf_io_close(g_codec_dev_io_handle);
        esp_gmf_io_deinit(g_codec_dev_io_handle);
        esp_gmf_obj_delete(g_codec_dev_io_handle);
        g_codec_dev_io_handle = NULL;
        ESP_LOGI(TAG, "Codec dev IO handle destroyed");
    }

    if (g_render_pool) {
        esp_gmf_pool_deinit(g_render_pool);
        g_render_pool = NULL;
        ESP_LOGI(TAG, "Audio processing pool destroyed");
    }
    g_audio_render_initialized = false;
    ESP_LOGI(TAG, "Audio render cleanup completed");
}

esp_player_err_t video_render_create_handle(void **render_handle, uint32_t pixel_format, uint32_t fps)
{
    if (!render_handle) {
        ESP_LOGE(TAG, "Invalid stream handle pointer");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }

    if (g_video_render_initialized) {
        ESP_LOGW(TAG, "Video render already initialized");
        *render_handle = g_video_render_handle;
        return ESP_PLAYER_ERR_OK;
    }

#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT
    dev_display_lcd_handles_t *lcd_handle = NULL;
    dev_display_lcd_config_t *lcd_cfg = NULL;

    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&lcd_handle);
    if (lcd_handle == NULL) {
        ESP_LOGE(TAG, "LCD display device not available");
        return ESP_PLAYER_ERR_FAIL;
    }

    esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&lcd_cfg);
    if (lcd_cfg == NULL) {
        ESP_LOGE(TAG, "LCD display config not available");
        return ESP_PLAYER_ERR_FAIL;
    }

    if (create_video_render_pool(&g_video_render_pool) != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to create video render pool");
        return ESP_PLAYER_ERR_FAIL;
    }

    esp_video_render_cfg_t render_cfg = {
        .pool = g_video_render_pool,
        .fps = (uint8_t)fps,
    };
    esp_video_render_handle_t render = NULL;
    if (esp_video_render_create(&render_cfg, &render) != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to create video render");
        esp_gmf_pool_deinit(g_video_render_pool);
        g_video_render_pool = NULL;
        return ESP_PLAYER_ERR_FAIL;
    }

    esp_video_render_lcd_cfg_t lcd_backend_cfg = {};
    if (fill_lcd_backend_cfg(&lcd_backend_cfg, lcd_cfg, lcd_handle) != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to fill LCD backend config");
        esp_video_render_destroy(render);
        esp_gmf_pool_deinit(g_video_render_pool);
        g_video_render_pool = NULL;
        return ESP_PLAYER_ERR_FAIL;
    }

    esp_video_render_backend_cfg_t backend_cfg = {
        .ops = esp_video_render_get_lcd_backend(),
        .cfg = &lcd_backend_cfg,
        .cfg_size = sizeof(lcd_backend_cfg),
    };

    if (esp_video_render_set_display(render, &backend_cfg) != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to set video render display backend");
        esp_video_render_destroy(render);
        esp_gmf_pool_deinit(g_video_render_pool);
        g_video_render_pool = NULL;
        return ESP_PLAYER_ERR_FAIL;
    }

    g_video_render_handle = render;
    *render_handle = g_video_render_handle;
    g_video_render_initialized = true;

    ESP_LOGI(TAG, "Video render handle created: width=%" PRIu32 ", height=%" PRIu32 ", out_format=0x%" PRIx32
                  ", fps=%" PRIu32, (uint32_t)lcd_backend_cfg.width, (uint32_t)lcd_backend_cfg.height, (uint32_t)lcd_backend_cfg.out_format, fps);

    return ESP_PLAYER_ERR_OK;
#else
    (void)pixel_format;
    ESP_LOGW(TAG, "LCD display not supported on this board configuration");
    *render_handle = NULL;
    return ESP_PLAYER_ERR_FAIL;
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT */
}

esp_player_err_t video_render_test_app_setup(void)
{
    g_video_render_ut_keep_alive = true;
    void *handle = NULL;
    esp_player_err_t ret = video_render_create_handle(&handle, ESP_VIDEO_CODEC_PIXEL_FMT_RGB888, 30);
#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT
    if (ret == ESP_PLAYER_ERR_OK) {
        esp_player_err_t warm = video_render_test_warmup_draw();
        if (warm != ESP_PLAYER_ERR_OK) {
            ESP_LOGW(TAG, "Video render warmup draw failed, ret=%d", (int)warm);
        }
    }
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT */
    return ret;
}

void video_render_destroy_handle(void)
{
    if (g_video_render_ut_keep_alive) {
        ESP_LOGD(TAG, "Skip video render destroy (UT keep-alive)");
        return;
    }
    if (g_video_render_handle) {
        esp_video_render_destroy((esp_video_render_handle_t)g_video_render_handle);
        g_video_render_handle = NULL;
        ESP_LOGI(TAG, "Video render handle destroyed");
    }
    if (g_video_render_pool) {
        esp_gmf_pool_deinit(g_video_render_pool);
        g_video_render_pool = NULL;
    }
    g_video_render_initialized = false;
    ESP_LOGI(TAG, "Video render cleanup completed");
}
