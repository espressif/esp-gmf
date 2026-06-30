# ESP Player Audio/Video Playback

- [中文版](./README_CN.md)
- Basic Example: ⭐

## Example Brief

This example shows how to play video url with **ESP Player** combine with the highlevel module provided by `esp-gmf`:

- **esp_board_manager** initializes the LCD, audio DAC, and microSD card.
- **esp_player** handles demux, decode, A/V sync, and playback control.
- **esp_audio_render** outputs decoded audio PCM to the speaker.
- **esp_video_render** handles decoded video frames and displays them on the LCD.

### Typical Scenarios

- Local MP4 playback on embedded devices with a display and speaker
- Validating the ESP Player A/V path (Extractor → Audio/Video Decoder → Render with sync)

### Supported Media Formats

**Enabled by default in this example**

| Format | Example | Notes |
|--------|---------|--------|
| MP4 (H.264 + AAC, typical) | `.mp4` | Default in `video_player_setup.h`: `/sdcard/test.mp4` |

`sdkconfig.defaults` enables the **MP4 extractor**, **H.264 video decoder**, **AAC audio decoder**, and **M4A simple decoder** for in-container audio. For other audio codecs inside MP4 (for example PCM), enable the matching decoder in menuconfig.

**To play other formats**

1. Run `idf.py menuconfig`.
2. Under **Component config**:
   - **ESP Extractor** — enable the matching container (for example `AVI EXTRACTOR SUPPORT`).
   - **ESP Video Codec** — enable the decoder you need (for example `VIDEO DECODER SW MJPEG SUPPORT`).
3. Edit `VIDEO_PLAYER_PLAY_URL` in `video_player_setup.h` if needed. Save and rebuild.

## Environment Setup

### Hardware Required

- **Development board**: [ESP32-S3-Korvo-2](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2.html) is the default reference; [ESP32-P4-Function-EV-Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html) and other boards with LCD and Audio DAC are also supported.
- **Resources**: microSD card (FAT), LCD module, Audio DAC, speaker.
- PSRAM is recommended for stable decode and display.

### Default IDF Branch

This example supports IDF release/v5.4 (>= v5.4.3) and release/v5.5 (>= v5.5.2).

### Software Requirements

- Place an MP4 test file on the microSD card that matches `VIDEO_PLAYER_PLAY_URL` in `video_player_setup.h` (default `/sdcard/test.mp4`; H.264 + AAC recommended).

## Build and Flash

### Build Preparation

Before building this example, ensure the ESP-IDF environment is set up. If it is already set up, skip this paragraph and go to the project directory. If not, run the following in the ESP-IDF root directory to complete the environment setup. For full steps, see the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/index.html).

```
./install.sh
. ./export.sh
```

Brief steps:

- Go to this example's project directory:

```
cd $YOUR_GMF_PATH/packages/esp_player/examples/video_player
```

This example uses [ESP Board Manager](https://github.com/espressif/esp-board-manager) to manage board-level resources. The [`esp-bmgr-assist`](https://pypi.org/project/esp-bmgr-assist/) helper tool is recommended as the default entry point.

- Install once in your activated ESP-IDF Python environment:

```bash
pip install esp-bmgr-assist
pip install --upgrade esp-bmgr-assist  # run this command when an update is requested
```

- List supported boards:

```bash
idf.py bmgr -l
```

  Example output:

```text
ℹ️  Board Components:
  espressif/esp_boards:
    [1] esp32_c3_lyra
    [2] esp32_lyrat_4_3
    [3] esp32_lyrat_mini_1_1
    [4] esp32_p4_eye
    [5] esp32_p4_function_ev_board
    [6] esp32_s31_function_coreboard_1
    [7] esp32_s31_korvo_1
    [8] esp32_s3_box_3
    [9] esp32_s3_box_lite
    [10] esp32_s3_korvo_2_3
    [11] esp32_s3_lcd_ev_board
    [12] esp_vocat_1_0
    [13] esp_vocat_1_2
```

The example output above is based on the board list and ordering from `esp_boards` 0.5.2. Different `esp_boards` versions or custom board dependencies may change the list and indexes. Use the actual output of `idf.py bmgr -l` when selecting a board.

- Select a board:

```bash
idf.py bmgr -b <board_index|board_name>
```

  For example, to select `esp32_s3_korvo_2_3`:

```bash
idf.py bmgr -b 10
# or
idf.py bmgr -b esp32_s3_korvo_2_3
```

  On first invocation, the component is downloaded automatically based on the `espressif/esp_board_manager` dependency declared in `main/idf_component.yml`.

> [!NOTE]
> To switch to a different board supported by `esp_board_manager`, repeat the same steps with the new board name or index.
> For a custom board, see [Creating a Board Guide](https://docs.espressif.com/projects/esp-board-manager/en/latest/create-board/index.html).
> For more information about `esp_board_manager`, see the [ESP Board Manager Getting Started Guide](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README.md).

### Project Configuration

Edit `video_player_setup.h`:

- `VIDEO_PLAYER_PLAY_URL` — media URL (default `/sdcard/test.mp4`)
- `VIDEO_PLAYER_OUTPUT_VOLUME` — codec output volume (`0`–`100`, default `60`)

To enable other media formats, use `idf.py menuconfig` under **Component config** (see **Supported Media Formats** above).

### Build and Flash

- Build the example

```
idf.py build
```

- Flash and open the serial monitor (replace PORT with your port name):

```
idf.py -p PORT flash monitor
```

- Exit the monitor with `Ctrl-]`

## How to Use the Example

### Features and Usage

- After reset, the example mounts the microSD card, initializes the LCD, codec, and ESP Player, plays the MP4 from `VIDEO_PLAYER_PLAY_URL` (audio and video together), waits until playback finishes, then releases resources.
- The default demo uses a local SD file (`/sdcard/test.mp4`). For HTTP/HTTPS or HLS, enable the matching options under **ESP Player → Input IO sources** in menuconfig, then edit `VIDEO_PLAYER_PLAY_URL` and see [ESP Player README](../../README.md).

### Log Output

- A successful run prints SD mount, media stack setup, playback start, wait for completion, and teardown. Key steps are marked `[ 1 ]`–`[ 4 ]`, followed by `Playback started`, `Playback finished`, and `video_player example finished`.

```c
I (853) main_task: Calling app_main()
I (864) VIDEO_PLAYER: [ 1 ] Mount SD card
I (1089) VIDEO_PLAYER: [ 2 ] Set up media stack, audio/video render, and ESP Player
I (1118) VIDEO_PLAYER: [ 3 ] Play /sdcard/test.mp4
I (1123) VIDEO_PLAYER: Playback started
I (1181) VIDEO_PLAYER: [ 4 ] Wait until playback finishes
I (45292) VIDEO_PLAYER: Playback finished
I (45416) VIDEO_PLAYER: video_player example finished
I (45420) main_task: Returned from app_main()
```

## Troubleshooting

### Cannot open file

- Check that files on the SD card match `VIDEO_PLAYER_PLAY_URL` in `video_player_setup.h` and that the card is mounted (FAT).

### video_player_setup failed / No display_lcd

- Ensure the selected board YAML includes a **display_lcd** device and that you ran `idf.py bmgr -b <board>` with an LCD-capable board.

### Playback error soon after start (`ESP_PLAYER_EVENT_ERROR`)

**Symptom**: You see `Playback started`, then the event callback logs an ERROR shortly afterward, followed by `Video playback failed`.

**Sample log**:

```text
I (xxxx) VIDEO_PLAYER: Playback started
E (xxxx) ESP_PLAYER_PIPE_EVT: Extractor error
E (xxxx) ESP_PLAYER_EXTRACTOR: Parse stream error, ret: -1
E (xxxx) VIDEO_PLAYER: Playback error (error_source=1)
E (xxxx) VIDEO_PLAYER: Video playback failed
```

`error_source` comes from `esp_player_error_source_t` in `esp_player_types.h` and is delivered with `ESP_PLAYER_EVENT_ERROR`. This example uses A/V sync playback (`ESP_PLAYER_MASK_AV`). Common values:

| `error_source` | Enum | Meaning | Common TAG / keywords | What to do |
|----------------|------|---------|----------------------|------------|
| `1` | `ESP_PLAYER_ERROR_SOURCE_EXTRACTOR` | Demux / read failure | `ESP_PLAYER_PIPE_EVT: Extractor error`; `ESP_PLAYER_EXTRACTOR: Open extractor error` / `Parse stream error` / `Read frame error`; `ESP_PLAYER: Invalid media URI` / `Unsupported format:` | Verify SD path matches `VIDEO_PLAYER_PLAY_URL` and the MP4 is not corrupt; for non-MP4 containers see [Supported Media Formats](#supported-media-formats) and enable the matching **ESP Extractor** in menuconfig |
| `2` | `ESP_PLAYER_ERROR_SOURCE_AUDIO_DECODER` | Audio track decode failure | `ESP_PLAYER_PIPE_EVT: Audio decoder error`; `ESP_PLAYER_PIPELINE: Cannot create audio decoder` | Container opens but the audio track cannot be decoded: enable the codec under **ESP Audio Codec** in menuconfig (AAC by default; PCM etc. need extra options); see [Video but no audio](#video-but-no-audio) |
| `3` | `ESP_PLAYER_ERROR_SOURCE_AUDIO_RENDER` | Audio render / output failure | `ESP_PLAYER_PIPE_EVT: Audio render error`; `ESP_PLAYER_AUDIO_RENDER: Open audio render stream error` / `Write audio render stream error`; `ESP_PLAYER_STATE: Failed to start audio renderer` | Run `idf.py bmgr -b <board>`, check board Audio DAC and `VIDEO_PLAYER_OUTPUT_VOLUME` (see [Video but no audio](#video-but-no-audio)) |
| `4` | `ESP_PLAYER_ERROR_SOURCE_VIDEO_DECODER` | Video track decode failure | `ESP_PLAYER_PIPE_EVT: Video decoder error`; `ESP_PLAYER_PIPELINE: video_decoder: esp_gmf_pipeline_report_info(VIDEO) failed` | Enable the decoder matching the MP4 video codec under **ESP Video Codec** in menuconfig (H.264 SW decode by default); see [Supported Media Formats](#supported-media-formats) |
| `5` | `ESP_PLAYER_ERROR_SOURCE_VIDEO_RENDER` | Video display / render failure | `ESP_PLAYER_PIPE_EVT: Video render error`; `ESP_PLAYER_VIDEO_RENDER: Failed to open video render stream` / `Invalid configuration or render handle`; `VIDEO_PLAYER_SETUP: Failed to set LCD display backend` | Ensure the board YAML includes display_lcd and `idf.py bmgr -b <board>` was run (see [video_player_setup failed / No display_lcd](#video_player_setup-failed--no-display_lcd)); enable PSRAM on RGB/DSI boards (see [Choppy video](#choppy-video) and target `sdkconfig.defaults.*`) |

**Suggested steps**:

1. Search the monitor for `Playback error (error_source=` and note the value.
2. Scroll up to the matching `E (...)` lines from the TAGs in the table above.
3. Apply the fix; if format support is involved, enable the required extractor/decoder `CONFIG_*` options and rebuild (see [Supported Media Formats](#supported-media-formats)).

> **Note**: If you never see `Playback started` and get `Playback start failed` instead, `esp_player_set_data_src()` / `esp_player_run()` failed synchronously—check URL and SD mount ([Cannot open file](#cannot-open-file)), not the runtime `error_source` table above. If `video_player_setup failed` appears at step `[ 2 ]`, LCD/render init failed—see [video_player_setup failed / No display_lcd](#video_player_setup-failed--no-display_lcd).

### Video but no audio

- Check that the board has an Audio DAC, `VIDEO_PLAYER_OUTPUT_VOLUME` in `video_player_setup.h` is reasonable, and the MP4 audio track uses an enabled codec (AAC by default).

### Choppy video

- Enable PSRAM or use a lower-resolution / lower-bitrate MP4 file.

### board_manager build or board selection failure

- Install `esp-bmgr-assist` in the activated IDF Python environment, run `idf.py bmgr -b <board>`, then rebuild. Please refer to [esp-bmgr-assist](https://docs.espressif.com/projects/esp-board-manager/zh_CN/latest/tools/esp-bmgr-assist.html#).

### Related References

- Component API, URLs, and advanced usage: [ESP Player README](../../README.md)

## Technical Support

For technical support, please use the following channels:

- ESP32 forum: [esp32.com](https://esp32.com/viewforum.php?f=20)
- GitHub issues: [esp-gmf issues](https://github.com/espressif/esp-gmf/issues)

We will get back to you as soon as possible.
