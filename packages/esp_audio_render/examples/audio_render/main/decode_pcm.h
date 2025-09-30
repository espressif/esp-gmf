/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int decode_pcm(const char *url,
               int (*on_info)(int sample_rate, uint8_t channel, uint8_t bits, void *ctx),
               int (*on_pcm)(uint8_t *pcm, uint32_t len, void *ctx),
               int (*on_exited)(void *ctx),
               void *ctx,
               uint8_t core);

#ifdef __cplusplus
}
#endif
