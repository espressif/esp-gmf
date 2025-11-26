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
 * @brief  Extra function descriptor structure
 *
 *         This structure is placed in a special linker section (.esp_board_extra_func)
 *         and automatically discovered by the extra function system.
 */
typedef struct {
    const char  *owner_name;  /*!< Extra function name (must be unique) */
    void        *extra_func;  /*!< Extra function function pointer */
} esp_board_extra_func_desc_t;

/**
 * @brief  Macro to define a extra_func implementation
 *
 *         This macro places the extra_func descriptor in the special linker section
 *         for automatic discovery by the extra_func system.
 *
 * @param  name        Extra function name (must be unique)
 * @param  extra_func  Extra function function pointer
 * @param  ctx         Optional: user context pointer (can be NULL)
 */
#define EXTRA_FUNC_IMPLEMENT(_name, _extra_func)                                                     \
    static const esp_board_extra_func_desc_t __attribute__((section(".esp_board_extra_func"), used)) \
    esp_board_extra_func_##_name = {                                                                 \
        .owner_name = #_name,                                                                        \
        .extra_func = (void *)_extra_func,                                                           \
    }

/**
 * @brief  Get extra_func function pointer and context by name
 *
 *         This function retrieves both the extra_func function pointer and the user context
 *         for a given extra_func name.
 *
 * @param  owner_name  Name of the extra_func owner
 * @param  extra_func  Pointer to store the extra_func function pointer (output)
 * @param  ctx         Pointer to store the user context pointer (output)
 * @return
 *       - 0   on success (extra_func found)
 *       - -1  if extra_func not found
 */
static inline int esp_board_extra_func_get(const char *owner_name, void *extra_func)
{
    extern const esp_board_extra_func_desc_t _esp_board_extra_func_array_start;
    extern const esp_board_extra_func_desc_t _esp_board_extra_func_array_end;

    for (const esp_board_extra_func_desc_t *desc = &_esp_board_extra_func_array_start;
         desc != &_esp_board_extra_func_array_end; desc++) {
        if (strcmp(desc->owner_name, owner_name) == 0) {
            *(void **)extra_func = desc->extra_func;
            return 0;
        }
    }
    return -1;
}

/**
 * @brief  List all registered extra_funcs (for debugging)
 */
static inline void esp_board_extra_func_list_all(void)
{
    printf("=== Registered Callbacks (from linker section) ===\n");
    extern const esp_board_extra_func_desc_t _esp_board_extra_func_array_start;
    extern const esp_board_extra_func_desc_t _esp_board_extra_func_array_end;

    int count = 0;
    for (const esp_board_extra_func_desc_t *it = &_esp_board_extra_func_array_start;
         it != &_esp_board_extra_func_array_end; ++it) {
        printf("- Extra function '%s', func=%p\n", it->owner_name, it->extra_func);
        count++;
    }
    printf("Total extra_funcs: %d\n", count);
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */
