/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_err.h"
#include "esp_log.h"

#include "esp_bt_audio_host.h"

static const char *TAG = "BT_AUD_HOST";

/* NimBLE and BlueDroid are mutually exclusive host stacks. The init/deinit
 * dispatch below assumes at most one is enabled; guard against a misconfigured
 * build where both are selected.
 */
#if CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_BLUEDROID_ENABLED
#error "esp_bt_audio: enable only one of NimBLE or BlueDroid as the BT host"
#endif

#ifdef CONFIG_BT_BLUEDROID_ENABLED
extern esp_err_t bt_audio_host_bluedroid_stack_init(void *cfg);
extern void bt_audio_host_bluedroid_stack_deinit(void);
#endif  /* CONFIG_BT_BLUEDROID_ENABLED */

#ifdef CONFIG_BT_NIMBLE_ENABLED
extern esp_err_t bt_audio_host_nimble_stack_init(void *cfg);
extern void bt_audio_host_nimble_stack_deinit(void);
#endif  /* CONFIG_BT_NIMBLE_ENABLED */

esp_err_t esp_bt_audio_host_init(void *cfg)
{
#if CONFIG_BT_NIMBLE_ENABLED
    return bt_audio_host_nimble_stack_init(cfg);
#elif CONFIG_BT_BLUEDROID_ENABLED
    return bt_audio_host_bluedroid_stack_init(cfg);
#else
    ESP_LOGE(TAG, "Host init failed: no supported BT host is enabled");
    return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_NIMBLE_ENABLED */
}

void esp_bt_audio_host_deinit(void)
{
#if CONFIG_BT_NIMBLE_ENABLED
    bt_audio_host_nimble_stack_deinit();
#endif  /* CONFIG_BT_NIMBLE_ENABLED */
#if CONFIG_BT_BLUEDROID_ENABLED
    bt_audio_host_bluedroid_stack_deinit();
#endif  /* CONFIG_BT_BLUEDROID_ENABLED */
}
