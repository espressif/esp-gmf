/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "video_render_proc.h"
#include "esp_video_render.h"
#include <esp_gmf_element.h>
#include "esp_video_enc_default.h"
#include "esp_video_enc.h"
#include "esp_video_codec_utils.h"
#include "video_pattern.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "video_render_utils.h"
#include "esp_video_render_utils.h"
#include "video_render_test.h"
#include "esp_video_render_blender.h"
#include "esp_vui_overlay.h"
#include "esp_gmf_oal_mem.h"
#include "esp_vui_widget_default.h"
#include "xiaozhi_panel_widgets.h"
#include "settings.h"

#define TAG  "RENDER_TEST"

/**
 * @brief  Base resolution: 1024x600 (used as reference for ratios)
 */
#define BASE_WIDTH   1024
#define BASE_HEIGHT  600

/**
 * @brief  Calculate size based on ratio of base resolution, with minimum size constraint
 */
#define SCALE_WIDTH(w, min_w)   ((int)((w) * (display_width) / BASE_WIDTH) < (min_w) ? (min_w) : (int)((w) * (display_width) / BASE_WIDTH))
#define SCALE_HEIGHT(h, min_h)  ((int)((h) * (display_height) / BASE_HEIGHT) < (min_h) ? (min_h) : (int)((h) * (display_height) / BASE_HEIGHT))
#define SCALE_X(x)              ((int)((x) * (display_width) / BASE_WIDTH))
#define SCALE_Y(y)              ((int)((y) * (display_height) / BASE_HEIGHT))

/**
 * @brief  Ensure value fits within display bounds
 */
#define CLAMP_WIDTH(w)   ((w) > (display_width) ? (display_width) : (w))
#define CLAMP_HEIGHT(h)  ((h) > (display_height) ? (display_height) : (h))
#define CLAMP_X(x)       ((x) < 0 ? 0 : ((x) > (display_width) ? (display_width) : (x)))
#define CLAMP_Y(y)       ((y) < 0 ? 0 : ((y) > (display_height) ? (display_height) : (y)))

/**
 * @brief  Scale font size based on display resolution (base: 32 at 1024x600)
 * @note  Use average of width and height scaling for better proportion
 */
#define SCALE_FONT_SIZE(base_size, min_size)  \
    ((int)((base_size) * ((display_width + display_height) / 2.0f) / ((BASE_WIDTH + BASE_HEIGHT) / 2.0f)) < (min_size) ? (min_size) : (int)((base_size) * ((display_width + display_height) / 2.0f) / ((BASE_WIDTH + BASE_HEIGHT) / 2.0f)))

#define MAX_WIDGETS  (4)

static inline const esp_video_render_backend_ops_t *get_backend_ops(void **cfg, int *cfg_size)
{
    backend_cfg_t *backend_cfg = get_lcd_backend_cfg();
    if (backend_cfg->is_lvgl) {
        *cfg = &backend_cfg->lvgl_cfg;
        *cfg_size = sizeof(backend_cfg->lvgl_cfg);
    } else {
        *cfg = &backend_cfg->lcd_cfg;
        *cfg_size = sizeof(backend_cfg->lcd_cfg);
    }
    return backend_cfg->ops;
}

static inline esp_video_render_format_t get_backend_out_format(void)
{
    backend_cfg_t *backend_cfg = get_lcd_backend_cfg();
    return backend_cfg->is_lvgl ? backend_cfg->lvgl_cfg.out_format : backend_cfg->lcd_cfg.out_format;
}

static inline uint16_t get_backend_width(void)
{
    backend_cfg_t *backend_cfg = get_lcd_backend_cfg();
    return backend_cfg->is_lvgl ? backend_cfg->lvgl_cfg.width : backend_cfg->lcd_cfg.width;
}

static inline uint16_t get_backend_height(void)
{
    backend_cfg_t *backend_cfg = get_lcd_backend_cfg();
    return backend_cfg->is_lvgl ? backend_cfg->lvgl_cfg.height : backend_cfg->lcd_cfg.height;
}

static int show_image(esp_video_render_img_t *image)
{
    esp_video_render_backend_handle_t backend = NULL;
    void *backend_cfg = NULL;
    int backend_cfg_size = 0;
    const esp_video_render_backend_ops_t *ops = get_backend_ops(&backend_cfg, &backend_cfg_size);
    int ret = 0;
    do {
        ret = ops->init(backend_cfg, backend_cfg_size, &backend);
        BREAK_ON_FAIL(ret);

        esp_video_render_disp_info_t display_info = {};
        ret = ops->get_display_info(backend, &display_info);
        BREAK_ON_FAIL(ret);
        if (image->info.width > display_info.width || image->info.height > display_info.height || image->info.format != display_info.format) {
            ESP_LOGE(TAG, "Image size %dx%d or format wrongly", image->info.width, image->info.height);
            printf("image %s display %s\n",
                   esp_video_codec_get_pixel_fmt_str((esp_video_codec_pixel_fmt_t)image->info.format),
                   esp_video_codec_get_pixel_fmt_str((esp_video_codec_pixel_fmt_t)display_info.format));
            ret = ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
            break;
        }
        esp_video_render_fb_t fb;
        ret = ops->get_fb(backend, &fb);
        BREAK_ON_FAIL(ret);
        int src_line_size = (video_render_get_pixel_bits(fb.info.format) * image->info.width) >> 3;
        int dst_line_size = (video_render_get_pixel_bits(fb.info.format) * display_info.width) >> 3;
        uint8_t *src_data = image->data;
        uint8_t *dst_data = fb.data;
        for (int i = 0; i < image->info.height; i++) {
            memcpy(dst_data, src_data, src_line_size);
            src_data += src_line_size;
            dst_data += dst_line_size;
        }
        ret = ops->write_fb(backend, &fb, NULL, NULL);
        BREAK_ON_FAIL(ret);
    } while (0);
    if (backend) {
        ops->deinit(backend);
    }
    return ret;
}

int video_render_lcd_backend_fb_test(int count)
{
    bool success = false;
    int ret = 0;
    esp_video_render_backend_handle_t backend = NULL;
    void *backend_cfg = NULL;
    int backend_cfg_size = 0;
    const esp_video_render_backend_ops_t *ops = NULL;
    uint8_t *image_data = NULL;
    do {
        ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);

        ops = get_backend_ops(&backend_cfg, &backend_cfg_size);
        ret = ops->init(backend_cfg, backend_cfg_size, &backend);
        BREAK_ON_FAIL(ret)

        esp_video_render_disp_info_t display_info = {};
        ret = ops->get_display_info(backend, &display_info);
        BREAK_ON_FAIL(ret);

        esp_video_render_fb_t fb = {};
        ret = ops->get_fb(backend, &fb);
        BREAK_ON_FAIL(ret);
        esp_video_render_img_t image = {
            .info = {
                .format = display_info.format,
                .width = display_info.width,
                .height = display_info.height,
            },
        };
        ret = gen_image(&image, true, 16);
        BREAK_ON_FAIL(ret);
        image_data = image.data;
        uint32_t start_time = esp_timer_get_time() / 1000;
        for (int i = 0; i < count; i++) {
            ret = ops->lock_fb(backend, &fb, true);
            BREAK_ON_FAIL(ret);
            if (fb.size != image.size) {
                ESP_LOGE(TAG, "Framebuffer size not matched");
                BREAK_ON_FAIL(-1);
            } else {
                memcpy(fb.data, image_data, image.size);
            }
            ret = ops->write_fb(backend, &fb, NULL, NULL);
            BREAK_ON_FAIL(ret);
            ret = ops->lock_fb(backend, &fb, false);
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);
        uint32_t end_time = esp_timer_get_time() / 1000;
        float fps = count * 1000.0 / (end_time - start_time);
        ESP_LOGI(TAG, "Render user frame buffer fps %.2f", fps);
        success = true;
    } while (0);
    if (backend) {
        ops->deinit(backend);
    }
    destroy_video_render();
    if (image_data) {
        free(image_data);
    }
    return success ? 0 : -1;
}

int video_render_lcd_backend_none_fb_test(int count)
{
    bool success = false;
    int ret = 0;
    esp_video_render_backend_handle_t backend = NULL;
    void *backend_cfg = NULL;
    int backend_cfg_size = 0;
    const esp_video_render_backend_ops_t *ops = NULL;
    uint8_t *image_data = NULL;
    do {
        ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);

        ops = get_backend_ops(&backend_cfg, &backend_cfg_size);
        ret = ops->init(backend_cfg, backend_cfg_size, &backend);
        BREAK_ON_FAIL(ret)

        esp_video_render_disp_info_t display_info = {};
        ret = ops->get_display_info(backend, &display_info);
        BREAK_ON_FAIL(ret);

        esp_video_render_img_t image = {
            .info = {
                .format = display_info.format,
                .width = display_info.width,
                .height = display_info.height,
            },
        };
        ret = gen_image(&image, true, 16);
        BREAK_ON_FAIL(ret);
        image_data = image.data;

        // FB is user provided
        esp_video_render_fb_t fb = {
            .info = image.info,
            .data = image.data,
            .size = image.size,
        };
        uint32_t start_time = esp_timer_get_time() / 1000;
        for (int i = 0; i < count; i++) {
            ret = ops->write_fb(backend, &fb, NULL, NULL);
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);
        uint32_t end_time = esp_timer_get_time() / 1000;
        float fps = count * 1000.0 / (end_time - start_time);
        ESP_LOGI(TAG, "Render user frame buffer fps %.2f", fps);
        success = true;
    } while (0);
    if (backend) {
        ops->deinit(backend);
    }
    destroy_video_render();
    if (image_data) {
        free(image_data);
    }
    return success ? 0 : -1;
}

static int decoder_output_hdlr(esp_video_render_frame_t *frame, void *ctx)
{
    esp_video_render_frame_t *out_frame = (esp_video_render_frame_t *)ctx;
    printf("Output %s %dx%d\n", esp_video_codec_get_pixel_fmt_str((esp_video_codec_pixel_fmt_t)frame->format),
           frame->width, frame->height);
    *out_frame = *frame;
    return 0;
}

int video_render_proc_decode_test(int write_count)
{
    bool success = false;
    esp_video_render_img_t image = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_MJPEG,
            .width = 320,
            .height = 320,
        },
    };
    video_render_proc_handle_t render_proc = NULL;
    do {
        int ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);

        ret = video_render_proc_create((esp_gmf_pool_handle_t)get_render_pool(), &render_proc);
        BREAK_ON_FAIL(ret);

        ret = gen_image(&image, false, 16);
        BREAK_ON_FAIL(ret);
        printf("Encode image size %d\n", (int)image.size);

        esp_video_render_proc_type_t proc_type = ESP_VIDEO_RENDER_PROC_DEC;
        ret = video_render_proc_add(render_proc, &proc_type, 1);
        BREAK_ON_FAIL(ret);

        esp_gmf_element_handle_t decoder_element = video_render_proc_get_element(render_proc, proc_type);
        if (decoder_element == NULL) {
            BREAK_ON_FAIL(ESP_VIDEO_RENDER_ERR_NOT_FOUND);
        }

        esp_video_render_frame_t out_frame = {};
        ret = video_render_proc_set_writer(render_proc, decoder_output_hdlr, &out_frame);
        BREAK_ON_FAIL(ret);

        esp_video_render_frame_info_t in_frame_info = {
            .format = image.info.format,
            .width = image.info.width,
            .height = image.info.height,
            .fps = 1,
        };
        esp_video_render_frame_info_t out_frame_info = in_frame_info;
        out_frame_info.format = get_backend_out_format();
        ret = video_render_proc_open(render_proc, &in_frame_info, &out_frame_info);
        BREAK_ON_FAIL(ret);

        for (int i = 0; i < write_count; i++) {
            esp_video_render_frame_t frame = {
                .width = image.info.width,
                .height = image.info.height,
                .data = image.data,
                .size = image.size,
            };
            ret = video_render_proc_write(render_proc, &frame);
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);
        // verify decode output as expected
        esp_video_codec_resolution_t res = {
            .width = image.info.width,
            .height = image.info.height,
        };
        uint32_t size = esp_video_codec_get_image_size((esp_video_codec_pixel_fmt_t)out_frame_info.format, &res);
        if (size != out_frame.size) {
            ESP_LOGE(TAG, "Decoded output size %d != %d (expected)", (int)out_frame.size, (int)size);
            BREAK_ON_FAIL(ESP_VIDEO_RENDER_ERR_FAIL);
        }
        success = true;
        esp_video_render_img_t out_image = {
            .info = {
                .format = out_frame.format,
                .width = out_frame.width,
                .height = out_frame.height,
            },
            .data = out_frame.data,
            .size = out_frame.size,
        };
        show_image(&out_image);
    } while (0);
    if (render_proc) {
        video_render_proc_close(render_proc);
    }
    if (image.data) {
        free(image.data);
    }
    if (render_proc) {
        video_render_proc_destroy(render_proc);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int video_render_proc_color_convert_test(int write_count)
{
    bool success = false;
    esp_video_render_img_t image = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB888,
            .width = 320,
            .height = 320,
        },
    };
    video_render_proc_handle_t render_proc = NULL;
    do {
        int ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);

        ret = video_render_proc_create((esp_gmf_pool_handle_t)get_render_pool(), &render_proc);
        BREAK_ON_FAIL(ret);

        ret = gen_image(&image, false, 16);
        BREAK_ON_FAIL(ret);
        printf("Encode image size %d\n", (int)image.size);

        esp_video_render_frame_t out_frame = {};
        ret = video_render_proc_set_writer(render_proc, decoder_output_hdlr, &out_frame);
        BREAK_ON_FAIL(ret);

        esp_video_render_frame_info_t in_frame_info = {
            .format = image.info.format,
            .width = image.info.width,
            .height = image.info.height,
            .fps = 1,
        };
        esp_video_render_frame_info_t out_frame_info = in_frame_info;
        out_frame_info.format = get_backend_out_format();
        ret = video_render_proc_open(render_proc, &in_frame_info, &out_frame_info);
        BREAK_ON_FAIL(ret);

        for (int i = 0; i < write_count; i++) {
            esp_video_render_frame_t frame = {
                .width = image.info.width,
                .height = image.info.height,
                .data = image.data,
                .size = image.size,
            };
            ret = video_render_proc_write(render_proc, &frame);
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);
        // verify decode output as expected
        esp_video_codec_resolution_t res = {
            .width = out_frame_info.width,
            .height = out_frame_info.height,
        };
        uint32_t size = esp_video_codec_get_image_size((esp_video_codec_pixel_fmt_t)out_frame_info.format, &res);
        if (size != out_frame.size) {
            ESP_LOGE(TAG, "Decoded output size %d != %d (expected)", (int)out_frame.size, (int)size);
            BREAK_ON_FAIL(ESP_VIDEO_RENDER_ERR_FAIL);
        }
        success = true;
        esp_video_render_img_t out_image = {
            .info = {
                .format = out_frame.format,
                .width = out_frame.width,
                .height = out_frame.height,
            },
            .data = out_frame.data,
            .size = out_frame.size,
        };
        show_image(&out_image);
    } while (0);
    if (render_proc) {
        video_render_proc_close(render_proc);
    }
    if (image.data) {
        free(image.data);
    }
    if (render_proc) {
        video_render_proc_destroy(render_proc);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int video_render_proc_scale_test(int write_count)
{
    bool success = false;
    esp_video_render_img_t image = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB888,
            .width = 320,
            .height = 320,
        },
    };
    video_render_proc_handle_t render_proc = NULL;
    do {
        int ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);

        ret = video_render_proc_create((esp_gmf_pool_handle_t)get_render_pool(), &render_proc);
        BREAK_ON_FAIL(ret);

        ret = gen_image(&image, false, 16);
        BREAK_ON_FAIL(ret);
        printf("Encode image size %d\n", (int)image.size);

        esp_video_render_frame_t out_frame = {};
        ret = video_render_proc_set_writer(render_proc, decoder_output_hdlr, &out_frame);
        BREAK_ON_FAIL(ret);

        esp_video_render_frame_info_t in_frame_info = {
            .format = image.info.format,
            .width = image.info.width,
            .height = image.info.height,
            .fps = 1,
        };
        esp_video_render_frame_info_t out_frame_info = in_frame_info;
        out_frame_info.format = get_backend_out_format();
        out_frame_info.width = in_frame_info.width / 2;
        out_frame_info.height = in_frame_info.height / 2;

        ret = video_render_proc_open(render_proc, &in_frame_info, &out_frame_info);
        BREAK_ON_FAIL(ret);

        for (int i = 0; i < write_count; i++) {
            esp_video_render_frame_t frame = {
                .width = image.info.width,
                .height = image.info.height,
                .data = image.data,
                .size = image.size,
            };
            ret = video_render_proc_write(render_proc, &frame);
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);
        // verify decode output as expected
        esp_video_codec_resolution_t res = {
            .width = out_frame_info.width,
            .height = out_frame_info.height,
        };
        uint32_t size = esp_video_codec_get_image_size((esp_video_codec_pixel_fmt_t)out_frame_info.format, &res);
        if (size != out_frame.size) {
            ESP_LOGE(TAG, "Decoded output size %d != %d (expected)", (int)out_frame.size, (int)size);
            BREAK_ON_FAIL(ESP_VIDEO_RENDER_ERR_FAIL);
        }
        success = true;
        esp_video_render_img_t out_image = {
            .info = {
                .format = out_frame.format,
                .width = out_frame.width,
                .height = out_frame.height,
            },
            .data = out_frame.data,
            .size = out_frame.size,
        };
        show_image(&out_image);
    } while (0);
    if (render_proc) {
        video_render_proc_close(render_proc);
    }
    if (image.data) {
        free(image.data);
    }
    if (render_proc) {
        video_render_proc_destroy(render_proc);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int video_render_stream_rotate_test(int frame_count)
{
    bool success = false;
    esp_video_render_img_t img = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_MJPEG,
            .width = 240,
            .height = 160,
        },
    };
    esp_video_render_stream_handle_t stream = NULL;
    do {
        int ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);
        img.info.format = get_backend_out_format();

        ret = gen_image(&img, true, 8);
        BREAK_ON_FAIL(ret);
        esp_video_render_clr_t color = {
            .r = 0xff,
            .g = 0xff,
            .b = 0xff,
        };
        ret = esp_video_render_set_bg_color(get_video_render(), &color);
        BREAK_ON_FAIL(ret);

        esp_video_render_stream_info_t stream_info = {
            .info = img.info,
        };
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream);
        BREAK_ON_FAIL(ret);

        int rotate_degree = 0;

        for (int i = 0; i < frame_count; i++) {
            ret = esp_video_render_stream_set_rotate(stream, rotate_degree);
            BREAK_ON_FAIL(ret);
            bool swap = (rotate_degree == 90 || rotate_degree == 270);
            esp_video_render_rect_t disp_rect = {
                .x = 0,
                .y = 0,
                .width = swap ? img.info.height : img.info.width,
                .height = swap ? img.info.width : img.info.height,
            };
            ret = esp_video_render_stream_set_disp_rect(stream, &disp_rect);
            BREAK_ON_FAIL(ret);
            esp_video_render_frame_t frame = {
                .format = img.info.format,
                .width = img.info.width,
                .height = img.info.height,
                .data = img.data,
                .size = img.size,
            };
            ret = esp_video_render_stream_write(stream, &frame);
            BREAK_ON_FAIL(ret);
            rotate_degree = (rotate_degree + 90) % 360;
            video_render_delay(500);
        }
        BREAK_ON_FAIL(ret);
        success = true;
    } while (0);
    if (stream) {
        esp_video_render_stream_close(stream);
    }
    if (img.data) {
        free(img.data);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int video_render_proc_chain_test(int write_count)
{
    bool success = false;
    esp_video_render_img_t image = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_MJPEG,
            .width = 320,
            .height = 320,
        },
    };
    video_render_proc_handle_t render_proc = NULL;
    do {
        int ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);

        ret = video_render_proc_create((esp_gmf_pool_handle_t)get_render_pool(), &render_proc);
        BREAK_ON_FAIL(ret);

        ret = gen_image(&image, false, 16);
        BREAK_ON_FAIL(ret);
        printf("Encode image size %d\n", (int)image.size);

        esp_video_render_frame_t out_frame = {};
        ret = video_render_proc_set_writer(render_proc, decoder_output_hdlr, &out_frame);
        BREAK_ON_FAIL(ret);

        esp_video_render_frame_info_t in_frame_info = {
            .format = image.info.format,
            .width = image.info.width,
            .height = image.info.height,
        };
        esp_video_render_frame_info_t out_frame_info = in_frame_info;
        out_frame_info.format = get_backend_out_format();
        out_frame_info.width = in_frame_info.width / 2;
        out_frame_info.height = in_frame_info.height / 2;

        ret = video_render_proc_open(render_proc, &in_frame_info, &out_frame_info);
        BREAK_ON_FAIL(ret);

        for (int i = 0; i < write_count; i++) {
            esp_video_render_frame_t frame = {
                .width = image.info.width,
                .height = image.info.height,
                .data = image.data,
                .size = image.size,
            };
            ret = video_render_proc_write(render_proc, &frame);
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);
        // verify decode output as expected
        esp_video_codec_resolution_t res = {
            .width = out_frame_info.width,
            .height = out_frame_info.height,
        };
        uint32_t size = esp_video_codec_get_image_size((esp_video_codec_pixel_fmt_t)out_frame_info.format, &res);
        if (size != out_frame.size) {
            ESP_LOGE(TAG, "Decoded output size %d != %d (expected)", (int)out_frame.size, (int)size);
            BREAK_ON_FAIL(ESP_VIDEO_RENDER_ERR_FAIL);
        }
        success = true;
        esp_video_render_img_t out_image = {
            .info = {
                .format = out_frame.format,
                .width = out_frame.width,
                .height = out_frame.height,
            },
            .data = out_frame.data,
            .size = out_frame.size,
        };
        show_image(&out_image);
    } while (0);
    if (render_proc) {
        video_render_proc_close(render_proc);
    }
    if (image.data) {
        free(image.data);
    }
    if (render_proc) {
        video_render_proc_destroy(render_proc);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int video_render_image_decode_test(int write_count)
{
    bool success = false;
    esp_video_render_img_t image = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_MJPEG,
            .width = 320,
            .height = 320,
        },
    };
    esp_video_render_frame_t out_frame = {};
    int ret;
    do {
        ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);

        ret = gen_image(&image, false, 16);
        BREAK_ON_FAIL(ret);

        ret = esp_video_render_decode_image(get_render_pool(), &image, ESP_VIDEO_RENDER_FORMAT_RGB565, &out_frame);
        BREAK_ON_FAIL(ret);
        if (out_frame.format != ESP_VIDEO_RENDER_FORMAT_RGB565 ||
            out_frame.width != image.info.width ||
            out_frame.height != image.info.height ||
            out_frame.data == NULL) {
            ESP_LOGE(TAG, "Deode image results not as expected output format %x %dx%d data %p",
                     out_frame.format, out_frame.width, out_frame.height, out_frame.data);

            ret = -1;
            BREAK_ON_FAIL(ret);
        }
        success = true;
    } while (0);
    if (out_frame.data) {
        free(out_frame.data);
    }
    if (image.data) {
        free(image.data);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int video_render_blend_test(int write_count)
{
    bool success = false;
    esp_video_render_img_t bk_image = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = 320,
            .height = 240,
        },
    };
    esp_video_render_img_t fg_image = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = bk_image.info.width / 2,
            .height = bk_image.info.height / 2,
        },
    };
    esp_video_render_blend_handle_t blender = NULL;
    int ret;
    do {
        ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);

        ret = esp_video_render_blend_open(NULL, &blender);
        BREAK_ON_FAIL(ret);

        ret = gen_image(&bk_image, false, 16);
        BREAK_ON_FAIL(ret);
        ret = gen_image(&fg_image, false, 16);
        BREAK_ON_FAIL(ret);

        esp_video_render_fb_t bk_fb = {
            .info = bk_image.info,
            .data = bk_image.data,
            .size = bk_image.size,
        };
        esp_video_render_fb_t fg_fb = {
            .info = fg_image.info,
            .data = fg_image.data,
            .size = fg_image.size,
        };
        esp_video_render_rect_t dst_rect = {
            .x = (bk_fb.info.width - fg_fb.info.width) / 2,
            .y = (bk_fb.info.height - fg_fb.info.height) / 2,
            .width = fg_fb.info.width,
            .height = fg_fb.info.height,
        };
        esp_video_render_rect_t src_rect = {
            .x = 0,
            .y = 0,
            .width = fg_fb.info.width,
            .height = fg_fb.info.height,
        };

        ret = esp_video_render_blend_process(blender, &bk_fb, &fg_fb, &dst_rect, &src_rect, 255);
        BREAK_ON_FAIL(ret);
        // Top filled with white
        esp_video_render_rect_t top_rect = {
            .x = 0,
            .y = 0,
            .width = bk_fb.info.width,
            .height = (bk_fb.info.height - fg_fb.info.height) / 2,
        };
        esp_video_render_clr_t top_color = {
            .r = 0xff,
            .g = 0xff,
            .b = 0xff,
        };
        ret = esp_video_render_blend_fill(blender, &bk_fb, &top_rect, &top_color);
        BREAK_ON_FAIL(ret);

        esp_video_render_rect_t bottom_rect = {
            .x = 0,
            .y = (bk_fb.info.height - fg_fb.info.height) / 2 + fg_fb.info.height,
            .width = bk_fb.info.width,
            .height = (bk_fb.info.height - fg_fb.info.height) / 2,
        };
        esp_video_render_clr_t bottom_color = {.r = 0xFF};
        ret = esp_video_render_blend_fill(blender, &bk_fb, &bottom_rect, &bottom_color);
        BREAK_ON_FAIL(ret);

        // Bottom filled with red
        success = true;
        show_image(&bk_image);
    } while (0);
    if (bk_image.data) {
        video_render_free(bk_image.data);
    }
    if (fg_image.data) {
        video_render_free(fg_image.data);
    }
    if (blender) {
        esp_video_render_blend_close(blender);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

static inline uint16_t rgb565_pack(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static inline uint16_t fb_get_rgb565(esp_video_render_fb_t *fb, int x, int y)
{
    uint16_t *row = (uint16_t *)(fb->data + (size_t)y * fb->info.width * 2);
    return row[x];
}

static inline uint8_t blend_ch_ppa(uint8_t src, uint8_t dst, uint8_t alpha)
{
    /* PPA blending path uses 255-based alpha math and hardware rounding. */
    return (uint8_t)(((src * alpha) + (dst * (255 - alpha)) + 127) / 255);
}

int video_render_blend_bitblt_test(int write_count)
{
    (void)write_count;
    bool success = false;
    esp_video_render_blend_handle_t blender = NULL;
    esp_video_render_fb_t src_fb = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = 240,
            .height = 160,
        },
    };
    esp_video_render_fb_t dst_fb = src_fb;
    int ret;
    do {
        src_fb.size = src_fb.info.width * src_fb.info.height * 2;
        dst_fb.size = dst_fb.info.width * dst_fb.info.height * 2;
        src_fb.data = esp_gmf_oal_malloc_align(video_render_get_default_alignment(), src_fb.size);
        dst_fb.data = esp_gmf_oal_malloc_align(video_render_get_default_alignment(), dst_fb.size);
        BREAK_ON_FAIL(src_fb.data ? 0 : -1);
        BREAK_ON_FAIL(dst_fb.data ? 0 : -1);

        for (int y = 0; y < src_fb.info.height; y++) {
            uint16_t *src_row = (uint16_t *)(src_fb.data + (size_t)y * src_fb.info.width * 2);
            uint16_t *dst_row = (uint16_t *)(dst_fb.data + (size_t)y * dst_fb.info.width * 2);
            for (int x = 0; x < src_fb.info.width; x++) {
                src_row[x] = rgb565_pack((x * 3) & 0xFF, (y * 4) & 0xFF, (x + y) & 0xFF);
                dst_row[x] = rgb565_pack(8, 16, 32);
            }
        }

        esp_video_render_rect_t src_rect = {
            .x = 20,
            .y = 12,
            .width = 120,
            .height = 100,
        };
        esp_video_render_rect_t dst_rect = {
            .x = 60,
            .y = 30,
            .width = 120,
            .height = 100,
        };

        ret = esp_video_render_blend_open(NULL, &blender);
        BREAK_ON_FAIL(ret);

        ret = esp_video_render_blend_bitblt(blender, &dst_fb, &src_fb, &dst_rect, &src_rect);
        BREAK_ON_FAIL(ret);

        uint16_t src_sample = fb_get_rgb565(&src_fb, src_rect.x + 13, src_rect.y + 17);
        uint16_t dst_sample = fb_get_rgb565(&dst_fb, dst_rect.x + 13, dst_rect.y + 17);
        BREAK_ON_FAIL(src_sample == dst_sample ? 0 : -1);
        BREAK_ON_FAIL(fb_get_rgb565(&dst_fb, 2, 2) == rgb565_pack(8, 16, 32) ? 0 : -1);

        esp_video_render_rect_t blend_src = {
            .x = 30,
            .y = 20,
            .width = 130,
            .height = 110,
        };
        esp_video_render_rect_t blend_dst = {
            .x = 40,
            .y = 18,
            .width = 130,
            .height = 110,
        };

        uint8_t alpha = 128;
        int check_sx = blend_src.x + 25;
        int check_sy = blend_src.y + 18;
        int check_dx = blend_dst.x + 25;
        int check_dy = blend_dst.y + 18;
        uint16_t src_pixel = fb_get_rgb565(&src_fb, check_sx, check_sy);
        uint16_t dst_pixel_before = fb_get_rgb565(&dst_fb, check_dx, check_dy);

        ret = esp_video_render_blend_process(blender, &dst_fb, &src_fb, &blend_dst, &blend_src, alpha);
        BREAK_ON_FAIL(ret);

        uint16_t dst_pixel_after = fb_get_rgb565(&dst_fb, check_dx, check_dy);
        uint8_t src_r = (src_pixel >> 11) & 0x1F;
        uint8_t src_g = (src_pixel >> 5) & 0x3F;
        uint8_t src_b = src_pixel & 0x1F;
        uint8_t dst_r = (dst_pixel_before >> 11) & 0x1F;
        uint8_t dst_g = (dst_pixel_before >> 5) & 0x3F;
        uint8_t dst_b = dst_pixel_before & 0x1F;
        uint8_t exp_r = blend_ch_ppa(src_r, dst_r, alpha);
        uint8_t exp_g = blend_ch_ppa(src_g, dst_g, alpha);
        uint8_t exp_b = blend_ch_ppa(src_b, dst_b, alpha);
        uint16_t expected_blend = (uint16_t)((exp_r << 11) | (exp_g << 5) | exp_b);
        int dr = (int)((dst_pixel_after >> 11) & 0x1F) - exp_r;
        int dg = (int)((dst_pixel_after >> 5) & 0x3F) - exp_g;
        int db = (int)(dst_pixel_after & 0x1F) - exp_b;
        if (dr < 0) {
            dr = -dr;
        }
        if (dg < 0) {
            dg = -dg;
        }
        if (db < 0) {
            db = -db;
        }
        printf("Expected blend: %x, Actual blend: %x\n", expected_blend, dst_pixel_after);
        /**
         * HW PPA blend and SW blend can differ a few LSBs due to different
         * normalization/rounding paths (255 vs 256 domain and channel quantization).
         */
        BREAK_ON_FAIL((dr <= 3 && dg <= 4 && db <= 3) ? 0 : -1);

        esp_video_render_rect_t fill_rect = {
            .x = 80,
            .y = 40,
            .width = 140,
            .height = 110,
        };
        esp_video_render_clr_t fill_color = {
            .r = 0x2a,
            .g = 0xd0,
            .b = 0x45,
        };
        uint16_t expected_fill = rgb565_pack(fill_color.r, fill_color.g, fill_color.b);
        ret = esp_video_render_blend_fill(blender, &dst_fb, &fill_rect, &fill_color);
        BREAK_ON_FAIL(ret);
        BREAK_ON_FAIL(fb_get_rgb565(&dst_fb, fill_rect.x + 10, fill_rect.y + 15) == expected_fill ? 0 : -1);
        BREAK_ON_FAIL(fb_get_rgb565(&dst_fb, fill_rect.x + fill_rect.width - 1, fill_rect.y + fill_rect.height - 1) == expected_fill ? 0 : -1);
        success = true;
    } while (0);

    if (blender) {
        esp_video_render_blend_close(blender);
    }
    if (src_fb.data) {
        esp_gmf_oal_free(src_fb.data);
    }
    if (dst_fb.data) {
        esp_gmf_oal_free(dst_fb.data);
    }
    return success ? 0 : -1;
}

int video_render_blend_transparent_color_test(int write_count)
{
    (void)write_count;
    bool success = false;
    esp_video_render_blend_handle_t blender = NULL;
    esp_video_render_fb_t src_fb = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = 200,
            .height = 140,
        },
    };
    esp_video_render_fb_t dst_fb = src_fb;
    int ret;
    do {
        src_fb.size = src_fb.info.width * src_fb.info.height * 2;
        dst_fb.size = dst_fb.info.width * dst_fb.info.height * 2;
        src_fb.data = esp_gmf_oal_malloc_align(video_render_get_default_alignment(), src_fb.size);
        dst_fb.data = esp_gmf_oal_malloc_align(video_render_get_default_alignment(), dst_fb.size);
        BREAK_ON_FAIL(src_fb.data ? 0 : -1);
        BREAK_ON_FAIL(dst_fb.data ? 0 : -1);

        /* Keep destination with a known background color */
        uint16_t bg_color = rgb565_pack(0x12, 0x34, 0x56);
        uint16_t trans_px = rgb565_pack(40, 160, 48);
        uint16_t near_trans_px = rgb565_pack(44, 164, 52);  /* around trans color */
        uint16_t visible_px = rgb565_pack(250, 20, 30);

        for (int y = 0; y < src_fb.info.height; y++) {
            uint16_t *src_row = (uint16_t *)(src_fb.data + (size_t)y * src_fb.info.width * 2);
            uint16_t *dst_row = (uint16_t *)(dst_fb.data + (size_t)y * dst_fb.info.width * 2);
            for (int x = 0; x < src_fb.info.width; x++) {
                src_row[x] = trans_px;
                dst_row[x] = bg_color;
            }
        }

        int ox = 18;
        int oy = 14;
        esp_video_render_rect_t src_rect = {
            .x = 10,
            .y = 8,
            .width = 150,
            .height = 100,
        };
        esp_video_render_rect_t dst_rect = {
            .x = ox,
            .y = oy,
            .width = src_rect.width,
            .height = src_rect.height,
        };

        /* One pixel near transparent range should still keep destination */
        ((uint16_t *)(src_fb.data + (size_t)(src_rect.y + 25) * src_fb.info.width * 2))[src_rect.x + 20] = near_trans_px;
        /* One clearly visible pixel should overwrite destination */
        ((uint16_t *)(src_fb.data + (size_t)(src_rect.y + 30) * src_fb.info.width * 2))[src_rect.x + 40] = visible_px;

        esp_video_render_clr_t trans_color = {
            .r = 40,
            .g = 160,
            .b = 48,
        };

        ret = esp_video_render_blend_open(NULL, &blender);
        BREAK_ON_FAIL(ret);

        ret = esp_video_render_blend_transparent_color(blender, &dst_fb, &src_fb, &dst_rect, &src_rect, &trans_color);
        BREAK_ON_FAIL(ret);

        /* Transparent pixel: destination should remain unchanged */
        BREAK_ON_FAIL(fb_get_rgb565(&dst_fb, dst_rect.x + 2, dst_rect.y + 2) == bg_color ? 0 : -1);
        /* Near-transparent pixel: destination should remain unchanged */
        BREAK_ON_FAIL(fb_get_rgb565(&dst_fb, dst_rect.x + 20, dst_rect.y + 25) == bg_color ? 0 : -1);
        /* Visible pixel: destination should become source pixel */
        BREAK_ON_FAIL(fb_get_rgb565(&dst_fb, dst_rect.x + 40, dst_rect.y + 30) == visible_px ? 0 : -1);
        success = true;
    } while (0);

    if (blender) {
        esp_video_render_blend_close(blender);
    }
    if (src_fb.data) {
        esp_gmf_oal_free(src_fb.data);
    }
    if (dst_fb.data) {
        esp_gmf_oal_free(dst_fb.data);
    }
    return success ? 0 : -1;
}

