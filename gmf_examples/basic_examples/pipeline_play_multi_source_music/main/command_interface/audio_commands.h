/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Initialize audio command interface
 *
 * @return
 *       - 0  on success, non-zero on error
 */
int audio_commands_init(void);

/**
 * @brief  Deinitialize audio command interface
 *
 * @return
 *       - 0  on success, non-zero on error
 */
int audio_commands_deinit(void);

/**
 * @brief  Register all audio CLI commands
 *
 *         This function matches the console_cmds_register callback signature
 *         required by esp_gmf_app_cli_init()
 */
void audio_commands_register_all(void);

/**
 * @brief  Check if the application should keep running
 *
 * @return
 *       - true  if app should keep running, false to exit
 */
bool audio_commands_keep_running(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
