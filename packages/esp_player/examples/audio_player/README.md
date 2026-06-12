# ESP Player Audio Playback

- [中文版](./README_CN.md)
- Basic Example: ⭐

## Example Brief

This example shows how to play audio urls with **ESP Player** combine with the highlevel module provided by `esp-gmf`:

- **esp_board_manager** initializes the codec, microSD card, and other board peripherals.
- **esp_player** handles demux, decode, A/V sync, and playback control.
- **esp_audio_render** mixes decoded PCM and drives the board Audio DAC through an output callback.
- Single-stream playback is the default (`AUDIO_PLAYER_STREAM_NUM` = 1). Set it to `2`–`4` in `audio_player_setup.h` to mix multiple ESP Player instances into one DAC.

### Typical Scenarios

- Music playback on audio products
- Prompt tones, notifications, and other decoded audio on embedded devices

### Supported Media Formats

**Enabled by default in this example**

| Format | Example | Notes |
|--------|---------|--------|
| MP3 | `.mp3` (elementary stream) | Default in `audio_player_setup.h`: `/sdcard/test.mp3` |
| AAC | `.aac` (elementary stream) | Edit `audio_player_play_urls[]` in `audio_player_setup.h` |

`sdkconfig.defaults` enables the ES elementary-stream extractor and MP3/AAC decoders only. Container formats such as `.m4a`, `.wav`, or `.mp4` are not enabled out of the box.

**To play other formats**

1. Run `idf.py menuconfig`.
2. Under **Component config**:
   - **ESP Audio Codec** — enable the decoder you need (for example `AUDIO DECODER FLAC SUPPORT`, `AUDIO DECODER PCM SUPPORT`).
   - **ESP Extractor** — enable the matching container/extractor (for example `MP4 EXTRACTOR SUPPORT` for `.m4a`/`.mp4`, `WAV EXTRACTOR SUPPORT` for `.wav`).
3. Edit `AUDIO_PLAYER_STREAM_NUM` and `audio_player_play_urls[]` in `audio_player_setup.h`. Save and rebuild.

For memory optimization this demo only enable ES8311 codec, if your board use other codec, enable the matching chip under **Component config → Audio Codec Device Configuration**.

## Environment Setup

### Hardware Required

- **Development board**: [ESP32-S3-Korvo-2](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2.html) is used as the default reference; any ESP audio board supported by `esp_board_manager` with an Audio DAC and speaker works as well.
- **Resources**: microSD card (FAT, for the default demo), Audio DAC, speaker.

### Default IDF Branch

This example supports IDF release/v5.4 (>= v5.4.3) and release/v5.5 (>= v5.5.2).

### Software Requirements

- Place a test file on the microSD card that matches `audio_player_play_urls[]` in `audio_player_setup.h` (default `/sdcard/test.mp3`). For network streams, configure Wi-Fi or other connectivity instead of using the SD card.

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
cd $YOUR_GMF_PATH/packages/esp_player/examples/audio_player
```

This example uses [ESP Board Manager](https://github.com/espressif/esp-board-manager) to manage board-level resources. The [`esp-bmgr-assist`](https://pypi.org/project/esp-bmgr-assist/) helper tool is recommended as the default entry point.

- Install once in your activated ESP-IDF Python environment:

```bash
pip install esp-bmgr-assist
pip install --upgrade esp-bmgr-assist
```

- List supported boards:

```bash
idf.py bmgr -l
```

  Example output:

```text
ℹ️  Main Boards:
  [1] dual_eyes_board_v1_0
  [2] esp32_c3_lyra
  [3] esp32_c5_spot
  [4] esp32_p4_function_ev
  [5] esp32_s3_korvo2_v3
  [6] esp32_s3_korvo2l
  [7] esp_box_3
  [8] esp_box_lite
  [9] esp_hi