static int copy_into_fb(esp_video_render_fb_t *fb, esp_video_render_img_t *image)
{
    if (fb->info.format != image->info.format || fb->info.width < image->info.width || fb->info.height < image->info.height) {
        return -1;
    }
    int src_line_size = (video_render_get_pixel_bits(fb->info.format) * image->info.width) >> 3;
    int dst_line_size = (video_render_get_pixel_bits(fb->info.format) * fb->info.width) >> 3;
    uint8_t *src_data = image->data;
    uint8_t *dst_data = fb->data;
    for (int i = 0; i < image->info.height; i++) {
        memcpy(dst_data, src_data, src_line_size);
        src_data += src_line_size;
        dst_data += dst_line_size;
    }
    return 0;
}

int video_render_one_stream_with_fb(int write_count)
{
    bool success = false;
    esp_video_render_img_t image = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = 320,
            .height = 240,
        },
    };
    esp_video_render_stream_handle_t stream = NULL;
    do {
        int ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);

        esp_video_render_stream_info_t stream_info = {
            .info = image.info,
        };
        image.info.format = get_backend_out_format();
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream);
        BREAK_ON_FAIL(ret);

        ret = gen_image(&image, false, 16);
        BREAK_ON_FAIL(ret);

        esp_video_render_fb_t fb = {};
        ret = esp_video_render_stream_acquire_fb(stream, &fb);
        BREAK_ON_FAIL(ret);

        ret = copy_into_fb(&fb, &image);
        BREAK_ON_FAIL(ret);

        ret = esp_video_render_stream_write_fb(stream, &fb);
        BREAK_ON_FAIL(ret);

        ret = esp_video_render_stream_release_fb(stream, &fb);
        BREAK_ON_FAIL(ret);

        success = true;
    } while (0);

    if (stream) {
        esp_video_render_stream_close(stream);
    }
    if (image.data) {
        free(image.data);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int demo_one_stream_video_only(int frame_count)
{
    bool success = false;
    esp_video_render_img_t img_a = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_MJPEG,
            .width = 320,
            .height = 240,
        },
    };
    esp_video_render_img_t img_b;
    esp_video_render_stream_handle_t stream = NULL;
    do {
        int ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);
        img_a.info.format = get_backend_out_format();
        img_b = img_a;

        // Generate two raw frames (simulating two JPEG-decoded frames)
        ret = gen_image(&img_a, true, 8);
        BREAK_ON_FAIL(ret);
        ret = gen_image(&img_b, true, 4);
        BREAK_ON_FAIL(ret);
        esp_video_render_stream_info_t stream_info = {
            .info = img_a.info,
        };
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream);
        BREAK_ON_FAIL(ret);

        esp_video_render_frame_t frame = {
            .format = img_a.info.format,
            .width = img_a.info.width,
            .height = img_a.info.height,
        };
        // Write alternating frames
        for (int i = 0; i < frame_count; ++i) {
            frame.data = (i % 2 == 0) ? img_a.data : img_b.data;
            frame.size = (i % 2 == 0) ? img_a.size : img_b.size;
            ret = esp_video_render_stream_write(stream, &frame);
            BREAK_ON_FAIL(ret);
        }
        success = true;
    } while (0);
    if (stream) {
        esp_video_render_stream_close(stream);
    }
    if (img_a.data) {
        free(img_a.data);
    }
    if (img_b.data) {
        free(img_b.data);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int demo_one_stream_video_with_overlay(int frame_count)
{
    bool success = false;
    esp_video_render_img_t img_bg = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_MJPEG,
            .width = 320,
            .height = 240,
        },
    };
    esp_video_render_img_t img_overlay = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = 80,
            .height = 60,
        },
    };
    esp_video_render_stream_handle_t stream = NULL;
    esp_vui_overlay_handle_t overlay = NULL;
    esp_vui_overlay_rgn_t rgn = {0};
    esp_vui_overlay_rgn_t rgn_b;
    do {
        int ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);
        img_overlay.info.format = get_backend_out_format();
        esp_video_render_stream_info_t stream_info = {
            .info = img_bg.info,
        };
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream);
        BREAK_ON_FAIL(ret);
        ret = gen_image(&img_bg, false, 8);
        BREAK_ON_FAIL(ret);
        ret = gen_image(&img_overlay, true, 8);
        BREAK_ON_FAIL(ret);

        // Write base video frame once
        esp_video_render_frame_t bg_frame = {
            .format = img_bg.info.format,
            .width = img_bg.info.width,
            .height = img_bg.info.height,
            .data = img_bg.data,
            .size = img_bg.size,
        };
        ret = esp_video_render_stream_write(stream, &bg_frame);
        BREAK_ON_FAIL(ret);

        // Create overlay and add one region
        ret = esp_video_render_stream_get_overlay(stream, &overlay);
        BREAK_ON_FAIL(ret);
        printf("Get overlay OK\n");

        rgn.compose.visible = true;
        rgn.compose.alpha = 255;
        rgn.compose.is_fresh = true;
        rgn.compose.disp_rect.x = 40;
        rgn.compose.disp_rect.y = 40;
        rgn.frame.format = img_overlay.info.format;
        rgn.frame.width = img_overlay.info.width;
        rgn.frame.height = img_overlay.info.height;
        rgn.frame.data = img_overlay.data;
        rgn.frame.size = img_overlay.size;
        printf("\nAdd region to test\n");
        ret = esp_vui_overlay_add_region(overlay, &rgn);
        BREAK_ON_FAIL(ret);

        rgn_b = rgn;
        rgn_b.compose.disp_rect.x = 160;
        rgn_b.compose.disp_rect.y = 0;
        ret = esp_vui_overlay_add_region(overlay, &rgn_b);
        BREAK_ON_FAIL(ret);

        ret = esp_video_render_stream_write(stream, &bg_frame);
        BREAK_ON_FAIL(ret);

        printf("\nNot remove write again make sure still overlay\n");
        ret = esp_video_render_stream_write(stream, &bg_frame);
        BREAK_ON_FAIL(ret);

        for (int i = 0; i < frame_count; ++i) {
            // Move overlay a bit
            vTaskDelay(pdMS_TO_TICKS(100));
            printf("\nremove region to test %d should not show\n", i);
            ret = esp_vui_overlay_remove_region(overlay, &rgn);
            BREAK_ON_FAIL(ret);
            ret = esp_video_render_stream_write(stream, &bg_frame);
            BREAK_ON_FAIL(ret);
            vTaskDelay(pdMS_TO_TICKS(100));

            rgn.compose.disp_rect.x = 40 + (i % 50);
            rgn.compose.disp_rect.y = 40 + (i % 30);
            printf("\nAdd region to test %d should show again %d-%d\n", i, rgn.compose.disp_rect.x, rgn.compose.disp_rect.y);
            ret = esp_vui_overlay_add_region(overlay, &rgn);
            BREAK_ON_FAIL(ret);
            ret = esp_video_render_stream_write(stream, &bg_frame);
            BREAK_ON_FAIL(ret);
        }
        success = true;
    } while (0);
    if (stream) {
        esp_video_render_stream_close(stream);
    }
    if (img_bg.data) {
        free(img_bg.data);
    }
    if (img_overlay.data) {
        free(img_overlay.data);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int demo_dual_stream_overlay_only(int frame_count)
{
    bool success = false;
    esp_video_render_stream_handle_t stream = NULL;
    esp_video_render_stream_handle_t stream_unused = NULL;
    esp_vui_overlay_handle_t overlay = NULL;
    esp_video_render_frame_info_t info = {
        .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        .width = get_backend_width(),
        .height = get_backend_height(),
    };
    esp_video_render_img_t img_a = {.info = {.format = info.format, .width = 64, .height = 64}};
    esp_video_render_img_t img_b = {.info = {.format = info.format, .width = 64, .height = 64}};
    do {
        int ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);
        esp_video_render_stream_info_t stream_info = {
            .info = info,
        };
        // Open a stream with display size, but no frames will be written
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream);
        BREAK_ON_FAIL(ret);

        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream_unused);
        BREAK_ON_FAIL(ret);

        esp_video_render_clr_t bg_clr = {
            .r = 0x55,
            .g = 0x55,
            .b = 0x55,
        };
        ret = esp_video_render_set_bg_color(get_video_render(), &bg_clr);
        BREAK_ON_FAIL(ret);

        ret = esp_video_render_stream_get_overlay(stream, &overlay);
        BREAK_ON_FAIL(ret);

        img_a.info.format = get_backend_out_format();
        img_b.info.format = get_backend_out_format();
        ret = gen_image(&img_a, false, 8);
        BREAK_ON_FAIL(ret);
        ret = gen_image(&img_b, true, 8);
        BREAK_ON_FAIL(ret);

        printf("\now add 2 image verify both show\n");
        esp_vui_overlay_rgn_t rgn_a = {0};
        rgn_a.compose.visible = true;
        rgn_a.compose.alpha = 255;
        rgn_a.compose.disp_rect.x = 0;
        rgn_a.compose.disp_rect.y = 0;
        rgn_a.frame.format = img_a.info.format;
        rgn_a.frame.width = img_a.info.width;
        rgn_a.frame.height = img_a.info.height;
        rgn_a.frame.data = img_a.data;
        rgn_a.frame.size = img_a.size;

        ret = esp_vui_overlay_add_region(overlay, &rgn_a);
        BREAK_ON_FAIL(ret);

        esp_vui_overlay_rgn_t rgn_b = rgn_a;
        rgn_b.frame.data = img_b.data;
        rgn_b.frame.size = img_b.size;
        rgn_b.compose.disp_rect.x = 80;
        rgn_b.compose.disp_rect.y = 40;
        ret = esp_vui_overlay_add_region(overlay, &rgn_b);
        BREAK_ON_FAIL(ret);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_video_render_rect_t a_orig_rect = rgn_a.compose.disp_rect;
        esp_video_render_rect_t b_orig_rect = rgn_b.compose.disp_rect;

        for (int i = 0; i < frame_count; ++i) {
            printf("\n%d Now remove 2 image verify both hide\n", i);
            BREAK_ON_FAIL(esp_vui_overlay_remove_region(overlay, &rgn_a));
            BREAK_ON_FAIL(esp_vui_overlay_remove_region(overlay, &rgn_b));
            vTaskDelay(pdMS_TO_TICKS(200));
            rgn_a.compose.disp_rect.x = a_orig_rect.x + (i * 3) % (info.width - rgn_a.frame.width);
            rgn_a.compose.disp_rect.y = a_orig_rect.y + (i * 2) % (info.height - rgn_a.frame.height);
            rgn_b.compose.disp_rect.x = b_orig_rect.x + (i * 2) % (info.width - rgn_b.frame.width);
            rgn_b.compose.disp_rect.y = b_orig_rect.y + (i * 3) % (info.height - rgn_b.frame.height);
            printf("\n%d Now add again make sure both show\n", i);
            BREAK_ON_FAIL(esp_vui_overlay_add_region(overlay, &rgn_a));
            BREAK_ON_FAIL(esp_vui_overlay_add_region(overlay, &rgn_b));
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        success = true;
    } while (0);
    if (stream) {
        esp_video_render_stream_close(stream);
    }
    if (stream_unused) {
        esp_video_render_stream_close(stream_unused);
    }
    if (img_a.data) {
        free(img_a.data);
    }
    if (img_b.data) {
        free(img_b.data);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int demo_dual_streams_video(int frame_count)
{
    bool success = false;
    esp_video_render_stream_handle_t stream_a = NULL;
    esp_video_render_stream_handle_t stream_b = NULL;
    esp_video_render_img_t img_a = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = 160,
            .height = 120}};
    esp_video_render_img_t img_b = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = img_a.info.width,
            .height = img_a.info.height}};
    do {
        int ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);
        img_a.info.format = get_backend_out_format();
        img_b.info.format = get_backend_out_format();
        ret = gen_image(&img_a, false, 8);
        BREAK_ON_FAIL(ret);
        ret = gen_image(&img_b, true, 8);
        BREAK_ON_FAIL(ret);

        esp_video_render_clr_t bg_clr = {
            .r = 0x55,
            .g = 0x55,
            .b = 0x55,
        };
        ret = esp_video_render_set_bg_color(get_video_render(), &bg_clr);
        BREAK_ON_FAIL(ret);
        esp_video_render_stream_info_t stream_info = {0};
        stream_info.info = img_a.info;
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream_a);
        BREAK_ON_FAIL(ret);
        stream_info.info = img_b.info;
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream_b);
        BREAK_ON_FAIL(ret);
        esp_video_render_rect_t rect_a = {
            .x = 50,
            .y = 50,
            .width = img_a.info.width,
            .height = img_a.info.height};
        esp_video_render_rect_t rect_b = {
            .x = img_a.info.width,
            .y = img_a.info.height,
            .width = img_b.info.width,
            .height = img_b.info.height};
        esp_video_render_stream_set_disp_rect(stream_a, &rect_a);
        esp_video_render_stream_set_disp_rect(stream_b, &rect_b);
        esp_video_render_fb_t fb_a = {.info = img_a.info,
                                      .data = img_a.data,
                                      .size = img_a.size};
        esp_video_render_fb_t fb_b = {.info = img_b.info,
                                      .data = img_b.data,
                                      .size = img_b.size};
        for (int i = 0; i < frame_count; ++i) {
            bool is_b = i > frame_count / 2;
            if (i == 50) {
                // Let stream a hide
                esp_video_render_stream_set_visible(stream_a, false);
            }
            if (i == frame_count - 1) {
                // Let stream a show again
                esp_video_render_stream_set_visible(stream_a, true);
            }
            BREAK_ON_FAIL(esp_video_render_stream_write_fb(stream_a, is_b ? &fb_b : &fb_a));
            BREAK_ON_FAIL(esp_video_render_stream_write_fb(stream_b, is_b ? &fb_a : &fb_b));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        success = true;
    } while (0);
    if (stream_a) {
        esp_video_render_stream_close(stream_a);
    }
    if (stream_b) {
        esp_video_render_stream_close(stream_b);
    }
    if (img_a.data) {
        free(img_a.data);
    }
    if (img_b.data) {
        free(img_b.data);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int demo_stream_visible(int frame_count)
{
    bool success = false;
    esp_video_render_img_t img = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = 32,
            .height = 32,
        }};
#define MAX_STREAM_NUM  (16)
    esp_video_render_stream_handle_t stream[MAX_STREAM_NUM] = {NULL};
    do {
        int ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);
        img.info.format = get_backend_out_format();
        ret = gen_image(&img, false, 8);
        BREAK_ON_FAIL(ret);
        esp_video_render_clr_t bg_clr = {
            .r = 0x55,
            .g = 0x55,
            .b = 0x55,
        };
        ret = esp_video_render_set_bg_color(get_video_render(), &bg_clr);
        BREAK_ON_FAIL(ret);

        esp_video_render_rect_t rect = {
            .x = 0,
            .y = 0,
            .width = img.info.width,
            .height = img.info.height,
        };
        esp_video_render_stream_info_t stream_info = {0};
        stream_info.info = img.info;
        for (int i = 0; i < MAX_STREAM_NUM; ++i) {
            ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream[i]);
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);
        int idx = 0;
        for (int i = 0; i < MAX_STREAM_NUM / 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                ret = esp_video_render_stream_set_disp_rect(stream[idx++], &rect);
                rect.x += img.info.width + 4;
                BREAK_ON_FAIL(ret);
            }
            rect.x = 0;
            rect.y += img.info.height + 4;
        }
        BREAK_ON_FAIL(ret);

        esp_video_render_fb_t fb = {.info = img.info,
                                    .data = img.data,
                                    .size = img.size};
        for (int i = 0; i < MAX_STREAM_NUM; i++) {
            BREAK_ON_FAIL(esp_video_render_stream_write_fb(stream[i], &fb));
        }

        bool visible = false;
        idx = 0;
        for (int i = 0; i < frame_count; ++i) {
            vTaskDelay(pdMS_TO_TICKS(200));
            ESP_LOGI(TAG, "Run %d Stream %d visible to %d", i, idx, visible);
            esp_video_render_stream_set_visible(stream[idx++], visible);
            if (idx >= MAX_STREAM_NUM) {
                idx = 0;
                visible = !visible;
            }
        }
        success = true;
    } while (0);
    for (int i = 0; i < MAX_STREAM_NUM; ++i) {
        if (stream[i]) {
            esp_video_render_stream_close(stream[i]);
        }
    }
    if (img.data) {
        free(img.data);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int simple_widget_test(bool with_cache, int frame_count)
{
    bool success = false;
    esp_video_render_img_t img = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = 32,
            .height = 32,
        }};
    esp_video_render_stream_handle_t stream = NULL;
    esp_video_render_stream_handle_t unused_stream = NULL;
    do {
        int ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);
        ret = gen_image(&img, false, 8);
        BREAK_ON_FAIL(ret);

        esp_video_render_clr_t bg_clr = {
            .r = 0x55,
            .g = 0x55,
            .b = 0x55,
        };
        ret = esp_video_render_set_bg_color(get_video_render(), &bg_clr);
        BREAK_ON_FAIL(ret);

        esp_video_render_stream_info_t stream_info = {
            .info = {
                .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
                .width = 320,
                .height = 240,
            }};
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream);
        BREAK_ON_FAIL(ret);

        // Fake stream so that render thread can auto started
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &unused_stream);
        BREAK_ON_FAIL(ret);

        esp_video_render_rect_t rect = {
            .x = 0,
            .y = 0,
            .width = 320,
            .height = 240,
        };
        esp_video_render_stream_set_disp_rect(stream, &rect);
        esp_vui_overlay_handle_t overlay = NULL;
        ret = esp_video_render_stream_get_overlay(stream, &overlay);
        BREAK_ON_FAIL(ret);

        esp_vui_container_handle_t container = NULL;
        esp_video_render_frame_info_t container_info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = 100,
            .height = 100,
        };
        esp_video_render_pos_t pos = {
            .x = 100,
            .y = 100,
        };
        ret = esp_vui_container_create(overlay, &container_info, &pos, with_cache, &container);
        BREAK_ON_FAIL(ret);

        esp_vui_widget_t *widgets[MAX_WIDGETS] = {NULL};
        pos.x = pos.y = 0;
        int idx = 0;
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 2; j++) {
                esp_vui_widget_t *w = esp_vui_image_widget_init(container, &img, &pos);
                BREAK_ON_NULL(w);
                widgets[idx++] = w;
                printf("create widget %d at %d-%d size %dx%d -> %p\n", idx - 1, pos.x, pos.y, img.info.width, img.info.height, (void *)w);
                pos.x += img.info.width + 4;
            }
            pos.x = 0;
            pos.y += img.info.height + 4;
        }

        vTaskDelay(pdMS_TO_TICKS(100));

        bool visible = false;
        idx = 0;
        for (int i = 0; i < frame_count; ++i) {
            vTaskDelay(pdMS_TO_TICKS(200));
            ESP_LOGI(TAG, "Run %d toggle vis idx=%d -> %d\n", i, idx, visible);
            esp_vui_widget_set_visible(widgets[idx++], visible);
            if (idx >= MAX_WIDGETS) {
                idx = 0;
                visible = !visible;
            }
        }
        success = true;
    } while (0);
    if (stream) {
        esp_video_render_stream_close(stream);
    }
    if (unused_stream) {
        esp_video_render_stream_close(unused_stream);
    }
    if (img.data) {
        free(img.data);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int dual_container_no_overlap(bool with_cache, int frame_count)
{
    bool success = false;
    esp_video_render_img_t img = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = 32,
            .height = 32,
        }};
    esp_video_render_stream_handle_t stream = NULL;
    do {
        int ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);
        img.info.format = get_backend_out_format();
        ret = gen_image(&img, false, 8);
        BREAK_ON_FAIL(ret);

        esp_video_render_clr_t bg_clr = {
            .r = 0x55,
            .g = 0x55,
            .b = 0x55,
        };
        ret = esp_video_render_set_bg_color(get_video_render(), &bg_clr);
        BREAK_ON_FAIL(ret);

        esp_video_render_stream_info_t stream_info = {
            .info = {
                .format = img.info.format,
                .width = 320,
                .height = 240,
            }};
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream);
        BREAK_ON_FAIL(ret);

        // Fake stream so that render thread can auto started
        ret = esp_video_render_stream_render_async(stream);
        BREAK_ON_FAIL(ret);

        esp_video_render_rect_t rect = {
            .x = 0,
            .y = 0,
            .width = 320,
            .height = 240,
        };
        esp_video_render_stream_set_disp_rect(stream, &rect);
        esp_vui_overlay_handle_t overlay = NULL;
        ret = esp_video_render_stream_get_overlay(stream, &overlay);
        BREAK_ON_FAIL(ret);

        esp_vui_container_handle_t container[2] = {NULL};
        esp_video_render_frame_info_t container_info = {
            .format = img.info.format,
            .width = 100,
            .height = 100,
        };
        esp_video_render_pos_t pos = {
            .x = 100,
            .y = 100,
        };
        ret = esp_vui_container_create(overlay, &container_info, &pos, with_cache, &container[0]);
        BREAK_ON_FAIL(ret);
        pos.x = 200;
        pos.y = 200;
        ret = esp_vui_container_create(overlay, &container_info, &pos, with_cache, &container[1]);
        BREAK_ON_FAIL(ret);

        esp_vui_widget_t *widgets[MAX_WIDGETS][2];
        pos.x = pos.y = 0;
        int idx = 0;
        for (int i = 0; i < 2; i++) {
            for (int i = 0; i < 2; i++) {
                esp_vui_widget_t *w = esp_vui_image_widget_init(container[0], &img, &pos);
                widgets[idx][0] = w;
                w = esp_vui_image_widget_init(container[1], &img, &pos);
                widgets[idx++][1] = w;
                pos.x += img.info.width + 4;
            }
            pos.x = 0;
            pos.y += img.info.height + 4;
        }

        vTaskDelay(pdMS_TO_TICKS(100));

        bool visible = false;
        idx = 0;
        for (int i = 0; i < frame_count; ++i) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGI(TAG, "Run %d toggle vis idx=%d -> %d\n", i, idx, visible);
            esp_vui_widget_set_visible(widgets[0][0], visible);
            esp_vui_widget_set_visible(widgets[0][1], visible);
            esp_vui_widget_set_visible(widgets[1][0], visible);
            esp_vui_widget_set_visible(widgets[1][1], visible);
            esp_vui_widget_set_visible(widgets[2][0], visible);
            esp_vui_widget_set_visible(widgets[2][1], visible);
            esp_vui_widget_set_visible(widgets[3][0], visible);
            esp_vui_widget_set_visible(widgets[3][1], visible);
            visible = !visible;
        }
        success = true;
    } while (0);
    if (stream) {
        esp_video_render_stream_close(stream);
    }
    if (img.data) {
        free(img.data);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int dual_container_overlap(bool with_cache, int frame_count)
{
    bool success = false;
    esp_video_render_img_t img = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = 32,
            .height = 32,
        }};
    esp_video_render_stream_handle_t stream = NULL;
    do {
        int ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);
        img.info.format = get_backend_out_format();
        ret = gen_image(&img, false, 8);
        BREAK_ON_FAIL(ret);

        esp_video_render_clr_t bg_clr = {
            .r = 0x55,
            .g = 0x55,
            .b = 0x55,
        };
        ret = esp_video_render_set_bg_color(get_video_render(), &bg_clr);
        BREAK_ON_FAIL(ret);

        esp_video_render_stream_info_t stream_info = {
            .info = {
                .format = img.info.format,
                .width = 320,
                .height = 240,
            }};
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream);
        BREAK_ON_FAIL(ret);
        ret = esp_video_render_stream_render_async(stream);
        BREAK_ON_FAIL(ret);

        esp_video_render_rect_t rect = {
            .x = 0,
            .y = 0,
            .width = 320,
            .height = 240,
        };
        esp_video_render_stream_set_disp_rect(stream, &rect);
        esp_vui_overlay_handle_t overlay = NULL;
        ret = esp_video_render_stream_get_overlay(stream, &overlay);
        BREAK_ON_FAIL(ret);

        esp_vui_container_handle_t container[2] = {NULL};
        esp_video_render_frame_info_t container_info = {
            .format = img.info.format,
            .width = 100,
            .height = 100,
        };
        esp_video_render_pos_t pos = {
            .x = 100,
            .y = 100,
        };
        ret = esp_vui_container_create(overlay, &container_info, &pos, with_cache, &container[0]);
        BREAK_ON_FAIL(ret);
        pos.x += img.info.width * 3 / 2 + 4;
        pos.y += img.info.height / 2;
        ret = esp_vui_container_create(overlay, &container_info, &pos, with_cache, &container[1]);
        BREAK_ON_FAIL(ret);

        esp_vui_widget_t *widgets[MAX_WIDGETS][2];
        pos.x = pos.y = 0;
        int idx = 0;
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 2; j++) {
                esp_vui_widget_t *w = esp_vui_image_widget_init(container[0], &img, &pos);
                widgets[idx][0] = w;
                printf("create widget %d at %d-%d size %dx%d -> %p\n", idx, pos.x, pos.y, img.info.width, img.info.height, (void *)w);
                w = esp_vui_image_widget_init(container[1], &img, &pos);
                widgets[idx++][1] = w;
                printf("create widget %d at %d-%d size %dx%d -> %p\n", idx - 1, pos.x, pos.y, img.info.width, img.info.height, (void *)w);
                pos.x += img.info.width + 4;
            }
            pos.x = 0;
            pos.y += img.info.height * 3 / 2;
        }

        vTaskDelay(pdMS_TO_TICKS(100));

        bool visible = false;
        idx = 0;
        for (int i = 0; i < frame_count; ++i) {
            vTaskDelay(pdMS_TO_TICKS(200));
            ESP_LOGI(TAG, "Run %d toggle vis idx=%d -> %d\n", i, idx, visible);
            int pair = idx == 0 ? 3 : 2;
            esp_vui_widget_set_visible(widgets[idx][0], visible);
            esp_vui_widget_set_visible(widgets[idx][1], visible);
            esp_vui_widget_set_visible(widgets[pair][0], visible);
            esp_vui_widget_set_visible(widgets[pair][1], visible);
            idx++;
            if (idx >= 2) {
                idx = 0;
                visible = !visible;
            }
        }
        success = true;
    } while (0);
    if (stream) {
        esp_video_render_stream_close(stream);
    }
    if (img.data) {
        free(img.data);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

// Complex auto-played game: Bouncing Balls
int demo_bouncing_balls_game(int frame_count)
{
    esp_log_level_set("*", ESP_LOG_ERROR);
#define MAX_BALLS  6

    bool success = false;
    esp_video_render_stream_handle_t bg_stream = NULL;

    // Ball images - one per ball with different colors
    esp_video_render_img_t ball_img[MAX_BALLS];
    for (int i = 0; i < MAX_BALLS; i++) {
        ball_img[i] = (esp_video_render_img_t) {.info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = 20, .height = 20}};
    }
    esp_video_render_img_t paddle_img = {.info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = 80, .height = 10}};
    esp_video_render_img_t paddle_vertical_img = {.info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = 10, .height = 80}};
    esp_video_render_img_t digit_img = {.info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = 16, .height = 24}};
    typedef struct {
        float  x, y, vx, vy;
        esp_vui_widget_t *widget;
        esp_vui_container_handle_t  container;
    } ball_t;
    typedef struct {
        float  x, y;
        esp_vui_container_handle_t  container;
        esp_vui_widget_t *widget;
    } paddle_t;
    ball_t balls[MAX_BALLS];
    paddle_t paddles[2];           // Horizontal paddles (top and bottom)
    paddle_t paddles_vertical[2];  // Vertical paddles (left and right)
    esp_vui_container_handle_t score_container = NULL;
    esp_vui_widget_t *score_widgets[3] = {NULL};

    do {
        int ret = create_video_render(30);
        BREAK_ON_FAIL(ret);

        // Set background color first (needed for circle generation)
        esp_video_render_clr_t bg_clr = {.r = 0x10, .g = 0x10, .b = 0x30};
        BREAK_ON_FAIL(esp_video_render_set_bg_color(get_video_render(), &bg_clr));

        int display_width = get_backend_width(), display_height = get_backend_height();

        // Scale ball size based on display resolution (base: 24x24 at 1024x600)
        int ball_size = SCALE_WIDTH(24, 16);
        for (int i = 0; i < MAX_BALLS; i++) {
            ball_img[i].info.width = ball_size;
            ball_img[i].info.height = ball_size;
        }

        // Scale paddle sizes (base: 120x20 horizontal, 20x120 vertical at 1024x600)
        paddle_img.info.width = SCALE_WIDTH(120, 80);
        paddle_img.info.height = SCALE_HEIGHT(20, 15);
        paddle_vertical_img.info.width = SCALE_WIDTH(20, 15);
        paddle_vertical_img.info.height = SCALE_HEIGHT(120, 80);

        // Scale digit size (base: 20x30 at 1024x600)
        digit_img.info.width = SCALE_WIDTH(20, 16);
        digit_img.info.height = SCALE_HEIGHT(30, 24);

        // Generate circle images for balls with different colors
        esp_video_render_clr_t ball_colors[MAX_BALLS] = {
            {.r = 0xFF, .g = 0x00, .b = 0x00},  // Red
            {.r = 0x00, .g = 0xFF, .b = 0x00},  // Green
            {.r = 0x00, .g = 0x00, .b = 0xFF},  // Blue
            {.r = 0xFF, .g = 0x00, .b = 0x00},  // Red
            {.r = 0x00, .g = 0xFF, .b = 0x00},  // Green
            {.r = 0x00, .g = 0x00, .b = 0xFF},  // Blue
        };

        // Generate each ball as a circle with its own color
        for (int i = 0; i < MAX_BALLS; i++) {
            int radius = (ball_img[i].info.width < ball_img[i].info.height ? ball_img[i].info.width : ball_img[i].info.height) / 2 - 1;
            BREAK_ON_FAIL(gen_circle_image(&ball_img[i], &bg_clr, &ball_colors[i], radius));
        }

        // Generate paddle and digit images (keep using bars for now)
        BREAK_ON_FAIL(gen_image(&paddle_img, true, 2));
        BREAK_ON_FAIL(gen_image(&paddle_vertical_img, true, 2));
        BREAK_ON_FAIL(gen_image(&digit_img, false, 10));

        esp_video_render_stream_info_t bg_info = {.info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = display_width, .height = display_height}};
        BREAK_ON_FAIL(esp_video_render_stream_open(get_video_render(), &bg_info, &bg_stream));
        BREAK_ON_FAIL(esp_video_render_stream_render_async(bg_stream));

        esp_video_render_rect_t bg_rect = {.x = 0, .y = 0, .width = display_width, .height = display_height};
        BREAK_ON_FAIL(esp_video_render_stream_set_disp_rect(bg_stream, &bg_rect));

        esp_vui_overlay_handle_t overlay = NULL;
        BREAK_ON_FAIL(esp_video_render_stream_get_overlay(bg_stream, &overlay));

        for (int i = 0; i < MAX_BALLS; i++) {
            // Initialize ball positions within safe bounds
            int container_width = ball_img[i].info.width + 4;
            int container_height = ball_img[i].info.height + 4;
            float safe_max_x = (float)(display_width - container_width);
            float safe_max_y = (float)(display_height - container_height);

            // Scale initial positions (base: 50, 50 at 1024x600)
            balls[i].x = SCALE_X(50) + i * SCALE_WIDTH(100, 50);
            balls[i].y = SCALE_Y(50) + i * SCALE_HEIGHT(80, 40);
            if (balls[i].x > safe_max_x) {
                balls[i].x = safe_max_x;
            }
            if (balls[i].y > safe_max_y) {
                balls[i].y = safe_max_y;
            }
            if (balls[i].x < 0) {
                balls[i].x = 0;
            }
            if (balls[i].y < 0) {
                balls[i].y = 0;
            }

            // Scale velocities based on display size
            float vel_scale = (float)display_width / BASE_WIDTH;
            balls[i].vx = (2.0f + i * 0.5f) * vel_scale;
            balls[i].vy = (1.5f + i * 0.3f) * vel_scale;

            esp_video_render_frame_info_t ball_info = {
                .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
                .width = container_width,
                .height = container_height};
            esp_video_render_pos_t ball_pos = {
                .x = (int)balls[i].x,
                .y = (int)balls[i].y};
            BREAK_ON_FAIL(esp_vui_container_create(overlay, &ball_info, &ball_pos, true, &balls[i].container));
            esp_video_render_pos_t widget_pos = {.x = 2, .y = 2};
            balls[i].widget = esp_vui_image_widget_init(balls[i].container, &ball_img[i], &widget_pos);
            if (!balls[i].widget) {
                ret = -1;
                break;
            }
        }
        BREAK_ON_FAIL(ret);

        for (int i = 0; i < 2; i++) {
            paddles[i].x = display_width / 2 - paddle_img.info.width / 2;
            paddles[i].y = i == 0 ? SCALE_Y(20) : (display_height - SCALE_HEIGHT(30, 20));
            esp_video_render_frame_info_t paddle_info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = paddle_img.info.width, .height = paddle_img.info.height};
            esp_video_render_pos_t paddle_pos = {.x = (int)paddles[i].x, .y = (int)paddles[i].y};
            BREAK_ON_FAIL(esp_vui_container_create(overlay, &paddle_info, &paddle_pos, false, &paddles[i].container));
            esp_video_render_pos_t widget_pos = {.x = 0, .y = 0};
            paddles[i].widget = esp_vui_image_widget_init(paddles[i].container, &paddle_img, &widget_pos);
            if (!paddles[i].widget) {
                ret = -1;
                break;
            }
        }
        BREAK_ON_FAIL(ret);

        // Create vertical paddles (left and right)
        for (int i = 0; i < 2; i++) {
            paddles_vertical[i].x = i == 0 ? SCALE_X(20) : (display_width - paddle_vertical_img.info.width - SCALE_X(20));
            paddles_vertical[i].y = display_height / 2 - paddle_vertical_img.info.height / 2;
            esp_video_render_frame_info_t paddle_info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = paddle_vertical_img.info.width, .height = paddle_vertical_img.info.height};
            esp_video_render_pos_t paddle_pos = {.x = (int)paddles_vertical[i].x, .y = (int)paddles_vertical[i].y};
            BREAK_ON_FAIL(esp_vui_container_create(overlay, &paddle_info, &paddle_pos, true, &paddles_vertical[i].container));
            esp_video_render_pos_t widget_pos = {.x = 0, .y = 0};
            paddles_vertical[i].widget = esp_vui_image_widget_init(paddles_vertical[i].container, &paddle_vertical_img, &widget_pos);
            if (!paddles_vertical[i].widget) {
                ret = -1;
                break;
            }
        }
        BREAK_ON_FAIL(ret);

        esp_video_render_frame_info_t score_info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = digit_img.info.width * 3 + 4, .height = digit_img.info.height + 4};
        esp_video_render_pos_t score_pos = {.x = display_width - score_info.width - SCALE_X(20), .y = SCALE_Y(20)};
        BREAK_ON_FAIL(esp_vui_container_create(overlay, &score_info, &score_pos, false, &score_container));

        for (int i = 0; i < 3; i++) {
            esp_video_render_pos_t digit_pos = {.x = i * (digit_img.info.width + 2), .y = 2};
            score_widgets[i] = esp_vui_image_widget_init(score_container, &digit_img, &digit_pos);
            if (!score_widgets[i]) {
                ret = -1;
                break;
            }
        }
        BREAK_ON_FAIL(ret);

        vTaskDelay(pdMS_TO_TICKS(200));

        int score = 0;
        for (int frame = 0; frame < frame_count; frame++) {
            vTaskDelay(pdMS_TO_TICKS(30));
            // Alternate between two background buffers to prevent DMA accessing memory being written
            int last_score = score;
            for (int i = 0; i < MAX_BALLS; i++) {
                // Update ball position
                balls[i].x += balls[i].vx;
                balls[i].y += balls[i].vy;

                // Calculate container dimensions (ball image + padding)
                int container_width = ball_img[i].info.width + 4;
                int container_height = ball_img[i].info.height + 4;
                float ball_radius = (float)ball_img[i].info.width / 2.0f;
                float ball_center_x = balls[i].x + ball_radius;
                float ball_center_y = balls[i].y + ball_radius;

                // Check collision with horizontal paddles (top and bottom)
                for (int p = 0; p < 2; p++) {
                    float paddle_left = paddles[p].x;
                    float paddle_right = paddles[p].x + paddle_img.info.width;
                    float paddle_top = paddles[p].y;
                    float paddle_bottom = paddles[p].y + paddle_img.info.height;

                    // Check if ball center is within paddle's x-range and if ball is approaching paddle
                    bool x_overlap = (ball_center_x >= paddle_left && ball_center_x <= paddle_right);
                    bool approaching = (p == 0 && balls[i].vy < 0) || (p == 1 && balls[i].vy > 0);

                    if (x_overlap && approaching) {
                        // Check vertical collision
                        if (p == 0) {
                            // Top paddle - ball moving up
                            if (balls[i].y <= paddle_bottom && balls[i].y + container_height >= paddle_top) {
                                balls[i].y = paddle_bottom;
                                balls[i].vy = -balls[i].vy;
                            }
                        } else {
                            // Bottom paddle - ball moving down
                            if (balls[i].y + container_height >= paddle_top && balls[i].y <= paddle_bottom) {
                                balls[i].y = paddle_top - container_height;
                                balls[i].vy = -balls[i].vy;
                                score++;
                            }
                        }
                    }
                }

                // Check collision with vertical paddles (left and right)
                for (int p = 0; p < 2; p++) {
                    float paddle_left = paddles_vertical[p].x;
                    float paddle_right = paddles_vertical[p].x + paddle_vertical_img.info.width;
                    float paddle_top = paddles_vertical[p].y;
                    float paddle_bottom = paddles_vertical[p].y + paddle_vertical_img.info.height;

                    // Check if ball center is within paddle's y-range and if ball is approaching paddle
                    bool y_overlap = (ball_center_y >= paddle_top && ball_center_y <= paddle_bottom);
                    bool approaching = (p == 0 && balls[i].vx < 0) || (p == 1 && balls[i].vx > 0);

                    if (y_overlap && approaching) {
                        // Check horizontal collision
                        if (p == 0) {
                            // Left paddle - ball moving left
                            if (balls[i].x <= paddle_right && balls[i].x + container_width >= paddle_left) {
                                balls[i].x = paddle_right;
                                balls[i].vx = -balls[i].vx;
                            }
                        } else {
                            // Right paddle - ball moving right
                            if (balls[i].x + container_width >= paddle_left && balls[i].x <= paddle_right) {
                                balls[i].x = paddle_left - container_width;
                                balls[i].vx = -balls[i].vx;
                            }
                        }
                    }
                }

                // Clamp position to ensure ball never moves outside framebuffer
                float min_x = 0.0f;
                float max_x = (float)(display_width - container_width);
                float min_y = 0.0f;
                float max_y = (float)(display_height - container_height);

                // Ensure bounds are valid (container might be larger than display)
                if (max_x < min_x) {
                    max_x = min_x;
                }
                if (max_y < min_y) {
                    max_y = min_y;
                }

                // Clamp position within bounds and reverse velocity on boundary hit
                if (balls[i].x < min_x) {
                    balls[i].x = min_x;
                    balls[i].vx = -balls[i].vx;
                } else if (balls[i].x > max_x) {
                    balls[i].x = max_x;
                    balls[i].vx = -balls[i].vx;
                }

                if (balls[i].y < min_y) {
                    balls[i].y = min_y;
                    balls[i].vy = -balls[i].vy;
                } else if (balls[i].y > max_y) {
                    balls[i].y = max_y;
                    balls[i].vy = -balls[i].vy;
                    score++;
                }

                // Update ball container position (ensure integer coordinates are within bounds)
                int rect_x = (int)balls[i].x;
                int rect_y = (int)balls[i].y;
                if (rect_x < 0) {
                    rect_x = 0;
                }
                if (rect_y < 0) {
                    rect_y = 0;
                }
                if (rect_x + container_width > display_width) {
                    rect_x = display_width - container_width;
                }
                if (rect_y + container_height > display_height) {
                    rect_y = display_height - container_height;
                }

                esp_video_render_rect_t new_rect = {
                    .x = rect_x,
                    .y = rect_y,
                    .width = container_width,
                    .height = container_height};
                esp_vui_overlay_update_region(overlay, (esp_vui_overlay_rgn_t *)balls[i].container, &new_rect);
            }

            // Update horizontal paddles (top and bottom)
            for (int i = 0; i < 2; i++) {
                float nearest_x = display_width / 2, min_dist = display_width;
                for (int j = 0; j < MAX_BALLS; j++) {
                    float dist = fabs(balls[j].x - paddles[i].x);
                    if (dist < min_dist && ((i == 0 && balls[j].y < display_height / 2) || (i == 1 && balls[j].y >= display_height / 2))) {
                        min_dist = dist;
                        nearest_x = balls[j].x;
                    }
                }
                float target_x = nearest_x - paddle_img.info.width / 2;
                paddles[i].x += (target_x - paddles[i].x) * 0.1f;
                if (paddles[i].x < 0) {
                    paddles[i].x = 0;
                }
                if (paddles[i].x > display_width - paddle_img.info.width) {
                    paddles[i].x = display_width - paddle_img.info.width;
                }
                esp_video_render_rect_t paddle_rect = {.x = (int)paddles[i].x, .y = (int)paddles[i].y, .width = paddle_img.info.width, .height = paddle_img.info.height};
                esp_vui_overlay_update_region(overlay, (esp_vui_overlay_rgn_t *)paddles[i].container, &paddle_rect);
            }

            // Update vertical paddles (left and right)
            for (int i = 0; i < 2; i++) {
                float nearest_y = display_height / 2, min_dist = display_height;
                for (int j = 0; j < MAX_BALLS; j++) {
                    float dist = fabs(balls[j].y - paddles_vertical[i].y);
                    if (dist < min_dist && ((i == 0 && balls[j].x < display_width / 2) || (i == 1 && balls[j].x >= display_width / 2))) {
                        min_dist = dist;
                        nearest_y = balls[j].y;
                    }
                }
                float target_y = nearest_y - paddle_vertical_img.info.height / 2;
                paddles_vertical[i].y += (target_y - paddles_vertical[i].y) * 0.1f;
                if (paddles_vertical[i].y < 0) {
                    paddles_vertical[i].y = 0;
                }
                if (paddles_vertical[i].y > display_height - paddle_vertical_img.info.height) {
                    paddles_vertical[i].y = display_height - paddle_vertical_img.info.height;
                }
                esp_video_render_rect_t paddle_rect = {.x = (int)paddles_vertical[i].x, .y = (int)paddles_vertical[i].y, .width = paddle_vertical_img.info.width, .height = paddle_vertical_img.info.height};
                esp_vui_overlay_update_region(overlay, (esp_vui_overlay_rgn_t *)paddles_vertical[i].container, &paddle_rect);
            }

            if (last_score != score) {
                esp_vui_widget_set_visible(score_widgets[0], (score & 1) > 0);
                esp_vui_widget_set_visible(score_widgets[1], (score & 2) > 0);
                esp_vui_widget_set_visible(score_widgets[2], (score & 4) > 0);
            }
        }

        ESP_LOGI(TAG, "Bouncing balls game completed: Score=%d", score);
        success = true;
    } while (0);

    if (bg_stream) {
        esp_video_render_stream_close(bg_stream);
    }
    // Free background frame buffers
    for (int i = 0; i < MAX_BALLS; i++) {
        if (ball_img[i].data) {
            free(ball_img[i].data);
        }
    }
    if (paddle_img.data) {
        free(paddle_img.data);
    }
    if (paddle_vertical_img.data) {
        free(paddle_vertical_img.data);
    }
    if (digit_img.data) {
        free(digit_img.data);
    }
    destroy_video_render();
    esp_log_level_set("*", ESP_LOG_INFO);
    return success ? 0 : -1;
}

