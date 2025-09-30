/**
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "piano_tone_gen.h"
#include "esp_audio_render.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Maximum number of tracks supported
 */
#define MAX_TRACKS 4

/**
 * @brief  Song configuration
 */
typedef struct {
    uint32_t  sample_rate;
    uint8_t   channels;
    uint8_t   bits_per_sample;
    uint8_t   track_count;
    uint16_t  tempo;
    uint16_t  beat_duration_ms;
    bool      loop;
    bool      realtime_track;
} song_cfg_t;

/**
 * @brief  Song render handle
 */
typedef struct song_render *song_render_handle_t;

/**
 * @brief  Create song renderer
 *
 * @param[in]   cfg     Song configuration
 * @param[out]  handle  Song render handle
 *
 * @return
 *       - 0       On success
 *       - Others  Failed to create
 */
int song_render_create(const song_cfg_t *cfg, song_render_handle_t *handle);

/**
 * @brief  Add a track to the song
 *
 * @param[in]  handle      Song renderer handle
 * @param[in]  track_id    Track identifier (0 to MAX_TRACKS-1)
 * @param[in]  notes       Array of notes
 * @param[in]  octaves     Array of octaves
 * @param[in]  durations   Array of durations in milliseconds
 * @param[in]  note_count  Number of notes in the track
 *
 * @return
 *       - 0       On success
 *       - Others  Failed to add track
 */
int song_render_add_track(song_render_handle_t  handle,
                          uint8_t               track_id,
                          const piano_note_t   *notes,
                          const piano_octave_t *octaves,
                          const uint16_t       *durations,
                          uint8_t               note_count);

/**
 * @brief  Start playing the song using audio render API
 *
 * @param[in]  handle        Song renderer handle
 * @param[in]  audio_render  Audio render handle
 *
 * @return
 *       - 0       On success
 *       - Others  Failed to play
 */
int song_render_play(song_render_handle_t handle, esp_audio_render_handle_t audio_render);

/**
 * @brief  Stop playing the song
 *
 * @param  handle  Song renderer handle
 */
void song_render_stop(song_render_handle_t handle);

/**
 * @brief  Check if song is currently playing
 *
 * @param  handle  Song renderer handle
 *
 * @return
 *       - true   Render is playing
 *       - false  Not playing
 */
bool song_render_is_playing(song_render_handle_t handle);

/**
 * @brief  Playback in realtime
 *
 * @param  handle    Song renderer handle
 * @param  note      Note
 * @param  oct       Octave
 * @param  released  Note pressed or released
 *
 * @return
 *       - 0   On success
 *       - -1  Failed
 *
 */
int song_render_add_realtime(song_render_handle_t handle, piano_note_t note, piano_octave_t oct, bool released);

/**
 * @brief  Mute song renderer
 *
 * @param  handle  Song renderer handle
 * @param  mute    Whether to mute song playback
 */
void song_render_mute(song_render_handle_t handle, bool mute);

/**
 * @brief  Destroy song renderer
 *
 * @param  handle  Song renderer handle
 */
void song_render_destroy(song_render_handle_t handle);

/**
 * @brief  Get current playback position
 *
 * @param  handle  Song renderer handle
 *
 * @return
 *       - Actual render position
 */
uint32_t song_render_get_position(song_render_handle_t handle);

/**
 * @brief  Get total song duration
 *
 * @param  handle  Song renderer handle
 *
 * @return
 *       - Duration of all tracks
 */
uint32_t song_render_get_duration(song_render_handle_t handle);

#ifdef __cplusplus
}
#endif
