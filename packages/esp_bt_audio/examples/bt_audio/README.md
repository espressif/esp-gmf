# Basic Bluetooth Audio Example

- [中文版](./README_CN.md)

- Regular Example: ⭐⭐

## Example Brief

This example initializes Bluetooth audio through the `esp_bt_audio` module and uses `esp_gmf_io_bt` to link the Bluetooth audio stream with a GMF pipeline, enabling audio playback or uplink streaming. It also provides serial commands to demonstrate control of Bluetooth audio playback and voice calls.

### Typical Scenarios

- **Bluetooth speaker (A2DP Sink)**: Phone connects to the device to play music; supports play/pause/next/previous, volume, and metadata
- **Bluetooth source (A2DP Source)**: Device discovers and connects to Bluetooth headphones or speakers and streams local or microSD audio to the remote device
- **Bluetooth voice call (HFP HF)**: Answer/reject incoming calls, dial; call state and telephony status reporting; AEC in the GMF pipeline to improve call clarity; fetch phonebook and call history

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
- **PBAP Client**: Use the `pb_fetch` command to retrieve the phonebook and call history
- **Companion devices**: A2DP Sink needs a phone or other A2DP Source; A2DP Source needs Bluetooth headphones or a speaker; HFP needs a phone that supports HFP AG

### Log Output

The following is a sample of key log lines during startup (board and GMF init, Bluetooth and pipeline ready):

```c
I (1398) main_task: Calling app_main()
I (1423) PERIPH_I2C: I2C master bus initialized successfully
W (1425) PERIPH_I2S: I2S[0] STD already enabled, tx:0x3f800dd8, rx:0x3f800f94
I (1425) PERIPH_I2S: I2S[0] STD,  TX, ws: 25, bclk: 5, dout: 26, din: 35
I (1431) PERIPH_I2S: I2S[0] initialize success: 0x3f800dd8
I (1437) PERIPH_I2S: I2S[1] STD, RX, ws: 33, bclk: 32, dout: -1, din: 36
I (1443) PERIPH_I2S: I2S[1] initialize success: 0x3f80136c
I (1448) PERIPH_GPIO: Initialize success, pin: 13, set the default level: 1
I (1455) PERIPH_GPIO: Initialize success, pin: 19, default_level: 0
I (1461) PERIPH_GPIO: Initialize success, pin: 21, set the default level: 0
I (1467) PERIPH_GPIO: Initialize success, pin: 22, set the default level: 0
I (1474) PERIPH_GPIO: Initialize success, pin: 27, set the default level: 0
I (1481) PERIPH_GPIO: Initialize success, pin: 34, default_level: 0
I (1490) PERIPH_ADC: Create adc oneshot unit success
I (1491) BOARD_MANAGER: All peripherals initialized
I (1496) DEV_POWER_CTRL_SUB_GPIO: Initializing GPIO power control: gpio_sd_power
I (1503) BOARD_PERIPH: Reuse periph: gpio_sd_power, ref_count=2
I (1509) DEV_POWER_CTRL_SUB_GPIO: GPIO power control initialized successfully
I (1516) DEV_POWER_CTRL: Power control device initialized successfully, sub_type: gpio
I (1523) BOARD_PERIPH: Reuse periph: i2s_audio_out, ref_count=2
I (1529) DEV_AUDIO_CODEC: DAC is ENABLED
I (1533) DEV_AUDIO_CODEC: Init audio_dac, i2s_name: i2s_audio_out, i2s_rx_handle:0x0, i2s_tx_handle:0x3f800dd8, data_if: 0x3ffd66c4
I (1544) BOARD_PERIPH: Reuse periph: i2c_master, ref_count=2
I (1558) ES8311: Work in Slave mode
I (1561) DEV_AUDIO_CODEC: Successfully initialized codec: audio_dac
I (1562) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3ffd6844, chip:es8311
I (1570) DEV_AUDIO_CODEC: ADC is ENABLED
I (1573) BOARD_PERIPH: Reuse periph: i2s_audio_in, ref_count=2
I (1579) DEV_AUDIO_CODEC: Init audio_adc, i2s_name: i2s_audio_in, i2s_rx_handle:0x3f80136c, i2s_tx_handle:0x3f8011b0, data_if: 0x3ffd688c
I (1591) BOARD_PERIPH: Reuse periph: i2c_master, ref_count=3
I (1613) DEV_AUDIO_CODEC: Successfully initialized codec: audio_adc
I (1613) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3ffd69c0, chip:es7243e
I (1615) BOARD_DEVICE: Device sdcard_power_ctrl config found: 0x3f433fb8 (size: 20)
I (1623) DEV_POWER_CTRL_SUB_GPIO: GPIO power control: ON, level: 0 for device: fs_sdcard
I (1630) DEV_FS_FAT_SUB_SDMMC: slot_config: cd=-1, wp=-1, clk=14, cmd=15, d0=2, d1=-1, d2=-1, d3=-1, d4=-1, d5=-1, d6=-1, d7=-1, width=1, flags=0x1
Name: BB1QT
Type: SDHC
Speed: 40.00 MHz (limit: 40.00 MHz)
Size: 30528MB
CSD: ver=2, sector_size=512, capacity=62521344 read_bl_len=9
SSR: bus_width=1
I (1851) DEV_FS_FAT: Filesystem mounted, base path: /sdcard
I (1857) BOARD_PERIPH: Reuse periph: adc_button, ref_count=2
I (1862) BOARD_PERIPH: Peripheral adc_button config found: 0x3f43410c (size: 52)
I (1869) DEV_BUTTON_SUB_ADC: Initializing 6 ADC buttons on unit 0, channel 3
I (1876) adc_button: ADC1 has been initialized
I (1880) adc_button: calibration scheme version is Line Fitting
I (1886) adc_button: Calibration Success
I (1889) button: IoT Button Version: 4.1.6
I (1893) DEV_BUTTON: Successfully initialized button: adc_button_group, sub_type: adc_multi
I (1901) BOARD_MANAGER: Board manager initialized
I (1906) BOARD_DEVICE: Device handle audio_dac found, Handle: 0x3ffd669c TO: 0x3ffd669c
I (1914) I2S_IF: channel mode 0 bits:16/16 channel:2 mask:3
I (1919) I2S_IF: STD Mode 1 bits:16/16 channel:2 sample_rate:48000 mask:3
I (1942) Adev_Codec: Open codec device OK
I (1942) BOARD_DEVICE: Device handle audio_adc found, Handle: 0x3ffd6874 TO: 0x3ffd6874
I (1943) I2S_IF: channel mode 0 bits:16/16 channel:2 mask:3
I (1948) I2S_IF: STD Mode 0 bits:16/16 channel:2 sample_rate:48000 mask:3
I (1954) I2S_IF: channel mode 0 bits:16/16 channel:2 mask:3
I (1959) I2S_IF: STD Mode 1 bits:16/16 channel:2 sample_rate:48000 mask:3
I (1967) Adev_Codec: Open codec device OK
I (1970) POOL_INIT: Registering GMF pool
I (1975) POOL_INIT: Registered: aud_aec
I (1977) BOARD_DEVICE: Device handle audio_dac found, Handle: 0x3ffd669c TO: 0x3ffd669c
I (1985) BOARD_DEVICE: Device handle audio_adc found, Handle: 0x3ffd6874 TO: 0x3ffd6874
I (1993) POOL_INIT: GMF pool initialization completed successfully
W (1999) ESP_GMF_THREAD: Make sure selected the `CONFIG_SPIRAM_BOOT_INIT` and `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY` by `make menuconfig`
I (2011) ESP_GMF_TASK: Waiting to run... [tsk:bt2codec_task-0x3ffd79bc, wk:0x0, run:0]
W (2019) ESP_GMF_THREAD: Make sure selected the `CONFIG_SPIRAM_BOOT_INIT` and `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY` by `make menuconfig`
I (2031) ESP_GMF_TASK: Waiting to run... [tsk:codec2bt_task-0x3ffd92c4, wk:0x0, run:0]
W (2032) ESP_GMF_THREAD: Make sure selected the `CONFIG_SPIRAM_BOOT_INIT` and `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY` by `make menuconfig`
I (2051) ESP_GMF_TASK: Waiting to run... [tsk:local2bt_task-0x3ffdab74, wk:0x0, run:0]
I (2051) BTDM_INIT: BT controller compile version [045a658]
I (2064) BTDM_INIT: Using main XTAL as clock source
I (2069) BTDM_INIT: Bluetooth MAC: a8:42:e3:66:1f:da
I (2075) phy_init: phy_version 4863,a3a4459,Oct 28 2025,14:30:06
I (2564) BT_AUD_HOST: Setting bluedroid discovery operations
I (2566) BT_AUD_AVRC_CT: CT init success
I (2568) BT_AUD_AVRC_TG: TG init success
W (2571) BT_BTC: A2DP Enable with AVRC
I (2576) BT_AUD_HOST: GAP event: 10
I (2578) BT_AUD_HOST: GAP event: 10
I (2579) BT_AUD_HOST: GAP event: 10
I (2581) BT_AUD_A2D_SINK: bt_a2d_event_cb unhandled event: 5
I (2586) BT_AUD_A2D_SINK: A2DP sink: initialized
I (2594) BT_AUD_HOST: GAP event: 10
I (2596) BT_AUD_HFP_HF: HF client init success
I (2599) BT_AUD_HOST: Setting bluedroid scan mode: connectable true, discoverable true
I (2606) BT_AUD_AVRC_CT: CT: Register notifications mask 0xff

Type 'help' to get the list of commands.
Use UP/DOWN arrows to navigate through command history.
Press TAB when typing command name to auto-complete.
I (2677) main_task: Returned from app_main()
BTAudio >
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

### Cannot Retrieve Phonebook or Call History

- For Classic Bluetooth pairing, some phones require allowing the device to access the phonebook; check in Bluetooth settings whether this is authorized.

## Technical Support

For technical support, use the links below:

- Technical support: [esp32.com](https://esp32.com/viewforum.php?f=20) forum
- Issue reports and feature requests: [GitHub issue](https://github.com/espressif/esp-gmf/issues)

We will reply as soon as possible.