int demo_clock_widget(bool with_cache, int frame_count)
{
    bool success = false;
    esp_video_render_stream_handle_t stream = NULL;
    esp_vui_overlay_handle_t overlay = NULL;
    esp_vui_container_handle_t container = NULL;
    esp_vui_widget_t *clock_widget = NULL;

    do {
        esp_video_render_err_t ret;
        ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);

        int display_width = get_backend_width();
        int display_height = get_backend_height();

        // Create a stream for overlay
        esp_video_render_stream_info_t stream_info = {
            .info = {
                .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
                .width = display_width,
                .height = display_height,
            }};
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream);
        BREAK_ON_FAIL(ret);

        esp_video_render_rect_t rect = {
            .x = 0,
            .y = 0,
            .width = display_width,
            .height = display_height,
        };
        ret = esp_video_render_stream_set_disp_rect(stream, &rect);
        BREAK_ON_FAIL(ret);

        ret = esp_video_render_stream_render_async(stream);
        BREAK_ON_FAIL(ret);

        ret = esp_video_render_stream_get_overlay(stream, &overlay);
        BREAK_ON_FAIL(ret);

        // Create container for clock widget (base: 200x200 at 1024x600)
        int clock_size = SCALE_WIDTH(200, 120);
        if (clock_size > display_height) {
            clock_size = display_height;  // Ensure it fits
        }
        esp_video_render_frame_info_t container_info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = clock_size,
            .height = clock_size,
        };
        esp_video_render_pos_t pos = {
            .x = (display_width - clock_size) / 2,
            .y = (display_height - clock_size) / 2,
        };
        ret = esp_vui_container_create(overlay, &container_info, &pos, with_cache, &container);
        BREAK_ON_FAIL(ret);

        // Define clock colors
        esp_video_render_clr_t bg_color = {.r = 255, .g = 255, .b = 255};  // White background
        esp_video_render_clr_t hour_color = {.r = 0, .g = 0, .b = 0};      // Black hour hand
        esp_video_render_clr_t minute_color = {.r = 0, .g = 0, .b = 0};    // Black minute hand
        esp_video_render_clr_t second_color = {.r = 255, .g = 0, .b = 0};  // Red second hand

        // Create clock widget (pos should be relative to container, so 0,0)
        esp_video_render_pos_t widget_pos = {.x = 0, .y = 0};
        clock_widget = esp_vui_widget_clock_init(container, &container_info, &widget_pos, clock_size,
                                                 &bg_color, &hour_color, &minute_color, &second_color);
        if (clock_widget == NULL) {
            ESP_LOGE(TAG, "Failed to create clock widget");
            break;
        }

        ESP_LOGI(TAG, "Clock widget test: cache=%d, size=%d, frames=%d", with_cache, clock_size, frame_count);

        vTaskDelay(pdMS_TO_TICKS(100));

        // Update clock every second for specified number of frames
        for (int frame = 0; frame < frame_count; frame++) {
            vTaskDelay(pdMS_TO_TICKS(1000));  // Update every second

            // Mark clock as dirty to trigger redraw
            ret = esp_vui_widget_clock_update(clock_widget);
            if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                ESP_LOGW(TAG, "Clock update failed: %d", ret);
            }

            if (frame % 10 == 0) {
                ESP_LOGI(TAG, "Clock widget frame %d/%d", frame, frame_count);
            }
        }

        success = true;
    } while (0);

    // Cleanup
    if (clock_widget) {
        esp_vui_widget_destroy(clock_widget);
    }
    if (container) {
        esp_vui_container_destroy(container);
    }
    if (stream) {
        esp_video_render_stream_close(stream);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

extern const uint8_t dejavu_sans_ttf_start[] asm("_binary_DejaVuSans_ttf_start");
extern const uint8_t dejavu_sans_ttf_end[] asm("_binary_DejaVuSans_ttf_end");

int demo_text_widget(bool with_cache, int frame_count, bool test_scroll, bool test_emoji)
{
    int ret = 0;
    bool success = false;
    esp_video_render_handle_t video_render = NULL;
    esp_video_render_stream_handle_t stream = NULL;
    esp_vui_container_handle_t container = NULL;
    esp_vui_widget_t *text_widget = NULL;

    // Font configuration - try memory-mapped assets first (like esp_emote_gfx pattern)
    ESP_LOGI(TAG, "Text widget test: cache=%d, frames=%d, scroll=%d, emoji=%d",
             with_cache, frame_count, test_scroll, test_emoji);

    do {
        // Create video render
        ret = create_video_render(3);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to create video render");
            break;
        }
        video_render = get_video_render();
        if (video_render == NULL) {
            ESP_LOGE(TAG, "Failed to get video render");
            break;
        }

        // Get display dimensions
        int display_width = get_backend_width();
        int display_height = get_backend_height();

        // Create stream with overlay only
        esp_video_render_stream_info_t stream_cfg = {
            .info = {
                .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
                .width = display_width,
                .height = display_height,
            }};
        ret = esp_video_render_stream_open(video_render, &stream_cfg, &stream);
        BREAK_ON_FAIL(ret);

        // Set display rect
        esp_video_render_rect_t rect = {
            .x = 0,
            .y = 0,
            .width = display_width,
            .height = display_height,
        };
        ret = esp_video_render_stream_set_disp_rect(stream, &rect);
        BREAK_ON_FAIL(ret);

        // Set overlay only mode
        ret = esp_video_render_stream_render_async(stream);
        BREAK_ON_FAIL(ret);

        esp_vui_overlay_handle_t overlay = NULL;
        ret = esp_video_render_stream_get_overlay(stream, &overlay);
        BREAK_ON_FAIL(ret);

        // Create text widget with optimized size (base: 974x200 at 1024x600)
        int widget_width = SCALE_WIDTH(974, 200);
        int widget_height = SCALE_HEIGHT(200, 100);
        // Ensure widget fits within display
        if (widget_width > display_width - SCALE_X(50)) {
            widget_width = display_width - SCALE_X(50);
        }
        if (widget_height > display_height - SCALE_Y(50)) {
            widget_height = display_height - SCALE_Y(50);
        }
        esp_video_render_pos_t widget_pos = {.x = SCALE_X(25), .y = (display_height - widget_height) / 2};

        int container_margin = SCALE_X(10);
        esp_video_render_frame_info_t container_info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = widget_width + container_margin * 2,
            .height = widget_height + container_margin * 2,
        };
        esp_video_render_pos_t container_pos = {
            .x = widget_pos.x - container_margin,
            .y = widget_pos.y - container_margin};
        // Adjust container size if it would exceed display bounds
        if (container_pos.x + container_info.width > display_width) {
            container_info.width = display_width - container_pos.x;
        }
        if (container_pos.y + container_info.height > display_height) {
            container_info.height = display_height - container_pos.y;
        }

        ret = esp_vui_container_create(overlay, &container_info, &container_pos, with_cache, &container);
        if (ret != ESP_VIDEO_RENDER_ERR_OK || container == NULL) {
            ESP_LOGE(TAG, "Failed to create container");
            break;
        }

        // Adjust widget position relative to container
        esp_video_render_frame_info_t widget_info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = widget_width,
            .height = widget_height,
        };
        esp_video_render_pos_t widget_pos_rel = {
            .x = widget_pos.x - container_pos.x,
            .y = widget_pos.y - container_pos.y};
        text_widget = esp_vui_text_widget_init(container, &widget_info, &widget_pos_rel, widget_width, widget_height);
        if (text_widget == NULL) {
            ESP_LOGE(TAG, "Failed to create text widget");
            break;
        }
        bool font_loaded = false;
        // Scale font size based on display resolution (base: 32 at 1024x600)
        int font_size = SCALE_FONT_SIZE(32, 20);
        ret = set_widget_font(text_widget, "DejaVuSans.ttf", font_size);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to load font");
            break;
        }
        font_loaded = true;
        // Load emoji font if testing emoji (multi-font support)
        if (test_emoji) {
            ret = set_widget_font(text_widget, "NotoEmoji-Regular.ttf", font_size);
            if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                ESP_LOGE(TAG, "Failed to load emoji font");
            }
        }
        // Final check
        if (!font_loaded) {
            ESP_LOGE(TAG, "Failed to load primary font, text rendering will not work");
            break;
        }
        // Set text colors
        esp_video_render_clr_t text_color = {.r = 255, .g = 255, .b = 255};  // White
        esp_video_render_clr_t bg_color = {.r = 60, .g = 60, .b = 60};       // Dark blue
        esp_vui_text_widget_set_text_color(text_widget, &text_color);
        esp_vui_text_widget_set_bg_color(text_widget, &bg_color, false);
        esp_vui_text_widget_set_align(text_widget, 0, 1);  // Left, Middle

        // Set text with emoji if testing emoji
        const char *test_text = NULL;
        if (test_emoji) {
            test_text = "Hello World! 😀 🎉 🎊 🌍 🌈 🚀 Emoji Test";
        } else {
            test_text = "Hello World! This is a scrolling text test. The quick brown fox jumps over the lazy dog. 0123456789 !@#$%^&*()";
        }
        ret = esp_vui_text_widget_set_text(text_widget, test_text);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to set text");
            break;
        }

        // Set scrolling if testing scroll
        if (test_scroll) {
            // Test horizontal scrolling
            ret = esp_vui_text_widget_set_scroll(text_widget, true, 1, 2);  // Horizontal, speed 2
            if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                ESP_LOGW(TAG, "Failed to set scroll");
            }
        }

        // Add shadow effect
        esp_video_render_clr_t shadow_color = {.r = 0, .g = 0, .b = 0};  // Black shadow
        esp_vui_text_widget_set_shadow(text_widget, true, &shadow_color, 2, 2);
        ESP_LOGI(TAG, "Text widget created successfully. Starting render loop...");

        vTaskDelay(pdMS_TO_TICKS(100));

        // Render loop
        for (int frame = 0; frame < frame_count; frame++) {
            // Update scroll position if scrolling is enabled
            if (test_scroll) {
                esp_vui_text_widget_scroll_update(text_widget);
            }
            // Change text periodically for visual testing
            if (frame > 0 && frame % 100 == 0) {
                static int text_variant = 0;
                const char *variants[] = {
                    test_emoji ? "Hello 😀 World 🌍" : "DejaVuSans Font Test - Variant 1",
                    test_emoji ? "Emoji 🎉 Test 🎊" : "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz",
                    test_emoji ? "Rainbow 🌈 Rocket 🚀" : "Numbers: 0123456789 Symbols: !@#$%^&*()",
                };
                text_variant = (text_variant + 1) % 3;
                esp_vui_text_widget_set_text(text_widget, variants[text_variant]);
            }

            // Change scroll direction periodically (more frequently to test mode switching)
            if (test_scroll && frame > 0 && frame % 50 == 0) {
                static int scroll_mode = 1;
                scroll_mode = (scroll_mode % 3) + 1;  // Cycle through 1, 2, 3
                esp_vui_text_widget_set_scroll(text_widget, true, scroll_mode, 2);
                ESP_LOGI(TAG, "Changed scroll mode to %d (1=horizontal, 2=vertical, 3=circular)", scroll_mode);
            }

            // Pause/resume scroll periodically
            if (test_scroll && frame > 0 && frame % 150 == 0) {
                static bool paused = false;
                paused = !paused;
                esp_vui_text_widget_scroll_pause(text_widget, paused);
                ESP_LOGI(TAG, "Scroll %s", paused ? "paused" : "resumed");
            }

            if (frame % 50 == 0) {
                ESP_LOGI(TAG, "Text widget frame %d/%d", frame, frame_count);
            }

            vTaskDelay(pdMS_TO_TICKS(100));  // 20 FPS
        }

        success = true;
    } while (0);

    // Cleanup
    if (text_widget) {
        esp_vui_widget_destroy(text_widget);
    }
    if (container) {
        esp_vui_container_destroy(container);
    }
    if (stream) {
        esp_video_render_stream_close(stream);
    }
    destroy_video_render();

    return success ? 0 : -1;
}

