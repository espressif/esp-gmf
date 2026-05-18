# Simple Piano Example

- [中文版](./README_CN.md)
- Complex Example: ⭐⭐⭐

## Example Brief

- This example builds a polyphonic piano player on top of `esp_audio_render`.
- It demonstrates four generated tracks (melody, harmony, bass, arpeggio), mixed in real time and played through board codec output.

### Typical Scenarios

- Learn multi-stream rendering with generated PCM data
- Verify real-time synthesis and mixing performance
- Use UART commands for interactive note on/off control

## Environment Setup

### Hardware Required

- Recommended board: [ESP32-S3-Korvo2](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2.html) or [ESP32-P4-Function-EV-Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html)
- Audio playback output (speaker or headphone)

### Default IDF Branch

This example supports IDF `release/v5.4` (>= v5.4.3) and `release/v5.5` (>= v5.5.2).

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

### Build and Flash Commands

```bash
idf.py build
idf.py -p PORT flash monitor
```

## How to Use the Example

### Flow Introduction

```mermaid
flowchart LR
  GEN[Note generation per track] --> W0[Stream 0 melody]
  GEN --> W1[Stream 1 harmony]
  GEN --> W2[Stream 2 bass]
  GEN --> W3[Stream 3 arpeggio]
  W0 --> MIX[esp_audio_render mixer]
  W1 --> MIX
  W2 --> MIX
  W3 --> MIX
  MIX --> SINK[esp_codec_dev output]
```

### Functionality and Usage

The example plays "Twinkle Twinkle Little Star" using 4 tracks:

- Track 0: melody
- Track 1: harmony
- Track 2: bass
- Track 3: arpeggio

Default render format:

- Sample rate: 16 kHz
- Bit width: 16 bit
- Channels: mono

Main execution flow:

1. Initialize audio DAC and create render instance
2. Open 4 render streams and create `song_render`
3. Generate note chunks and feed each track stream
4. Mix and play output through `esp_codec_dev`

### **Enable Real-time Piano (Optional)**
To enable interactive piano control via UART:

1. **Rebuild after Enable the feature** in [piano_example.c](main/piano_example.c):
   ```bash
   #define SUPPORT_REALTIME_TRACK
   ```

2. **Use the Python controller** (in a separate terminal):
   ```bash
   # Install dependencies
   pip install pyserial

   # Run the piano controller
   python3 piano_key.py --port /dev/ttyUSB0 --baud 115200
   ```

3. **Play piano in real-time**:
   - **Numbers 1-7**: C4-B4 (mid octave)
   - **Letters Q-U**: C5-B5 (high octave)
   - **ESC**: Stop piano
   - **Ctrl+C**: Exit controller

## Troubleshooting

### No sound from piano output

- Confirm DAC device init succeeds (`ESP_BOARD_DEVICE_NAME_AUDIO_DAC`).
- Check output volume (`esp_codec_dev_set_out_vol`)
- Confirm speaker/headphone hardware path

### Realtime control does not respond

- Ensure UART0 is connected to host serial tool
- Check command format (`P:C4`, `R:C4`, `P:ESC`)

## Technical Support

- Technical support forum: [esp32.com](https://esp32.com/viewforum.php?f=20)
- Issues and feature requests: [GitHub issue](https://github.com/espressif/esp-gmf/issues)
