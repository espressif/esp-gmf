#pragma once

#include "esp_gmf_oal_mem.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Memory management functions
 *
 * @note  These functions are wrappers around the GMF OAL memory management functions
 *        They provide a consistent interface for memory allocation and deallocation
 */

#define audio_render_malloc(size)              esp_gmf_oal_malloc(size)
#define audio_render_malloc_align(size, align) esp_gmf_oal_malloc_align(align, size)
#define audio_render_free(ptr)                 esp_gmf_oal_free(ptr)
#define audio_render_calloc(n, size)           esp_gmf_oal_calloc(n, size)
#define audio_render_realloc(ptr, size)        esp_gmf_oal_realloc(ptr, size)

#define SAFE_PTR(obj, member)                  ((obj) ? (obj)->member : NULL)

/**
 * @brief  Event group handle type
 */
typedef void *audio_render_event_grp_handle_t;

/**
 * @brief  Maximum lock time value (infinite wait)
 */
#define AUDIO_RENDER_MAX_LOCK_TIME (0xFFFFFFFF)

/**
 * @brief  Convert milliseconds to FreeRTOS ticks
 */
#define AUDIO_RENDER_TIME_TO_TICKS(ms) ((ms) == AUDIO_RENDER_MAX_LOCK_TIME ? portMAX_DELAY : (ms) / portTICK_PERIOD_MS)

/**
 * @brief  Event group management functions
 *
 * @note  These functions provide a wrapper around FreeRTOS event group functions
 *        for event synchronization and signaling
 */
#define audio_render_event_grp_create(event_group_ptr)         (*(event_group_ptr) = (audio_render_event_grp_handle_t)xEventGroupCreate())
#define audio_render_event_grp_set_bits(event_group, bits)     xEventGroupSetBits((EventGroupHandle_t)(event_group), (bits))
#define audio_render_event_grp_clr_bits(event_group, bits)     xEventGroupClearBits((EventGroupHandle_t)(event_group), (bits))
#define audio_render_event_grp_destroy(event_group)            vEventGroupDelete((EventGroupHandle_t)(event_group))

#define audio_render_delay(ms) vTaskDelay(AUDIO_RENDER_TIME_TO_TICKS(ms))

/**
 * @brief  Wait for bits (in milliseconds) to be set in an event group
 */
#define audio_render_event_grp_wait_bits(event_group, bits, timeout) \
    (uint32_t)xEventGroupWaitBits((EventGroupHandle_t)(event_group), (bits), false, true, AUDIO_RENDER_TIME_TO_TICKS(timeout))

#ifdef __cplusplus
}
#endif  /* __cplusplus */
