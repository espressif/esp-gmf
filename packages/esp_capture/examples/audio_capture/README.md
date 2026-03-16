# ESP Audio Capture Example

- [中文版](./README_CN.md)
- Regular Example: ⭐⭐

## Example Brief

- This example demonstrates common audio capture pipelines using `esp_capture`.
- It includes basic capture, AEC capture, MP4 recording, and customized processing pipelines.

### Typical Scenarios

- Capture microphone audio for fixed duration.
- Improve duplex capture quality with AEC.
- Record captured audio slices to SD card as MP4.
- Extend capture graph with custom processing elements (for example ALC).

## Environment Setup

### Hardware Required

- Recommended board: [ESP32-S3-Korvo2](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2.html) or [ESP32-P4-Function-EV-Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html)
- Audio input device (ADC microphone)
- SD card (optional, required for muxer recording case)

### Default IDF Branch

This example supports IDF `release/v5.4` (>= v5.4.3) and `release/v5.5` (>= v5.5.2).

## Build and Flash

### Build Preparation

```bash
cd $YOUR_GMF_PATH/packages/esp_capture/examples/audio_capture
idf.py gen-bmgr-config -l
idf.py gen-bmgr-config -b esp32_s3_korvo2_v3
```

> [!NOTE]
> For other supported boards, use the same command flow with the corresponding board name.
> For custom boards, see [Custom Board Guide](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/docs/how_to_customize_board.md).

### Build and Flash Commands

```bash
idf.py build
idf.py -p PORT flash monitor
```

## How to Use the Example

### Flow Introduction

```mermaid
flowchart LR
  MIC[ADC microphone] --> CAP[esp_capture audio source]
  CAP --> PROC[Optional process / AEC / custom elements]
  PROC --> ENC[Audio encoder]
  ENC --> USE[Frame callback or sink]
  ENC --> MUX[MP4 muxer optional]
  MUX --> SD[/sdcard/cap_X.mp4]
```

### Functionality and Usage

`app_main()` runs the following cases sequentially with a 10-second duration:

1. `audio_capture_run`
2. `audio_capture_run_with_aec`
3. `audio_capture_run_with_muxer` (only when SD card mount succeeds)
4. `audio_capture_run_with_customized_process`

Additional behavior:

- Registers default audio encoders (`esp_audio_enc_register_default`)
- Registers MP4 muxer (`mp4_muxer_register`)
- Applies custom thread scheduler through `esp_capture_set_thread_scheduler`

### Configuration

Main configurable items in `main/settings.h`:

- `AUDIO_CAPTURE_FORMAT` (default: `ESP_CAPTURE_FMT_ID_AAC`)
- `AUDIO_CAPTURE_SAMPLE_RATE` (default: `16000`)
- `AUDIO_CAPTURE_CHANNEL` (default: `2`)

## Troubleshooting

### Capture start fails

- Confirm audio ADC device init succeeds
- Confirm board audio input hardware is connected and available

### Recording file not generated

- Confirm SD card mount succeeds in `app_main()`
- Check SD card free space

### AEC case not available or poor effect

- AEC support is target dependent (mainly ESP32-S3/ESP32-P4)
- Confirm both playback and record paths are correctly configured for the board

## Technical Support

- Technical support forum: [esp32.com](https://esp32.com/viewforum.php?f=20)
- Issues and feature requests: [GitHub issue](https://github.com/espressif/esp-gmf/issues)
