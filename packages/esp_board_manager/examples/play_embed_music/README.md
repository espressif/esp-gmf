# Play Embedded WAV Music

- [中文版](./README_CN.md)
- Example difficulty: ⭐

## Example Brief

- This example plays a WAV file bundled with the firmware from flash, without using a microSD card or network.
- Technically it demonstrates `esp_board_manager` device initialization for the audio DAC, retrieval of `dev_audio_codec_handles_t`, and playback through `esp_codec_dev` (open, set volume, write). Audio is linked via CMake `EMBED_TXTFILES`.

### Typical Scenarios

- Firmware-included prompt tones, boot jingles, or small demonstration clips where no external storage is required.

### Prerequisites

The WAV file is built into the application using ESP-IDF [embedded binary data](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/build-system.html#cmake-embed-data). Place your file under `main/audio_files/` (default: `test.wav`) and ensure `main/CMakeLists.txt` lists it in `EMBED_TXTFILES`. The C symbols are `_binary_<name>_start` / `_binary_<name>_end` (object name derived from the file path).

### Run Flow

`app_main` initializes the DAC via the board manager, opens the codec with parameters parsed from the WAV header in flash, pass the data stream through `esp_codec_dev_write` for output, then deinitializes the device.

### File Structure

```
├── main
│   ├── audio_files              Place test.wav (or your WAV) here
│   ├── CMakeLists.txt           EMBED_TXTFILES for WAV
│   ├── idf_component.yml
│   └── play_embed_music.c
├── CMakeLists.txt               Project: bmgr_play_embed_music
├── partitions.csv
├── sdkconfig.defaults
├── sdkconfig.defaults.esp32
├── sdkconfig.defaults.esp32s3
├── sdkconfig.defaults.esp32p4
├── README.md
└── README_CN.md
```

## Environment Setup

### Hardware Required

- Speaker as required by the board.

### Default IDF Branch

This example supports IDF release/v5.4 (>= v5.4.3) and release/v5.5 (>= v5.5.2).

## Build and Flash

### Build Preparation

Before building this example, ensure the ESP-IDF environment is set up. If it is already set up, skip to the project directory steps below. If not, run the following in the ESP-IDF root directory. For full steps, see the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/index.html).

```
./install.sh
. ./export.sh
```

Short steps:

- Go to this example's project directory:

```
cd $YOUR_GMF_PATH/packages/esp_board_manager/examples/play_embed_music
```

- Configure the `esp_board_manager` path to activate the environment (only needs to be executed once under the current terminal):

```
# Linux / macOS:
export IDF_EXTRA_ACTIONS_PATH=$YOUR_GMF_PATH/packages/esp_board_manager

# Windows PowerShell:
$env:IDF_EXTRA_ACTIONS_PATH = "$YOUR_GMF_PATH/packages/esp_board_manager"

# Windows CMD:
set IDF_EXTRA_ACTIONS_PATH=$YOUR_GMF_PATH/packages/esp_board_manager
```

- Generate board configuration and code for your board (example: `esp32_s3_korvo2_v3`):

```
idf.py gen-bmgr-config -b esp32_s3_korvo2_v3
```

Custom boards: see [Custom board](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md#custom-board).

- List supported boards:

```
idf.py gen-bmgr-config -l
```

### Project Configuration

- Board selection and chip target are applied via `gen-bmgr-config` and generated `components/gen_bmgr_codes/board_manager.defaults`

### Build and Flash Commands

- Build:

```
idf.py build
```

- Flash and monitor (replace `PORT`):

```
idf.py -p PORT flash monitor
```

- Exit monitor: `Ctrl-]`

## How to Use the Example

### Functionality and Usage

- After flashing, the device plays the embedded `test.wav` once through the DAC, then exits `app_main`.

### Log Output

Key lines showing initialization and playback completion:

```text
I (918) BMGR_EMBED_MUSIC: Playing embedded music
I (923) PERIPH_I2S: I2S[0] TDM,  TX, ws: 45, bclk: 9, dout: 8, din: 10
I (929) PERIPH_I2S: I2S[0] initialize success: 0x3c1629b0
I (934) DEV_AUDIO_CODEC: DAC is ENABLED
I (938) DEV_AUDIO_CODEC: Init audio_dac, i2s_name: i2s_audio_out, i2s_rx_handle:0x0, i2s_tx_handle:0x3c1629b0, data_if: 0x3fcea7f4
I (950) PERIPH_I2C: I2C master bus initialized successfully
I (960) ES8311: Work in Slave mode
I (963) DEV_AUDIO_CODEC: Successfully initialized codec: audio_dac
I (964) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fceaa48, chip:es8311
I (971) BOARD_MANAGER: Device audio_dac initialized
I (976) BOARD_DEVICE: Device handle audio_dac found, Handle: 0x3fce9a7c TO: 0x3fce9a7c
I (983) BMGR_EMBED_MUSIC: Embedded WAV file size: 818920 bytes
I (989) BMGR_EMBED_MUSIC: WAV file info: 48000 Hz, 2 channels, 16 bits
I (996) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:3
I (1002) I2S_IF: TDM Mode 1 bits:16/16 channel:2 sample_rate:48000 mask:3
I (1023) Adev_Codec: Open codec device OK
I (5273) BMGR_EMBED_MUSIC: Embedded WAV file playback completed
I (5273) BOARD_DEVICE: Deinit device audio_dac ref_count: 0 device_handle:0x3fce9a7c
I (5286) BOARD_DEVICE: Device audio_dac config found: 0x3c12f0e4 (size: 92)
I (5286) BOARD_PERIPH: Deinit peripheral i2s_audio_out ref_count: 0
E (5288) i2s_common: i2s_channel_disable(1217): the channel has not been enabled yet
W (5296) PERIPH_I2S: Caution: Releasing TX (0x0).
W (5300) PERIPH_I2S: Caution: RX (0x3c162b64) forced to stop.
E (5306) i2s_common: i2s_channel_disable(1217): the channel has not been enabled yet
I (5313) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 0
I (5319) PERIPH_I2C: I2C master bus deinitialized successfully
I (5324) BOARD_MANAGER: Device audio_dac deinitialized
```

## Troubleshooting

### `idf.py gen-bmgr-config` has no `-b` option

`IDF_EXTRA_ACTIONS_PATH` must point to `esp_board_manager`. Verify:

```
# Linux / macOS:
echo $IDF_EXTRA_ACTIONS_PATH

# Windows PowerShell:
echo $env:IDF_EXTRA_ACTIONS_PATH

# Windows CMD:
echo %IDF_EXTRA_ACTIONS_PATH%
```

### Embedded WAV missing or build error

Ensure `main/audio_files/test.wav` exists and `main/CMakeLists.txt` contains `EMBED_TXTFILES` for that path.

### Custom board

See [esp_board_manager README](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md).
