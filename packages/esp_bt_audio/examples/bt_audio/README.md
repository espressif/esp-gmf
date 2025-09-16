# Basic Bluetooth Audio Example

- [中文版](./README_CN.md)

- Regular Example: ⭐⭐ (multiple roles and GMF pipeline coordination, serial command control)

## Example Brief

This example initializes Bluetooth audio through the `esp_bt_audio` module and uses `esp_gmf_io_bt` to link the Bluetooth audio stream with a GMF pipeline, enabling audio playback or recording. It also provides serial commands to demonstrate control of Bluetooth audio playback and voice calls.

### Typical Scenarios

- **Bluetooth speaker (A2DP Sink)**: Phone connects to the device to play music; supports play/pause/next/previous, volume, and metadata
- **Bluetooth source (A2DP Source)**: Device discovers and connects to Bluetooth headphones or speakers and streams local or microSD audio to the remote device
- **Bluetooth voice call (HFP HF)**: Answer/reject incoming calls, dial; call state and telephony status reporting; AEC in the GMF pipeline to improve call clarity

### Prerequisites

- This example involves Bluetooth concepts and protocols; see the official [Bluetooth Specifications](https://www.bluetooth.com/specifications/specs/)
- This example uses `esp_board_manager` for board-level resources; see [ESP Board Manager](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md) for setup

### Resources

- An audio development board with Audio DAC/ADC, I2S, and microSD (e.g. lyrat_mini_v1_1); for A2DP Source, prepare a microSD card and test audio files

## Environment Setup

### Hardware Required

- **Board**: Default is `lyrat_mini_v1_1`; other ESP32-based audio boards with I2S codec and microSD are also supported
- **Peripherals**: Audio DAC, Audio ADC, I2S, microSD card (for A2DP Source, store `media0.mp3`, `media1.mp3`, `media2.mp3`)
- **Bluetooth**: Classic Bluetooth (BR/EDR) for A2DP, AVRCP, and HFP

### Default IDF Branch

This example supports IDF release/v5.5 (>= v5.5.2).

### Software Requirements

- For A2DP Source, place three test audio files on the microSD root: `media0.mp3`, `media1.mp3`, `media2.mp3`
- For A2DP Sink, a phone or other A2DP Source device is needed; for A2DP Source, Bluetooth headphones or a speaker are needed

## Build and Flash

### Build Preparation

Before building this example, ensure the ESP-IDF environment is set up. If it is already set up, skip to the project directory and run the board setup steps below. If not, run the following in the ESP-IDF root directory to complete the environment setup. For full steps, see the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/index.html).

```
./install.sh
. ./export.sh
```

Short steps:

- Go to this example's project directory (example path below; replace with your actual path):

```
cd $YOUR_GMF_PATH/packages/esp_bt_audio/examples/bt_audio
```

- This example uses `esp_board_manager` for board-level resources; add board support first

On Linux / macOS:

```bash
idf.py set-target esp32
export IDF_EXTRA_ACTIONS_PATH=./managed_components/esp_board_manager
idf.py gen-bmgr-config -b lyrat_mini_v1_1
```

On Windows:

```powershell
idf.py set-target esp32
$env:IDF_EXTRA_ACTIONS_PATH = ".\managed_components\esp_board_manager"
idf.py gen-bmgr-config -b lyrat_mini_v1_1
```

For a custom board, see [Custom board](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md#custom-board).

### Project Configuration

Select the Bluetooth role and options in menuconfig:

```bash
idf.py menuconfig
```

Configure the following in menuconfig (example):

- `BT Audio Basic Example (GMF)` → `Classic Audio Roles Configuration` → Select A2DP role (A2DP Sink / A2DP Source) or HFP HF, etc
- For A2DP Source, ensure microSD is configured and the card contains `media0.mp3`, `media1.mp3`, `media2.mp3`

> Press `s` to save and `Esc` to exit after configuration.

### Build and Flash Commands

- Build the example:

```
idf.py build
```

- Flash the firmware and run the serial monitor (replace PORT with your port name):

```
idf.py -p PORT flash monitor
```

Exit the monitor with `Ctrl-]`.

## How to Use the Example

### Functionality and Usage

- **Roles and commands**: The example supports classic Bluetooth roles (A2DP Sink, A2DP Source, HFP HF, AVRCP Controller/Target) selectable via menuconfig; after building and flashing, type `help` in the serial console to see the command list
- **A2DP Sink**: The device waits for a phone or other source to connect; after connection, use serial commands to control playback: `play`, `pause`, `stop`, `next`, `prev`, and `vol_set <0-100>` for volume
- **A2DP Source**: Use `start_discovery` and `connect <mac>` to discover and connect to a Bluetooth speaker or headphones; use `start_media` and `stop_media` to control streaming
- **HFP HF**: Supports answer/reject incoming call, dial, and call/telephony status reporting; the AEC element in the GMF pipeline is used for echo cancellation during calls
- **Companion devices**: A2DP Sink needs a phone or other A2DP Source; A2DP Source needs Bluetooth headphones or a speaker; HFP needs a phone that supports HFP AG

### Log Output

The following is a sample of key log lines during startup (board and GMF init, Bluetooth and pipeline ready):

```c
I (1460) BOARD_MANAGER: All peripherals initialized
I (1731) BOARD_MANAGER: Board manager initialized
I (1885) POOL_INIT: GMF pool initialization completed successfully
I (1901) ESP_GMF_TASK: Waiting to run... [tsk:bt2codec_task-0x3ffd376c, wk:0x0, run:0]
I (1902) ESP_GMF_TASK: Waiting to run... [tsk:codec2bt_task-0x3ffd4ff8, wk:0x0, run:0]
I (1930) BTDM_INIT: Bluetooth MAC: ec:62:60:4e:1f:8a
I (2385) AVRC: CT init success
I (2386) AVRC: TG init success
W (2387) BT_BTC: A2DP Enable with AVRC
```

Connection and media control output may vary. To reduce log noise, use `esp_log_level_set()` in code.

### References

- [ESP Board Manager](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/index.html)

## Troubleshooting

### microSD or Audio File Not Found (A2DP Source)

If the log shows file open or path errors, ensure the microSD card is mounted and the root directory contains `media0.mp3`, `media1.mp3`, and `media2.mp3` (or the filenames configured in code).

### Bluetooth Won't Connect or No Sound

- Confirm the Bluetooth role in menuconfig matches the other device (Sink with Source, Source with Sink)
- Confirm the devices are paired/connected and there are no connection or A2DP/AVRCP errors in the log
- For HFP, confirm the phone has granted phone and audio access

### Build or Board-Related Errors

- Confirm you have run `idf.py set-target esp32`, set `IDF_EXTRA_ACTIONS_PATH` to the `esp_board_manager` directory, and run `idf.py gen-bmgr-config -b <board>`
- For a custom board, see [Custom board](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md#custom-board)

## Technical Support

For technical support, use the links below:

- Technical support: [esp32.com](https://esp32.com/viewforum.php?f=20) forum
- Issue reports and feature requests: [GitHub issue](https://github.com/espressif/esp-gmf/issues)

We will reply as soon as possible.
