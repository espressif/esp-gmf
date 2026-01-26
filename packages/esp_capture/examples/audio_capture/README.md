# ESP Audio Capture Example

This example demonstrates various audio capture capabilities using the ESP Capture framework. It showcases different audio capture scenarios including basic capture, AEC (Acoustic Echo Cancellation), file recording, and customized audio processing.

## Use Cases

- Basic audio capture with configurable duration
- Audio capture with AEC support (ESP32-S3 and ESP32-P4 only)
- Audio capture with MP4 file recording
- Audio capture with customized processing pipeline (including ALC - Automatic Level Control)

## Hardware Requirements

- Recommend to use [ESP32-S3-Korvo2](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2.html) or [esp32-p4-function-ev-board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html) development board

## Software Requirements

- ESP-IDF v5.4 or later
- esp_capture
- ESP GMF framework

## Building and Flash
List available boards and select the corresponding board, taking `ESP32-S3-Korvo-2` as an example:
> For detailed usage refer to [Quick Start](https://github.com/espressif/esp-gmf/tree/main/packages/esp_board_manager#quick-start)

```
idf.py gen-bmgr-config -l
idf.py gen-bmgr-config -b esp32_s3_korvo2_v3
```
- Building and flash
```bash
idf.py build
idf.py -p /dev/XXXXX flash monitor
```

> [!NOTE]
> For other supported boards (via `esp_board_manager`), follow the same steps above.
> For customer board refer to the [Custom Board Guide](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/docs/how_to_customize_board.md) for details.

## Usage Examples

### Basic Audio Capture

The basic audio capture example demonstrates simple audio capture functionality. It captures audio for a specified duration and processes the frames.

```c
audio_capture_run(duration_ms);
```

### Audio Capture with AEC

This example enables Acoustic Echo Cancellation (AEC) for better audio quality in scenarios where both microphone and speaker are active. AEC is only supported on ESP32-S3 and ESP32-P4.

```c
audio_capture_run_with_aec(duration_ms);
```

### Audio Capture with File Recording

This example demonstrates how to capture audio and save it to MP4 files on an SD card. The recording is automatically sliced into multiple files based on the configured duration.

```c
audio_capture_run_with_muxer(duration_ms);
```

The recorded files will be saved as `/sdcard/cap_X.mp4` where X is the slice index.

### Audio Capture with Customized Processing

This example shows how to customize the audio processing pipeline by adding additional elements like ALC (Automatic Level Control). It demonstrates:

1. Adding custom elements to the capture pipeline
2. Configuring element parameters
3. Building a custom processing pipeline through connection-ship

```c
audio_capture_run_with_customized_process(duration_ms);
```

## Configuration

The example can be configured through the following settings in [settings.h](main/settings.h):

- `AUDIO_CAPTURE_FORMAT`: Audio format (e.g., AAC, OPUS)
- `AUDIO_CAPTURE_SAMPLE_RATE`: Sample rate (e.g., 16000 Hz)
- `AUDIO_CAPTURE_CHANNEL`: Number of audio channels
