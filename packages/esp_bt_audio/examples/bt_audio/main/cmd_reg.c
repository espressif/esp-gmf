/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_log.h"

#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_classic.h"
#include "esp_bt_audio_vol.h"
#include "esp_bt_audio_media.h"
#include "esp_bt_audio_playback.h"
#include "esp_gmf_oal_sys.h"
#include "esp_bt_audio_tel.h"

#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#include "dev_audio_codec.h"
#include "cmd_reg.h"

static uint8_t target_device_bda[6] = {0};
#ifdef CONFIG_GMF_EXAMPLE_A2DP_SOURCE
static char target_device_name[32] = {0};
static const char *TAG = "CMD_REG";
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */

static int cmd_playback_play(int argc, char **argv)
{
    esp_err_t ret = esp_bt_audio_playback_play();
    if (ret == ESP_OK) {
        printf("Media play command sent\n");
    } else {
        printf("Failed to send media play command: %s\n", esp_err_to_name(ret));
    }
    return 0;
}

static int cmd_playback_pause(int argc, char **argv)
{
    esp_err_t ret = esp_bt_audio_playback_pause();
    if (ret == ESP_OK) {
        printf("Media pause command sent\n");
    } else {
        printf("Failed to send media pause command: %s\n", esp_err_to_name(ret));
    }
    return 0;
}

static int cmd_playback_stop(int argc, char **argv)
{
    esp_err_t ret = esp_bt_audio_playback_stop();
    if (ret == ESP_OK) {
        printf("Media stop command sent\n");
    } else {
        printf("Failed to send media stop command: %s\n", esp_err_to_name(ret));
    }
    return 0;
}

static int cmd_playback_next(int argc, char **argv)
{
    esp_err_t ret = esp_bt_audio_playback_next();
    if (ret == ESP_OK) {
        printf("Media next command sent\n");
    } else {
        printf("Failed to send media next command: %s\n", esp_err_to_name(ret));
    }
    return 0;
}

static int cmd_playback_prev(int argc, char **argv)
{
    esp_err_t ret = esp_bt_audio_playback_prev();
    if (ret == ESP_OK) {
        printf("Media previous command sent\n");
    } else {
        printf("Failed to send media previous command: %s\n", esp_err_to_name(ret));
    }
    return 0;
}

static int cmd_playback_metadata(int argc, char **argv)
{
    if (argc != 2) {
        printf("Usage: metadata <mask>\n");
        printf("  mask bits: 0x1 title, 0x2 artist, 0x4 album, 0x8 track_num,\n");
        printf("             0x10 num_tracks, 0x20 genre, 0x40 time, 0x80 cover\n");
        return 1;
    }

    char *endptr = NULL;
    uint32_t mask = (uint32_t)strtoul(argv[1], &endptr, 0);
    if (argv[1][0] == '\0' || (endptr && *endptr != '\0')) {
        printf("Invalid mask: %s\n", argv[1]);
        return 1;
    }

    esp_err_t ret = esp_bt_audio_playback_request_metadata(mask);
    if (ret == ESP_OK) {
        printf("Media metadata request sent\n");
    } else {
        printf("Failed to send media metadata request: %s\n", esp_err_to_name(ret));
    }
    return 0;
}

static int cmd_volume_set(int argc, char **argv)
{
    if (argc != 2) {
        printf("Usage: vol_set <volume>\n");
        printf("  volume: 0-100\n");
        return 1;
    }

    int volume = atoi(argv[1]);
    if (volume < 0 || volume > 100) {
        printf("Volume must be between 0 and 100\n");
        return 1;
    }
#if CONFIG_GMF_EXAMPLE_A2DP_SOURCE
    esp_err_t ret = esp_bt_audio_vol_set_absolute((uint32_t)volume);
    if (ret == ESP_OK) {
        printf("Volume set to %d\n", volume);
    } else {
        printf("Failed to set volume: %s\n", esp_err_to_name(ret));
    }
#else   /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
    dev_audio_codec_handles_t *codec_handle = NULL;
    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&codec_handle);
    esp_codec_dev_set_out_vol(codec_handle->codec_dev, volume);

    esp_err_t ret = esp_bt_audio_vol_notify((uint32_t)volume);
    if (ret == ESP_OK) {
        printf("Volume set to %d\n", volume);
    } else {
        printf("Failed to set volume: %s\n", esp_err_to_name(ret));
    }
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
    return 0;
}

static int cmd_volume_up(int argc, char **argv)
{
#if CONFIG_GMF_EXAMPLE_A2DP_SOURCE
    esp_err_t ret = esp_bt_audio_vol_set_relative(true);
    if (ret == ESP_OK) {
        printf("Volume up\n");
    } else {
        printf("Failed to increase volume: %s\n", esp_err_to_name(ret));
    }
#else   /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
    int current_volume = 0;
    dev_audio_codec_handles_t *codec_handle = NULL;
    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&codec_handle);
    esp_codec_dev_get_out_vol(codec_handle->codec_dev, &current_volume);
    current_volume = (current_volume >= 90) ? 100 : current_volume + 10;
    esp_codec_dev_set_out_vol(codec_handle->codec_dev, current_volume);

    esp_err_t ret = esp_bt_audio_vol_notify(current_volume);
    if (ret == ESP_OK) {
        printf("Volume up to %d\n", (int)current_volume);
    } else {
        printf("Failed to increase volume: %s\n", esp_err_to_name(ret));
    }
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
    return 0;
}

static int cmd_volume_down(int argc, char **argv)
{
#if CONFIG_GMF_EXAMPLE_A2DP_SOURCE
    esp_err_t ret = esp_bt_audio_vol_set_relative(false);
    if (ret == ESP_OK) {
        printf("Volume down\n");
    } else {
        printf("Failed to decrease volume: %s\n", esp_err_to_name(ret));
    }
#else   /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
    int current_volume = 0;
    dev_audio_codec_handles_t *codec_handle = NULL;
    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&codec_handle);
    esp_codec_dev_get_out_vol(codec_handle->codec_dev, &current_volume);
    current_volume = (current_volume <= 10) ? 0 : current_volume - 10;
    esp_codec_dev_set_out_vol(codec_handle->codec_dev, current_volume);

    esp_err_t ret = esp_bt_audio_vol_notify(current_volume);
    if (ret == ESP_OK) {
        printf("Volume down to %d\n", (int)current_volume);
    } else {
        printf("Failed to decrease volume: %s\n", esp_err_to_name(ret));
    }
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
    return 0;
}

