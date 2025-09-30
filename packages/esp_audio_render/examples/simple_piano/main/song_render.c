/**
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "song_render.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "SONG_RENDER"

#define MAX_NOTE_NUM (64)

// Track state
typedef struct {
    uint8_t         track_id;
    piano_note_t    notes[MAX_NOTE_NUM];
    piano_octave_t  octaves[MAX_NOTE_NUM];
    uint16_t        durations[MAX_NOTE_NUM];
    uint8_t         note_count;
    uint8_t         current_note;
    uint32_t        played_duration;
    bool            active;
} track_state_t;

// Song renderer structure
typedef struct song_render {
    song_cfg_t                 cfg;
    track_state_t              tracks[MAX_TRACKS];
    piano_tone_gen_handle_t    tone_gen;
    esp_audio_render_handle_t  audio_render;
    bool                       playing;
    bool                       mute;
    uint32_t                   current_position;
    TaskHandle_t               render_task;
    piano_note_t               realtime_note;
    piano_octave_t             realtime_oct;
    uint32_t                   press_time;
    uint32_t                   release_time;
    uint8_t                    realtime_track;
    bool                       realtime_active;
} song_render_t;

// Piano tone generator configuration
static const piano_tone_cfg_t default_piano_cfg = {
    .sample_rate = 16000,
    .channels = 1,
    .bits_per_sample = 16,
    .volume = 0.8f,
    .attack_time = 0.01f,  // 10ms attack
    .decay_time = 0.05f,   // 50ms decay
    .sustain_level = 0.7f, // 70% sustain
    .release_time = 0.1f   // 100ms release
};

// Render task function
static void song_render_task(void *arg)
{
    song_render_t *render = (song_render_t *)arg;
    const uint32_t chunk_duration_ms = 20; // Process in 20ms chunks
    uint32_t pcm_size = (render->cfg.sample_rate * chunk_duration_ms / 1000) * render->cfg.channels * 2;
    ESP_LOGI(TAG, "Song render task started with %" PRIu32 " ms chunks", chunk_duration_ms);
    uint32_t total_duration = song_render_get_duration((song_render_handle_t)render);
    ESP_LOGI(TAG, "Total song duration: %" PRIu32, total_duration);
    uint32_t current_time = 0;
    uint8_t *pcm_buffer = malloc(pcm_size);
    while (pcm_buffer && render->playing && (current_time < total_duration || render->cfg.loop)) {
        if (current_time > total_duration) {
            for (int track_idx = 0; track_idx < MAX_TRACKS; track_idx++) {
                track_state_t *track = &render->tracks[track_idx];
                track->current_note = 0;
                track->played_duration = 0;
            }
            current_time = 0;
            render->press_time = 0;
            render->release_time = 0;
            render->realtime_note = NOTE_COUNT;
        }
        // Process each track independently and send continuous data to each stream
        for (int track_idx = 0; track_idx < MAX_TRACKS; track_idx++) {
            if (!render->tracks[track_idx].active) {
                continue;
            }

            track_state_t *track = &render->tracks[track_idx];
            memset(pcm_buffer, 0, pcm_size);

            // Process notes for this track
            for (int note_idx = track->current_note; note_idx < track->note_count; note_idx++) {
                uint32_t note_start = track->played_duration;
                uint32_t note_end = track->played_duration + track->durations[note_idx];
                if (current_time >= note_end) {
                    // Move to next note
                    track->played_duration += track->durations[note_idx];
                    track->current_note = note_idx + 1;
                    continue;
                }
                // Check if this note is currently active
                if (current_time >= note_start && current_time < note_end && render->mute == false) {
                    int note_elapsed = current_time - note_start;
                    // Generate the note starting from the current time position
                    int samples = piano_tone_gen_note_from_offset(
                        render->tone_gen, track->notes[note_idx], track->octaves[note_idx], chunk_duration_ms,
                        (int16_t *)pcm_buffer, pcm_size, note_elapsed, track->durations[note_idx]);
                    if (samples > 0) {
                        ESP_LOGD(TAG, "Track %d:%d gen note %s%d at %" PRIu32 "ms (%d-%d), samples=%d", track->track_id,
                                 track->current_note, piano_get_note_name(track->notes[note_idx]),
                                 track->octaves[note_idx], current_time, note_elapsed, track->durations[note_idx],
                                 samples);
                    }
                }
                break;
            }
            esp_audio_render_stream_handle_t stream = NULL;
            esp_audio_render_stream_get(render->audio_render, ESP_AUDIO_RENDER_STREAM_ID(track->track_id), &stream);
            esp_audio_render_stream_write(stream, (uint8_t *)pcm_buffer, pcm_size);
        }
        // Handle realtime track
        if (render->realtime_track) {
            esp_audio_render_stream_handle_t real_stream = NULL;
            esp_audio_render_stream_get(render->audio_render, ESP_AUDIO_RENDER_STREAM_ID(render->realtime_track), &real_stream);
            bool need_send = false;
            if (render->realtime_note != NOTE_COUNT) {
                if (current_time >= render->press_time) {
                    piano_tone_gen_note_from_offset(
                        render->tone_gen, render->realtime_note, render->realtime_oct, chunk_duration_ms,
                        (int16_t *)pcm_buffer, pcm_size, current_time - render->press_time, 0xFFFF);
                    need_send = true;
                }
            } else {
                if (render->realtime_active &&
                    current_time >= render->press_time && current_time < render->release_time + 50) {
                    uint16_t duration = render->release_time + 50 - current_time;
                    piano_tone_gen_note_from_offset(
                        render->tone_gen, render->realtime_note, render->realtime_oct, chunk_duration_ms,
                        (int16_t *)pcm_buffer, pcm_size, current_time - render->press_time, duration);
                    need_send = true;
                }
                if (current_time > render->release_time + 200) {
                    if (render->realtime_active) {
                        esp_audio_render_stream_close(real_stream);
                        render->realtime_active = false;
                        render->release_time  = 0;
                        render->realtime_note = NOTE_COUNT;
                        ESP_LOGI(TAG, "Realtime track closed");
                    }
                }
            }
            if (need_send) {
                if (render->realtime_active == false) {
                    esp_audio_render_sample_info_t info = {
                        .sample_rate = render->cfg.sample_rate,
                        .bits_per_sample = render->cfg.bits_per_sample,
                        .channel = render->cfg.channels,
                    };
                    esp_audio_render_stream_open(real_stream, &info);
                    render->realtime_active = true;
                    ESP_LOGI(TAG, "Realtime track opened");
                }
                esp_audio_render_stream_write(real_stream, (uint8_t *)pcm_buffer, pcm_size);
            }
        }
        current_time += chunk_duration_ms;
        render->current_position = current_time;
    }
    if (pcm_buffer) {
        free(pcm_buffer);
    }
    ESP_LOGI(TAG, "Song render task finished, total time: %" PRIu32 " ms", current_time);
    vTaskDelete(NULL);
}

int song_render_create(const song_cfg_t *cfg, song_render_handle_t *handle)
{
    if (!cfg || !handle) {
        return -1;
    }
    song_render_t *render = calloc(1, sizeof(song_render_t));
    if (!render) {
        return -1;
    }
    memcpy(&render->cfg, cfg, sizeof(song_cfg_t));
    // Create piano tone generator
    piano_tone_cfg_t piano_cfg = default_piano_cfg;
    piano_cfg.sample_rate = cfg->sample_rate;
    piano_cfg.channels = cfg->channels;
    piano_cfg.bits_per_sample = cfg->bits_per_sample;
    if (piano_tone_gen_create(&piano_cfg, &render->tone_gen) != 0) {
        ESP_LOGE(TAG, "Failed to create piano tone generator");
        free(render);
        return -1;
    }

    // Initialize tracks
    for (int i = 0; i < MAX_TRACKS; i++) {
        render->tracks[i].track_id = i;
        render->tracks[i].active = false;
        render->tracks[i].current_note = 0;
        render->tracks[i].played_duration = 0;
    }
    if (cfg->realtime_track) {
        render->realtime_track = MAX_TRACKS;
    }
    render->realtime_note = NOTE_COUNT;
    *handle = render;
    ESP_LOGI(TAG, "Song renderer created: %d tracks, %d BPM", cfg->track_count, cfg->tempo);
    return 0;
}

int song_render_add_track(song_render_handle_t handle, uint8_t track_id, const piano_note_t *notes,
                          const piano_octave_t *octaves, const uint16_t *durations, uint8_t note_count)
{
    if (!handle || !notes || !octaves || !durations || track_id >= MAX_TRACKS) {
        return -1;
    }
    song_render_t *render = (song_render_t *)handle;
    track_state_t *track = &render->tracks[track_id];
    if (note_count > MAX_NOTE_NUM) {
        ESP_LOGW(TAG, "Track %d has too many notes (%d), truncating to MAX_NOTE_NUM", track_id, note_count);
        note_count = MAX_NOTE_NUM;
    }
    memcpy(track->notes, notes, note_count * sizeof(piano_note_t));
    memcpy(track->octaves, octaves, note_count * sizeof(piano_octave_t));
    memcpy(track->durations, durations, note_count * sizeof(uint16_t));
    track->note_count = note_count;
    track->active = true;
    track->track_id = track_id;
    track->current_note = 0;
    ESP_LOGI(TAG, "Added track %d with %d notes", track_id, note_count);
    return 0;
}

int song_render_add_realtime(song_render_handle_t handle, piano_note_t note, piano_octave_t oct, bool released)
{
     if (!handle) {
        return -1;
    }
    song_render_t *render = (song_render_t *)handle;
    if (render->realtime_note != NOTE_COUNT) {
        // Already pressed key
        if (released == false) {
            ESP_LOGI(TAG, "Skip for only support one");
            return 0;
        }
        if (note == render->realtime_note && oct == render->realtime_oct) {
            render->realtime_note = NOTE_COUNT;
            render->release_time = render->current_position;
        }
    } else {
        if (released) {
            ESP_LOGI(TAG, "Skip for release for no key pressed");
            return 0;
        }
        render->realtime_note = note;
        render->realtime_oct = oct;
        render->press_time = render->current_position;
    }
    return 0;
}

void song_render_mute(song_render_handle_t handle, bool mute)
{
    if (!handle) {
        return;
    }
    song_render_t *render = (song_render_t *)handle;
    render->mute = mute;
    ESP_LOGI(TAG, "Mute set to %d", mute);
}

int song_render_play(song_render_handle_t handle, esp_audio_render_handle_t audio_render)
{
    if (!handle || !audio_render) {
        return -1;
    }
    song_render_t *render = (song_render_t *)handle;
    if (render->playing) {
        ESP_LOGW(TAG, "Song is already playing");
        return -1;
    }
    render->audio_render = audio_render;
    render->playing = true;
    render->current_position = 0;

    // Reset all tracks
    for (int i = 0; i < MAX_TRACKS; i++) {
        if (render->tracks[i].active) {
            render->tracks[i].current_note = 0;
            render->tracks[i].played_duration = 0;
        }
    }

    // Create render task
    BaseType_t ret = xTaskCreate(song_render_task, "song_render", 10 * 1024, render, 5, &render->render_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create render task");
        render->playing = false;
        return -1;
    }

    ESP_LOGI(TAG, "Song started playing");
    return 0;
}

void song_render_stop(song_render_handle_t handle)
{
    if (!handle) {
        return;
    }

    song_render_t *render = (song_render_t *)handle;
    if (render->playing) {
        render->playing = false;
        // Wait for task to finish
        if (render->render_task) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        ESP_LOGI(TAG, "Song stopped");
    }
}

bool song_render_is_playing(song_render_handle_t handle)
{
    if (!handle) {
        return false;
    }
    return ((song_render_t *)handle)->playing;
}

void song_render_destroy(song_render_handle_t handle)
{
    if (!handle) {
        return;
    }

    song_render_t *render = (song_render_t *)handle;

    // Stop if playing
    if (render->playing) {
        song_render_stop(handle);
    }

    // Clean up resources
    if (render->tone_gen) {
        piano_tone_gen_destroy(render->tone_gen);
    }

    free(render);
    ESP_LOGI(TAG, "Song renderer destroyed");
}

uint32_t song_render_get_position(song_render_handle_t handle)
{
    if (!handle) {
        return 0;
    }
    return ((song_render_t *)handle)->current_position;
}

uint32_t song_render_get_duration(song_render_handle_t handle)
{
    if (!handle) {
        return 0;
    }

    song_render_t *render = (song_render_t *)handle;
    uint32_t max_duration = 0;
    // Find the longest track duration
    for (int i = 0; i < MAX_TRACKS; i++) {
        if (render->tracks[i].active) {
            uint32_t track_duration = 0;
            for (int j = 0; j < render->tracks[i].note_count; j++) {
                track_duration += render->tracks[i].durations[j];
            }
            if (track_duration > max_duration) {
                max_duration = track_duration;
            }
        }
    }

    return max_duration;
}
