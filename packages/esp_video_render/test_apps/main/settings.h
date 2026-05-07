/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define TEST_USE_UNITY
#define TEST_VIDEO_RENDER_FPS  10
#define FULL_TEST              0

#if !FULL_TEST
#define VIDEO_RENDER_USE_FAKE_BACKEND
#endif  /* !FULL_TEST */

#ifdef __cplusplus
}
#endif  /* __cplusplus */
