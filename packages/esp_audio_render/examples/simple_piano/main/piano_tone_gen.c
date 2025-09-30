/**
 * Optimized Polyphonic Piano Tone Generator for ESP-IDF ESP32-S3
 * Crackle-free with polyphony headroom scaling
 */

#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <inttypes.h>
#include "piano_tone_gen.h"
#include "esp_log.h"

#define TAG "PIANO_TONE"

#define PI     3.14159265359f
#define TWO_PI (2.0f * PI)

// Lookup table size
#define SINE_TABLE_SIZE 1024

typedef struct {
    piano_tone_cfg_t  cfg;
    float             sample_rate;
} piano_tone_gen_t;

static float sine_table[SINE_TABLE_SIZE];

// Frequencies for C4–B4
static const float note_frequencies[NOTE_COUNT] = {
    261.63f, 293.66f, 329.63f, 349.23f, 392.00f, 440.00f, 493.88f
};
static const char *note_names[NOTE_COUNT] = {
    "C", "D", "E", "F", "G", "A", "B"
};

static void init_sine_table(void)
{
    static bool table_initialized = false;
    if (table_initialized) {
        return;
    }
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        float angle = (float)i * TWO_PI / SINE_TABLE_SIZE;
        sine_table[i] = sinf(angle);
    }
    table_initialized = true;
}

static inline float fast_sin(float angle)
{
    angle = fmodf(angle, TWO_PI);
    if (angle < 0) {
        angle += TWO_PI;
    }
    float idx = angle * (SINE_TABLE_SIZE / TWO_PI);
    int idx1 = (int)idx;
    int idx2 = (idx1 + 1) % SINE_TABLE_SIZE;
    float frac = idx - idx1;
    return sine_table[idx1] * (1.0f - frac) + sine_table[idx2] * frac;
}

static inline float calculate_adsr_envelope(float t, float attack, float decay,
                                            float sustain, float release, float duration)
{
    if (t < 0) {
        return 0.0f;
    }
    if (t < attack) {
        return t / attack;
    } else if (t < attack + decay) {
        return 1.0f - ((t - attack) / decay) * (1.0f - sustain);
    } else if (t < duration - release) {
        return sustain;
    } else if (t < duration) {
        return sustain * (1.0f - (t - (duration - release)) / release);
    } else {
        return 0.0f;
    }
}

static inline float generate_piano_wave(float frequency, float t)
{
    return fast_sin(TWO_PI * frequency * t) * 0.6f + fast_sin(TWO_PI * frequency * 2.0f * t) * 0.3f + fast_sin(TWO_PI * frequency * 3.0f * t) * 0.15f + fast_sin(TWO_PI * frequency * 4.0f * t) * 0.08f;
}

int piano_tone_gen_create(const piano_tone_cfg_t *cfg, piano_tone_gen_handle_t *handle)
{
    if (!cfg || !handle) {
        return -1;
    }
    init_sine_table();
    piano_tone_gen_t *gen = malloc(sizeof(piano_tone_gen_t));
    if (!gen) {
        return -1;
    }
    memcpy(&gen->cfg, cfg, sizeof(piano_tone_cfg_t));
    gen->sample_rate = (float)cfg->sample_rate;
    *handle = (piano_tone_gen_handle_t)gen;
    ESP_LOGI(TAG, "Piano tone gen created: %" PRIu32 " Hz, %d ch, %d bit",
             cfg->sample_rate, cfg->channels, cfg->bits_per_sample);
    return 0;
}

void piano_tone_gen_destroy(piano_tone_gen_handle_t handle)
{
    if (handle) {
        free(handle);
    }
}

float piano_get_note_frequency(piano_note_t note, piano_octave_t octave)
{
    if (note >= NOTE_COUNT) {
        return 0.0f;
    }
    return note_frequencies[note] * powf(2.0f, (float)(octave - 4));
}

const char *piano_get_note_name(piano_note_t note)
{
    return (note < NOTE_COUNT) ? note_names[note] : "?";
}

static int gen_note_partial(piano_tone_gen_t *gen,
                            float frequency,
                            uint16_t duration_ms,
                            int16_t *buffer,
                            uint32_t buffer_size)
{
    uint32_t samples_needed = (uint32_t)(gen->sample_rate * duration_ms / 1000.0f);
    uint32_t needed_bytes = samples_needed * gen->cfg.channels * sizeof(int16_t);
    if (needed_bytes > buffer_size) {
        return -1;
    }

    float duration_sec = duration_ms / 1000.0f;

    for (uint32_t i = 0; i < samples_needed; i++) {
        float t = (float)i / gen->sample_rate;
        float env = calculate_adsr_envelope(t,
                                            gen->cfg.attack_time,
                                            gen->cfg.decay_time,
                                            gen->cfg.sustain_level,
                                            gen->cfg.release_time,
                                            duration_sec);
        float wave = generate_piano_wave(frequency, t) * env;

        int16_t pcm = (int16_t)(wave * 32767.0f * 0.8f); // headroom for safety
        for (int ch = 0; ch < gen->cfg.channels; ch++) {
            buffer[i * gen->cfg.channels + ch] = pcm;
        }
    }
    return samples_needed;
}

int piano_tone_gen_create_note(piano_tone_gen_handle_t handle,
                               piano_note_t note,
                               piano_octave_t octave,
                               uint16_t duration_ms,
                               int16_t *buffer,
                               uint32_t buffer_size)
{
    if (!handle || note >= NOTE_COUNT) {
        return -1;
    }
    piano_tone_gen_t *gen = (piano_tone_gen_t *)handle;
    float freq = piano_get_note_frequency(note, octave);
    return gen_note_partial(gen, freq, duration_ms, buffer, buffer_size);
}

int piano_tone_gen_create_note_float(piano_tone_gen_handle_t handle,
                                     float frequency,
                                     uint16_t duration_ms,
                                     int16_t *buffer,
                                     uint32_t buffer_size)
{
    if (!handle || frequency <= 0.0f) {
        return -1;
    }
    piano_tone_gen_t *gen = (piano_tone_gen_t *)handle;
    return gen_note_partial(gen, frequency, duration_ms, buffer, buffer_size);
}

int piano_tone_gen_chord(piano_tone_gen_handle_t handle,
                         const piano_note_t *notes,
                         const piano_octave_t *octaves,
                         uint8_t note_count,
                         uint16_t duration_ms,
                         int16_t *buffer,
                         uint32_t buffer_size)
{
    if (!handle || !notes || !octaves || !buffer || note_count == 0) {
        return -1;
    }

    piano_tone_gen_t *gen = (piano_tone_gen_t *)handle;
    uint32_t samples_needed = (uint32_t)(gen->sample_rate * duration_ms / 1000.0f);
    uint32_t needed_bytes = samples_needed * gen->cfg.channels * sizeof(int16_t);
    if (needed_bytes > buffer_size) {
        return -1;
    }

    memset(buffer, 0, needed_bytes);
    float duration_sec = duration_ms / 1000.0f;

    for (uint32_t i = 0; i < samples_needed; i++) {
        float t = (float)i / gen->sample_rate;
        float mix = 0.0f;

        for (uint8_t n = 0; n < note_count; n++) {
            if (notes[n] >= NOTE_COUNT) {
                continue;
            }
            float freq = piano_get_note_frequency(notes[n], octaves[n]);
            float env = calculate_adsr_envelope(t,
                                                gen->cfg.attack_time,
                                                gen->cfg.decay_time,
                                                gen->cfg.sustain_level,
                                                gen->cfg.release_time,
                                                duration_sec);
            mix += generate_piano_wave(freq, t) * env;
        }

        // Polyphony-safe scaling
        mix *= 0.8f / (float)note_count;

        int16_t pcm = (int16_t)(mix * 32767.0f);
        for (int ch = 0; ch < gen->cfg.channels; ch++) {
            buffer[i * gen->cfg.channels + ch] = pcm;
        }
    }

    return samples_needed;
}

int piano_tone_gen_note_from_offset(piano_tone_gen_handle_t handle,
                                    piano_note_t note,
                                    piano_octave_t octave,
                                    uint16_t duration_ms,
                                    int16_t *buffer,
                                    uint32_t buffer_size,
                                    uint32_t start_time_offset_ms,
                                    uint16_t full_note_duration_ms)
{
    if (!handle || !buffer || note >= NOTE_COUNT) {
        return -1;
    }

    piano_tone_gen_t *gen = (piano_tone_gen_t *)handle;

    // How many samples we need to generate for this chunk
    uint32_t samples_needed = (uint32_t)(gen->sample_rate * (float)duration_ms / 1000.0f);
    uint32_t needed_bytes = samples_needed * gen->cfg.channels * sizeof(int16_t);
    if (needed_bytes > buffer_size) {
        ESP_LOGW(TAG, "Buffer too small for %u ms chunk: need %" PRIu32 " bytes, have %" PRIu32,
                 (unsigned)duration_ms, needed_bytes, buffer_size);
        return -1;
    }

    // Frequency of the target note
    float freq = piano_get_note_frequency(note, octave);
    const float start_t = (float)start_time_offset_ms / 1000.0f; // Where this chunk begins in the note
    const float note_T = (float)full_note_duration_ms / 1000.0f; // Full envelope duration, not chunk

    // Generate
    for (uint32_t i = 0; i < samples_needed; i++) {
        float t = start_t + (float)i / gen->sample_rate; // Absolute time within the note
        float env = calculate_adsr_envelope(t,
                                            gen->cfg.attack_time,
                                            gen->cfg.decay_time,
                                            gen->cfg.sustain_level,
                                            gen->cfg.release_time,
                                            note_T);

        // Harmonic-rich piano waveform * envelope
        float s = generate_piano_wave(freq, t) * env;

        // Headroom to avoid clipping/clicks when concatenating chunks
        int16_t pcm = (int16_t)(s * 32767.0f * 0.8f);

        // Write same mono sample to all channels
        for (int ch = 0; ch < gen->cfg.channels; ch++) {
            buffer[i * gen->cfg.channels + ch] = pcm;
        }
    }

    return samples_needed;
}

int piano_tone_gen_chord_float(piano_tone_gen_handle_t handle,
                               const float *frequencies,
                               uint8_t note_count,
                               uint16_t duration_ms,
                               int16_t *buffer,
                               uint32_t buffer_size)
{
    if (!handle || !frequencies || !buffer || note_count == 0) {
        return -1;
    }

    piano_tone_gen_t *gen = (piano_tone_gen_t *)handle;
    uint32_t samples_needed = (uint32_t)(gen->sample_rate * duration_ms / 1000.0f);
    uint32_t needed_bytes = samples_needed * gen->cfg.channels * sizeof(int16_t);
    if (needed_bytes > buffer_size) {
        return -1;
    }

    memset(buffer, 0, needed_bytes);
    float duration_sec = duration_ms / 1000.0f;

    for (uint32_t i = 0; i < samples_needed; i++) {
        float t = (float)i / gen->sample_rate;
        float mix = 0.0f;

        for (uint8_t n = 0; n < note_count; n++) {
            if (frequencies[n] <= 0.0f) {
                continue;
            }
            float env = calculate_adsr_envelope(t,
                                                gen->cfg.attack_time,
                                                gen->cfg.decay_time,
                                                gen->cfg.sustain_level,
                                                gen->cfg.release_time,
                                                duration_sec);
            mix += generate_piano_wave(frequencies[n], t) * env;
        }

        mix *= 0.8f / (float)note_count;

        int16_t pcm = (int16_t)(mix * 32767.0f);
        for (int ch = 0; ch < gen->cfg.channels; ch++) {
            buffer[i * gen->cfg.channels + ch] = pcm;
        }
    }

    return samples_needed;
}
