# ESP Video Capture Example

This example demonstrates various video capture capabilities using the ESP Capture. It showcases different video capture scenarios including basic capture, one-shot capture, file recording, overlay, and dual-path capture.

## Features

- Basic video capture with configurable duration
- One-shot video capture mode
- Video capture with MP4 file recording
- Video capture with text overlay
- Dual-path video capture (multiple output formats)
- Customized processing pipeline
- Configurable task scheduling

## Hardware Requirements

- Recommend to use [ESP32-S3-Korvo2](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2.html) or [esp32-p4-function-ev-board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html) development board
- For other board can check [Other Board Support](#other-board-support) section

## Software Requirements

- ESP-IDF v5.4 or later
- esp_capture
- ESP GMF framework

## Building and Flash

```bash
idf.py -p /dev/XXXXX flash monitor
```

## Usage Examples

### Basic Video Capture

The basic video capture example demonstrates simple video capture functionality. It captures both video and audio for a specified duration and processes the frames.

```c
video_capture_run(duration_ms);
```

### One-Shot Video Capture

This example demonstrates how to capture video frames in one-shot mode, which is useful for scenarios where you need to capture frames at specific intervals. It captures a frame every 500ms.

```c
video_capture_run_one_shot(duration_ms);
```

### Video Capture with File Recording

This example demonstrates how to capture video and audio, then save it to MP4 files on an SD card. The recording is automatically sliced into multiple files based on the configured duration.

```c
video_capture_run_with_muxer(duration_ms);
```

The recorded files will be saved as `/sdcard/vid_X.mp4` where X is the slice index.

### Video Capture with Overlay

This example shows how to add text overlay to the video stream. It demonstrates:
1. Creating and configuring text overlay
2. Updating overlay content dynamically
3. Managing overlay regions and colors

```c
video_capture_run_with_overlay(duration_ms);
```

### Dual-Path Video Capture

This example demonstrates how to capture video in two different formats simultaneously. It's useful for scenarios where you need multiple output formats (e.g., one for recording and one for streaming).

```c
video_capture_run_dual_path(duration_ms);
```

### Customized Processing Pipeline

This example shows how to customize the video processing pipeline by:
1. Adding custom elements to the capture pipeline
2. Configuring element parameters
3. Building a custom processing pipeline through connection-ship

```c
video_capture_run_with_customized_process(duration_ms);
```

## Task Scheduler Configuration

The example includes a configurable task scheduler that allows you to optimize the performance of different capture components. The scheduler configuration `capture_test_scheduler` in [main.c](main/main.c) demonstrates how to:

1. Set stack sizes for different tasks
2. Assign tasks to specific CPU cores
3. Set task priorities
4. Optimize resource usage


## Configuration

The example can be configured through the following settings in [settings.h](main/settings.h):

- Video format and resolution settings:
  - `VIDEO_SINK0_FMT`: Primary video format
  - `VIDEO_SINK0_WIDTH`: Primary video width
  - `VIDEO_SINK0_HEIGHT`: Primary video height
  - `VIDEO_SINK0_FPS`: Primary video frame rate
- Audio settings:
  - `AUDIO_SINK0_FMT`: Primary audio format
  - `AUDIO_SINK0_SAMPLE_RATE`: Primary audio sample rate
  - `AUDIO_SINK0_CHANNEL`: Primary audio channels
- Secondary path settings (for dual-path):
  - `VIDEO_SINK1_FMT`: Secondary video format
  - `VIDEO_SINK1_WIDTH`: Secondary video width
  - `VIDEO_SINK1_HEIGHT`: Secondary video height
  - `VIDEO_SINK1_FPS`: Secondary video frame rate

## Other Board Support

This example uses `gmf_app_utils` for simplified board initialization to enable rapid verification. You can check board compatibility in the menuconfig under `GMF APP Configuration`. For detailed other board support, see the [gmf_app_utils documentation](https://github.com/espressif/esp-gmf/blob/main/packages/gmf_app_utils/README.md).

Or you can use [esp-bsp](https://github.com/espressif/esp-bsp/tree/master) APIs as alternatives:
 - Use `bsp_audio_codec_microphone_init()` to replace `esp_gmf_app_get_record_handle()`
 - Use `bsp_sdcard_mount()` to replace `esp_gmf_app_setup_sdcard()`
 - Use `BSP_CAMERA_DEFAULT_CONFIG` to replace `get_camera_cfg()`


## Troubleshooting

1. If video capture fails to start:
   - Check if the camera is properly initialized
   - Verify camera connections
   - Check video format and resolution settings

2. If audio capture fails:
   - Check if the audio codec is properly initialized
   - Verify microphone connections
   - Check audio format and sample rate settings

3. If file recording fails:
   - Verify SD card is properly mounted
   - Check SD card has sufficient free space

4. If performance issues occur:
   - Review task scheduler configuration
   - Adjust task priorities and core assignments
   - Monitor stack usage and adjust if necessary
