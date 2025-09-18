/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Function pointer type for peripheral initialization
 */
typedef esp_err_t (*esp_board_periph_init_func)(void *cfg, int cfg_size, void **periph_handle);

/**
 * @brief  Function pointer type for peripheral deinitialization
 */
typedef esp_err_t (*esp_board_periph_deinit_func)(void *periph_handle);

/**
 * @brief  Structure representing a peripheral descriptor
 */
typedef struct esp_board_periph_desc {
    const struct esp_board_periph_desc *next;      /*!< Pointer to next peripheral descriptor */
    const char                         *name;      /*!< Peripheral name */
    const char                         *type;      /*!< Peripheral type */
    const char                         *role;      /*!< Peripheral role */
    const char                         *format;    /*!< Peripheral format */
    const void                         *cfg;       /*!< Peripheral configuration data */
    int                                 cfg_size;  /*!< Size of configuration data */
    int                                 id;        /*!< Peripheral ID extracted from name (e.g., 48 from gpio48, 0 from iic0) */
} esp_board_periph_desc_t;

/**
 * @brief  Structure representing a peripheral entry
 */
typedef struct esp_board_periph_entry_ {
    struct esp_board_periph_entry_  *next;    /*!< Pointer to next peripheral entry */
    const char                      *type;    /*!< Peripheral type */
    const char                      *role;    /*!< Peripheral role */
    esp_board_periph_init_func       init;    /*!< Peripheral initialization function */
    esp_board_periph_deinit_func     deinit;  /*!< Peripheral deinitialization function */
} esp_board_periph_entry_t;

/**
 * @brief  Structure representing a peripheral list entry
 */
typedef struct esp_board_periph_list {
    struct esp_board_periph_list *next;           /*!< Pointer to next peripheral list entry */
    const char                   *name;           /*!< Peripheral name */
    const char                   *type;           /*!< Peripheral type */
    const char                   *role;           /*!< Peripheral role */
    void                         *periph_handle;  /*!< Peripheral-specific handle */
    unsigned int                  ref_count;      /*!< Reference count */
} esp_board_periph_list_t;

/**
 * @brief  Initialize a peripheral by name
 *
 *         This function initializes a peripheral with reference counting support. If the peripheral
 *         is already initialized, it increments the reference count instead of reinitializing.
 *         The peripheral is only actually initialized when the reference count reaches 1.
 *         Peripherals are found by matching type and role from the handle list.
 *
 * @param[in]  name  Peripheral name to initialize
 *
 * @return
 *       - ESP_OK                            On success
 *       - ESP_ERR_NO_MEM                    If memory allocation fails for list entry
 *       - ESP_BOARD_ERR_PERIPH_INVALID_ARG  If name is NULL
 *       - ESP_BOARD_ERR_PERIPH_NOT_FOUND    If peripheral descriptor not found
 *       - ESP_BOARD_ERR_PERIPH_NO_HANDLE    If no matching handle found for type/role
 *       - ESP_BOARD_ERR_PERIPH_NO_INIT      If no init function is available
 *       - Others                            Error codes from peripheral-specific initialization
 */
esp_err_t esp_board_periph_init(const char *name);

/**
 * @brief  Get peripheral handle by name
 *
 *         Retrieves the peripheral handle for a given peripheral name. The peripheral
 *         must be initialized before calling this function.
 *
 * @param[in]   name           Peripheral name
 * @param[out]  periph_handle  Pointer to store the peripheral handle
 *
 * @return
 *       - ESP_OK                            On success
 *       - ESP_BOARD_ERR_PERIPH_INVALID_ARG  If name or periph_handle is NULL
 *       - ESP_BOARD_ERR_PERIPH_NO_HANDLE    If peripheral not initialized or handle not found
 */
esp_err_t esp_board_periph_get_handle(const char *name, void **periph_handle);

/**
 * @brief  Get peripheral configuration by name
 *
 * @param[in]   name    Peripheral name
 * @param[out]  config  Pointer to store the configuration
 *
 * @return
 *       - ESP_OK                            On success
 *       - ESP_BOARD_ERR_PERIPH_INVALID_ARG  If name or config is NULL
 *       - ESP_BOARD_ERR_PERIPH_NOT_FOUND    If peripheral configuration not found
 *       - ESP_ERR_NOT_FOUND                 If peripheral has no configuration
 */
esp_err_t esp_board_periph_get_config(const char *name, void **config);

/**
 * @brief  Initialize peripheral with custom functions
 *
 *         Associates custom init and deinit functions with a peripheral and initializes it.
 *         This allows runtime configuration of peripheral behavior. If the peripheral
 *         is already initialized, only the functions are updated.
 *
 * @param[in]  name    Peripheral name
 * @param[in]  init    Initialization function pointer
 * @param[in]  deinit  Deinitialization function pointer
 *
 * @return
 *       - ESP_OK                            On success
 *       - ESP_ERR_NO_MEM                    If memory allocation fails for list entry
 *       - ESP_BOARD_ERR_PERIPH_INVALID_ARG  If any parameter is NULL
 *       - ESP_BOARD_ERR_PERIPH_NOT_FOUND    If peripheral descriptor not found
 *       - ESP_BOARD_ERR_PERIPH_NO_HANDLE    If no matching handle found for type/role
 *       - ESP_BOARD_ERR_PERIPH_INIT_FAILED  If initialization fails
 */
esp_err_t esp_board_periph_init_custom(const char *name, esp_board_periph_init_func init, esp_board_periph_deinit_func deinit);

/**
 * @brief  Deinitialize a peripheral by name
 *
 *         Decrements the reference count for a peripheral. The peripheral is only actually
 *         deinitialized when the reference count reaches 0. If the peripheral is not
 *         initialized, this function returns success.
 *
 * @param[in]  name  Peripheral name to deinitialize
 *
 * @return
 *       - ESP_OK                            On success
 *       - ESP_BOARD_ERR_PERIPH_INVALID_ARG  If name is NULL
 *       - ESP_BOARD_ERR_PERIPH_NOT_FOUND    If peripheral list entry not found
 *       - ESP_BOARD_ERR_PERIPH_NO_INIT      If no deinit function is available
 *       - Others                            Error codes from peripheral-specific deinitialization
 */
esp_err_t esp_board_periph_deinit(const char *name);

/**
 * @brief  Show peripheral information
 *
 *         Displays detailed information about peripherals. If name is NULL, shows
 *         information for all peripherals. Otherwise, shows information for the
 *         specified peripheral including type, role, handle status, and reference count.
 *
 * @param[in]  name  Peripheral name (NULL for all peripherals)
 *
 * @return
 *       - ESP_OK                          On success
 *       - ESP_BOARD_ERR_PERIPH_NOT_FOUND  If specific peripheral not found (when name is not NULL)
 */
esp_err_t esp_board_periph_show(const char *name);

/**
 * @brief  Initialize all peripherals
 *
 *         Iterates through all peripheral descriptors and initializes each peripheral.
 *         Continues initializing even if some peripherals fail, but logs errors.
 *
 *   NOTE: Peripheral initialization follows the order defined in board_peripherals.yaml
 *
 * @return
 *       - ESP_OK  On success (always returns ESP_OK, errors are logged)
 */
esp_err_t esp_board_periph_init_all(void);

/**
 * @brief  Deinitialize all peripherals
 *
 *         Iterates through all peripheral list entries and deinitializes each peripheral.
 *         Continues deinitializing even if some peripherals fail, but logs errors.
 *
 * @return
 *       - ESP_OK  On success (always returns ESP_OK, errors are logged)
 */
esp_err_t esp_board_periph_deinit_all(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