```

- Select a board:

```bash
idf.py bmgr -b <board_index|board_name>
```

  For example, to select `esp32_s3_korvo2_v3`:

```bash
idf.py bmgr -b 5
# or
idf.py bmgr -b esp32_s3_korvo2_v3
```

  On first invocation, the component is downloaded automatically based on the `espressif/esp_board_manager` dependency declared in `main/idf_component.yml`.

> [!NOTE]
> To switch to a different board supported by `esp_board_manager`, repeat the same steps with the new board name or index.
> For a custom board, see [How to customize board](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/docs/how_to_customize_board.md).
> For more information about `esp_board_manager`, see the [ESP Board Manager Getting Started Guide](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README.md).

### Project Configuration

Edit `audio_player_setup.h`:

- `AUDIO_PLAYER_STREAM_NUM` — mixer stream count (`1`–`4`)
- `audio_player_play_urls[]` — one URL per stream (index `0` … `AUDIO_PLAYER_STREAM_NUM - 1`)
- `AUDIO_PLAYER_OUTPUT_VOLUME` — codec output volume (`0`–`100`, default `60`)

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

- After reset, the example mounts the microSD card, initializes the codec and ESP Player, plays URLs from `audio_player_play_urls[]`, waits until playback finishes, then releases resources.
- The default demo uses a local SD file (`/sdcard/test.mp3`). For HTTP/HTTPS or HLS, enable the matching options under **ESP Player → Input IO sources** in menuconfig, then edit `audio_player_play_urls[]` and see [ESP Player README](../../README.md) for URL grammar.
- For mixer mode, set `AUDIO_PLAYER_STREAM_NUM` to `2`–`4` and the first N entries in `audio_player_play_urls[]`; each stream plays independently and is mixed to the same DAC.

### Log Output

- A successful run prints SD mount, media stack setup, playback start, wait for completion, and teardown. Key steps are marked `[ 1 ]`–`[ 4 ]`, followed by `Playback started`, `Playback finished`, and `audio_player example finished`.

```c
I (853) main_task: Calling app_main()
I (864) AUDIO_PLAYER: [ 1 ] Mount SD card
I (1089) AUDIO_PLAYER: [ 2 ] Set up media stack, audio render, and ESP Player
I (1118) AUDIO_PLAYER: [ 3 ] Play /sdcard/test.mp3
I (1123) AUDIO_PLAYER: Playback started
I (1181) AUDIO_PLAYER: [ 4 ] Wait until playback finishes
I (11292) AUDIO_PLAYER: Playback finished
I (11416) AUDIO_PLAYER: audio_player example finished
I (11420) main_task: Returned from app_main()
```

## Troubleshooting

### File or stream cannot be opened

- Check that files on the SD card match `audio_player_play_urls[]` in `audio_player_setup.h` (local files), or that the network is available (HTTP/HTTPS).

### Playback error soon after start (`ESP_PLAYER_EVENT_ERROR`)

**Symptom**: You see `[stream N] playback started`, then the event callback logs an ERROR shortly afterward, followed by `Audio playback failed`.

**Sample log** (stream 0):

```text
I (xxxx) AUDIO_PLAYER: [stream 0] playback started
E (xxxx) ESP_PLAYER_PIPE_EVT: Extractor error
E (xxxx) ESP_PLAYER_EXTRACTOR: Open extractor error, ret: -1
E (xxxx) AUDIO_PLAYER: [stream 0] playback error (error_source=1)
E (xxxx) AUDIO_PLAYER: Audio playback failed
```

`error_source` comes from `esp_player_error_source_t` in `esp_player_types.h` and is delivered with `ESP_PLAYER_EVENT_ERROR`. In this audio-only example, common values are:

| `error_source` | Enum | Meaning | Common TAG / keywords | What to do |
|----------------|------|---------|----------------------|------------|
| `1` | `ESP_PLAYER_ERROR_SOURCE_EXTRACTOR` | Demux / read failure | `ESP_PLAYER_PIPE_EVT: Extractor error`; `ESP_PLAYER_EXTRACTOR: Open extractor error` / `Parse stream error` / `Read frame error`; `ESP_PLAYER: Invalid media URI` / `Unsupported format:` | Verify SD path matches `audio_player_play_urls[]` and the file is not corrupt; for container formats (e.g. `.m4a`/`.mp4`/`.wav`) see [Supported Media Formats](#supported-media-formats) and enable the matching **ESP Extractor** in menuconfig; mismatched extension vs. content often fails here |
| `2` | `ESP_PLAYER_ERROR_SOURCE_AUDIO_DECODER` | Audio decode failure | `ESP_PLAYER_PIPE_EVT: Audio decoder error`; `ESP_PLAYER_PIPELINE: Cannot create audio decoder` | Container opens but the bitstream cannot be decoded: enable the codec under **ESP Audio Codec** in menuconfig (see [Supported Media Formats](#supported-media-formats)); ensure file encoding matches the URL extension |
| `3` | `ESP_PLAYER_ERROR_SOURCE_AUDIO_RENDER` | Audio render / output failure | `ESP_PLAYER_PIPE_EVT: Audio render error`; `ESP_PLAYER_AUDIO_RENDER: Open audio render stream error` / `Write audio render stream error`; `ESP_PLAYER_STATE: Failed to start audio renderer` | Run `idf.py bmgr -b <board>`, check board Audio DAC settings (see [No audio and no error](#no-audio-and-no-error)); in mixer mode, keep `AUDIO_PLAYER_STREAM_NUM` within render limits |

**Suggested steps**:

1. Search the monitor for `playback error (error_source=` and note the value.
2. Scroll up to the matching `E (...)` lines from the TAGs in the table above.
3. Apply the fix; if format support is involved, enable the required extractor/decoder `CONFIG_*` options and rebuild (see [Supported Media Formats](#supported-media-formats)).

> **Note**: If you never see `playback started` and get `stream N start failed` instead, `esp_player_set_data_src()` / `esp_player_run()` failed synchronously—check URL and SD mount ([File or stream cannot be opened](#file-or-stream-cannot-be-opened)), not the runtime `error_source` table above.

### No audio and no error

- Confirm `idf.py bmgr -b <board>` was run, `AUDIO_PLAYER_OUTPUT_VOLUME` in `audio_player_setup.h` is reasonable, and DAC / amplifier hardware and codec settings in the board YAML are correct.

### board_manager build or board selection failure

- Install `esp-bmgr-assist` in the activated IDF Python environment, run `idf.py bmgr -b <board>`, then rebuild. Please refer to [esp-bmgr-assist](https://docs.espressif.com/projects/esp-board-manager/zh_CN/latest/tools/esp-bmgr-assist.html#).

### Related References

- Component API, URLs, and advanced usage: [ESP Player README](../../README.md)

## Technical Support

For technical support, please use the following channels:

- ESP32 forum: [esp32.com](https://esp32.com/viewforum.php?f=20)
- GitHub issues: [esp-gmf issues](https://github.com/espressif/esp-gmf/issues)

We will get back to you as soon as possible.