int demo_text_widget_alignment(bool with_cache, int frame_count)
{
    int ret = 0;
    bool success = false;
    esp_video_render_handle_t video_render = NULL;
    esp_video_render_stream_handle_t stream = NULL;
    esp_vui_container_handle_t container = NULL;
    esp_vui_widget_t *text_widget = NULL;

    ESP_LOGI(TAG, "Text widget alignment test: cache=%d, frames=%d", with_cache, frame_count);

    do {
        // Create video render
        ret = create_video_render(3);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to create video render");
            break;
        }
        video_render = get_video_render();
        if (video_render == NULL) {
            ESP_LOGE(TAG, "Failed to get video render");
            break;
        }

        // Get display dimensions
        int display_width = get_backend_width();
        int display_height = get_backend_height();

        // Create stream with overlay only
        esp_video_render_stream_info_t stream_cfg = {
            .info = {
                .width = display_width,
                .height = display_height,
                .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            }};
        ret = esp_video_render_stream_open(video_render, &stream_cfg, &stream);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to open stream");
            break;
        }

        ret = esp_video_render_stream_render_async(stream);
        BREAK_ON_FAIL(ret);
        esp_vui_overlay_handle_t overlay = NULL;
        ret = esp_video_render_stream_get_overlay(stream, &overlay);
        BREAK_ON_FAIL(ret);

        // Create container for text widget
        esp_video_render_frame_info_t container_info = {
            .width = display_width - 50,
            .height = display_height - 200,
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        };
        esp_video_render_pos_t container_pos = {.x = 25, .y = 100};

        ret = esp_vui_container_create(overlay, &container_info, &container_pos, with_cache, &container);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to create container");
            break;
        }
        // Create text widget
        esp_video_render_frame_info_t text_info = {
            .width = container_info.width - 20,
            .height = container_info.height - 20,
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        };
        esp_video_render_pos_t text_pos = {.x = 10, .y = 10};
        text_widget = esp_vui_text_widget_init(container, &text_info, &text_pos,
                                               text_info.width, text_info.height);
        if (text_widget == NULL) {
            ESP_LOGE(TAG, "Failed to create text widget");
            break;
        }
        // Scale font size based on display resolution (base: 32 at 1024x600)
        int font_size = SCALE_FONT_SIZE(32, 20);
        ret = set_widget_font(text_widget, "DejaVuSans.ttf", font_size);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to load font");
            break;
        }
        // Set text colors
        esp_video_render_clr_t text_color = {.r = 255, .g = 255, .b = 255};  // White
        esp_video_render_clr_t bg_color = {.r = 60, .g = 60, .b = 60};       // Dark gray
        esp_vui_text_widget_set_text_color(text_widget, &text_color);
        esp_vui_text_widget_set_bg_color(text_widget, &bg_color, false);

        ESP_LOGI(TAG, "Text widget created successfully. Testing alignments...");

        // Test all alignment combinations
        const char *align_h_names[] = {"LEFT", "CENTER", "RIGHT"};
        const char *align_v_names[] = {"TOP", "MIDDLE", "BOTTOM"};
        int alignment_index = 0;
        // Render loop - cycle through all alignments
        for (int frame = 0; frame < frame_count; frame++) {
            if (frame % 2 == 0) {
                int align_h = alignment_index % 3;
                int align_v = (alignment_index / 3) % 3;
                char show_text[128];
                snprintf(show_text, sizeof(show_text),
                         "Alignment test\nAlign as %s %s\nVerify the actual results",
                         align_h_names[align_h], align_v_names[align_v]);
                ESP_LOGI(TAG, "Frame %d/%d: Alignment H=%s, V=%s",
                         frame, frame_count, align_h_names[align_h], align_v_names[align_v]);
                ret = esp_vui_text_widget_set_text(text_widget, show_text);
                BREAK_ON_FAIL(ret);
                ret = esp_vui_text_widget_set_align(text_widget, align_h, align_v);
                BREAK_ON_FAIL(ret);
                alignment_index++;
            }
            vTaskDelay(pdMS_TO_TICKS(100));  // 10 FPS
        }

        success = true;
    } while (0);

    // Cleanup
    if (text_widget) {
        esp_vui_widget_destroy(text_widget);
    }
    if (container) {
        esp_vui_container_destroy(container);
    }
    if (stream) {
        esp_video_render_stream_close(stream);
    }
    destroy_video_render();

    return success ? 0 : -1;
}

int demo_text_widget_scroll(bool with_cache, int frame_count)
{
    int ret = 0;
    bool success = false;
    esp_video_render_handle_t video_render = NULL;
    esp_video_render_stream_handle_t stream = NULL;
    esp_vui_container_handle_t container = NULL;
    esp_vui_widget_t *text_widget = NULL;

    // Text for horizontal scrolling - single long line
    const char *horizontal_text = "This is a very long text string that should scroll horizontally without wrapping. "
                                  "It contains many words and should extend far beyond the widget width. "
                                  "The text should scroll smoothly from right to left (or left to right). "
                                  "Let's add even more text to make sure it's really long: "
                                  "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 0123456789 !@#$%^&*() "
                                  "More text here to ensure scrolling works properly. "
                                  "The scroll effect should be smooth and continuous. "
                                  "Text should not wrap when scrolling is enabled.";

    // Text for vertical scrolling - multiple lines that exceed widget height
    const char *vertical_text = "Line 1: This is the first line.\n"
                                "Line 2: Vertical scrolling should show multiple lines.\n"
                                "Line 3: Each line should be displayed one after another.\n"
                                "Line 4: The text should scroll smoothly from top to bottom.\n"
                                "Line 5: Or from bottom to top depending on direction.\n"
                                "Line 6: More lines to ensure scrolling works properly.\n"
                                "Line 7: Vertical scrolling requires multi-line text.\n"
                                "Line 8: This line should exceed the widget height.\n"
                                "Line 9: So that scrolling can be tested effectively.\n"
                                "Line 10: Keep adding more lines for better testing.\n"
                                "Line 11: The scroll effect should be smooth.\n"
                                "Line 12: Text should not wrap when scrolling vertically.\n"
                                "Line 13: Each line should move as a complete unit.\n"
                                "Line 14: More content to test vertical scrolling.\n"
                                "Line 15: Final line of the vertical scroll test.";

    // Text for circular scrolling - can be long line or multi-line
    const char *circular_text = "Circular scrolling: This text will wrap around continuously. "
                                "When it reaches the end, it starts again from the beginning. "
                                "This creates a seamless scrolling effect. "
                                "The text should appear to loop infinitely. "
                                "More text for circular scrolling test. "
                                "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 0123456789";

    ESP_LOGI(TAG, "Text widget scroll test: cache=%d, frames=%d", with_cache, frame_count);

    do {
        // Create video render
        ret = create_video_render(30);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to create video render");
            break;
        }
        video_render = get_video_render();
        if (video_render == NULL) {
            ESP_LOGE(TAG, "Failed to get video render");
            break;
        }

        // Get display dimensions
        int display_width = get_backend_width();
        int display_height = get_backend_height();

        // Create stream with overlay only
        esp_video_render_stream_info_t stream_cfg = {
            .info = {
                .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
                .width = display_width,
                .height = display_height,
            }};
        ret = esp_video_render_stream_open(video_render, &stream_cfg, &stream);
        BREAK_ON_FAIL(ret);

        // Set display rect
        esp_video_render_rect_t rect = {
            .x = 0,
            .y = 0,
            .width = display_width,
            .height = display_height,
        };
        ret = esp_video_render_stream_set_disp_rect(stream, &rect);
        BREAK_ON_FAIL(ret);

        // Set overlay only mode
        ret = esp_video_render_stream_render_async(stream);
        BREAK_ON_FAIL(ret);

        esp_vui_overlay_handle_t overlay = NULL;
        ret = esp_video_render_stream_get_overlay(stream, &overlay);
        BREAK_ON_FAIL(ret);

        // Widget size - wider for horizontal, taller for vertical (base: 500x120 at 1024x600)
        int widget_width = SCALE_WIDTH(500, 200);
        int widget_height = SCALE_HEIGHT(120, 80);
        // Ensure widget fits within display
        if (widget_width > display_width) {
            widget_width = display_width;
        }
        if (widget_height > display_height) {
            widget_height = display_height;
        }
        esp_video_render_pos_t widget_pos = {
            .x = (display_width - widget_width) / 2,
            .y = (display_height - widget_height) / 2};

        int container_margin = SCALE_X(10);
        esp_video_render_frame_info_t container_info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = widget_width + container_margin * 2,
            .height = widget_height + container_margin * 2,
        };
        esp_video_render_pos_t container_pos = {
            .x = widget_pos.x - container_margin,
            .y = widget_pos.y - container_margin};

        ret = esp_vui_container_create(overlay, &container_info, &container_pos, with_cache, &container);
        if (ret != ESP_VIDEO_RENDER_ERR_OK || container == NULL) {
            ESP_LOGE(TAG, "Failed to create container");
            break;
        }

        // Adjust widget position relative to container
        esp_video_render_frame_info_t widget_info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = widget_width,
            .height = widget_height,
        };
        esp_video_render_pos_t widget_pos_rel = {
            .x = widget_pos.x - container_pos.x,
            .y = widget_pos.y - container_pos.y};
        text_widget = esp_vui_text_widget_init(container, &widget_info, &widget_pos_rel, widget_width, widget_height);
        if (text_widget == NULL) {
            ESP_LOGE(TAG, "Failed to create text widget");
            break;
        }

        // Scale font size based on display resolution (base: 32 at 1024x600)
        int font_size = SCALE_FONT_SIZE(32, 20);
        ret = set_widget_font(text_widget, "DejaVuSans.ttf", font_size);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to load font");
            break;
        }

        // Set colors
        esp_video_render_clr_t text_color = {.r = 255, .g = 255, .b = 255};  // White
        esp_video_render_clr_t bg_color = {.r = 40, .g = 40, .b = 40};       // Dark gray
        esp_vui_text_widget_set_text_color(text_widget, &text_color);
        esp_vui_text_widget_set_bg_color(text_widget, &bg_color, true);  // Enable background

        // Explicitly set overflow to CLIP to ensure no wrapping (should be automatic when scrolling)
        ret = esp_vui_text_widget_set_overflow(text_widget, 0);  // OVERFLOW_CLIP
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGW(TAG, "Failed to set overflow mode");
        }

        // Start with horizontal scrolling
        int current_scroll_mode = 1;  // 1=horizontal, 2=vertical, 3=circular
        ret = esp_vui_text_widget_set_text(text_widget, horizontal_text);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to set text");
            break;
        }
        ret = esp_vui_text_widget_set_scroll(text_widget, true, current_scroll_mode, 3);  // Horizontal, speed 3
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to set scroll");
            break;
        }
        ESP_LOGI(TAG, "Text widget scroll test created. Starting render loop...");

        vTaskDelay(pdMS_TO_TICKS(100));

        // Render loop
        for (int frame = 0; frame < frame_count; frame++) {
            // Update scroll position
            esp_vui_text_widget_scroll_update(text_widget);

            // Periodically change scroll mode and update text accordingly
            if (frame > 0 && frame % 200 == 0) {
                current_scroll_mode = (current_scroll_mode % 3) + 1;  // Cycle through 1, 2, 3

                // Update text based on scroll mode
                const char *text_to_use = NULL;
                const char *mode_name = NULL;
                if (current_scroll_mode == 1) {
                    text_to_use = horizontal_text;
                    mode_name = "horizontal";
                } else if (current_scroll_mode == 2) {
                    text_to_use = vertical_text;
                    mode_name = "vertical";
                } else {  // circular
                    text_to_use = circular_text;
                    mode_name = "circular";
                }

                // Set new text
                ret = esp_vui_text_widget_set_text(text_widget, text_to_use);
                if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                    ESP_LOGW(TAG, "Failed to set text for mode %d", current_scroll_mode);
                }

                // Set scroll mode (circular mode uses horizontal direction by default)
                ret = esp_vui_text_widget_set_scroll(text_widget, true, current_scroll_mode, 3);
                if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                    ESP_LOGW(TAG, "Failed to set scroll mode %d", current_scroll_mode);
                }

                ESP_LOGI(TAG, "Frame %d/%d: Changed scroll mode to %d (%s) with appropriate text",
                         frame, frame_count, current_scroll_mode, mode_name);
            }

            // Pause/resume scroll periodically
            if (frame > 0 && frame % 20 == 0) {
                static bool paused = false;
                paused = !paused;
                esp_vui_text_widget_scroll_pause(text_widget, paused);
                ESP_LOGI(TAG, "Frame %d/%d: Scroll %s", frame, frame_count, paused ? "paused" : "resumed");
            }

            vTaskDelay(pdMS_TO_TICKS(33));  // ~30 FPS
        }

        success = true;
        ESP_LOGI(TAG, "Text widget scroll test completed successfully");

    } while (0);

    if (container) {
        esp_vui_container_destroy(container);
    }
    if (stream) {
        esp_video_render_stream_close(stream);
    }
    destroy_video_render();

    return success ? 0 : -1;
}

