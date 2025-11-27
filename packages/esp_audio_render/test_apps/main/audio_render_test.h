/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

int audio_render_proc_bypass_test(int write_count);

int audio_render_proc_basic_test(int write_count);

int audio_render_proc_typical_test(int write_count);

int audio_render_one_stream_no_proc(int write_count);

int audio_render_one_stream_with_proc(int write_count);

int audio_render_one_stream_with_enc_post(int write_count);

int audio_render_dual_stream_no_proc(int write_count);

int audio_render_dual_stream_with_proc(int write_count);

int audio_render_dual_stream_one_slow(int write_count);

int audio_render_with_no_pool(int write_count);

int audio_render_dual_stream_solo(int write_count);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