static int cmd_connect_device(int argc, char **argv)
{
    if (argc != 2) {
        printf("Usage: connect <mac_address>\n");
        printf("Example: connect 01:02:03:04:05:06\n");
        return 1;
    }

    uint8_t bda[6] = {0};
    if (sscanf(argv[1], "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
               &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) != 6) {
        printf("Invalid MAC address format. Use: XX:XX:XX:XX:XX:XX\n");
        return 1;
    }
#if CONFIG_GMF_EXAMPLE_A2DP_SOURCE
    esp_err_t ret = esp_bt_audio_classic_connect(ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC, bda);
#else   /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
    esp_err_t ret = esp_bt_audio_classic_connect(ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SNK, bda);
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
    if (ret == ESP_OK) {
        printf("Connecting to device %s...\n", argv[1]);
        memcpy(target_device_bda, bda, sizeof(target_device_bda));
    } else {
        printf("Failed to connect: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

static int cmd_disconnect_device(int argc, char **argv)
{
    bool is_bda_empty = true;
    for (int i = 0; i < sizeof(target_device_bda); ++i) {
        if (target_device_bda[i] != 0) {
            is_bda_empty = false;
            break;
        }
    }
    if (is_bda_empty) {
        printf("No device is currently connected.\n");
        return 1;
    }
#if CONFIG_GMF_EXAMPLE_A2DP_SOURCE
    esp_err_t ret = esp_bt_audio_classic_disconnect(ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC, target_device_bda);
#else   /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
    esp_err_t ret = esp_bt_audio_classic_disconnect(ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SNK, target_device_bda);
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
    if (ret == ESP_OK) {
        printf("Disconnecting device...\n");
        memset(target_device_bda, 0, sizeof(target_device_bda));
    } else {
        printf("Failed to disconnect: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

static int cmd_hf_connect(int argc, char **argv)
{
    if (argc != 2) {
        printf("Usage: hf_connect <mac_address>\n");
        printf("Example: hf_connect 01:02:03:04:05:06\n");
        return 1;
    }

    uint8_t bda[6];
    if (sscanf(argv[1], "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
               &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]) != 6) {
        printf("Invalid MAC address format. Use: XX:XX:XX:XX:XX:XX\n");
        return 1;
    }

    esp_err_t ret = esp_bt_audio_classic_connect(ESP_BT_AUDIO_CLASSIC_ROLE_HFP_HF, bda);
    if (ret == ESP_OK) {
        printf("Connecting HFP HF to device %s...\n", argv[1]);
        memcpy(target_device_bda, bda, sizeof(target_device_bda));
    } else {
        printf("Failed to connect HFP HF: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

static int cmd_hf_disconnect(int argc, char **argv)
{
    bool is_bda_empty = true;
    for (int i = 0; i < sizeof(target_device_bda); ++i) {
        if (target_device_bda[i] != 0) {
            is_bda_empty = false;
            break;
        }
    }
    if (is_bda_empty) {
        printf("No device is currently connected.\n");
        return 1;
    }

    esp_err_t ret = esp_bt_audio_classic_disconnect(ESP_BT_AUDIO_CLASSIC_ROLE_HFP_HF, target_device_bda);
    if (ret == ESP_OK) {
        printf("Disconnecting HFP HF device...\n");
        memset(target_device_bda, 0, sizeof(target_device_bda));
    } else {
        printf("Failed to disconnect HFP HF: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

static int cmd_call_answer(int argc, char **argv)
{
    esp_err_t ret = esp_bt_audio_call_answer(0);
    if (ret == ESP_OK) {
        printf("Call answer command sent\n");
    } else {
        printf("Failed to send call answer command: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

static int cmd_call_reject(int argc, char **argv)
{
    esp_err_t ret = esp_bt_audio_call_reject(0);
    if (ret == ESP_OK) {
        printf("Call reject/terminate command sent\n");
    } else {
        printf("Failed to send call reject/terminate command: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

static int cmd_call_dial(int argc, char **argv)
{
    if (argc > 2) {
        printf("Usage: call_dial [number]\n");
        printf("  number: Optional. If omitted, redial last number\n");
        return 1;
    }
    const char *number = (argc == 2) ? argv[1] : NULL;
    esp_err_t ret = esp_bt_audio_call_dial(number);
    if (ret == ESP_OK) {
        printf("Call dial command sent\n");
    } else {
        printf("Failed to send call dial command: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

#ifdef CONFIG_GMF_EXAMPLE_A2DP_SOURCE
static int cmd_start_discovery(int argc, char **argv)
{
    memset(target_device_name, 0, sizeof(target_device_name));

    if (argc == 2) {
        strncpy(target_device_name, argv[1], sizeof(target_device_name) - 1);
        target_device_name[sizeof(target_device_name) - 1] = '\0';
        printf("Discovery started - looking for device: %s\n", target_device_name);
    } else if (argc == 1) {
        printf("Discovery started - scanning for all devices\n");
    } else {
        printf("Usage: start_discovery [device_name]\n");
        printf("  device_name: Optional. If provided, will auto-connect to this device\n");
        return 1;
    }

    esp_err_t ret = esp_bt_audio_classic_discovery_start();
    if (ret == ESP_OK) {
        if (strlen(target_device_name) > 0) {
            printf("Will auto-connect to '%s' when found\n", target_device_name);
        }
    } else {
        printf("Failed to start discovery: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

static int cmd_stop_discovery(int argc, char **argv)
{
    esp_err_t ret = esp_bt_audio_classic_discovery_stop();
    if (ret == ESP_OK) {
        printf("Discovery stopped\n");
    } else {
        printf("Failed to stop discovery: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

static int cmd_start_media(int argc, char **argv)
{
    esp_err_t ret = esp_bt_audio_media_start(ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC, NULL);
    if (ret == ESP_OK) {
        printf("Media started\n");
    } else {
        printf("Failed to start media: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

static int cmd_stop_media(int argc, char **argv)
{
    esp_err_t ret = esp_bt_audio_media_stop(ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC);
    if (ret == ESP_OK) {
        printf("Media stopped\n");
    } else {
        printf("Failed to stop media: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */

void cli_register_bt(void)
{
    const esp_console_cmd_t play_cmd = {
        .command = "play",
        .help = "Send media play command",
        .hint = NULL,
        .func = &cmd_playback_play,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&play_cmd));

    const esp_console_cmd_t pause_cmd = {
        .command = "pause",
        .help = "Send media pause command",
        .hint = NULL,
        .func = &cmd_playback_pause,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&pause_cmd));

    const esp_console_cmd_t stop_cmd = {
        .command = "stop",
        .help = "Send media stop command",
        .hint = NULL,
        .func = &cmd_playback_stop,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&stop_cmd));

    const esp_console_cmd_t next_cmd = {
        .command = "next",
        .help = "Send media next track command",
        .hint = NULL,
        .func = &cmd_playback_next,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&next_cmd));

    const esp_console_cmd_t prev_cmd = {
        .command = "prev",
        .help = "Send media previous track command",
        .hint = NULL,
        .func = &cmd_playback_prev,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&prev_cmd));

    const esp_console_cmd_t metadata_cmd = {
        .command = "metadata",
        .help = "Request media metadata with mask",
        .hint = "<mask>",
        .func = &cmd_playback_metadata,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&metadata_cmd));

    const esp_console_cmd_t vol_set_cmd = {
        .command = "vol_set",
        .help = "Set volume level (0-100)",
        .hint = "<volume>",
        .func = &cmd_volume_set,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&vol_set_cmd));

    const esp_console_cmd_t vol_up_cmd = {
        .command = "vol_up",
        .help = "Increase volume by 10",
        .hint = NULL,
        .func = &cmd_volume_up,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&vol_up_cmd));

    const esp_console_cmd_t vol_down_cmd = {
        .command = "vol_down",
        .help = "Decrease volume by 10",
        .hint = NULL,
        .func = &cmd_volume_down,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&vol_down_cmd));

    const esp_console_cmd_t connect_cmd = {
        .command = "connect",
        .help = "Connect to a Bluetooth device",
        .hint = "<mac_address>",
        .func = &cmd_connect_device,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&connect_cmd));

    const esp_console_cmd_t disconnect_cmd = {
        .command = "disconnect",
        .help = "Disconnect from current Bluetooth device",
        .hint = NULL,
        .func = &cmd_disconnect_device,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&disconnect_cmd));

    const esp_console_cmd_t hf_connect_cmd = {
        .command = "hf_connect",
        .help = "Connect HFP HF to a Bluetooth device",
        .hint = "<mac_address>",
        .func = &cmd_hf_connect,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&hf_connect_cmd));

    const esp_console_cmd_t hf_disconnect_cmd = {
        .command = "hf_disconnect",
        .help = "Disconnect HFP HF from current Bluetooth device",
        .hint = NULL,
        .func = &cmd_hf_disconnect,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&hf_disconnect_cmd));

    const esp_console_cmd_t call_answer_cmd = {
        .command = "call_answer",
        .help = "Answer incoming call",
        .hint = NULL,
        .func = &cmd_call_answer,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&call_answer_cmd));

    const esp_console_cmd_t call_reject_cmd = {
        .command = "call_reject",
        .help = "Reject / terminate call",
        .hint = NULL,
        .func = &cmd_call_reject,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&call_reject_cmd));

    const esp_console_cmd_t call_dial_cmd = {
        .command = "call_dial",
        .help = "Dial number; omit for redial",
        .hint = "[number]",
        .func = &cmd_call_dial,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&call_dial_cmd));

#ifdef CONFIG_GMF_EXAMPLE_A2DP_SOURCE
    const esp_console_cmd_t start_discovery_cmd = {
        .command = "start_discovery",
        .help = "Start Bluetooth device discovery with optional auto-connect",
        .hint = "[device_name]",
        .func = &cmd_start_discovery,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&start_discovery_cmd));

    const esp_console_cmd_t stop_discovery_cmd = {
        .command = "stop_discovery",
        .help = "Stop Bluetooth device discovery",
        .hint = NULL,
        .func = &cmd_stop_discovery,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&stop_discovery_cmd));

    const esp_console_cmd_t start_media_cmd = {
        .command = "start_media",
        .help = "Start media playback",
        .hint = NULL,
        .func = &cmd_start_media,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&start_media_cmd));

    const esp_console_cmd_t stop_media_cmd = {
        .command = "stop_media",
        .help = "Stop media playback",
        .hint = NULL,
        .func = &cmd_stop_media,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&stop_media_cmd));
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
}

static int restart(int argc, char **argv)
{
    printf("Restarting\n");
    esp_restart();
}

static int free_mem(int argc, char **argv)
{
    printf("\nFree heap size: internal %u, psram %u\nmin  heap size: internal %u, psram %u\n",
           heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));

    return 0;
}

/** 'tasks' command prints the list of tasks and related information */
static int tasks_info(int argc, char **argv)
{
    return esp_gmf_oal_sys_get_real_time_stats(1000, false);
}

void cli_register_sys()
{
    static const esp_console_cmd_t cmds[] = {
        {
            .command = "restart",
            .help = "Software reset of the chip",
            .hint = NULL,
            .func = &restart,
        },
        {
            .command = "free",
            .help = "Get the current size of free heap memory",
            .hint = NULL,
            .func = &free_mem,
        },
        {
            .command = "tasks",
            .help = "Get information about running tasks",
            .hint = NULL,
            .func = &tasks_info,
        }};

    for (int i = 0; i < sizeof(cmds) / sizeof(esp_console_cmd_t); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}

void cli_init()
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "BTAudio >";
#if CONFIG_ESP_CONSOLE_UART
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t cdc_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&cdc_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t usbjtag_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usbjtag_config, &repl_config, &repl));
#endif  /* CONFIG_ESP_CONSOLE_UART */

    cli_register_sys();
    cli_register_bt();

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

void cli_bt_device_found(const char *name, const uint8_t *bda)
{
#if CONFIG_GMF_EXAMPLE_A2DP_SOURCE
    if (strlen(target_device_name) > 0) {
        if (strstr(name, target_device_name) != NULL) {
            ESP_LOGI(TAG, "Found target device '%s', initiating auto-connect...", target_device_name);
            esp_bt_audio_classic_discovery_stop();
            esp_err_t ret = esp_bt_audio_classic_connect(ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC, (uint8_t *)bda);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Connecting to %s...\n", name);
                memset(target_device_name, 0, sizeof(target_device_name));
            } else {
                ESP_LOGE(TAG, "Failed to connect to %s: %s\n", name, esp_err_to_name(ret));
            }
        }
    }
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
}

void cli_bt_device_conn_st_chg(const uint8_t *bda, bool connected)
{
    if (connected) {
        memcpy(target_device_bda, bda, sizeof(target_device_bda));
    } else {
        memset(target_device_bda, 0, sizeof(target_device_bda));
    }
}