int demo_stream_src_rect_change(int frame_count)
{
    bool success = false;
    esp_video_render_stream_handle_t stream = NULL;
    esp_video_render_img_t img = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = 320,
            .height = 240,
        }};

    do {
        int ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);

        int display_width = get_backend_width();
        int display_height = get_backend_height();

        int disp_width = display_width * 3 / 4;
        int disp_height = display_height * 3 / 4;
        if (disp_width < 80) {
            disp_width = display_width;
        }
        if (disp_height < 60) {
            disp_height = display_height;
        }
        img.info.width = disp_width * 3 / 4;
        img.info.height = disp_height * 3 / 4;
        if (img.info.width < 64) {
            img.info.width = disp_width;
        }
        if (img.info.height < 48) {
            img.info.height = disp_height;
        }

        // Generate a large image with distinct patterns
        ret = gen_image(&img, false, 8);
        BREAK_ON_FAIL(ret);

        // Set background color
        esp_video_render_clr_t bg_clr = {.r = 0x20, .g = 0x20, .b = 0x20};
        ret = esp_video_render_set_bg_color(get_video_render(), &bg_clr);
        BREAK_ON_FAIL(ret);

        esp_video_render_stream_info_t stream_info = {
            .info = img.info,
        };
        // Open stream
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream);
        BREAK_ON_FAIL(ret);

        // Set display position (center of screen)
        int crop_width = img.info.width / 2;
        int crop_height = img.info.height / 2;

        esp_video_render_rect_t disp_rect = {
            .x = (display_width - disp_width) / 2,
            .y = (display_height - disp_height) / 2,
            .width = disp_width,
            .height = disp_height,
        };
        ret = esp_video_render_stream_set_disp_rect(stream, &disp_rect);
        BREAK_ON_FAIL(ret);

        ESP_LOGI(TAG, "Stream src_rect test: frames=%d, source=%dx%d, display=%dx%d",
                 frame_count, img.info.width, img.info.height, crop_width, crop_height);

        // Test different src_rect positions
        int src_rects[][4] = {
            // {x, y, width, height}
            {0, 0, crop_width, crop_height},                                                                    // Top-left
            {img.info.width - crop_width, 0, crop_width, crop_height},                                          // Top-right
            {0, img.info.height - crop_height, crop_width, crop_height},                                        // Bottom-left
            {img.info.width - crop_width, img.info.height - crop_height, crop_width, crop_height},              // Bottom-right
            {(img.info.width - crop_width) / 2, (img.info.height - crop_height) / 2, crop_width, crop_height},  // Center crop
        };
        int num_rects = sizeof(src_rects) / sizeof(src_rects[0]);

        esp_video_render_frame_t frame = {
            .format = img.info.format,
            .width = img.info.width,
            .height = img.info.height,
            .data = img.data,
            .size = img.size,
        };
        int rect_idx = 0;
        for (int i = 0; i < frame_count; i++) {
            // Change src_rect every 10 frames
            if (i > 0 && (i % 3) == 0) {
                esp_video_render_rect_t src_rect = {
                    .x = src_rects[rect_idx][0],
                    .y = src_rects[rect_idx][1],
                    .width = src_rects[rect_idx][2],
                    .height = src_rects[rect_idx][3],
                };
                ret = esp_video_render_stream_set_src_rect(stream, &src_rect);
                BREAK_ON_FAIL(ret);
                ESP_LOGI(TAG, "Frame %d/%d: Changed src_rect to %d-%d %dx%d",
                         i, frame_count, src_rect.x, src_rect.y, src_rect.width, src_rect.height);
                rect_idx = (rect_idx + 1) % num_rects;
            }
            // Write frame to let video process run
            ret = esp_video_render_stream_write(stream, &frame);
            BREAK_ON_FAIL(ret);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        success = true;
    } while (0);

    if (stream) {
        esp_video_render_stream_close(stream);
    }
    if (img.data) {
        free(img.data);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int demo_stream_disp_rect_change(int frame_count)
{
    bool success = false;
    esp_video_render_stream_handle_t stream = NULL;
    esp_video_render_img_t img = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = 160,
            .height = 120,
        }};

    do {
        int ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);

        ret = gen_image(&img, true, 8);
        BREAK_ON_FAIL(ret);

        // Set background color
        esp_video_render_clr_t bg_clr = {.r = 0x30, .g = 0x30, .b = 0x30};
        ret = esp_video_render_set_bg_color(get_video_render(), &bg_clr);
        BREAK_ON_FAIL(ret);

        // Open stream
        esp_video_render_stream_info_t stream_info = {
            .info = img.info,
        };
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream);
        BREAK_ON_FAIL(ret);

        int display_width = get_backend_width();
        int display_height = get_backend_height();

        ESP_LOGI(TAG, "Stream disp_rect test: frames=%d, display=%dx%d",
                 frame_count, display_width, display_height);

        // Test different display positions
        int disp_positions[][2] = {
            // {x, y}
            {0, 0},                                                                              // Top-left
            {display_width - img.info.width, 0},                                                 // Top-right
            {0, display_height - img.info.height},                                               // Bottom-left
            {display_width - img.info.width, display_height - img.info.height},                  // Bottom-right
            {(display_width - img.info.width) / 2, (display_height - img.info.height) / 2},      // Center
            {display_width / 4, display_height / 4},                                             // Quarter
            {display_width * 3 / 4 - img.info.width, display_height * 3 / 4 - img.info.height},  // Three-quarters
        };
        int num_positions = sizeof(disp_positions) / sizeof(disp_positions[0]);
        int pos_idx = 0;
        esp_video_render_frame_t frame = {
            .format = img.info.format,
            .width = img.info.width,
            .height = img.info.height,
            .data = img.data,
            .size = img.size,
        };
        for (int i = 0; i < frame_count; i++) {
            // Change disp_rect every 10 frames
            if (i && (i % 3) == 0) {
                esp_video_render_rect_t disp_rect = {
                    .x = disp_positions[pos_idx][0],
                    .y = disp_positions[pos_idx][1],
                    .width = img.info.width,
                    .height = img.info.height,
                };
                ret = esp_video_render_stream_set_disp_rect(stream, &disp_rect);
                BREAK_ON_FAIL(ret);
                ESP_LOGI(TAG, "Frame %d/%d: Changed disp_rect to %d-%d %dx%d",
                         i, frame_count, disp_rect.x, disp_rect.y, disp_rect.width, disp_rect.height);
                pos_idx = (pos_idx + 1) % num_positions;
            }
            // Write frame
            ret = esp_video_render_stream_write(stream, &frame);
            BREAK_ON_FAIL(ret);

            vTaskDelay(pdMS_TO_TICKS(300));
        }

        success = true;
    } while (0);

    if (stream) {
        esp_video_render_stream_close(stream);
    }
    if (img.data) {
        free(img.data);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int demo_stream_src_disp_rect_change(int frame_count)
{
    bool success = false;
    esp_video_render_stream_handle_t stream = NULL;
    esp_video_render_img_t img = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = 320,
            .height = 240,
        }};
    do {
        int ret = create_video_render(3);
        BREAK_ON_FAIL(ret);

        int display_width = get_backend_width();
        int display_height = get_backend_height();

        int disp_width = display_width * 3 / 4;
        int disp_height = display_height * 3 / 4;
        if (disp_width < 80) {
            disp_width = display_width;
        }
        if (disp_height < 60) {
            disp_height = display_height;
        }
        img.info.width = disp_width * 3 / 4;
        img.info.height = disp_height * 3 / 4;
        if (img.info.width < 64) {
            img.info.width = disp_width;
        }
        if (img.info.height < 48) {
            img.info.height = disp_height;
        }

        ret = gen_image(&img, false, 8);
        BREAK_ON_FAIL(ret);

        // Set background color
        esp_video_render_clr_t bg_clr = {.r = 0x40, .g = 0x40, .b = 0x40};
        ret = esp_video_render_set_bg_color(get_video_render(), &bg_clr);
        BREAK_ON_FAIL(ret);

        // Open stream
        esp_video_render_stream_info_t stream_info = {
            .info = img.info,
        };
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream);
        BREAK_ON_FAIL(ret);

        esp_video_render_frame_t frame = {
            .format = img.info.format,
            .width = img.info.width,
            .height = img.info.height,
            .data = img.data,
            .size = img.size,
        };

        ESP_LOGI(TAG, "Stream src_rect + disp_rect test: frames=%d", frame_count);

        // Test scenarios: zoom in/out by changing src_rect, and move by changing disp_rect
        int crop_sizes[][2] = {
            // {width, height} - different crop sizes
            {img.info.width / 2, img.info.height / 2},          // 2x zoom
            {img.info.width * 2 / 3, img.info.height * 2 / 3},  // 1.5x zoom
            {img.info.width * 3 / 4, img.info.height * 3 / 4},  // 1.33x zoom
            {img.info.width, img.info.height},                  // No zoom (full image)
        };
        int num_crops = sizeof(crop_sizes) / sizeof(crop_sizes[0]);

        for (int i = 0; i < frame_count; i++) {
            int crop_idx = i % num_crops;
            int crop_w = crop_sizes[crop_idx][0];
            int crop_h = crop_sizes[crop_idx][1];

            // Calculate src_rect (center crop from source)
            esp_video_render_rect_t src_rect = {
                .x = (img.info.width - crop_w) / 2,
                .y = (img.info.height - crop_h) / 2,
                .width = crop_w,
                .height = crop_h,
            };

            // Calculate disp_rect (center on display, but vary position slightly)
            int max_x = display_width - disp_width;
            int max_y = display_height - disp_height;
            int offset_x = max_x > 0 ? (i * 5) % (max_x + 1) : 0;
            int offset_y = max_y > 0 ? (i * 5) % (max_y + 1) : 0;
            esp_video_render_rect_t disp_rect = {
                .x = offset_x,
                .y = offset_y,
                .width = disp_width,
                .height = disp_height,
            };

            // Change both rects every 10 frames
            if (i % 10 == 0) {
                ret = esp_video_render_stream_set_src_rect(stream, &src_rect);
                BREAK_ON_FAIL(ret);
                ret = esp_video_render_stream_set_disp_rect(stream, &disp_rect);
                BREAK_ON_FAIL(ret);
                ESP_LOGI(TAG, "Frame %d/%d: src_rect=%d-%d %dx%d, disp_rect=%d-%d %dx%d",
                         i, frame_count,
                         src_rect.x, src_rect.y, src_rect.width, src_rect.height,
                         disp_rect.x, disp_rect.y, disp_rect.width, disp_rect.height);
            }
            // Write frame
            ret = esp_video_render_stream_write(stream, &frame);
            BREAK_ON_FAIL(ret);

            vTaskDelay(pdMS_TO_TICKS(100));
        }

        success = true;
    } while (0);

    if (stream) {
        esp_video_render_stream_close(stream);
    }
    if (img.data) {
        free(img.data);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int demo_dual_stream_rect_change(int frame_count)
{
    bool success = false;
    esp_video_render_stream_handle_t stream_a = NULL;
    esp_video_render_stream_handle_t stream_b = NULL;
    esp_video_render_img_t img_a = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = 320,
            .height = 240,
        }};
    esp_video_render_img_t img_b = img_a;

    do {
        int ret = create_video_render(3);
        BREAK_ON_FAIL(ret);

        int display_width = get_backend_width();
        int display_height = get_backend_height();

        int stream_a_disp_width = display_width * 3 / 4;
        int stream_a_disp_height = display_height * 3 / 4;
        if (stream_a_disp_width < 80) {
            stream_a_disp_width = display_width;
        }
        if (stream_a_disp_height < 60) {
            stream_a_disp_height = display_height;
        }
        img_a.info.width = stream_a_disp_width * 3 / 4;
        img_a.info.height = stream_a_disp_height * 3 / 4;
        if (img_a.info.width < 64) {
            img_a.info.width = stream_a_disp_width;
        }
        if (img_a.info.height < 48) {
            img_a.info.height = stream_a_disp_height;
        }
        img_b.info.width = display_width * 2 / 3;
        img_b.info.height = display_height * 2 / 3;
        if (img_b.info.width < 64) {
            img_b.info.width = display_width;
        }
        if (img_b.info.height < 48) {
            img_b.info.height = display_height;
        }

        ret = gen_image(&img_a, false, 8);
        BREAK_ON_FAIL(ret);
        ret = gen_image(&img_b, true, 8);
        BREAK_ON_FAIL(ret);

        // Set background color
        esp_video_render_clr_t bg_clr = {.r = 0x50, .g = 0x50, .b = 0x50};
        ret = esp_video_render_set_bg_color(get_video_render(), &bg_clr);
        BREAK_ON_FAIL(ret);

        // Open streams
        esp_video_render_stream_info_t stream_info = {
            .info = img_a.info,
        };
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream_a);
        BREAK_ON_FAIL(ret);
        stream_info.info = img_b.info;
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream_b);
        BREAK_ON_FAIL(ret);

        // Create framebuffers
        esp_video_render_frame_t frame_a = {
            .format = img_a.info.format,
            .width = img_a.info.width,
            .height = img_a.info.height,
            .data = img_a.data,
            .size = img_a.size,
        };
        esp_video_render_frame_t frame_b = {
            .format = img_b.info.format,
            .width = img_b.info.width,
            .height = img_b.info.height,
            .data = img_b.data,
            .size = img_b.size,
        };
        // Stream A: Change src_rect (crop different regions)
        // Stream B: Change disp_rect (move around screen)
        int crop_width = img_a.info.width / 2;
        int crop_height = img_a.info.height / 2;
        esp_video_render_rect_t disp_rect_a = {
            .x = (display_width - stream_a_disp_width) / 2,
            .y = (display_height - stream_a_disp_height) / 2,
            .width = stream_a_disp_width,
            .height = stream_a_disp_height,
        };
        esp_video_render_rect_t src_rect_b = {
            .x = 0,
            .y = 0,
            .width = img_b.info.width,
            .height = img_b.info.height,
        };
        int change_idx = 0;
        for (int i = 0; i < frame_count; i++) {
            if (i % 3 == 0) {
                int src_x = (change_idx % 4) * (img_a.info.width - crop_width) / 3;
                int src_y = (change_idx % 3) * (img_a.info.height - crop_height) / 2;
                esp_video_render_rect_t src_rect_a = {
                    .x = src_x,
                    .y = src_y,
                    .width = crop_width,
                    .height = crop_height,
                };
                ret = esp_video_render_stream_set_src_rect(stream_a, &src_rect_a);
                BREAK_ON_FAIL(ret);
                ret = esp_video_render_stream_set_disp_rect(stream_a, &disp_rect_a);
                BREAK_ON_FAIL(ret);

                int disp_x = (change_idx % 5) * (display_width - img_b.info.width) / 4;
                int disp_y = (change_idx % 4) * (display_height - img_b.info.height) / 3;
                esp_video_render_rect_t disp_rect_b = {
                    .x = disp_x,
                    .y = disp_y,
                    .width = img_b.info.width,
                    .height = img_b.info.height,
                };
                ret = esp_video_render_stream_set_src_rect(stream_b, &src_rect_b);
                BREAK_ON_FAIL(ret);
                if (disp_x + img_b.info.width > display_width) {
                    disp_rect_b.x = display_width - img_b.info.width;
                }
                if (disp_y + img_b.info.height > display_height) {
                    disp_rect_b.y = display_height - img_b.info.height;
                }
                ret = esp_video_render_stream_set_disp_rect(stream_b, &disp_rect_b);
                BREAK_ON_FAIL(ret);

                ESP_LOGI(TAG, "Frame %d/%d: Stream A src=%d-%d %dx%d disp=%d-%d %dx%d, Stream B src=%d-%d %dx%d disp=%d-%d %dx%d",
                         i, frame_count,
                         src_rect_a.x, src_rect_a.y, src_rect_a.width, src_rect_a.height,
                         disp_rect_a.x, disp_rect_a.y, disp_rect_a.width, disp_rect_a.height,
                         src_rect_b.x, src_rect_b.y, src_rect_b.width, src_rect_b.height,
                         disp_rect_b.x, disp_rect_b.y, disp_rect_b.width, disp_rect_b.height);
                change_idx++;
            }

            // Write frames
            ret = esp_video_render_stream_write(stream_a, &frame_a);
            BREAK_ON_FAIL(ret);
            ret = esp_video_render_stream_write(stream_b, &frame_b);
            BREAK_ON_FAIL(ret);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        success = true;
    } while (0);

    if (stream_a) {
        esp_video_render_stream_close(stream_a);
    }
    if (stream_b) {
        esp_video_render_stream_close(stream_b);
    }
    if (img_a.data) {
        free(img_a.data);
    }
    if (img_b.data) {
        free(img_b.data);
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int demo_stream_zorder_test(int frame_count)
{
    bool success = false;
    esp_video_render_stream_handle_t streams[4] = {NULL};
    esp_vui_overlay_handle_t overlays[4] = {NULL};
    esp_vui_container_handle_t containers[4] = {NULL};
    esp_video_render_img_t circle_imgs[4] = {0};

    do {
        int ret = create_video_render(3);
        BREAK_ON_FAIL(ret);

        int display_width = get_backend_width();
        int display_height = get_backend_height();

        // Set background color
        esp_video_render_clr_t bg_clr = {.r = 0x20, .g = 0x20, .b = 0x20};
        ret = esp_video_render_set_bg_color(get_video_render(), &bg_clr);
        BREAK_ON_FAIL(ret);

        // Circle colors for each stream
        esp_video_render_clr_t circle_colors[4] = {
            {.r = 0xFF, .g = 0x00, .b = 0x00},  // Red
            {.r = 0x00, .g = 0xFF, .b = 0x00},  // Green
            {.r = 0x00, .g = 0x00, .b = 0xFF},  // Blue
            {.r = 0xFF, .g = 0xFF, .b = 0x00},  // Yellow
        };

        // Circle positions (Audi-style: four circles in a horizontal row with overlap)
        // Scale circle size (base: 80x80 at 1024x600)
        int circle_size = SCALE_WIDTH(80, 40);
        // Ensure circles fit on screen - if too large, reduce size
        int min_total_width = circle_size * 4 - (circle_size / 3) * 3;
        if (min_total_width > display_width) {
            circle_size = (display_width + 9) / 13;  // Approximate: 4*size - 3*(size/3) = 13*size/3
            if (circle_size < 20) {
                circle_size = 20;  // Minimum size
            }
        }
        int circle_radius = circle_size / 2 - 2;
        int overlap_amount = circle_size / 3;                        // Overlap amount between adjacent circles
        int total_width = (circle_size * 4) - (overlap_amount * 3);  // Total width of 4 overlapping circles
        int start_x = (display_width - total_width) / 2;             // Center horizontally
        int center_y = display_height / 2;

        // Calculate positions for horizontal row (left to right)
        esp_video_render_pos_t circle_positions[4] = {
            {.x = start_x, .y = center_y - circle_size / 2},                                       // Leftmost circle
            {.x = start_x + circle_size - overlap_amount, .y = center_y - circle_size / 2},        // Second circle
            {.x = start_x + (circle_size - overlap_amount) * 2, .y = center_y - circle_size / 2},  // Third circle
            {.x = start_x + (circle_size - overlap_amount) * 3, .y = center_y - circle_size / 2},  // Rightmost circle
        };

        // Initialize circle images
        for (int i = 0; i < 4; i++) {
            circle_imgs[i].info = (esp_video_render_frame_info_t) {
                .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
                .width = circle_size,
                .height = circle_size,
            };
            circle_imgs[i].data = NULL;
            circle_imgs[i].size = 0;  // Initialize size to ensure buffer allocation
            ret = gen_circle_image(&circle_imgs[i], &bg_clr, &circle_colors[i], circle_radius);
            BREAK_ON_FAIL(ret);
        }

        // Create streams and overlays
        esp_video_render_stream_info_t stream_info = {
            .info = {
                .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
                .width = display_width,
                .height = display_height,
            }};

        for (int i = 0; i < 4; i++) {
            ret = esp_video_render_stream_open(get_video_render(), &stream_info, &streams[i]);
            BREAK_ON_FAIL(ret);

            ret = esp_video_render_stream_render_async(streams[i]);
            BREAK_ON_FAIL(ret);

            ret = esp_video_render_stream_get_overlay(streams[i], &overlays[i]);
            BREAK_ON_FAIL(ret);

            // Set initial zorder (0, 1, 2, 3)
            ret = esp_video_render_stream_set_zorder(streams[i], i);
            BREAK_ON_FAIL(ret);
            esp_video_render_rect_t disp_rect = {
                .x = circle_positions[i].x,
                .y = circle_positions[i].y,
                .width = circle_size,
                .height = circle_size,
            };
            ret = esp_video_render_stream_set_disp_rect(streams[i], &disp_rect);
            BREAK_ON_FAIL(ret);

            // Create container for circle
            esp_video_render_frame_info_t container_info = {
                .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
                .width = circle_size,
                .height = circle_size,
            };
            ret = esp_vui_container_create(overlays[i], &container_info, &circle_positions[i], true, &containers[i]);
            BREAK_ON_FAIL(ret);

            esp_video_render_pos_t widget_pos = {.x = 0, .y = 0};
            esp_vui_widget_t *w = esp_vui_image_widget_init(containers[i], &circle_imgs[i], &widget_pos);
            if (w == NULL) {
                ret = ESP_VIDEO_RENDER_ERR_NO_MEM;
                BREAK_ON_FAIL(ret);
            }
        }

        ESP_LOGI(TAG, "Zorder test: 4 streams with circles, frames=%d", frame_count);
        esp_video_render_measure_enable(true);
        // Test zorder changes in a loop
        for (int i = 0; i < frame_count; i++) {
            // Cycle through different zorder arrangements
            // Pattern: rotate zorders so each stream gets a chance to be on top
            uint8_t zorders[4];
            for (int j = 0; j < 4; j++) {
                zorders[j] = (i + j) % 4;
            }

            // Set new zorders
            for (int j = 0; j < 4; j++) {
                ret = esp_video_render_stream_set_zorder(streams[j], zorders[j]);
                BREAK_ON_FAIL(ret);
            }

            ESP_LOGI(TAG, "Frame %d/%d: zorders = [%d, %d, %d, %d]",
                     i, frame_count, zorders[0], zorders[1], zorders[2], zorders[3]);

            vTaskDelay(pdMS_TO_TICKS(200));
        }
        esp_video_render_measure_enable(false);

        success = true;
    } while (0);

    // Cleanup
    for (int i = 0; i < 4; i++) {
        if (streams[i]) {
            esp_video_render_stream_close(streams[i]);
        }
        if (circle_imgs[i].data) {
            free(circle_imgs[i].data);
        }
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int demo_fullscreen_stream_with_overlay_widget(int frame_count)
{
    bool success = false;
    esp_video_render_stream_handle_t stream = NULL;
    esp_vui_overlay_handle_t overlay = NULL;
    esp_vui_container_handle_t container = NULL;
    esp_vui_widget_t *text_widget = NULL;
    esp_video_render_img_t images[3] = {};
    do {
        int ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);

        int display_width = get_backend_width();
        int display_height = get_backend_height();
        esp_video_render_clr_t colors[3] = {
            {.r = 0x30, .g = 0x60, .b = 0x80},  // Blue-green 1
            {.r = 0x40, .g = 0x70, .b = 0x90},  // Blue-green 2 (slightly brighter)
            {.r = 0x35, .g = 0x65, .b = 0x85},  // Blue-green 3 (slightly different hue)
        };
        for (int i = 0; i < 3; i++) {
            images[i] = (esp_video_render_img_t) {
                .info = {
                    .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
                    .width = display_width,
                    .height = display_height,
                },
            };
            ret = gen_image(&images[i], false, 1);
            BREAK_ON_FAIL(ret)
            ESP_LOGI(TAG, "Generated image %d: RGB(%02x, %02x, %02x) %p %d",
                     i, colors[i].r, colors[i].g, colors[i].b, images[i].data, (int)images[i].size);
        }

        // Create full screen stream
        esp_video_render_stream_info_t stream_info = {
            .info = {
                .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
                .width = display_width,
                .height = display_height,
            }};
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream);
        BREAK_ON_FAIL(ret);

        // Set full screen display rect
        esp_video_render_rect_t disp_rect = {
            .x = 0,
            .y = 0,
            .width = display_width,
            .height = display_height,
        };
        ret = esp_video_render_stream_set_disp_rect(stream, &disp_rect);
        BREAK_ON_FAIL(ret);

        // Get overlay
        ret = esp_video_render_stream_get_overlay(stream, &overlay);
        BREAK_ON_FAIL(ret);

        // Create container for text widget (centered, scaled size)
        int widget_width = SCALE_WIDTH(300, 200);
        int widget_height = SCALE_HEIGHT(100, 60);
        if (widget_width > display_width) {
            widget_width = display_width;
        }
        if (widget_height > display_height) {
            widget_height = display_height;
        }

        esp_video_render_frame_info_t container_info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = widget_width + SCALE_X(20),
            .height = widget_height + SCALE_Y(20),
        };
        esp_video_render_pos_t container_pos = {
            .x = (display_width - container_info.width) / 2,
            .y = (display_height - container_info.height) / 2,
        };

        ret = esp_vui_container_create(overlay, &container_info, &container_pos, true, &container);
        BREAK_ON_FAIL(ret);

        // Create text widget inside container
        esp_video_render_frame_info_t widget_info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = widget_width,
            .height = widget_height,
        };
        esp_video_render_pos_t widget_pos = {.x = SCALE_X(10), .y = SCALE_Y(10)};

        text_widget = esp_vui_text_widget_init(container, &widget_info, &widget_pos,
                                               widget_width, widget_height);
        if (text_widget == NULL) {
            ESP_LOGE(TAG, "Failed to create text widget");
            break;
        }

        // Scale font size
        int font_size = SCALE_FONT_SIZE(32, 20);
        ret = set_widget_font(text_widget, "DejaVuSans.ttf", font_size);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to load font");
            break;
        }

        // Set text colors (white text on dark background)
        esp_video_render_clr_t text_color = {.r = 255, .g = 255, .b = 255};  // White
        esp_video_render_clr_t bg_color = {.r = 0, .g = 0, .b = 0};          // Black background
        esp_vui_text_widget_set_text_color(text_widget, &text_color);
        esp_vui_text_widget_set_bg_color(text_widget, &bg_color, false);  // Enable background
        esp_vui_text_widget_set_align(text_widget, 1, 1);                 // Center, Middle

        ESP_LOGI(TAG, "Full screen stream with overlay widget test: frames=%d, display=%dx%d",
                 frame_count, display_width, display_height);

        // Main loop: cycle through images
        esp_video_render_frame_t frame_data = {
            .format = images[0].info.format,
            .width = images[0].info.width,
            .height = images[0].info.height,

        };

        for (int frame = 0; frame < frame_count; frame++) {
            int image_idx = frame % 3;
            // Update text to show current image number
            char text_buf[64];
            snprintf(text_buf, sizeof(text_buf), "Image %d/3\nRGB(%02x,%02x,%02x)",
                     image_idx + 1, colors[image_idx].r, colors[image_idx].g, colors[image_idx].b);
            ret = esp_vui_text_widget_set_text(text_widget, text_buf);
            if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                ESP_LOGW(TAG, "Failed to update text at frame %d", frame);
            }
            frame_data.data = images[image_idx].data;
            frame_data.size = images[image_idx].size;
            ret = esp_video_render_stream_write(stream, &frame_data);
            BREAK_ON_FAIL(ret);
            if (frame % 10 == 0) {
                ESP_LOGI(TAG, "Frame %d/%d: Displaying image %d", frame, frame_count, image_idx + 1);
            }

            vTaskDelay(pdMS_TO_TICKS(200));  // 500ms per frame for visibility
        }

        success = true;
    } while (0);

    // Cleanup
    if (text_widget) {
        esp_vui_widget_destroy(text_widget);
    }
    if (container) {
        esp_vui_container_destroy(container);
    }
    if (stream) {
        esp_video_render_stream_close(stream);
    }
    for (int i = 0; i < 3; i++) {
        if (images[i].data) {
            free(images[i].data);
        }
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int demo_dual_stream_with_overlay(int frame_count)
{
    bool success = false;
    esp_video_render_stream_handle_t stream = NULL;
    esp_video_render_stream_handle_t overlay_stream = NULL;
    esp_vui_overlay_handle_t overlay = NULL;
    esp_vui_container_handle_t container = NULL;
    esp_vui_widget_t *text_widget = NULL;
    esp_video_render_img_t images[3] = {};
    do {
        int ret = create_video_render(TEST_VIDEO_RENDER_FPS);
        BREAK_ON_FAIL(ret);

        int display_width = get_backend_width();
        int display_height = get_backend_height();
        esp_video_render_clr_t colors[3] = {
            {.r = 0x30, .g = 0x60, .b = 0x80},  // Blue-green 1
            {.r = 0x40, .g = 0x70, .b = 0x90},  // Blue-green 2 (slightly brighter)
            {.r = 0x35, .g = 0x65, .b = 0x85},  // Blue-green 3 (slightly different hue)
        };
        for (int i = 0; i < 3; i++) {
            images[i] = (esp_video_render_img_t) {
                .info = {
                    .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
                    .width = display_width,
                    .height = display_height,
                },
            };
            ret = gen_image(&images[i], false, 1);
            BREAK_ON_FAIL(ret)
            ESP_LOGI(TAG, "Generated image %d: RGB(%02x, %02x, %02x) %p %d",
                     i, colors[i].r, colors[i].g, colors[i].b, images[i].data, (int)images[i].size);
        }

        // Create full screen stream
        esp_video_render_stream_info_t stream_info = {
            .info = {
                .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
                .width = display_width,
                .height = display_height,
            }};
        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &stream);
        BREAK_ON_FAIL(ret);

        ret = esp_video_render_stream_open(get_video_render(), &stream_info, &overlay_stream);
        BREAK_ON_FAIL(ret);

        // Set full screen display rect
        esp_video_render_rect_t disp_rect = {
            .x = 0,
            .y = 0,
            .width = display_width,
            .height = display_height,
        };
        ret = esp_video_render_stream_set_disp_rect(stream, &disp_rect);
        BREAK_ON_FAIL(ret);

        // Get overlay
        ret = esp_video_render_stream_get_overlay(overlay_stream, &overlay);
        BREAK_ON_FAIL(ret);

        // Create container for text widget (centered, scaled size)
        int widget_width = SCALE_WIDTH(300, 200);
        int widget_height = SCALE_HEIGHT(100, 60);
        if (widget_width > display_width) {
            widget_width = display_width;
        }
        if (widget_height > display_height) {
            widget_height = display_height;
        }

        esp_video_render_frame_info_t container_info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = widget_width + SCALE_X(20),
            .height = widget_height + SCALE_Y(20),
        };
        esp_video_render_pos_t container_pos = {
            .x = (display_width - container_info.width) / 2,
            .y = (display_height - container_info.height) / 2,
        };

        ret = esp_vui_container_create(overlay, &container_info, &container_pos, true, &container);
        BREAK_ON_FAIL(ret);

        // Create text widget inside container
        esp_video_render_frame_info_t widget_info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = widget_width,
            .height = widget_height,
        };
        esp_video_render_pos_t widget_pos = {.x = SCALE_X(10), .y = SCALE_Y(10)};

        text_widget = esp_vui_text_widget_init(container, &widget_info, &widget_pos,
                                               widget_width, widget_height);
        if (text_widget == NULL) {
            ESP_LOGE(TAG, "Failed to create text widget");
            break;
        }

        // Scale font size
        int font_size = SCALE_FONT_SIZE(32, 20);
        ret = set_widget_font(text_widget, "DejaVuSans.ttf", font_size);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to load font");
            break;
        }

        // Set text colors (white text on dark background)
        esp_video_render_clr_t text_color = {.r = 255, .g = 255, .b = 255};  // White
        esp_video_render_clr_t bg_color = {.r = 0, .g = 0, .b = 0};          // Black background
        esp_vui_text_widget_set_text_color(text_widget, &text_color);
        esp_vui_text_widget_set_bg_color(text_widget, &bg_color, false);  // Enable background
        esp_vui_text_widget_set_align(text_widget, 1, 1);                 // Center, Middle

        ESP_LOGI(TAG, "Full screen stream with overlay widget test: frames=%d, display=%dx%d",
                 frame_count, display_width, display_height);

        // Main loop: cycle through images
        esp_video_render_frame_t frame_data = {
            .format = images[0].info.format,
            .width = images[0].info.width,
            .height = images[0].info.height,

        };

        for (int frame = 0; frame < frame_count; frame++) {
            int font_idx = frame % 3;
            // Update text to show current image number
            char text_buf[64];
            snprintf(text_buf, sizeof(text_buf), "Image %d/3\nRGB(%02x,%02x,%02x)",
                     font_idx + 1, colors[font_idx].r, colors[font_idx].g, colors[font_idx].b);
            ret = esp_vui_text_widget_set_text(text_widget, text_buf);
            if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                ESP_LOGW(TAG, "Failed to update text at frame %d", frame);
            }
            if ((frame % 2) == 0) {
                int image_idx = (frame / 2) % 3;
                frame_data.data = images[image_idx].data;
                frame_data.size = images[image_idx].size;
                ret = esp_video_render_stream_write(stream, &frame_data);
                BREAK_ON_FAIL(ret);
            }
            vTaskDelay(pdMS_TO_TICKS(200));  // 500ms per frame for visibility
        }
        success = true;
    } while (0);

    // Cleanup
    if (text_widget) {
        esp_vui_widget_destroy(text_widget);
    }
    if (container) {
        esp_vui_container_destroy(container);
    }
    if (stream) {
        esp_video_render_stream_close(stream);
    }
    for (int i = 0; i < 3; i++) {
        if (images[i].data) {
            free(images[i].data);
        }
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int demo_xiaozhi_panel(bool with_cache, int frame_count)
{
    bool success = false;
    esp_video_render_stream_handle_t stream = NULL;
    esp_vui_overlay_handle_t overlay = NULL;
    void *panel_handle = NULL;

    do {
        int ret = create_video_render(20);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to create video render");
            break;
        }
        int display_width = get_backend_width();
        int display_height = get_backend_height();

        esp_video_render_handle_t render = get_video_render();
        if (render == NULL) {
            ESP_LOGE(TAG, "Failed to get video render");
            break;
        }

        // Set display background color (black) - makes panel boundaries easy to see
        esp_video_render_clr_t bg_clr = {.r = 0, .g = 0, .b = 0};
        ret = esp_video_render_set_bg_color(render, &bg_clr);
        BREAK_ON_FAIL(ret);

        // Create overlay-only stream
        esp_video_render_stream_info_t stream_info = {
            .info = {
                .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
                .width = display_width,
                .height = display_height}};
        ret = esp_video_render_stream_open(render, &stream_info, &stream);
        BREAK_ON_FAIL(ret);

        ret = esp_video_render_stream_render_async(stream);
        BREAK_ON_FAIL(ret);

        ret = esp_video_render_stream_get_overlay(stream, &overlay);
        BREAK_ON_FAIL(ret);

        // Create xiaozhi panel
        int panel_width = SCALE_WIDTH(400, 320);
        int panel_height = SCALE_HEIGHT(500, 240);
        int panel_x = (display_width - panel_width) / 2;
        int panel_y = (display_height - panel_height) / 2;

        esp_video_render_frame_info_t panel_info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = panel_width,
            .height = panel_height};
        esp_video_render_pos_t panel_pos = {
            .x = panel_x,
            .y = panel_y};

        ret = xiaozhi_panel_create(overlay, &panel_info, &panel_pos, with_cache, &panel_handle);
        BREAK_ON_FAIL(ret);

        ESP_LOGI(TAG, "Xiaozhi panel test: cache=%d, frames=%d", with_cache, frame_count);

        // Test dynamic updates
        char text_lines[] = "xiaozhi.me\nYour ID";
        ret = xiaozhi_panel_set_text_lines(panel_handle, text_lines);
        BREAK_ON_FAIL(ret);

        vTaskDelay(pdMS_TO_TICKS(500));

        // Simulate dynamic updates
        for (int frame = 0; frame < frame_count; frame++) {
            // Update status every 20 frames
            if ((frame % 20 == 0)) {
                int wifi_strength = (frame / 20) % 4;
                int battery_level = 4 - ((frame / 20) % 5);
                const char *status_texts[] = {"Registered", "Connected", "Running"};
                const char *status_text = status_texts[(frame / 20) % 3];

                ret = xiaozhi_panel_set_status(panel_handle, status_text,
                                               (wifi_strength_t)wifi_strength, (battery_level_t)battery_level);
                if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                    ESP_LOGW(TAG, "Failed to update status: %d", ret);
                }
            }

            // Toggle speaking indicator every 10 frames
            if ((frame % 10 == 0)) {
                bool is_speaking = (frame / 10) % 2 == 1;
                ret = xiaozhi_panel_set_speaking(panel_handle, is_speaking);
                if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                    ESP_LOGW(TAG, "Failed to update speaking: %d", ret);
                }
            }

            if (frame % 2 == 0) {
                static const char *const emojis[] = {
                    // Keep this list aligned with `NotoColorEmojiBitmap-Subset.ttf` contents.
                    // We only keep single-codepoint emojis (no ZWJ sequences, no VS16).
                    "😀", "😁", "😂", "🤣", "😃", "😄", "😅", "😆", "😉", "😊", "😋", "😎", "😍", "😘", "😗", "😙", "😚", "🙂", "🤗", "🤩",
                    "🤔", "🤨", "😐", "😑", "😶", "🙄", "😏", "😣", "😥", "😮", "🤐", "😯", "😪", "😫", "😴", "😌", "😛", "😜", "😝", "🤤",
                    "😒", "😓", "😔", "😕", "🙃", "🫠", "🫢", "🫣", "🫡", "🤫", "🤭", "🫥", "😳", "🥺", "😦", "😧", "😨", "😰", "😱", "😵",
                    "🤯", "😠", "😡", "🤬", "😷", "🤒", "🤕", "🤢", "🤮", "🤧", "🥵", "🥶", "🥴", "😇", "🤠", "🥳", "🥸", "😈", "👿", "💀",
                    "👻", "👽", "👾", "🤖", "🎃", "😺", "😸", "😹", "😻", "😼", "😽", "🙀", "😿", "😾", "👋", "🤚", "✋", "🖖", "👌", "🤌",
                    "🤏", "🤞", "🫰", "🤟", "🤘", "🤙", "👈", "👉", "👆", "🖕", "👇", "👍", "👎", "✊", "👊", "🤛", "🤜", "👏", "🙌", "👐",
                };
                const size_t emoji_count = sizeof(emojis) / sizeof(emojis[0]);
                const char *emoji = emojis[(frame / 2) % emoji_count];

                ret = xiaozhi_panel_set_emoji(panel_handle, emoji);
                if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                    ESP_LOGW(TAG, "Failed to update emoji: %d", ret);
                }
            }

            // Update text lines every 40 frames
            if ((frame % 3 == 0)) {
                char new_lines[128];
                snprintf(new_lines, sizeof(new_lines), "Simulation\nFrame: %d\nStatus: OK", frame);
                ret = xiaozhi_panel_set_text_lines(panel_handle, new_lines);
                if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                    ESP_LOGW(TAG, "Failed to update text: %d", ret);
                }
            }
            if (frame % 50 == 0) {
                ESP_LOGI(TAG, "Xiaozhi panel frame %d/%d", frame, frame_count);
            }

            vTaskDelay(pdMS_TO_TICKS(50));
        }

        success = true;
    } while (0);

    // Cleanup
    if (panel_handle) {
        xiaozhi_panel_destroy(panel_handle);
    }
    if (stream) {
        esp_video_render_stream_close(stream);
    }
    destroy_video_render();
    return success ? 0 : -1;
}
