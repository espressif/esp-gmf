/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Function pointer type for board entry initialization
 *
 * @param  config         Configuration data for the entry
 * @param  cfg_size       Size of the configuration data
 * @param  device_handle  Pointer to store the device handle
 *
 * @return
 *       - 0   On success
 *       - <0  On failure
 */
typedef int (*esp_board_entry_init_func_t)(void *config, int cfg_size, void **device_handle);

/**
 * @brief  Function pointer type for board entry deinitialization
 *
 * @param  device_handle  Device handle to deinitialize
 *
 * @return
 *       -  0  On success
 *       - <0  On failure
 */
typedef int (*esp_board_entry_deinit_func_t)(void *device_handle);

/**
 * @brief  Structure describing a board entry implementation
 *
 *         This structure is placed in a special linker section (.esp_board_entries_desc)
 *         and automatically discovered by the board entry system.
 */
typedef struct {
    const char                     *entry_name;  /*!< Board entry name (must match YAML config) */
    esp_board_entry_init_func_t    init_func;    /*!< Entry initialization function */
    esp_board_entry_deinit_func_t  deinit_func;  /*!< Entry deinitialization function */
} esp_board_entry_desc_t;

/**
 * @brief  Macro to define a board entry implementation
 *
 *         This macro places the entry descriptor in the special linker section
 *         for automatic discovery by the board entry system
 *
 * @param  name         Entry name (must match YAML config)
 * @param  init_func    Initialization function pointer
 * @param  deinit_func  Deinitialization function pointer
 */
#define ESP_BOARD_ENTRY_IMPLEMENT(name, init_func_entry, deinit_func_entry) \
    static const esp_board_entry_desc_t __attribute__((section(".esp_board_entries_desc"), used)) \
    esp_board_entry_##name = { \
        .entry_name = #name, \
        .init_func = init_func_entry, \
        .deinit_func = deinit_func_entry \
    }

/**
 * @brief  Legacy macro for board device implementation
 *
 *         This is a compatibility macro that maps to ESP_BOARD_ENTRY_IMPLEMENT.
 *         It is provided for backward compatibility with existing code.
 *
 * @param  name    Device name
 * @param  init    Initialization function
 * @param  deinit  Deinitialization function
 */
#define CUSTOM_DEVICE_IMPLEMENT(name, init, deinit)  ESP_BOARD_ENTRY_IMPLEMENT(name, init, deinit)

/**
 * @brief  List all registered board entries
 *
 *         This function prints information about all statically registered entries to the console.
 *         It iterates through the linker section containing all registered entries and displays
 *         their names and function pointers.
 */
static inline void esp_board_entry_list_all(void)
{
    /* Use printf for easy control of the output format */
    printf("=== Static Entries (from linker section) ===\n");
    extern const esp_board_entry_desc_t _esp_board_entries_array_start;
    extern const esp_board_entry_desc_t _esp_board_entries_array_end;
    int count = 0;
    for (const esp_board_entry_desc_t *it = &_esp_board_entries_array_start;
         it != &_esp_board_entries_array_end; ++it) {
        printf("- Entry '%s', init=%p, deinit=%p\n",
               it->entry_name, it->init_func, it->deinit_func);
        count++;
    }
    printf("Total entries: %d\n", count);
}

/**
 * @brief  Find a board entry descriptor by name
 *
 *         This function searches for an entry by name in static registrations.
 *         It performs a linear search through the linker section containing
 *         all registered entries.
 *
 * @param  entry_name  Name of the entry to find
 *
 * @return
 *       - NULL    If not found or entry_name is NULL
 *       - Others  Pointer to the entry descriptor
 */
static inline const esp_board_entry_desc_t *esp_board_entry_find_desc(const char *entry_name)
{
    if (entry_name == NULL) {
        return NULL;
    }
    extern const esp_board_entry_desc_t _esp_board_entries_array_start;
    extern const esp_board_entry_desc_t _esp_board_entries_array_end;
    for (const esp_board_entry_desc_t *desc = &_esp_board_entries_array_start;
         desc != &_esp_board_entries_array_end; desc++) {
        if (strcmp(desc->entry_name, entry_name) == 0) {
            return desc;
        }
    }
    return NULL;
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */
