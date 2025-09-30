/**
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_pool.h"
#include "esp_codec_dev.h"
#include "esp_audio_render.h"
#include "piano_tone_gen.h"
#include "song_render.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "driver/uart.h"

#define TAG "PIANO_EXAMPLE"

#define SUPPORT_REALTIME_TRACK

// Classic song: "Twinkle Twinkle Little Star" with harmony
static const piano_note_t melody_notes[] = {
    NOTE_C, NOTE_C, NOTE_G, NOTE_G, NOTE_A, NOTE_A, NOTE_G, // Twinkle twinkle little star
    NOTE_F, NOTE_F, NOTE_E, NOTE_E, NOTE_D, NOTE_D, NOTE_C, // How I wonder what you are
    NOTE_G, NOTE_G, NOTE_F, NOTE_F, NOTE_E, NOTE_E, NOTE_D, // Up above the world so high
    NOTE_G, NOTE_G, NOTE_F, NOTE_F, NOTE_E, NOTE_E, NOTE_D, // Like a diamond in the sky
    NOTE_C, NOTE_C, NOTE_G, NOTE_G, NOTE_A, NOTE_A, NOTE_G, // Twinkle twinkle little star
    NOTE_F, NOTE_F, NOTE_E, NOTE_E, NOTE_D, NOTE_D, NOTE_C  // How I wonder what you are
};

static const piano_octave_t melody_octaves[] = {
    OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, // C4, C4, G4, G4, A4, A4, G4
    OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, // F4, F4, E4, E4, D4, D4, C4
    OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, // G4, G4, F4, F4, E4, E4, D4
    OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, // G4, G4, F4, F4, E4, E4, D4
    OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, // C4, C4, G4, G4, A4, A4, G4
    OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_4  // F4, F4, E4, E4, D4, D4, C4
};

static const uint16_t melody_durations[] = {
    500, 500, 500, 500, 500, 500, 1000, // Quarter notes, half note at end
    500, 500, 500, 500, 500, 500, 1000,
    500, 500, 500, 500, 500, 500, 1000,
    500, 500, 500, 500, 500, 500, 1000,
    500, 500, 500, 500, 500, 500, 1000,
    500, 500, 500, 500, 500, 500, 1000
};

// Harmony track (chords)
static const piano_note_t harmony_notes[] = {
    NOTE_C, NOTE_G, NOTE_C, NOTE_G, NOTE_F, NOTE_C, NOTE_F, NOTE_C, // C major, F major
    NOTE_G, NOTE_D, NOTE_G, NOTE_D, NOTE_C, NOTE_G, NOTE_C, NOTE_G,
    NOTE_C, NOTE_G, NOTE_C, NOTE_G, NOTE_F, NOTE_C, NOTE_F, NOTE_C
};

static const piano_octave_t harmony_octaves[] = {
    OCTAVE_3, OCTAVE_3, OCTAVE_4, OCTAVE_4, OCTAVE_3, OCTAVE_3, OCTAVE_4, OCTAVE_4, // Lower octave for harmony
    OCTAVE_3, OCTAVE_3, OCTAVE_4, OCTAVE_4, OCTAVE_3, OCTAVE_3, OCTAVE_4, OCTAVE_4,
    OCTAVE_3, OCTAVE_3, OCTAVE_4, OCTAVE_4, OCTAVE_3, OCTAVE_3, OCTAVE_4, OCTAVE_4
};

static const uint16_t harmony_durations[] = {
    1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, // Half notes
    1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000,
    1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000
};

// Bass line
static const piano_note_t bass_notes[] = {
    NOTE_C, NOTE_C, NOTE_G, NOTE_G, NOTE_F, NOTE_F, NOTE_C, NOTE_C,
    NOTE_G, NOTE_G, NOTE_C, NOTE_C, NOTE_G, NOTE_G, NOTE_C, NOTE_C,
    NOTE_C, NOTE_C, NOTE_G, NOTE_G, NOTE_F, NOTE_F, NOTE_C, NOTE_C
};

static const piano_octave_t bass_octaves[] = {
    OCTAVE_2, OCTAVE_2, OCTAVE_2, OCTAVE_2, OCTAVE_2, OCTAVE_2, OCTAVE_2, OCTAVE_2, // Very low octave for bass
    OCTAVE_2, OCTAVE_2, OCTAVE_2, OCTAVE_2, OCTAVE_2, OCTAVE_2, OCTAVE_2, OCTAVE_2,
    OCTAVE_2, OCTAVE_2, OCTAVE_2, OCTAVE_2, OCTAVE_2, OCTAVE_2, OCTAVE_2, OCTAVE_2
};

static const uint16_t bass_durations[] = {
    1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000,
    1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000,
    1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000
};

// Arpeggio track (broken chords)
static const piano_note_t arpeggio_notes[] = {
    NOTE_C, NOTE_E, NOTE_G, NOTE_C, NOTE_E, NOTE_G, NOTE_C, NOTE_E, // C major arpeggio
    NOTE_F, NOTE_A, NOTE_C, NOTE_F, NOTE_A, NOTE_C, NOTE_F, NOTE_A,
    NOTE_G, NOTE_B, NOTE_D, NOTE_G, NOTE_B, NOTE_D, NOTE_G, NOTE_B
};

static const piano_octave_t arpeggio_octaves[] = {
    OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_5, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_5, // Middle to high octave
    OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_5, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_5,
    OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_5, OCTAVE_4, OCTAVE_4, OCTAVE_4, OCTAVE_5
};

static const uint16_t arpeggio_durations[] = {
    250, 250, 250, 250, 250, 250, 250, 250, // Eighth notes
    250, 250, 250, 250, 250, 250, 250, 250,
    250, 250, 250, 250, 250, 250, 250, 250
};

static bool realtime_track_running = false;

static int create_default_pool(esp_gmf_pool_handle_t *pool)
{
    *pool = NULL;
    if (esp_gmf_pool_init(pool) != ESP_GMF_ERR_OK) {
        return -1;
    }

    esp_gmf_element_handle_t el = NULL;
    esp_ae_ch_cvt_cfg_t ch_cvt_cfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    esp_gmf_ch_cvt_init(&ch_cvt_cfg, &el);
    esp_gmf_pool_register_element(*pool, el, NULL);

    esp_ae_bit_cvt_cfg_t bit_cvt_cfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
    esp_gmf_bit_cvt_init(&bit_cvt_cfg, &el);
    esp_gmf_pool_register_element(*pool, el, NULL);

    esp_ae_rate_cvt_cfg_t rate_cvt_cfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
    esp_gmf_rate_cvt_init(&rate_cvt_cfg, &el);
    esp_gmf_pool_register_element(*pool, el, NULL);
    return 0;
}

static int piano_output_cb(uint8_t *pcm_data, uint32_t pcm_size, void *ctx)
{
    esp_codec_dev_handle_t codec_dev = (esp_codec_dev_handle_t)ctx;
    esp_codec_dev_write(codec_dev, pcm_data, pcm_size);
    return 0;
}

#ifdef SUPPORT_REALTIME_TRACK
static bool get_note_by_key(char *key, piano_note_t *note, piano_octave_t *oct)
{
    if (key[0] >= 'A' && key[0] <= 'G' &&
        key[1] <= '7' && key[1] >= '2') {
        if (key[0] <= 'B') {
            *note = NOTE_A + key[0] - 'A';
        } else {
            *note = NOTE_C + key[0] - 'C';

        }
        *oct = OCTAVE_2 + key[1] - '2';
        return true;
    }
    return false;
}

static void realtime_track_task(void *arg)
{
    song_render_handle_t song = (song_render_handle_t) arg;
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
    uint8_t buf[32];
    char line[64];
    size_t line_len = 0;
    bool mute = false;
    while (realtime_track_running) {
        int len = uart_read_bytes(UART_NUM_0, buf, sizeof(buf), pdMS_TO_TICKS(50));
        for (int i = 0; i < len; i++) {
            char c = (char)buf[i];
            if (c == '\n') {
                line[line_len] = '\0';
                ESP_LOGI(TAG, "Got %s\n", line);
                piano_note_t note;
                piano_octave_t oct;
                if (strncmp(line, "P:", 2) == 0) {
                    if (strncmp(line + 2, "ESC", 3) == 0) {
                        ESP_LOGI(TAG, "Received ESC command");
                        realtime_track_running = false;
                        break;
                    }
                    if (strncmp(line + 2, "MUTE", 4) == 0) {
                        mute = !mute;
                        song_render_mute(song, mute);
                        line_len = 0;
                        continue;
                    }
                    if (get_note_by_key(line + strlen("P:"), &note, &oct)) {
                        song_render_add_realtime(song, note, oct, false);
                    }
                } else if (strncmp(line, "R:", 2) == 0) {
                    if (get_note_by_key(line + strlen("R:"), &note, &oct)) {
                        song_render_add_realtime(song, note, oct, true);
                    }
                }
                line_len = 0;
            } else {
                if (line_len < sizeof(line) - 1) {
                    line[line_len++] = c;
                } else {
                    line_len = 0; // overflow reset
                }
            }
        }
    }
    vTaskDelete(NULL);
}
#endif

void run_piano_example(esp_codec_dev_handle_t codec_dev)
{
    esp_gmf_pool_handle_t pool = NULL;
    uint8_t stream_num = 4;
    create_default_pool(&pool);

    // Create audio renderer with 4 streams (melody, harmony, bass, arpeggio)
    esp_audio_render_handle_t render = NULL;
    esp_audio_render_cfg_t cfg = {
#ifdef SUPPORT_REALTIME_TRACK
        .max_stream_num = stream_num + 1,
#else
        .max_stream_num = stream_num,
#endif
        .pool = pool,
        .out_writer = piano_output_cb,
        .out_ctx = codec_dev,
    };
    esp_audio_render_create(&cfg, &render);
    // Set fixed output format (16kHz output) and open output devices
    esp_audio_render_sample_info_t fixed = { .sample_rate = 16000, .bits_per_sample = 16, .channel = 1 };
    esp_audio_render_set_out_sample_info(render, &fixed);
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = fixed.sample_rate,
        .bits_per_sample = fixed.bits_per_sample,
        .channel = fixed.channel,
    };
    esp_codec_dev_open(codec_dev, &fs);

    // Open all streams with 16kHz input (will be converted to 16kHz output)
    esp_audio_render_sample_info_t input_info = { .sample_rate = 16000, .bits_per_sample = 16, .channel = 1 };
    esp_audio_render_stream_handle_t stream[stream_num]; // Melody, Harmony, Bass, Arpeggio
    for (uint8_t i = 0; i < stream_num; i++) {
        esp_audio_render_stream_get(render, ESP_AUDIO_RENDER_STREAM_ID(i), &stream[i]);
        esp_audio_render_stream_open(stream[i], &input_info);
    }

    // Create song renderer
    song_cfg_t song_cfg = {
        .sample_rate = input_info.sample_rate,
        .channels = input_info.channel,
        .bits_per_sample = input_info.bits_per_sample,
        .track_count = stream_num,
        .tempo = 120,
        .beat_duration_ms = 500,
#ifdef SUPPORT_REALTIME_TRACK
        .loop = true,
        .realtime_track = true,
#endif
    };

    song_render_handle_t song = NULL;
    song_render_create(&song_cfg, &song);

    // Add tracks
    song_render_add_track(song, 0, melody_notes, melody_octaves, melody_durations,
                          sizeof(melody_notes) / sizeof(melody_notes[0]));
    song_render_add_track(song, 1, harmony_notes, harmony_octaves, harmony_durations,
                          sizeof(harmony_notes) / sizeof(harmony_notes[0]));
    song_render_add_track(song, 2, bass_notes, bass_octaves, bass_durations,
                          sizeof(bass_notes) / sizeof(bass_notes[0]));
    song_render_add_track(song, 3, arpeggio_notes, arpeggio_octaves, arpeggio_durations,
                          sizeof(arpeggio_notes) / sizeof(arpeggio_notes[0]));

    ESP_LOGI(TAG, "Starting piano test: 'Twinkle Twinkle Little Star'");
    ESP_LOGI(TAG, "Track 0: Melody (main tune) - 16kHz generation");
    ESP_LOGI(TAG, "Track 1: Harmony (chords) - 16kHz generation");
    ESP_LOGI(TAG, "Track 2: Bass (low notes) - 16kHz generation");
    ESP_LOGI(TAG, "Track 3: Arpeggio (broken chords) - 16kHz generation");

    // Start playing
    song_render_play(song, render);
#ifdef SUPPORT_REALTIME_TRACK
    realtime_track_running = true;
    BaseType_t ret = xTaskCreate(realtime_track_task, "note_read", 3 * 1024, song, 8, NULL);
    if (ret != pdPASS) {
        realtime_track_running = false;
    }
#endif
    // Wait for song to finish
    uint32_t duration = song_render_get_duration(song);
    ESP_LOGI(TAG, "Song duration: %" PRIu32 " ms", duration);
#ifdef SUPPORT_REALTIME_TRACK
    while (realtime_track_running) {
        vTaskDelay(pdMS_TO_TICKS(50)); // Wait esc key press to exit
    }
#else
    vTaskDelay(pdMS_TO_TICKS(duration + 1000)); // Wait for song + 1 second
#endif
    // Stop and cleanup
    song_render_stop(song);
    song_render_destroy(song);
    // Close render
    for (uint8_t i = 0; i < stream_num; i++) {
        esp_audio_render_stream_close(stream[i]);
    }
    // Close devices
    esp_codec_dev_close(codec_dev);
    esp_audio_render_destroy(render);
    esp_gmf_pool_deinit(pool);
    ESP_LOGI(TAG, "Piano performance completed!");
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    // Prepare for codec device
    esp_gmf_app_setup_codec_dev(NULL);
    esp_codec_dev_set_out_vol(esp_gmf_app_get_playback_handle(), 70);
    esp_codec_dev_close(esp_gmf_app_get_playback_handle());
    esp_codec_dev_close(esp_gmf_app_get_record_handle());
    run_piano_example(esp_gmf_app_get_playback_handle());
    ESP_LOGI(TAG, "Simple piano run finished");
}
