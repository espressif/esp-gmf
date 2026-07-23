/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "driver/gpio.h"
#include "driver/i2s_etm.h"
#include "driver/i2s_common.h"
#include "esp_bt_audio_le_playback_sync.h"
#include "esp_attr.h"
#include "esp_bit_defs.h"
#include "esp_check.h"
#include "esp_etm.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "modem/modem_etm.h"
#include "soc/soc_caps.h"

/**
 * @brief  ETM resources used to start I2S from the BLE ISO timing event.
 */
struct esp_bt_audio_le_playback_sync {
    esp_etm_channel_handle_t  etm_ch;          /*!< ETM channel connecting modem event to I2S task */
    esp_etm_task_handle_t     i2s_start_task;  /*!< I2S start ETM task */
    esp_etm_event_handle_t    modem_event;     /*!< Modem ETM timing event */
#if CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR
    esp_etm_channel_handle_t  monitor_ch;      /*!< Extra ETM channel toggling a GPIO on G1 event */
    esp_etm_task_handle_t     gpio_task;       /*!< GPIO toggle ETM task */
#endif  /* CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR */
};

struct esp_bt_audio_le_clk_sync {
    i2s_chan_handle_t         tx_handle;            /*!< I2S TX channel handle */
    QueueHandle_t             monitor_queue;        /*!< Queue for reporting count difference */
    esp_etm_channel_handle_t  etm_ch;               /*!< ETM channel connecting modem event to I2S FIFO sync task */
    esp_etm_task_handle_t     i2s_sync_task;        /*!< I2S FIFO sync ETM task */
    esp_etm_event_handle_t    modem_event;          /*!< Modem ETM timing event */
    bool                      enabled;              /*!< Whether the ETM channel is enabled */
    bool                      fifo_sync_configured; /*!< Whether TX FIFO sync has been configured */
    bool                      callback_registered;  /*!< Whether TX sync callback has been registered */
#if CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR
    esp_etm_channel_handle_t  monitor_ch;           /*!< Extra ETM channel toggling a GPIO on G2 event */
    esp_etm_task_handle_t     gpio_task;            /*!< GPIO toggle ETM task */
#endif  /* CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR */
};

static const char *TAG = "PLAYBACK_SYNC";

#if CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM

#if SOC_I2S_SUPPORTS_TX_FIFO_SYNC
static bool IRAM_ATTR i2s_clk_sync_callback(i2s_chan_handle_t handle,
                                            const i2s_sync_event_data_t *event,
                                            void *user_ctx)
{
    esp_bt_audio_le_clk_sync_handle_t sync = (esp_bt_audio_le_clk_sync_handle_t)user_ctx;
    if (!sync || !sync->monitor_queue) {
        return false;
    }

    BaseType_t need_yield = pdFALSE;
    i2s_sync_count_t count = {};
    i2s_channel_get_sync_count(handle, &count, false);
    esp_bt_audio_le_clk_sync_msg_t msg = {
        .diff = event ? event->diff_count : count.diff_count,
        .fifo_cnt = count.fifo_count,
        .bck_cnt = count.bclk_count,
    };
    xQueueSendFromISR(sync->monitor_queue, &msg, &need_yield);
    return need_yield == pdTRUE;
}

static esp_err_t i2s_clk_sync_register_callback(esp_bt_audio_le_clk_sync_handle_t sync)
{
    if (!sync->monitor_queue) {
        return ESP_OK;
    }
    i2s_event_callbacks_t cbs = {
        .on_tx_sync_evt = i2s_clk_sync_callback,
    };
    esp_err_t ret = i2s_channel_register_event_callback(sync->tx_handle, &cbs, sync);
    if (ret == ESP_OK) {
        sync->callback_registered = true;
    }
    return ret;
}

static esp_err_t i2s_clk_sync_unregister_callback(esp_bt_audio_le_clk_sync_handle_t sync)
{
    if (!sync->callback_registered) {
        return ESP_OK;
    }
    i2s_event_callbacks_t cbs = {};
    esp_err_t ret = i2s_channel_register_event_callback(sync->tx_handle, &cbs, NULL);
    if (ret == ESP_OK) {
        sync->callback_registered = false;
    }
    return ret;
}
#endif  /* SOC_I2S_SUPPORTS_TX_FIFO_SYNC */

