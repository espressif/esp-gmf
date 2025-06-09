
/**
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "esp_gmf_video_ppa.h"
#include "esp_gmf_video_enc.h"
#include "esp_gmf_video_dec.h"
#include "esp_gmf_video_fps_cvt.h"
#include "esp_gmf_video_overlay.h"
#include "esp_gmf_pool.h"
#include "esp_video_enc_default.h"
#include "esp_video_dec_default.h"
#include "esp_video_codec_utils.h"
#include "gmf_video_pattern.h"
#include "esp_gmf_video_param.h"
#include "esp_gmf_caps_def.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_gmf_video_scale.h"
#include "esp_gmf_video_crop.h"
#include "esp_gmf_video_rotate.h"
#include "esp_gmf_video_color_convert.h"
#include "gmf_loader_setup_defaults.h"

#define TAG "VID_EL_TEST"

#if CONFIG_IDF_TARGET_ESP32P4
#define TEST_PATTERN_WIDTH  (1280)
#define TEST_PATTERN_HEIGHT (720)
#else
#define TEST_PATTERN_WIDTH  (320)
#define TEST_PATTERN_HEIGHT (240)
#endif
#define TEST_PATTERN_VERTICAL  (false)
#define TEST_PATTERN_BAR_COUNT (8)
#define TEST_VIDEO_ALIGNMENT   (128)

#define SAFE_FREE(ptr) if (ptr) {  \
    esp_gmf_oal_free(ptr);         \
    ptr = NULL;                    \
}

#define ELEMS(arr)              (sizeof(arr) / sizeof((arr)[0]))
#define VIDEO_EL_MAX_STACK_SIZE (40 * 1024)

typedef struct {
    // Src information
    uint32_t                    in_frame_count;
    uint32_t                    src_codec;
    esp_gmf_video_resolution_t  src_res;
    uint8_t                    *src_pixel;
    uint32_t                    src_size;
    uint8_t                    *src_copy;

    // Overlay information
    uint8_t                    *overlay_data;
    uint32_t                    overlay_size;
    esp_gmf_video_resolution_t  overlay_res;

    // Out information
    uint16_t                    rotate_degree;
    uint32_t                    out_codec;
    esp_gmf_video_resolution_t  out_res;
    uint8_t                    *out_pixel;
    uint32_t                    out_size;
    uint32_t                    out_frame_count;
    uint32_t                    out_max_size;
    bool                        no_need_free;
} video_el_test_t;

typedef struct {
    esp_gmf_pool_handle_t      pool;
    esp_gmf_pipeline_handle_t  pipe;
    esp_gmf_task_handle_t      task;
    esp_gmf_element_handle_t   convert_hd;
    esp_gmf_element_handle_t   enc_hd;
    esp_gmf_element_handle_t   rate_hd;
    esp_gmf_element_handle_t   overlay_hd;
    esp_gmf_element_handle_t   dec_hd;
} convert_res_t;

static video_el_test_t video_el_inst;

static esp_gmf_element_handle_t get_element_by_caps_from_pool(esp_gmf_pool_handle_t pool, uint64_t caps_cc)
{
    const void *iter = NULL;
    esp_gmf_element_handle_t element = NULL;
    while (esp_gmf_pool_iterate_element(pool, &iter, &element) == ESP_GMF_ERR_OK) {
        const esp_gmf_cap_t *caps = NULL;
        esp_gmf_element_get_caps(element, &caps);
        while (caps) {
            if (caps->cap_eightcc == caps_cc) {
                return element;
            }
            caps = caps->next;
        }
    }
    return NULL;
}

static esp_gmf_element_handle_t get_element_by_caps_from_pipe(esp_gmf_pipeline_handle_t pipe, uint64_t caps_cc)
{
    const esp_gmf_cap_t *caps = NULL;
    esp_gmf_element_handle_t element = NULL;
    esp_gmf_pipeline_get_head_el(pipe, &element);

    for (; element; esp_gmf_pipeline_get_next_el(pipe, element, &element)) {
        esp_gmf_element_get_caps(element, &caps);
        for (; caps; caps = caps->next) {
            if (caps->cap_eightcc == caps_cc) {
                return element;
            }
        }
    }
    return NULL;
}

static void get_pattern_info(pattern_info_t *info, bool is_out)
{
    if (is_out == false) {
        info->format_id = video_el_inst.src_codec;
        info->res = video_el_inst.src_res;
        info->pixel = video_el_inst.src_pixel;
        info->data_size = video_el_inst.src_size;
        info->bar_count = TEST_PATTERN_BAR_COUNT;
        info->vertical = TEST_PATTERN_VERTICAL;
    } else {
        info->format_id = video_el_inst.out_codec;
        info->res = video_el_inst.out_res;
        info->pixel = video_el_inst.out_pixel;
        info->data_size = video_el_inst.out_size;
        info->bar_count = TEST_PATTERN_BAR_COUNT;
        if (video_el_inst.rotate_degree == 90 || video_el_inst.rotate_degree == 270) {
            info->vertical = !TEST_PATTERN_VERTICAL;
        }
    }
}

static int allocate_src_pattern(uint32_t src_codec, bool copy)
{
    video_el_inst.src_res.width = TEST_PATTERN_WIDTH;
    video_el_inst.src_res.height = TEST_PATTERN_HEIGHT;
    video_el_inst.src_codec = src_codec;
    esp_video_codec_resolution_t res = {
        .width = video_el_inst.src_res.width,
        .height = video_el_inst.src_res.height,
    };
    video_el_inst.src_size = esp_video_codec_get_image_size((esp_video_codec_pixel_fmt_t)src_codec, &res);
    video_el_inst.src_pixel = esp_gmf_oal_malloc_align(TEST_VIDEO_ALIGNMENT, video_el_inst.src_size);
    if (video_el_inst.src_pixel == NULL) {
        return -1;
    }
    pattern_info_t pattern_info = {};
    get_pattern_info(&pattern_info, false);
    gen_pattern_color_bar(&pattern_info);
    return 0;
}

static void show_result_pattern(void)
{
    pattern_info_t src_info = {};
    pattern_info_t out_info = {};
    get_pattern_info(&src_info, false);
    get_pattern_info(&out_info, true);
    draw_convert_result(&src_info, &out_info);
}

static void free_video_el_inst(void)
{
    SAFE_FREE(video_el_inst.src_pixel);
    SAFE_FREE(video_el_inst.overlay_data);
    if (video_el_inst.no_need_free == false) {
        SAFE_FREE(video_el_inst.out_pixel);
    }
    video_el_inst.out_max_size = 0;
    video_el_inst.out_pixel = NULL;
}

static esp_gmf_err_io_t in_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    load->pts = video_el_inst.in_frame_count * 100;
    load->buf = video_el_inst.src_pixel;
    load->valid_size = video_el_inst.src_size;
    load->buf_length = load->valid_size;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t in_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    video_el_inst.in_frame_count++;
    vTaskDelay(10 / portTICK_RATE_MS);
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t overlay_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    load->pts = 255;
    load->buf = video_el_inst.overlay_data;
    load->valid_size = video_el_inst.overlay_size;
    load->buf_length = video_el_inst.overlay_size;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t overlay_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t out_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    if (load->buf) {
        video_el_inst.no_need_free = true;
    } else {
        if (wanted_size > video_el_inst.out_max_size) {
            SAFE_FREE(video_el_inst.out_pixel);
            uint8_t *new_buf = esp_gmf_oal_malloc_align(TEST_VIDEO_ALIGNMENT, wanted_size + TEST_VIDEO_ALIGNMENT);
            if (new_buf == NULL) {
                ESP_LOGE(TAG, "Fail to allocate %d bytes for output buffer", (int)wanted_size + TEST_VIDEO_ALIGNMENT);
                return -1;
            }
            video_el_inst.out_pixel = new_buf;
            video_el_inst.out_max_size = wanted_size;
        }
        load->buf = video_el_inst.out_pixel;
        load->buf_length = video_el_inst.out_max_size;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t out_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    video_el_inst.out_pixel = load->buf;
    video_el_inst.out_size = load->valid_size;
    ESP_LOGI(TAG, "Out frame %d size %d", (int)video_el_inst.out_frame_count, (int)video_el_inst.out_size);
    video_el_inst.out_frame_count++;
    // FIXME: why add this ugly code
    if (video_el_inst.no_need_free == false) {
        load->buf = NULL;
    }
    return ESP_GMF_IO_OK;
}

static int prepare_pool(convert_res_t *res)
{
    memset(res, 0, sizeof(*res));
    esp_gmf_pool_init(&res->pool);
    if (res->pool == NULL) {
        return -1;
    }
    gmf_loader_setup_video_codec_default(res->pool);
    gmf_loader_setup_video_effects_default(res->pool);
    ESP_GMF_POOL_SHOW_ITEMS(res->pool);
    return 0;
}

static int prepare_convert_pipeline(convert_res_t *res, const char **elements)
{
    int n = 0;
    const char **header = elements;
    while (*header) {
        n++;
        header++;
    }
    esp_gmf_pool_new_pipeline(res->pool, NULL, elements, n, NULL, &res->pipe);
    if (res->pipe == NULL) {
        return -1;
    }

    // Set input and output buffer
    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BLOCK(in_acquire, in_release, NULL, NULL, 0, ESP_GMF_MAX_DELAY);
    esp_gmf_pipeline_reg_el_port(res->pipe, elements[0], ESP_GMF_IO_DIR_READER, in_port);
    // Use byte block to avoid malloc again
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BLOCK(out_acquire, out_release, NULL, NULL, 0, ESP_GMF_MAX_DELAY);
    esp_gmf_pipeline_reg_el_port(res->pipe, elements[n - 1], ESP_GMF_IO_DIR_WRITER, out_port);

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.thread.stack_in_ext = true;
    cfg.thread.stack = VIDEO_EL_MAX_STACK_SIZE;
    esp_gmf_task_init(&cfg, &res->task);
    if (res->task == NULL) {
        return -1;
    }

    esp_gmf_pipeline_bind_task(res->pipe, res->task);
    esp_gmf_pipeline_loading_jobs(res->pipe);

    esp_gmf_pipeline_get_el_by_name(res->pipe, "vid_ppa", &res->convert_hd);
    esp_gmf_pipeline_get_el_by_name(res->pipe, "vid_enc", &res->enc_hd);
    esp_gmf_pipeline_get_el_by_name(res->pipe, "vid_fps_cvt", &res->rate_hd);
    esp_gmf_pipeline_get_el_by_name(res->pipe, "vid_overlay", &res->overlay_hd);
    esp_gmf_pipeline_get_el_by_name(res->pipe, "vid_dec", &res->dec_hd);

    return 0;
}

static void release_convert_pipeline(convert_res_t *res)
{
    if (res->pipe) {
        esp_gmf_pipeline_stop(res->pipe);
    }
    if (res->task) {
        esp_gmf_task_deinit(res->task);
    }
    if (res->pipe) {
        esp_gmf_pipeline_destroy(res->pipe);
    }
    gmf_loader_teardown_video_codec_default(res->pool);
    gmf_loader_teardown_video_effects_default(res->pool);
    esp_gmf_pool_deinit(res->pool);
}

static int test_color_convert(convert_res_t *res, uint32_t convert_pair[][2], int n)
{
    for (int i = 0; i < n; i++) {
        // Gen pattern
        allocate_src_pattern(convert_pair[i][0], false);
        video_el_inst.out_res = video_el_inst.src_res;
        video_el_inst.out_codec = convert_pair[i][1];
        esp_gmf_info_video_t info = {
            .format_id = convert_pair[i][0],
            .width = TEST_PATTERN_WIDTH,
            .height = TEST_PATTERN_HEIGHT,
        };
        esp_gmf_element_handle_t convert_hd;
        convert_hd = get_element_by_caps_from_pipe(res->pipe, ESP_GMF_CAPS_VIDEO_COLOR_CONVERT);
        esp_gmf_video_param_set_dst_format(convert_hd, convert_pair[i][1]);
        esp_gmf_pipeline_report_info(res->pipe, ESP_GMF_INFO_VIDEO, &info, sizeof(info));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res->pipe));
        vTaskDelay(1000 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res->pipe));
        show_result_pattern();
        free_video_el_inst();
        esp_gmf_pipeline_reset(res->pipe);
        esp_gmf_pipeline_loading_jobs(res->pipe);
    }
    return 0;
}

#ifdef CONFIG_IDF_TARGET_ESP32P4
TEST_CASE("Color convert HW", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_ppa", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, (const char **)name));
    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_OUYY_EVYY},
        {ESP_FOURCC_OUYY_EVYY, ESP_FOURCC_RGB16},
    };
    test_color_convert(&res, convert_pair, ELEMS(convert_pair));

    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}
#endif

TEST_CASE("Color convert SW", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_color_cvt", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, (const char **)name));

    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_OUYY_EVYY},
        {ESP_FOURCC_OUYY_EVYY, ESP_FOURCC_RGB16},
        {ESP_FOURCC_RGB16, ESP_FOURCC_BGR16},
        {ESP_FOURCC_RGB16, ESP_FOURCC_RGB24},
    };
    test_color_convert(&res, convert_pair, ELEMS(convert_pair));

    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Color convert by caps", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    esp_gmf_element_handle_t element;
    element = get_element_by_caps_from_pool(res.pool, ESP_GMF_CAPS_VIDEO_COLOR_CONVERT);
    const char *name[] = {OBJ_GET_TAG(element), NULL};
    printf("Get element name %s\n", OBJ_GET_TAG(element));
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, (const char **)name));

    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_OUYY_EVYY},
        {ESP_FOURCC_OUYY_EVYY, ESP_FOURCC_RGB16},
    };
    test_color_convert(&res, convert_pair, ELEMS(convert_pair));

    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}

static int test_scale(convert_res_t *res, uint32_t convert_pair[][2], int n)
{
    for (int i = 0; i < n; i++) {
        // Gen pattern
        allocate_src_pattern(convert_pair[i][0], false);
        video_el_inst.out_res.width = video_el_inst.src_res.width >> 1;
        video_el_inst.out_res.height = video_el_inst.src_res.height >> 1;
        video_el_inst.out_codec = convert_pair[i][1];
        esp_gmf_element_handle_t convert_hd;
        convert_hd = get_element_by_caps_from_pipe(res->pipe, ESP_GMF_CAPS_VIDEO_COLOR_CONVERT);
        esp_gmf_video_param_set_dst_format(convert_hd, convert_pair[i][1]);
        esp_gmf_element_handle_t scale_hd;
        scale_hd = get_element_by_caps_from_pipe(res->pipe, ESP_GMF_CAPS_VIDEO_SCALE);
        esp_gmf_video_param_set_dst_resolution(scale_hd, &video_el_inst.out_res);

        esp_gmf_info_video_t info = {
            .format_id = convert_pair[i][0],
            .width = TEST_PATTERN_WIDTH,
            .height = TEST_PATTERN_HEIGHT,
        };

        esp_gmf_pipeline_report_info(res->pipe, ESP_GMF_INFO_VIDEO, &info, sizeof(info));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res->pipe));
        vTaskDelay(1000 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res->pipe));
        show_result_pattern();
        free_video_el_inst();
        esp_gmf_pipeline_reset(res->pipe);
        esp_gmf_pipeline_loading_jobs(res->pipe);
        esp_cpu_clear_watchpoint(0);
    }
    return 0;
}

#ifdef CONFIG_IDF_TARGET_ESP32P4
TEST_CASE("Scale HW", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_ppa", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, (const char **)name));

    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_OUYY_EVYY},
        {ESP_FOURCC_OUYY_EVYY, ESP_FOURCC_RGB16},
    };
    test_scale(&res, convert_pair, ELEMS(convert_pair));

    // Clearup resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}
#endif

TEST_CASE("Scale SW", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_scale", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, (const char **)name));
    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_RGB16},
        {ESP_FOURCC_RGB24, ESP_FOURCC_RGB24},
    };
    test_scale(&res, convert_pair, ELEMS(convert_pair));

    // Clearup resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}

static int test_rotate(convert_res_t *res, uint32_t convert_pair[][2], int n)
{
    for (int i = 0; i < n; i++) {
        // Gen pattern
        allocate_src_pattern(convert_pair[i][0], false);
        video_el_inst.out_res.width = video_el_inst.src_res.height;
        video_el_inst.out_res.height = video_el_inst.src_res.width;
        video_el_inst.out_codec = convert_pair[i][1];
        video_el_inst.rotate_degree = i & 1 ? 90 : 270;

        esp_gmf_element_handle_t convert_hd;
        convert_hd = get_element_by_caps_from_pipe(res->pipe, ESP_GMF_CAPS_VIDEO_COLOR_CONVERT);
        esp_gmf_video_param_set_dst_format(convert_hd, convert_pair[i][1]);
        esp_gmf_element_handle_t rotate_hd;
        rotate_hd = get_element_by_caps_from_pipe(res->pipe, ESP_GMF_CAPS_VIDEO_ROTATE);
        esp_gmf_video_param_set_rotate_angle(rotate_hd, video_el_inst.rotate_degree);
        esp_gmf_video_param_set_dst_resolution(rotate_hd, &video_el_inst.out_res);

        esp_gmf_info_video_t info = {
            .format_id = convert_pair[i][0],
            .width = TEST_PATTERN_WIDTH,
            .height = TEST_PATTERN_HEIGHT,
        };

        esp_gmf_pipeline_report_info(res->pipe, ESP_GMF_INFO_VIDEO, &info, sizeof(info));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res->pipe));
        vTaskDelay(1000 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res->pipe));
        show_result_pattern();
        free_video_el_inst();
        esp_gmf_pipeline_reset(res->pipe);
        esp_gmf_pipeline_loading_jobs(res->pipe);
    }
    return 0;
}

#ifdef CONFIG_IDF_TARGET_ESP32P4
TEST_CASE("Rotate HW", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_ppa", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, name));
    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_OUYY_EVYY},
        {ESP_FOURCC_OUYY_EVYY, ESP_FOURCC_RGB16},
    };
    test_rotate(&res, convert_pair, ELEMS(convert_pair));

    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}
#endif

TEST_CASE("Rotate SW", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_rotate", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, (const char **)name));
    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_RGB16},
    };
    test_rotate(&res, convert_pair, ELEMS(convert_pair));

    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}

static int test_crop(convert_res_t *res, uint32_t convert_pair[][2], int n)
{
    for (int i = 0; i < n; i++) {
        // Gen pattern
        allocate_src_pattern(convert_pair[i][0], false);
        video_el_inst.out_res.width = video_el_inst.src_res.width >> 1;
        video_el_inst.out_res.height = video_el_inst.src_res.height >> 1;
        video_el_inst.out_codec = convert_pair[i][1];

        esp_gmf_video_rgn_t crop_rgn = {
            .x = video_el_inst.src_res.width >> 2,
            .y = video_el_inst.src_res.height >> 2,
            .width = video_el_inst.out_res.width,
            .height = video_el_inst.out_res.height,
        };
        esp_gmf_element_handle_t convert_hd;
        convert_hd = get_element_by_caps_from_pipe(res->pipe, ESP_GMF_CAPS_VIDEO_COLOR_CONVERT);
        esp_gmf_video_param_set_dst_format(convert_hd, convert_pair[i][1]);

        esp_gmf_element_handle_t crop_hd;
        crop_hd = get_element_by_caps_from_pipe(res->pipe, ESP_GMF_CAPS_VIDEO_CROP);
        esp_gmf_video_param_set_cropped_region(crop_hd, &crop_rgn);
        esp_gmf_video_param_set_dst_resolution(crop_hd, &video_el_inst.out_res);

        esp_gmf_info_video_t info = {
            .format_id = convert_pair[i][0],
            .width = TEST_PATTERN_WIDTH,
            .height = TEST_PATTERN_HEIGHT,
        };

        esp_gmf_pipeline_report_info(res->pipe, ESP_GMF_INFO_VIDEO, &info, sizeof(info));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res->pipe));
        vTaskDelay(1000 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res->pipe));
        show_result_pattern();
        free_video_el_inst();
        esp_gmf_pipeline_reset(res->pipe);
        esp_gmf_pipeline_loading_jobs(res->pipe);
    }
    return 0;
}

#ifdef CONFIG_IDF_TARGET_ESP32P4
TEST_CASE("Crop HW", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_ppa", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, name));
    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_OUYY_EVYY},
        {ESP_FOURCC_OUYY_EVYY, ESP_FOURCC_RGB16},
    };
    test_crop(&res, convert_pair, ELEMS(convert_pair));
    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}
#endif

TEST_CASE("Crop only SW", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);

    const char *name[] = {"vid_crop", NULL};

    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, name));

    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_RGB16},
        // { ESP_FOURCC_RGB24, ESP_FOURCC_RGB16 },
    };
    test_crop(&res, convert_pair, ELEMS(convert_pair));
    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Encoder only", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_enc", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, name));

    uint32_t convert_pair[][2] = {
#if CONFIG_IDF_TARGET_ESP32P4
        {ESP_FOURCC_OUYY_EVYY, ESP_FOURCC_H264},
        {ESP_FOURCC_RGB16, ESP_FOURCC_MJPG},
#else
        {ESP_FOURCC_RGB24, ESP_FOURCC_MJPG},
#if CONFIG_IDF_TARGET_ESP32S3
        {ESP_FOURCC_YUV420P, ESP_FOURCC_H264},
#endif  /* CON FIG_IDF_TARGET_ESP32S3 */
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
    };
    for (int i = 0; i < ELEMS(convert_pair); i++) {
        // Gen pattern
        allocate_src_pattern(convert_pair[i][0], false);
        video_el_inst.out_res = video_el_inst.src_res;
        video_el_inst.out_codec = convert_pair[i][1];

        esp_gmf_info_video_t info = {
            .format_id = convert_pair[i][0],
            .width = TEST_PATTERN_WIDTH,
            .height = TEST_PATTERN_HEIGHT,
            .fps = 10,
        };
        // esp_gmf_video_enc_set_dst_codec(res.enc_hd, convert_pair[i][1]);
        esp_gmf_video_param_set_dst_codec(res.enc_hd, convert_pair[i][1]);
        esp_gmf_pipeline_report_info(res.pipe, ESP_GMF_INFO_VIDEO, &info, sizeof(info));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res.pipe));
        vTaskDelay(1000 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res.pipe));
        free_video_el_inst();
        esp_gmf_pipeline_reset(res.pipe);
        esp_gmf_pipeline_loading_jobs(res.pipe);
    }
    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("FPS convert only", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_fps_cvt", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, name));
    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_MJPG},
    };
    for (int i = 0; i < ELEMS(convert_pair); i++) {
        // Gen pattern
        allocate_src_pattern(convert_pair[i][0], false);
        video_el_inst.out_res = video_el_inst.src_res;
        video_el_inst.out_codec = convert_pair[i][1];
        esp_gmf_info_video_t info = {
            .format_id = convert_pair[i][0],
            .width = TEST_PATTERN_WIDTH,
            .height = TEST_PATTERN_HEIGHT,
            .fps = 10,
        };
        // esp_gmf_video_fps_cvt_set_fps(res.rate_hd, 5);
        esp_gmf_video_param_set_fps(res.rate_hd, 5);
        esp_gmf_pipeline_report_info(res.pipe, ESP_GMF_INFO_VIDEO, &info, sizeof(info));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res.pipe));
        vTaskDelay(100 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res.pipe));
        free_video_el_inst();
        esp_gmf_pipeline_reset(res.pipe);
        esp_gmf_pipeline_loading_jobs(res.pipe);
    }
    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Overlay Test", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_overlay", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, name));

    allocate_src_pattern(ESP_FOURCC_RGB16, false);

    // Create overlay buffer
    video_el_inst.overlay_res.width = video_el_inst.src_res.width / 2;
    video_el_inst.overlay_res.height = video_el_inst.src_res.height / 2;
    esp_video_codec_resolution_t overlay_res = {
        .width = video_el_inst.overlay_res.width,
        .height = video_el_inst.overlay_res.height,
    };
    video_el_inst.overlay_size = esp_video_codec_get_image_size((esp_video_codec_pixel_fmt_t)ESP_FOURCC_RGB16,
                                                                (esp_video_codec_resolution_t *)&overlay_res);
    video_el_inst.overlay_data = esp_gmf_oal_malloc_align(TEST_VIDEO_ALIGNMENT, video_el_inst.overlay_size);
    TEST_ASSERT_NOT_NULL(video_el_inst.src_pixel);
    pattern_info_t pattern_info = {
        .format_id = ESP_FOURCC_RGB16,
        .res = video_el_inst.overlay_res,
        .pixel = video_el_inst.overlay_data,
        .data_size = video_el_inst.overlay_size,
        .bar_count = TEST_PATTERN_BAR_COUNT / 2,
        .vertical = !TEST_PATTERN_VERTICAL,
    };
    gen_pattern_color_bar(&pattern_info);

    video_el_inst.out_res = video_el_inst.src_res;
    video_el_inst.out_codec = ESP_FOURCC_RGB16;

    esp_gmf_overlay_rgn_info_t rgn = {
        .format_id = ESP_FOURCC_RGB16,
        .dst_rgn = {
            .x = video_el_inst.src_res.width / 4,
            .y = video_el_inst.src_res.height / 4,
            .width = video_el_inst.overlay_res.width,
            .height = video_el_inst.overlay_res.height,
        },
    };
    // esp_gmf_video_overlay_set_rgn(res.overlay_hd, &rgn);
    esp_gmf_video_param_set_overlay_rgn(res.overlay_hd, &rgn);

    esp_gmf_port_handle_t overlay_port = NEW_ESP_GMF_PORT_IN_BLOCK(overlay_acquire, overlay_release, NULL, NULL, 0, ESP_GMF_MAX_DELAY);
    // esp_gmf_video_overlay_set_overlay_port(res.overlay_hd, overlay_port);
    esp_gmf_video_param_set_overlay_port(res.overlay_hd, overlay_port);
    // esp_gmf_video_overlay_enable(res.overlay_hd, true);
    esp_gmf_video_param_overlay_enable(res.overlay_hd, true);

    esp_gmf_info_video_t info = {
        .format_id = ESP_FOURCC_RGB16,
        .width = TEST_PATTERN_WIDTH,
        .height = TEST_PATTERN_HEIGHT,
        .fps = 10,
    };
    esp_gmf_pipeline_report_info(res.pipe, ESP_GMF_INFO_VIDEO, &info, sizeof(info));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res.pipe));
    vTaskDelay(100 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res.pipe));
    show_result_pattern();

    free_video_el_inst();
    // Clear up resources
    release_convert_pipeline(&res);
    if (overlay_port) {
        esp_gmf_port_deinit(overlay_port);
    }

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Encoder to Decode", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_enc", "vid_dec", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, name));

    uint32_t convert_pair[][3] = {
#if CONFIG_IDF_TARGET_ESP32P4
        {ESP_FOURCC_OUYY_EVYY, ESP_FOURCC_H264, ESP_FOURCC_YUV420P},
        {ESP_FOURCC_RGB16, ESP_FOURCC_MJPG, ESP_FOURCC_RGB16},
#else
        {ESP_FOURCC_RGB24, ESP_FOURCC_MJPG, ESP_FOURCC_RGB16},
#if CONFIG_IDF_TARGET_ESP32S3
        {ESP_FOURCC_YUV420P, ESP_FOURCC_H264, ESP_FOURCC_YUV420P},
#endif  /* CON FIG_IDF_TARGET_ESP32S3 */
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
    };
    for (int i = 0; i < ELEMS(convert_pair); i++) {
        // Gen pattern
        allocate_src_pattern(convert_pair[i][0], false);
        video_el_inst.out_res = video_el_inst.src_res;
        video_el_inst.out_codec = convert_pair[i][2];

        esp_gmf_info_video_t info = {
            .format_id = convert_pair[i][0],
            .width = TEST_PATTERN_WIDTH,
            .height = TEST_PATTERN_HEIGHT,
            .fps = 10,
        };
        // esp_gmf_video_enc_set_dst_codec(res.enc_hd, convert_pair[i][1]);
        esp_gmf_video_param_set_dst_codec(res.enc_hd, convert_pair[i][1]);
        esp_gmf_pipeline_report_info(res.pipe, ESP_GMF_INFO_VIDEO, &info, sizeof(info));
        // Set decoder information
        // esp_gmf_video_dec_set_dst_format(res.dec_hd, convert_pair[i][2]);
        esp_gmf_video_param_set_dst_format(res.dec_hd, convert_pair[i][2]);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res.pipe));
        vTaskDelay(1000 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res.pipe));
        show_result_pattern();
        free_video_el_inst();
        esp_gmf_pipeline_reset(res.pipe);
        esp_gmf_pipeline_loading_jobs(res.pipe);
    }
    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}
