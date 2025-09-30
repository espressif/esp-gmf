/**
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Musical note frequencies (C4 = middle C)
 */
typedef enum {
    NOTE_C = 0,  /*!< Do */
    NOTE_D,      /*!< Re */
    NOTE_E,      /*!< Mi */
    NOTE_F,      /*!< Fa */
    NOTE_G,      /*!< Sol */
    NOTE_A,      /*!< La */
    NOTE_B,      /*!< Si */
    NOTE_COUNT   /*!< Note count */
} piano_note_t;

/**
 * @brief  Octave definitions
 */
typedef enum {
    OCTAVE_2 = 2,
    OCTAVE_3 = 3,
    OCTAVE_4 = 4,  /*!< Middle C */
    OCTAVE_5 = 5,
    OCTAVE_6 = 6,
    OCTAVE_7 = 7
} piano_octave_t;

/**
 * @brief  Track definition for multi-track music
 */
typedef struct {
    uint8_t       track_id;
    piano_note_t  notes[64];      /*!< Up to 64 notes per track */
    uint8_t       octaves[64];    /*!< Octave for each note*/
    uint16_t      durations[64];  /*!< Duration in milliseconds */
    uint8_t       note_count;
    bool          active;
} piano_track_t;

/**
 * @brief  Piano tone generator configuration
 */
typedef struct {
    uint32_t  sample_rate;
    uint8_t   channels;
    uint8_t   bits_per_sample;
    float     volume;          /*!< 0.0 to 1.0 */
    float     attack_time;     /*!< Attack time in seconds */
    float     decay_time;      /*!< Decay time in seconds */
    float     sustain_level;   /*!< Sustain level (0.0 to 1.0) */
    float     release_time;   /*!<  Release time in seconds */
} piano_tone_cfg_t;

/**
 * @brief  Piano tone generator handle
 */
typedef struct piano_tone_gen *piano_tone_gen_handle_t;

/**
 * @brief  Create piano tone generator
 *
 * @param[in]   cfg     Configuration parameters
 * @param[out]  handle  Output handle
 * @return
 *       - 0       On success
 *       - Others  Failed to create generator
 */
int piano_tone_gen_create(const piano_tone_cfg_t *cfg, piano_tone_gen_handle_t *handle);

/**
 * @brief  Generate piano tone for a specific note and octave
 *
 * @param[in]   handle       Generator handle
 * @param[in]   note         Musical note (C, D, E, F, G, A, B)
 * @param[in]   octave       Octave number (2-7)
 * @param[in]   duration_ms  Duration in milliseconds
 * @param[out]  buffer       Output buffer for PCM data
 * @param[in]   buffer_size  Size of output buffer
 *
 * @return
 *      - 0       On success
 *      - Others  Failed to generate note
 */
int piano_tone_gen_note(piano_tone_gen_handle_t handle,
                        piano_note_t            note,
                        piano_octave_t          octave,
                        uint16_t                duration_ms,
                        int16_t                *buffer,
                        uint32_t                buffer_size);

/**
 * @brief  Generate piano tone for a specific note and octave starting from a time offset
 *
 * @param[in]   handle                 Generator handle
 * @param[in]   note                   Musical note (C, D, E, F, G, A, B)
 * @param[in]   octave                 Octave number (2-7)
 * @param[in]   duration_ms            Duration in milliseconds (chunk duration)
 * @param[out]  buffer                 Output buffer for PCM data
 * @param[in]   buffer_size            Size of output buffer
 * @param[in]   start_time_offset_ms   Time offset from note start to begin generation
 * @param[in]   full_note_duration_ms  Full duration of the complete note (for envelope calculation)
 *
 * @return
 *      - 0       On success
 *      - Others  Failed to generate note
 */
int piano_tone_gen_note_from_offset(piano_tone_gen_handle_t handle,
                                    piano_note_t            note,
                                    piano_octave_t          octave,
                                    uint16_t                duration_ms,
                                    int16_t                *buffer,
                                    uint32_t                buffer_size,
                                    uint32_t                start_time_offset_ms,
                                    uint16_t                full_note_duration_ms);

/**
 * @brief  Generate piano tone for a specific note and octave (partial note support)
 *
 * @param[in]   handle          Generator handle
 * @param[in]   note            Musical note (C, D, E, F, G, A, B)
 * @param[in]   octave          Octave number (2-7)
 * @param[in]   duration_ms     Duration in milliseconds
 * @param[out]  buffer          Output buffer for PCM data
 * @param[in]   buffer_size     Size of output buffer
 * @param[in]   time_offset_ms  Time offset from note start (for partial note generation)
 *
 * @return
 *      - 0       On success
 *      - Others  Failed to generate note
 */
int piano_tone_gen_note_partial(piano_tone_gen_handle_t handle,
                                piano_note_t            note,
                                piano_octave_t          octave,
                                uint16_t                duration_ms,
                                int16_t                *buffer,
                                uint32_t                buffer_size,
                                uint32_t                time_offset_ms);

/**
 * @brief  Generate chord (multiple notes simultaneously)
 *
 * @param[in]   handle       Generator handle
 * @param[in]   notes        Array of notes
 * @param[in]   octaves      Array of octaves
 * @param[in]   note_count   Number of notes in chord
 * @param[in]   duration_ms  Duration in milliseconds
 * @param[out]  buffer       Output buffer for PCM data
 * @param[in]   buffer_size  Size of output buffer
 *
 * @return
 *      - 0       On success
 *      - Others  Failed to generate chord
 */
int piano_tone_gen_chord(piano_tone_gen_handle_t handle,
                         const piano_note_t     *notes,
                         const piano_octave_t   *octaves,
                         uint8_t                 note_count,
                         uint16_t                duration_ms,
                         int16_t                *buffer,
                         uint32_t                buffer_size);

/**
 * @brief  Destroy piano tone generator
 *
 * @param[in]  handle  Generator handle
 */
void piano_tone_gen_destroy(piano_tone_gen_handle_t handle);

/**
 * @brief  Get frequency for a note and octave
 *
 * @param[in]  note    Musical note
 * @param[in]  octave  Octave number
 *
 * @return
 *       - Note frequency
 */
float piano_get_note_frequency(piano_note_t note, piano_octave_t octave);

/**
 * @brief  Get note name string
 *
 * @param[in]  note  Musical note
 *
 * @return
 *       - Note name
 */
const char *piano_get_note_name(piano_note_t note);

#ifdef __cplusplus
}
#endif