esp_err_t esp_bt_audio_le_playback_sync_init(i2s_chan_handle_t tx_handle,
                                             esp_bt_audio_le_playback_sync_handle_t *out_handle)
{
    esp_err_t ret = ESP_OK;
    esp_bt_audio_le_playback_sync_handle_t sync = NULL;

    ESP_RETURN_ON_FALSE(out_handle, ESP_ERR_INVALID_ARG, TAG, "Init failed: out_handle is NULL");
    *out_handle = NULL;
    ESP_RETURN_ON_FALSE(tx_handle, ESP_ERR_INVALID_ARG, TAG, "Init failed: tx_handle is NULL");

    sync = heap_caps_calloc_prefer(1, sizeof(struct esp_bt_audio_le_playback_sync), 2,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                   MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(sync, ESP_ERR_NO_MEM, TAG, "Allocation failed: playback sync context");

    esp_etm_channel_config_t etm_config = {};
    i2s_etm_task_config_t i2s_task_cfg = {
        .task_type = I2S_ETM_TASK_START,
    };
    ESP_GOTO_ON_ERROR(i2s_new_etm_task(tx_handle, &i2s_task_cfg, &sync->i2s_start_task),
                      err, TAG, "I2S ETM task allocation failed");

    modem_etm_event_config_t modem_event_cfg = {
        .event_type = MODEM_ETM_EVENT_G1,
    };
    ESP_GOTO_ON_ERROR(modem_new_etm_event(&modem_event_cfg, &sync->modem_event),
                      err, TAG, "Modem ETM event allocation failed");
    ESP_GOTO_ON_ERROR(esp_etm_new_channel(&etm_config, &sync->etm_ch),
                      err, TAG, "ETM channel allocation failed");
    ESP_GOTO_ON_ERROR(esp_etm_channel_connect(sync->etm_ch, sync->modem_event, sync->i2s_start_task),
                      err, TAG, "ETM channel connect failed");

#if CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR_GPIO),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "Playback sync monitor GPIO config failed");
    gpio_set_level(CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR_GPIO, 0);

    gpio_etm_task_config_t gpio_task_cfg = {
        .action = GPIO_ETM_TASK_ACTION_TOG,
    };
    ESP_GOTO_ON_ERROR(gpio_new_etm_task(&gpio_task_cfg, &sync->gpio_task),
                      err, TAG, "Playback sync monitor GPIO ETM task alloc failed");
    ESP_GOTO_ON_ERROR(gpio_etm_task_add_gpio(sync->gpio_task, CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR_GPIO),
                      err, TAG, "Playback sync monitor GPIO ETM task add GPIO failed");
    esp_etm_channel_config_t monitor_etm_cfg = {};
    ESP_GOTO_ON_ERROR(esp_etm_new_channel(&monitor_etm_cfg, &sync->monitor_ch),
                      err, TAG, "Playback sync monitor ETM channel alloc failed");
    ESP_GOTO_ON_ERROR(esp_etm_channel_connect(sync->monitor_ch, sync->modem_event, sync->gpio_task),
                      err, TAG, "Playback sync monitor ETM channel connect failed");
#endif  /* CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR */

    *out_handle = sync;
    return ESP_OK;

err:
    if (sync) {
#if CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR
        if (sync->monitor_ch) {
            esp_etm_del_channel(sync->monitor_ch);
        }
        if (sync->gpio_task) {
            gpio_etm_task_rm_gpio(sync->gpio_task, CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR_GPIO);
            esp_etm_del_task(sync->gpio_task);
        }
#endif  /* CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR */
        if (sync->etm_ch) {
            esp_etm_del_channel(sync->etm_ch);
        }
        if (sync->i2s_start_task) {
            esp_etm_del_task(sync->i2s_start_task);
        }
        if (sync->modem_event) {
            esp_etm_del_event(sync->modem_event);
        }
        heap_caps_free(sync);
        sync = NULL;
    }
    *out_handle = NULL;
    ESP_LOGE(TAG, "Init failed: %s", esp_err_to_name(ret));
    return ret;
}

esp_err_t esp_bt_audio_le_playback_sync_enable(esp_bt_audio_le_playback_sync_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Enable failed: handle is NULL");
#if CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR
    esp_etm_channel_enable(handle->monitor_ch);
#endif  /* CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR */
    esp_err_t err = esp_etm_channel_enable(handle->etm_ch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Enable failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t esp_bt_audio_le_playback_sync_disable(esp_bt_audio_le_playback_sync_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Disable failed: handle is NULL");
#if CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR
    esp_etm_channel_disable(handle->monitor_ch);
#endif  /* CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR */
    esp_err_t err = esp_etm_channel_disable(handle->etm_ch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Disable failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t esp_bt_audio_le_playback_sync_deinit(esp_bt_audio_le_playback_sync_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Deinit failed: handle is NULL");
#if CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR
    esp_etm_channel_disable(handle->monitor_ch);
    esp_etm_del_channel(handle->monitor_ch);
    gpio_etm_task_rm_gpio(handle->gpio_task, CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR_GPIO);
    esp_etm_del_task(handle->gpio_task);
    gpio_reset_pin(CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR_GPIO);
    gpio_set_direction(CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR_GPIO, 0);
#endif  /* CONFIG_ESP_BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR */
    esp_etm_channel_disable(handle->etm_ch);
    esp_etm_del_channel(handle->etm_ch);
    esp_etm_del_task(handle->i2s_start_task);
    esp_etm_del_event(handle->modem_event);
    heap_caps_free(handle);
    handle = NULL;
    return ESP_OK;
}

esp_err_t esp_bt_audio_le_clk_sync_init(i2s_chan_handle_t tx_handle,
                                        QueueHandle_t monitor_queue,
                                        esp_bt_audio_le_clk_sync_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(out_handle, ESP_ERR_INVALID_ARG, TAG, "Clock sync init failed: out_handle is NULL");
    *out_handle = NULL;
    ESP_RETURN_ON_FALSE(tx_handle, ESP_ERR_INVALID_ARG, TAG, "Clock sync init failed: tx_handle is NULL");

#if !SOC_I2S_SUPPORTS_TX_FIFO_SYNC
    return ESP_ERR_NOT_SUPPORTED;
#else
    esp_err_t ret = ESP_OK;
    esp_bt_audio_le_clk_sync_handle_t sync = NULL;

    sync = heap_caps_calloc(1, sizeof(struct esp_bt_audio_le_clk_sync), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(sync, ESP_ERR_NO_MEM, TAG, "Allocation failed: clock sync context");
    sync->tx_handle = tx_handle;
    sync->monitor_queue = monitor_queue;
    ESP_GOTO_ON_ERROR(i2s_clk_sync_register_callback(sync),
                      err, TAG, "I2S TX FIFO sync callback register failed");

    esp_etm_channel_config_t etm_config = {};
    i2s_etm_task_config_t i2s_task_cfg = {
        .task_type = I2S_ETM_TASK_SYNC_FIFO,
    };
    ESP_GOTO_ON_ERROR(i2s_new_etm_task(tx_handle, &i2s_task_cfg, &sync->i2s_sync_task),
                      err, TAG, "I2S sync ETM task allocation failed");

    modem_etm_event_config_t modem_event_cfg = {
        .event_type = MODEM_ETM_EVENT_G2,
    };
    ESP_GOTO_ON_ERROR(modem_new_etm_event(&modem_event_cfg, &sync->modem_event),
                      err, TAG, "Modem ETM event allocation failed");
    ESP_GOTO_ON_ERROR(esp_etm_new_channel(&etm_config, &sync->etm_ch),
                      err, TAG, "ETM channel allocation failed");
    ESP_GOTO_ON_ERROR(esp_etm_channel_connect(sync->etm_ch, sync->modem_event, sync->i2s_sync_task),
                      err, TAG, "ETM channel connect failed");

#if CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR_GPIO),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "Clock sync monitor GPIO config failed");
    gpio_set_level(CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR_GPIO, 0);

    gpio_etm_task_config_t gpio_task_cfg = {
        .action = GPIO_ETM_TASK_ACTION_TOG,
    };
    ESP_GOTO_ON_ERROR(gpio_new_etm_task(&gpio_task_cfg, &sync->gpio_task),
                      err, TAG, "Clock sync monitor GPIO ETM task alloc failed");
    ESP_GOTO_ON_ERROR(gpio_etm_task_add_gpio(sync->gpio_task, CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR_GPIO),
                      err, TAG, "Clock sync monitor GPIO ETM task add GPIO failed");
    esp_etm_channel_config_t monitor_etm_cfg = {};
    ESP_GOTO_ON_ERROR(esp_etm_new_channel(&monitor_etm_cfg, &sync->monitor_ch),
                      err, TAG, "Clock sync monitor ETM channel alloc failed");
    ESP_GOTO_ON_ERROR(esp_etm_channel_connect(sync->monitor_ch, sync->modem_event, sync->gpio_task),
                      err, TAG, "Clock sync monitor ETM channel connect failed");
#endif  /* CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR */

    *out_handle = sync;
    return ESP_OK;

err:
    if (sync) {
#if CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR
        if (sync->monitor_ch) {
            esp_etm_del_channel(sync->monitor_ch);
        }
        if (sync->gpio_task) {
            gpio_etm_task_rm_gpio(sync->gpio_task, CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR_GPIO);
            esp_etm_del_task(sync->gpio_task);
        }
#endif  /* CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR */
        if (sync->etm_ch) {
            esp_etm_del_channel(sync->etm_ch);
        }
        if (sync->i2s_sync_task) {
            esp_etm_del_task(sync->i2s_sync_task);
        }
        if (sync->modem_event) {
            esp_etm_del_event(sync->modem_event);
        }
        if (sync->fifo_sync_configured) {
            i2s_channel_enable_tx_fifo_sync(sync->tx_handle, false);
        }
        i2s_clk_sync_unregister_callback(sync);
        heap_caps_free(sync);
    }
    *out_handle = NULL;
    ESP_LOGE(TAG, "Clock sync init failed: %s", esp_err_to_name(ret));
    return ret;
#endif  /* !SOC_I2S_SUPPORTS_TX_FIFO_SYNC */
}

esp_err_t esp_bt_audio_le_clk_sync_enable(esp_bt_audio_le_clk_sync_handle_t handle,
                                          uint32_t ideal_cnt,
                                          uint32_t diff_threshold)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Clock sync enable failed: handle is NULL");
    ESP_RETURN_ON_FALSE(diff_threshold > 0, ESP_ERR_INVALID_ARG, TAG,
                        "Clock sync enable failed: diff_threshold is zero");
    if (handle->enabled) {
        return ESP_OK;
    }
#if SOC_I2S_SUPPORTS_TX_FIFO_SYNC
    i2s_tx_fifo_sync_config_t sync_cfg = {
        .ideal_cnt = ideal_cnt,
        .manual_suppl_thresh = diff_threshold * 2,
        .auto_suppl_thresh = diff_threshold,
        .suppl_mode = I2S_TX_FIFO_SYNC_SUPPL_MODE_LAST_DATA,
    };
    ESP_RETURN_ON_ERROR(i2s_channel_config_tx_fifo_sync(handle->tx_handle, &sync_cfg),
                        TAG, "I2S TX FIFO sync config failed");
    handle->fifo_sync_configured = true;
    ESP_LOGI(TAG, "Clock sync config: ideal=%lu manual_th=%lu auto_th=%lu int_en=%d",
             ideal_cnt,
             sync_cfg.manual_suppl_thresh,
             sync_cfg.auto_suppl_thresh,
             handle->monitor_queue != NULL);

    i2s_sync_count_t sync_count = {};
    ESP_RETURN_ON_ERROR(i2s_channel_get_sync_count(handle->tx_handle, &sync_count, true),
                        TAG, "I2S TX sync count reset failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable_tx_fifo_sync(handle->tx_handle, true),
                        TAG, "I2S TX FIFO sync enable failed");
#endif  /* SOC_I2S_SUPPORTS_TX_FIFO_SYNC */
#if CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR
    esp_etm_channel_enable(handle->monitor_ch);
#endif  /* CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR */
    esp_err_t err = esp_etm_channel_enable(handle->etm_ch);
    if (err == ESP_OK) {
        handle->enabled = true;
    } else {
#if SOC_I2S_SUPPORTS_TX_FIFO_SYNC
        if (handle->fifo_sync_configured) {
            i2s_channel_enable_tx_fifo_sync(handle->tx_handle, false);
        }
#endif  /* SOC_I2S_SUPPORTS_TX_FIFO_SYNC */
        ESP_LOGE(TAG, "Clock sync enable failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t esp_bt_audio_le_clk_sync_disable(esp_bt_audio_le_clk_sync_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Clock sync disable failed: handle is NULL");
    if (!handle->enabled) {
        return ESP_OK;
    }
#if CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR
    esp_etm_channel_disable(handle->monitor_ch);
#endif  /* CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR */
    esp_err_t err = esp_etm_channel_disable(handle->etm_ch);
    if (err == ESP_OK) {
#if SOC_I2S_SUPPORTS_TX_FIFO_SYNC
        i2s_channel_enable_tx_fifo_sync(handle->tx_handle, false);
#endif  /* SOC_I2S_SUPPORTS_TX_FIFO_SYNC */
        handle->enabled = false;
    } else {
        ESP_LOGE(TAG, "Clock sync disable failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t esp_bt_audio_le_clk_sync_deinit(esp_bt_audio_le_clk_sync_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Clock sync deinit failed: handle is NULL");
    esp_bt_audio_le_clk_sync_disable(handle);
#if SOC_I2S_SUPPORTS_TX_FIFO_SYNC
    if (handle->fifo_sync_configured) {
        i2s_channel_enable_tx_fifo_sync(handle->tx_handle, false);
    }
    i2s_clk_sync_unregister_callback(handle);
#endif  /* SOC_I2S_SUPPORTS_TX_FIFO_SYNC */
#if CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR
    esp_etm_channel_disable(handle->monitor_ch);
    esp_etm_del_channel(handle->monitor_ch);
    gpio_etm_task_rm_gpio(handle->gpio_task, CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR_GPIO);
    esp_etm_del_task(handle->gpio_task);
    gpio_reset_pin(CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR_GPIO);
    gpio_set_direction(CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR_GPIO, 0);
#endif  /* CONFIG_ESP_BT_AUDIO_LE_CLK_SYNC_MONITOR */
    esp_etm_channel_disable(handle->etm_ch);
    esp_etm_del_channel(handle->etm_ch);
    esp_etm_del_task(handle->i2s_sync_task);
    esp_etm_del_event(handle->modem_event);
    heap_caps_free(handle);
    return ESP_OK;
}

#endif  /* CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM */
