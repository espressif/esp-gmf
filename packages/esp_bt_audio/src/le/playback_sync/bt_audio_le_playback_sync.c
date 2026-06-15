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
#include "esp_bit_defs.h"
#include "esp_check.h"
#include "esp_etm.h"
#include "esp_heap_caps.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_private/etm_interface.h"
#include "esp_private/i2s_sync.h"
#include "hal/i2s_hal.h"
#include "hal/i2s_periph.h"
#include "modem/modem_etm.h"
#include "soc/i2s_struct.h"
#include "soc/soc_etm_source.h"
#include "soc/soc_caps.h"

#define BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR       0
#define BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR_GPIO  13

#define BT_AUDIO_LE_CLK_SYNC_MONITOR       0
#define BT_AUDIO_LE_CLK_SYNC_MONITOR_GPIO  14

/**
 * @brief  ETM resources used to start I2S from the BLE ISO timing event.
 */
struct esp_bt_audio_le_playback_sync {
    esp_etm_channel_handle_t  etm_ch;          /*!< ETM channel connecting modem event to I2S task */
    esp_etm_task_handle_t     i2s_start_task;  /*!< I2S start ETM task */
    esp_etm_event_handle_t    modem_event;     /*!< Modem ETM timing event */
#if BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR
    esp_etm_channel_handle_t  monitor_ch;      /*!< Extra ETM channel toggling a GPIO on G1 event */
    esp_etm_task_handle_t     gpio_task;       /*!< GPIO toggle ETM task */
#endif  /* BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR */
};

struct esp_bt_audio_le_clk_sync {
    i2s_chan_handle_t         tx_handle;       /*!< I2S TX channel handle */
    i2s_dev_t                *i2s_dev;         /*!< I2S hardware instance from tx_handle */
    int                       i2s_port;        /*!< I2S port id from tx_handle */
    intr_handle_t             i2s_intr;        /*!< I2S interrupt handle for sync diff notification */
    QueueHandle_t             monitor_queue;   /*!< Queue for reporting count difference */
    esp_etm_channel_handle_t  etm_ch;          /*!< ETM channel connecting modem event to I2S FIFO sync task */
    esp_etm_task_handle_t     i2s_sync_task;   /*!< I2S FIFO sync ETM task */
    esp_etm_event_handle_t    modem_event;     /*!< Modem ETM timing event */
    bool                      enabled;         /*!< Whether the ETM channel is enabled */
#if BT_AUDIO_LE_CLK_SYNC_MONITOR
    esp_etm_channel_handle_t  monitor_ch;      /*!< Extra ETM channel toggling a GPIO on G2 event */
    esp_etm_task_handle_t     gpio_task;       /*!< GPIO toggle ETM task */
#endif  /* BT_AUDIO_LE_CLK_SYNC_MONITOR */
};

static const char *TAG = "PLAYBACK_SYNC";

#if CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM

#if SOC_I2S_SUPPORTS_TX_SYNC_CNT
typedef struct {
    esp_etm_task_t  base;
} i2s_etm_sync_task_t;

static esp_err_t i2s_etm_sync_task_del(esp_etm_task_t *task)
{
    i2s_etm_sync_task_t *i2s_task = __containerof(task, i2s_etm_sync_task_t, base);
    heap_caps_free(i2s_task);
    return ESP_OK;
}

static esp_err_t i2s_clk_sync_get_i2s_info(i2s_chan_handle_t tx_handle, int *i2s_port, i2s_dev_t **i2s_dev)
{
    i2s_chan_info_t i2s_info = {};
    ESP_RETURN_ON_ERROR(i2s_channel_get_info(tx_handle, &i2s_info), TAG, "Get I2S channel info failed");
    ESP_RETURN_ON_FALSE(i2s_info.dir == I2S_DIR_TX, ESP_ERR_INVALID_ARG, TAG, "I2S channel is not TX");
    ESP_RETURN_ON_FALSE(i2s_info.id >= 0 && i2s_info.id < I2S_LL_GET(INST_NUM),
                        ESP_ERR_INVALID_ARG, TAG, "Invalid I2S port");

    i2s_dev_t *dev = I2S_LL_GET_HW(i2s_info.id);
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "Invalid I2S hardware instance");
    *i2s_port = i2s_info.id;
    *i2s_dev = dev;
    return ESP_OK;
}

static uint32_t i2s_clk_sync_get_task_id(int i2s_port)
{
    switch (i2s_port) {
#if defined(I2S0_TASK_SYNC_CHECK)
        case 0:
            return I2S0_TASK_SYNC_CHECK;
#endif  /* defined(I2S0_TASK_SYNC_CHECK) */
#if defined(I2S1_TASK_SYNC_CHECK)
        case 1:
            return I2S1_TASK_SYNC_CHECK;
#endif  /* defined(I2S1_TASK_SYNC_CHECK) */
        default:
            return 0;
    }
}

static esp_err_t i2s_new_etm_sync_task(int i2s_port, esp_etm_task_handle_t *out_task)
{
    ESP_RETURN_ON_FALSE(out_task, ESP_ERR_INVALID_ARG, TAG, "Invalid argument");
    uint32_t task_id = i2s_clk_sync_get_task_id(i2s_port);
    ESP_RETURN_ON_FALSE(task_id, ESP_ERR_INVALID_ARG, TAG, "Invalid I2S sync ETM task port");

    i2s_etm_sync_task_t *task = heap_caps_calloc(1, sizeof(i2s_etm_sync_task_t), MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(task, ESP_ERR_NO_MEM, TAG, "No memory for I2S sync ETM task");
    task->base.task_id = task_id;
    task->base.trig_periph = ETM_TRIG_PERIPH_I2S;
    task->base.del = i2s_etm_sync_task_del;
    *out_task = &task->base;
    return ESP_OK;
}

static inline int32_t sign_extend_i2s_fifo_sync_diff(uint32_t diff)
{
    return (diff & BIT(30)) ? (int32_t)(diff | BIT(31)) : (int32_t)diff;
}

static inline void i2s_clk_sync_reset_diff_count(i2s_dev_t *dev)
{
    dev->cnt_diff.tx_cnt_diff_rst = 1;
    dev->cnt_diff.tx_cnt_diff_rst = 0;
}

static inline void i2s_clk_sync_clear_intr(i2s_dev_t *dev)
{
    dev->int_clr.tx_sync_int_clr = 1;
    dev->int_clr.tx_sync_int_clr = 0;
}

static inline void i2s_clk_sync_enable_intr(i2s_dev_t *dev, bool enable)
{
    dev->int_ena.tx_sync_int_ena = enable;
}

static inline void i2s_clk_sync_config_hw(i2s_dev_t *dev, bool hw_sync_enable,
                                          uint32_t ideal_cnt, uint32_t diff_threshold)
{
    i2s_ll_tx_set_etm_sync_ideal_cnt(dev, ideal_cnt);
    dev->sync_sw_thres.tx_cnt_diff_sw_thres = diff_threshold;
    dev->sync_hw_thres.tx_cnt_diff_hw_thres = diff_threshold / 2;
    dev->hw_sync_conf.tx_hw_sync_en = hw_sync_enable;
    dev->hw_sync_conf.tx_hw_sync_suppl_mode = 0;
    i2s_ll_tx_update(dev);
}

static IRAM_ATTR void i2s_clk_sync_isr(void *arg)
{
    BaseType_t need_yield = pdFALSE;
    esp_bt_audio_le_clk_sync_handle_t sync = (esp_bt_audio_le_clk_sync_handle_t)arg;
    i2s_dev_t *dev = sync->i2s_dev;

    if (dev->int_st.tx_sync_int_st) {
        esp_bt_audio_le_clk_sync_msg_t msg = {
            .diff = sign_extend_i2s_fifo_sync_diff(dev->cnt_diff.tx_cnt_diff),
            .fifo_cnt = i2s_sync_get_fifo_count(sync->tx_handle),
            .bck_cnt = i2s_sync_get_bclk_count(sync->tx_handle),
        };
        i2s_clk_sync_reset_diff_count(dev);
        i2s_clk_sync_clear_intr(dev);
        if (sync->monitor_queue) {
            xQueueSendFromISR(sync->monitor_queue, &msg, &need_yield);
        }
    }
    if (need_yield) {
        portYIELD_FROM_ISR();
    }
}
#endif  /* SOC_I2S_SUPPORTS_TX_SYNC_CNT && defined(I2S0_TASK_SYNC_CHECK) */

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

#if BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR_GPIO),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "Playback sync monitor GPIO config failed");
    gpio_set_level(BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR_GPIO, 0);

    gpio_etm_task_config_t gpio_task_cfg = {
        .action = GPIO_ETM_TASK_ACTION_TOG,
    };
    ESP_GOTO_ON_ERROR(gpio_new_etm_task(&gpio_task_cfg, &sync->gpio_task),
                      err, TAG, "Playback sync monitor GPIO ETM task alloc failed");
    ESP_GOTO_ON_ERROR(gpio_etm_task_add_gpio(sync->gpio_task, BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR_GPIO),
                      err, TAG, "Playback sync monitor GPIO ETM task add GPIO failed");
    esp_etm_channel_config_t monitor_etm_cfg = {};
    ESP_GOTO_ON_ERROR(esp_etm_new_channel(&monitor_etm_cfg, &sync->monitor_ch),
                      err, TAG, "Playback sync monitor ETM channel alloc failed");
    ESP_GOTO_ON_ERROR(esp_etm_channel_connect(sync->monitor_ch, sync->modem_event, sync->gpio_task),
                      err, TAG, "Playback sync monitor ETM channel connect failed");
#endif  /* BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR */

    *out_handle = sync;
    return ESP_OK;

err:
    if (sync) {
#if BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR
        if (sync->monitor_ch) {
            esp_etm_del_channel(sync->monitor_ch);
        }
        if (sync->gpio_task) {
            gpio_etm_task_rm_gpio(sync->gpio_task, BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR_GPIO);
            esp_etm_del_task(sync->gpio_task);
        }
#endif  /* BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR */
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
#if BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR
    esp_etm_channel_enable(handle->monitor_ch);
#endif  /* BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR */
    esp_err_t err = esp_etm_channel_enable(handle->etm_ch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Enable failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t esp_bt_audio_le_playback_sync_disable(esp_bt_audio_le_playback_sync_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Disable failed: handle is NULL");
#if BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR
    esp_etm_channel_disable(handle->monitor_ch);
#endif  /* BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR */
    esp_err_t err = esp_etm_channel_disable(handle->etm_ch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Disable failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t esp_bt_audio_le_playback_sync_deinit(esp_bt_audio_le_playback_sync_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Deinit failed: handle is NULL");
#if BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR
    esp_etm_channel_disable(handle->monitor_ch);
    esp_etm_del_channel(handle->monitor_ch);
    gpio_etm_task_rm_gpio(handle->gpio_task, BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR_GPIO);
    esp_etm_del_task(handle->gpio_task);
    gpio_reset_pin(BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR_GPIO);
    gpio_set_direction(BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR_GPIO, 0);
#endif  /* BT_AUDIO_LE_PLAYBACK_SYNC_MONITOR */
    esp_etm_channel_disable(handle->etm_ch);
    esp_etm_del_channel(handle->etm_ch);
    esp_etm_del_task(handle->i2s_start_task);
    esp_etm_del_event(handle->modem_event);
    heap_caps_free(handle);
    handle = NULL;
    return ESP_OK;
}

esp_err_t esp_bt_audio_le_clk_sync_init(i2s_chan_handle_t tx_handle,
                                        uint32_t ideal_cnt,
                                        uint32_t diff_threshold,
                                        bool hw_sync_enable,
                                        QueueHandle_t monitor_queue,
                                        esp_bt_audio_le_clk_sync_handle_t *out_handle)
{
    esp_err_t ret = ESP_OK;
    esp_bt_audio_le_clk_sync_handle_t sync = NULL;

    ESP_RETURN_ON_FALSE(out_handle, ESP_ERR_INVALID_ARG, TAG, "Clock sync init failed: out_handle is NULL");
    *out_handle = NULL;
    ESP_RETURN_ON_FALSE(tx_handle, ESP_ERR_INVALID_ARG, TAG, "Clock sync init failed: tx_handle is NULL");

#if !(SOC_I2S_SUPPORTS_TX_SYNC_CNT && defined(I2S0_TASK_SYNC_CHECK))
    return ESP_ERR_NOT_SUPPORTED;
#else
    sync = heap_caps_calloc_prefer(1, sizeof(struct esp_bt_audio_le_clk_sync), 2,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                   MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(sync, ESP_ERR_NO_MEM, TAG, "Allocation failed: clock sync context");
    sync->tx_handle = tx_handle;
    sync->monitor_queue = monitor_queue;
    ESP_GOTO_ON_ERROR(i2s_clk_sync_get_i2s_info(tx_handle, &sync->i2s_port, &sync->i2s_dev),
                      err, TAG, "I2S channel info get failed");

    i2s_clk_sync_config_hw(sync->i2s_dev, hw_sync_enable, ideal_cnt, diff_threshold);
    i2s_sync_reset_bclk_count(tx_handle);
    i2s_sync_reset_fifo_count(tx_handle);
    i2s_clk_sync_reset_diff_count(sync->i2s_dev);
    i2s_clk_sync_clear_intr(sync->i2s_dev);
    i2s_clk_sync_enable_intr(sync->i2s_dev, monitor_queue != NULL);
    ESP_LOGI(TAG, "Clock sync config: ideal=%lu sw_th=%lu hw_th=%lu hw_en=%d int_en=%d stop_en=%d",
             ideal_cnt,
             diff_threshold,
             diff_threshold / 2,
             hw_sync_enable,
             monitor_queue != NULL,
             sync->i2s_dev->tx_conf.tx_stop_en);

    if (monitor_queue) {
        ESP_GOTO_ON_ERROR(esp_intr_alloc(i2s_periph_signal[sync->i2s_port].irq, 0, i2s_clk_sync_isr,
                                         sync, &sync->i2s_intr),
                          err, TAG, "I2S sync interrupt allocation failed");
    }

    esp_etm_channel_config_t etm_config = {};
    ESP_GOTO_ON_ERROR(i2s_new_etm_sync_task(sync->i2s_port, &sync->i2s_sync_task),
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

#if BT_AUDIO_LE_CLK_SYNC_MONITOR
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(BT_AUDIO_LE_CLK_SYNC_MONITOR_GPIO),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "Clock sync monitor GPIO config failed");
    gpio_set_level(BT_AUDIO_LE_CLK_SYNC_MONITOR_GPIO, 0);

    gpio_etm_task_config_t gpio_task_cfg = {
        .action = GPIO_ETM_TASK_ACTION_TOG,
    };
    ESP_GOTO_ON_ERROR(gpio_new_etm_task(&gpio_task_cfg, &sync->gpio_task),
                      err, TAG, "Clock sync monitor GPIO ETM task alloc failed");
    ESP_GOTO_ON_ERROR(gpio_etm_task_add_gpio(sync->gpio_task, BT_AUDIO_LE_CLK_SYNC_MONITOR_GPIO),
                      err, TAG, "Clock sync monitor GPIO ETM task add GPIO failed");
    esp_etm_channel_config_t monitor_etm_cfg = {};
    ESP_GOTO_ON_ERROR(esp_etm_new_channel(&monitor_etm_cfg, &sync->monitor_ch),
                      err, TAG, "Clock sync monitor ETM channel alloc failed");
    ESP_GOTO_ON_ERROR(esp_etm_channel_connect(sync->monitor_ch, sync->modem_event, sync->gpio_task),
                      err, TAG, "Clock sync monitor ETM channel connect failed");
#endif  /* BT_AUDIO_LE_CLK_SYNC_MONITOR */

    *out_handle = sync;
    return ESP_OK;

err:
    if (sync) {
#if BT_AUDIO_LE_CLK_SYNC_MONITOR
        if (sync->monitor_ch) {
            esp_etm_del_channel(sync->monitor_ch);
        }
        if (sync->gpio_task) {
            gpio_etm_task_rm_gpio(sync->gpio_task, BT_AUDIO_LE_CLK_SYNC_MONITOR_GPIO);
            esp_etm_del_task(sync->gpio_task);
        }
#endif  /* BT_AUDIO_LE_CLK_SYNC_MONITOR */
        if (sync->etm_ch) {
            esp_etm_del_channel(sync->etm_ch);
        }
        if (sync->i2s_sync_task) {
            esp_etm_del_task(sync->i2s_sync_task);
        }
        if (sync->modem_event) {
            esp_etm_del_event(sync->modem_event);
        }
        if (sync->i2s_intr) {
            esp_intr_free(sync->i2s_intr);
        }
        if (sync->i2s_dev) {
            i2s_clk_sync_enable_intr(sync->i2s_dev, false);
        }
        heap_caps_free(sync);
    }
    *out_handle = NULL;
    ESP_LOGE(TAG, "Clock sync init failed: %s", esp_err_to_name(ret));
    return ret;
#endif  /* !(SOC_I2S_SUPPORTS_TX_SYNC_CNT && defined(I2S0_TASK_SYNC_CHECK)) */
}

esp_err_t esp_bt_audio_le_clk_sync_enable(esp_bt_audio_le_clk_sync_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Clock sync enable failed: handle is NULL");
    if (handle->enabled) {
        return ESP_OK;
    }
#if SOC_I2S_SUPPORTS_TX_SYNC_CNT && defined(I2S0_TASK_SYNC_CHECK)
    i2s_sync_reset_bclk_count(handle->tx_handle);
    i2s_sync_reset_fifo_count(handle->tx_handle);
    i2s_clk_sync_reset_diff_count(handle->i2s_dev);
    i2s_clk_sync_clear_intr(handle->i2s_dev);
#endif  /* SOC_I2S_SUPPORTS_TX_SYNC_CNT && defined(I2S0_TASK_SYNC_CHECK) */
#if BT_AUDIO_LE_CLK_SYNC_MONITOR
    esp_etm_channel_enable(handle->monitor_ch);
#endif  /* BT_AUDIO_LE_CLK_SYNC_MONITOR */
    esp_err_t err = esp_etm_channel_enable(handle->etm_ch);
    if (err == ESP_OK) {
        handle->enabled = true;
    } else {
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
#if BT_AUDIO_LE_CLK_SYNC_MONITOR
    esp_etm_channel_disable(handle->monitor_ch);
#endif  /* BT_AUDIO_LE_CLK_SYNC_MONITOR */
    esp_err_t err = esp_etm_channel_disable(handle->etm_ch);
    if (err == ESP_OK) {
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
#if SOC_I2S_SUPPORTS_TX_SYNC_CNT && defined(I2S0_TASK_SYNC_CHECK)
    i2s_clk_sync_enable_intr(handle->i2s_dev, false);
#endif  /* SOC_I2S_SUPPORTS_TX_SYNC_CNT && defined(I2S0_TASK_SYNC_CHECK) */
#if BT_AUDIO_LE_CLK_SYNC_MONITOR
    esp_etm_channel_disable(handle->monitor_ch);
    esp_etm_del_channel(handle->monitor_ch);
    gpio_etm_task_rm_gpio(handle->gpio_task, BT_AUDIO_LE_CLK_SYNC_MONITOR_GPIO);
    esp_etm_del_task(handle->gpio_task);
    gpio_reset_pin(BT_AUDIO_LE_CLK_SYNC_MONITOR_GPIO);
    gpio_set_direction(BT_AUDIO_LE_CLK_SYNC_MONITOR_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BT_AUDIO_LE_CLK_SYNC_MONITOR_GPIO, 0);
#endif  /* BT_AUDIO_LE_CLK_SYNC_MONITOR */
    esp_etm_channel_disable(handle->etm_ch);
    esp_etm_del_channel(handle->etm_ch);
    esp_etm_del_task(handle->i2s_sync_task);
    esp_etm_del_event(handle->modem_event);
    if (handle->i2s_intr) {
        esp_intr_free(handle->i2s_intr);
    }
    heap_caps_free(handle);
    return ESP_OK;
}

#endif  /* CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM */
