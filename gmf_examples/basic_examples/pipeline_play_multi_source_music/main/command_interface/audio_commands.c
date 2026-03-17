/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "esp_log.h"
#include "audio_commands.h"
#include "audio_multi_source_player.h"

static const char *TAG = "AUDIO_COMMANDS";

static struct {
    struct arg_str *source;
    struct arg_end *end;
} switch_args;

static struct {
    struct arg_int *volume;
    struct arg_end *end;
} volume_args;

static bool g_app_keep_running = true;

int audio_commands_init(void)
{
    g_app_keep_running = true;

    switch_args.source = arg_str0(NULL, NULL, "<http|sdcard>", "Audio source to switch to");
    switch_args.end = arg_end(1);

    volume_args.volume = arg_int1(NULL, NULL, "<0-100>", "Volume level (0-100)");
    volume_args.end = arg_end(1);

    return 0;
}

int audio_commands_deinit(void)
{
    g_app_keep_running = false;

    if (switch_args.source || switch_args.end) {
        void *argtable[] = {switch_args.source, switch_args.end};
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        switch_args.source = NULL;
        switch_args.end = NULL;
    }

    if (volume_args.volume || volume_args.end) {
        void *argtable[] = {volume_args.volume, volume_args.end};
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        volume_args.volume = NULL;
        volume_args.end = NULL;
    }

    return 0;
}

bool audio_commands_keep_running(void)
{
    return g_app_keep_running;
}

static int cmd_audio_play(int argc, char **argv)
{
    audio_source_t current_source;
    audio_state_t current_state;
    if (audio_multi_source_player_get_current_source(&current_source) != AUDIO_MS_PLAYER_OK ||
        audio_multi_source_player_get_current_state(&current_state) != AUDIO_MS_PLAYER_OK) {
        ESP_LOGE(TAG, "Failed to get player state");
        return 1;
    }

    if (current_state != AUDIO_STATE_PLAYING) {
        audio_ms_player_err_t ret = audio_multi_source_player_play(current_source);
        if (ret == AUDIO_MS_PLAYER_OK) {
            ESP_LOGI(TAG, "Playback started from %s",
                     audio_multi_source_player_get_source_name(current_source));
        } else {
            ESP_LOGE(TAG, "Failed to start playback: %d", ret);
            return 1;
        }
    } else {
        ESP_LOGI(TAG, "Already playing");
    }
    return 0;
}

static int cmd_audio_pause(int argc, char **argv)
{
    audio_state_t current_state;
    if (audio_multi_source_player_get_current_state(&current_state) != AUDIO_MS_PLAYER_OK) {
        ESP_LOGE(TAG, "Failed to get player state");
        return 1;
    }

    if (current_state == AUDIO_STATE_PLAYING) {
        audio_ms_player_err_t ret = audio_multi_source_player_pause();
        if (ret == AUDIO_MS_PLAYER_OK) {
            ESP_LOGI(TAG, "Playback paused");
        } else {
            ESP_LOGE(TAG, "Failed to pause: %d", ret);
            return 1;
        }
    } else {
        ESP_LOGW(TAG, "Not playing");
    }
    return 0;
}

static int cmd_audio_resume(int argc, char **argv)
{
    audio_state_t current_state;
    if (audio_multi_source_player_get_current_state(&current_state) != AUDIO_MS_PLAYER_OK) {
        ESP_LOGE(TAG, "Failed to get player state");
        return 1;
    }

    if (current_state == AUDIO_STATE_PAUSED) {
        audio_ms_player_err_t ret = audio_multi_source_player_resume();
        if (ret == AUDIO_MS_PLAYER_OK) {
            ESP_LOGI(TAG, "Playback resumed");
        } else {
            ESP_LOGE(TAG, "Failed to resume: %d", ret);
            return 1;
        }
    } else {
        ESP_LOGW(TAG, "Not paused");
    }
    return 0;
}

static int cmd_audio_stop(int argc, char **argv)
{
    audio_state_t current_state;
    if (audio_multi_source_player_get_current_state(&current_state) != AUDIO_MS_PLAYER_OK) {
        ESP_LOGE(TAG, "Failed to get player state");
        return 1;
    }

    if (current_state == AUDIO_STATE_PLAYING || current_state == AUDIO_STATE_PAUSED) {
        audio_ms_player_err_t ret = audio_multi_source_player_stop();
        if (ret == AUDIO_MS_PLAYER_OK) {
            ESP_LOGI(TAG, "Playback stopped");
        } else {
            ESP_LOGE(TAG, "Failed to stop: %d", ret);
            return 1;
        }
    } else {
        ESP_LOGW(TAG, "Not playing");
    }
    return 0;
}

static int cmd_audio_switch(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&switch_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, switch_args.end, argv[0]);
        return 1;
    }

    audio_source_t current_source;
    if (audio_multi_source_player_get_current_source(&current_source) != AUDIO_MS_PLAYER_OK) {
        ESP_LOGE(TAG, "Failed to get current source");
        return 1;
    }

    audio_source_t new_source;
    const char *source_str = NULL;

    if (switch_args.source->count == 0) {
        if (current_source == AUDIO_SRC_HTTP) {
            new_source = AUDIO_SRC_SDCARD;
            source_str = "sdcard";
        } else {
            new_source = AUDIO_SRC_HTTP;
            source_str = "http";
        }
        ESP_LOGI(TAG, "Switching from %s to %s",
                 audio_multi_source_player_get_source_name(current_source), source_str);
    } else {
        source_str = switch_args.source->sval[0];
        if (strcasecmp(source_str, "http") == 0) {
            new_source = AUDIO_SRC_HTTP;
        } else if (strcasecmp(source_str, "sdcard") == 0) {
            new_source = AUDIO_SRC_SDCARD;
        } else {
            ESP_LOGE(TAG, "Invalid source. Use 'http' or 'sdcard'");
            return 1;
        }
        ESP_LOGI(TAG, "Switching from %s to %s",
                 audio_multi_source_player_get_source_name(current_source), source_str);
    }

    audio_ms_player_err_t ret = audio_multi_source_player_switch_source(new_source);
    return (ret == AUDIO_MS_PLAYER_OK) ? 0 : 1;
}

static int cmd_audio_status(int argc, char **argv)
{
    audio_source_t current_source;
    audio_state_t current_state;
    if (audio_multi_source_player_get_current_source(&current_source) != AUDIO_MS_PLAYER_OK ||
        audio_multi_source_player_get_current_state(&current_state) != AUDIO_MS_PLAYER_OK) {
        ESP_LOGE(TAG, "Failed to get player state");
        return 1;
    }

    bool flash_playing = audio_multi_source_player_is_flash_playing();

    ESP_LOGI(TAG, "Playback Status:");
    ESP_LOGI(TAG, "  Source: %s", audio_multi_source_player_get_source_name(current_source));
    ESP_LOGI(TAG, "  State: %s", audio_multi_source_player_get_state_name(current_state));
    ESP_LOGI(TAG, "  Flash tone playing: %s", flash_playing ? "Yes" : "No");

    int volume = 0;
    if (audio_multi_source_player_get_volume(&volume) == AUDIO_MS_PLAYER_OK) {
        ESP_LOGI(TAG, "  Volume: %d (range: 0-100)", volume);
    }

    return 0;
}

static int cmd_audio_tone(int argc, char **argv)
{
    audio_ms_player_err_t ret = audio_multi_source_player_play_flash_tone();
    return (ret == AUDIO_MS_PLAYER_OK) ? 0 : 1;
}

static int cmd_audio_get_vol(int argc, char **argv)
{
    int volume = 0;
    audio_ms_player_err_t ret = audio_multi_source_player_get_volume(&volume);
    if (ret == AUDIO_MS_PLAYER_OK) {
        ESP_LOGI(TAG, "Current volume: %d (range: 0-100)", volume);
    } else {
        ESP_LOGE(TAG, "Failed to get volume: %d", ret);
        return 1;
    }
    return 0;
}

static int cmd_audio_set_vol(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&volume_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, volume_args.end, argv[0]);
        return 1;
    }

    if (volume_args.volume->count == 0) {
        ESP_LOGE(TAG, "Volume value is required. Usage: set_vol <0-100>");
        return 1;
    }

    int new_vol = volume_args.volume->ival[0];
    if (new_vol < 0 || new_vol > 100) {
        ESP_LOGE(TAG, "Volume must be between 0 and 100, got: %d", new_vol);
        return 1;
    }

    audio_ms_player_err_t ret = audio_multi_source_player_set_volume(new_vol);
    if (ret == AUDIO_MS_PLAYER_OK) {
        ESP_LOGI(TAG, "Volume set to: %d", new_vol);
    } else {
        ESP_LOGE(TAG, "Failed to set volume: %d", ret);
        return 1;
    }
    return 0;
}

static int cmd_audio_exit(int argc, char **argv)
{
    ESP_LOGI(TAG, "Exiting application...");
    audio_multi_source_player_stop();
    g_app_keep_running = false;
    return 0;
}

static const esp_console_cmd_t s_audio_commands[] = {
    { .command = "play",    .help = "Start playback",                                                      .func = &cmd_audio_play },
    { .command = "pause",   .help = "Pause playback",                                                      .func = &cmd_audio_pause },
    { .command = "resume",  .help = "Resume playback",                                                     .func = &cmd_audio_resume },
    { .command = "stop",    .help = "Stop playback",                                                       .func = &cmd_audio_stop },
    { .command = "switch",  .help = "Switch audio source (http or sdcard).",                               .func = &cmd_audio_switch,  .argtable = &switch_args },
    { .command = "status",  .help = "Show playback status",                                                .func = &cmd_audio_status },
    { .command = "tone",    .help = "Play flash tone (pauses current playback, plays tone, then resumes)", .func = &cmd_audio_tone },
    { .command = "get_vol", .help = "Get current volume (0-100)",                                          .func = &cmd_audio_get_vol },
    { .command = "set_vol", .help = "Set volume (0-100)",                                                  .func = &cmd_audio_set_vol, .argtable = &volume_args },
    { .command = "exit",    .help = "Exit the application",                                                .func = &cmd_audio_exit },
};

void audio_commands_register_all(void)
{
    for (size_t i = 0; i < sizeof(s_audio_commands) / sizeof(s_audio_commands[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&s_audio_commands[i]));
    }
}
