# Dual Eyes Display Example

- [中文版](./README_CN.md)
- Regular Example: ⭐⭐

## Example Brief

- This example demonstrates how to use the `esp_video_render_dual_stream_*` API to drive left and right eye streams as one synchronized rendering task.
- It shows both side-by-side output on a single display and split output on two displays, depending on the build configuration.
- It also demonstrates optional LVGL integration and frame-rate controlled playback using MJPEG sources from an SD card.

### Typical Scenarios

- Robot eyes and animated face displays
- AI companion products
- Simple stereo or dual-panel video effects
- Two-stream synchronized playback with one control flow

### Run Flow

After startup, the example initializes the required board devices, opens the dual-eyes renderer, and repeatedly plays the left and right eye MJPEG files.

- In single-display mode, both eyes are placed on one screen.
- In dual-display mode, each eye is rendered to its own display path.
- The example also runs both non-LVGL and LVGL-backed display flows when available.

```mermaid
sequenceDiagram
    participant App
    participant DualEyes

    App->>DualEyes: esp_video_render_dual_stream_open()
    App->>DualEyes: esp_video_render_dual_stream_set_display_rect()
    loop for each pair of MJPEG frames
        App->>DualEyes: esp_video_render_dual_stream_get_buffer()
        App->>DualEyes: fill left/right frame buffers
        App->>DualEyes: esp_video_render_dual_stream_send_buffer()
    end
    App->>DualEyes: esp_video_render_dual_stream_close()
```

### File Structure

```text
examples/dual_eyes
├── main
│   ├── dual_display.h
│   ├── dual_eyes.c
│   ├── dual_eyes.h
│   ├── main.c
│   ├── settings.h
│   └── video_render_sys.c
├── CMakeLists.txt
├── partitions.csv
├── README.md
└── README_CN.md
```

## Environment Setup

### Hardware Required

For single-display mode:

- An ESP board with LCD support, such as:
  - [ESP32-S3-Korvo2](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2.html)
  - [ESP32-P4-Function-EV-Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html)
- One supported display panel
- An SD card with MJPEG test files

For dual-display mode:

- Board and display wiring that matches `main/dual_display.h`
- Two supported panels

### Default IDF Branch

This example supports IDF release/v5.5 (>= v5.5.2).

### Software Requirements

- MJPEG test files on the SD card
- Default input files in `main/settings.h`:
  - `LEFT_FILE`
  - `RIGHT_FILE`

## Build and Flash

### Select and configure a development board

This example uses [ESP Board Manager](https://github.com/espressif/esp-board-manager) to manage board-level resources. The [`esp-bmgr-assist`](https://pypi.org/project/esp-bmgr-assist/) helper tool is recommended as the default entry point.

Install once in your activated ESP-IDF Python environment:

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

Key configuration items:

- `main/settings.h`
  - `VIDEO_WIDTH`
  - `VIDEO_HEIGHT`
  - `MAX_FRAME_SIZE`
  - `LEFT_FILE`
  - `RIGHT_FILE`
- `DUAL_EYES_ON_DUAL_DISPLAY`
  - comment out for one-display mode
  - enable for dual-display mode
- `main/dual_display.h`
  - update display wiring and panel parameters for dual-display targets

Generate MJPEG files with:

```bash
ffmpeg -i input.mp4 -q:v 1 -c:v mjpeg -pix_fmt yuvj420p -vtag MJPG left.mjpeg
```

Check the largest encoded frame size with:

```bash
ffprobe -v error -select_streams v:0 -show_entries packet=size -of csv=p=0 left.mjpeg | sort -n | tail -1
```

Use that result to size `MAX_FRAME_SIZE`.

### Build and Flash Commands

```bash
idf.py build
idf.py -p PORT flash monitor
```

## How to Use the Example

### Functionality and Usage

- Copy the left and right MJPEG files [left.mjpeg](../../assets/left.mjpeg), [rignt.mjpeg](../../assets/right.mjpeg) to the SD card.
- Flash the example and reset the board.
- The example automatically runs the configured display mode:
  - one display with left and right eyes side-by-side
  - or dual display with one eye per panel
- When LVGL support is enabled, the example also exercises the LVGL-backed path.

### Results

When everything is configured correctly, you should see:

- synchronized left and right eye playback
- automatic display rectangle setup for both eyes
- repeated playback for stress or leak checking
- FPS logging after playback loops complete

## Troubleshooting

### Display too small

If the panel is smaller than the configured eye resolution, single-display mode may scale down, but some hardware combinations may still be too small for the requested layout.

### File open failure

If the example skips playback, verify the SD card is mounted and the files match the paths defined in `main/settings.h`.

### Incorrect dual-display wiring

If dual-display mode does not work, check the pin and panel configuration in `main/dual_display.h`.

## Technical Support

- Technical support: [esp32.com](https://esp32.com/viewforum.php?f=20) forum
- Issue reports and feature requests: [GitHub issue](https://github.com/espressif/esp-gmf/issues)
