#include "esp_timer.h"

#include <time.h>
#include <stdlib.h>

uint64_t esp_timer_get_time(void)
{
    static struct timespec init_ts;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    if (init_ts.tv_sec == 0 && init_ts.tv_nsec == 0) {
        init_ts = ts;
    }
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000) -
           (uint64_t)init_ts.tv_sec * 1000000ULL - (uint64_t)(init_ts.tv_nsec / 1000);
}

esp_err_t esp_timer_create(const esp_timer_create_args_t *create_args, esp_timer_handle_t *out_handle)
{
    (void)create_args;
    if (!out_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_handle = NULL;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us)
{
    (void)timer;
    (void)timeout_us;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period)
{
    (void)timer;
    (void)period;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_timer_restart(esp_timer_handle_t timer, uint64_t timeout_us)
{
    (void)timer;
    (void)timeout_us;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_timer_stop(esp_timer_handle_t timer)
{
    (void)timer;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_timer_delete(esp_timer_handle_t timer)
{
    (void)timer;
    return ESP_ERR_NOT_SUPPORTED;
}
