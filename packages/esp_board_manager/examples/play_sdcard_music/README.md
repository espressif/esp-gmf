# Play Music from microSD Card

- [中文版](./README_CN.md)
- Example difficulty: ⭐

## Example Brief

- This example plays a WAV file from the microSD card root (`/sdcard/test.wav`).
- Technically it demonstrates `esp_board_manager` initialization for Audio DAC and FAT/SD card (`fs_sdcard`), then `esp_codec_dev` for playback using format information from the WAV header (shared helper in `common/wav_header`).

### Typical Scenarios

- Local music playback, voice prompt playback from removable storage, or audio assets shipped on SD.

### Run Flow

Mount SD card → open WAV → `read_wav_header` → `esp_codec_dev_open` / `write` in a loop → unmount and deinit.

### File Structure

```
├── common
│   ├── CMakeLists.txt
│   ├── wav_header.c
│   └── include/wav_header.h
├── main
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   └── play_sdcard_music.c
├── CMakeLists.txt               Project: bmgr_play_sdcard_music (EXTRA_COMPONENT_DIRS ../common)
├── partitions.csv
├── sdkconfig.defaults           Includes FatFs / LFN settings
├── sdkconfig.defaults.esp32
├── sdkconfig.defaults.esp32s3
├── sdkconfig.defaults.esp32p4
├── README.md
└── README_CN.md
```

## Environment Setup

### Hardware Required

- A development board with an SD card, put 'test.wav' in the card root.

### Default IDF Branch

This example supports IDF release/v5.4 (>= v5.4.3) and release/v5.5 (>= v5.5.2).

### Software Requirements

- WAV file named `test.wav` on the SD card root.

## Build and Flash

### Build Preparation

Before building, set up ESP-IDF if needed:

```
./install.sh
. ./export.sh
```

Short steps:

```
cd $YOUR_GMF_PATH/packages/esp_board_manager/examples/play_sdcard_music
```

Register the extension (once per terminal):

```
# Linux / macOS:
export IDF_EXTRA_ACTIONS_PATH=$YOUR_GMF_PATH/packages/esp_board_manager

# Windows PowerShell:
$env:IDF_EXTRA_ACTIONS_PATH = "$YOUR_GMF_PATH/packages/esp_board_manager"

# Windows CMD:
set IDF_EXTRA_ACTIONS_PATH=$YOUR_GMF_PATH/packages/esp_board_manager
```

Generate board files:

```
idf.py gen-bmgr-config -b esp32_s3_korvo2_v3
idf.py gen-bmgr-config -l
```

Custom boards: [Custom board](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md#custom-board).

### Build and Flash Commands

```
idf.py build
```

```
idf.py -p PORT flash monitor
```

Exit monitor: `Ctrl-]`

## How to Use the Example

### Functionality and Usage

- Power on with SD inserted; firmware plays `/sdcard/test.wav` once through the DAC.

### Log Output

```text
I (732) BMGR_PLAY_SDCARD_MUSIC: Playing music from /sdcard/test.wav
I (739) PERIPH_I2S: I2S[0] TDM,  TX, ws: 45, bclk: 9, dout: 8, din: 10
I (745) PERIPH_I2S: I2S[0] initialize success: 0x3c096c58
I (750) DEV_AUDIO_CODEC: DAC is ENABLED
I (754) DEV_AUDIO_CODEC: Init audio_dac, i2s_name: i2s_audio_out, i2s_rx_handle:0x0, i2s_tx_handle:0x3c096c58, data_if: 0x3fcea804
I (765) PERIPH_I2C: i2c_new_master_bus initialize success
I (775) ES8311: Work in Slave mode
I (778) DEV_AUDIO_CODEC: Successfully initialized codec: audio_dac
I (779) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fceaa68, chip:es8311
I (787) BOARD_MANAGER: Device audio_dac initialized
I (791) DEV_FS_FAT: slot_config: cd=-1, wp=-1, clk=15, cmd=7, d0=4, d1=-1, d2=-1, d3=-1, d4=-1, d5=-1, d6=-1, d7=-1, width=1, flags=0x0
I (873) DEV_FS_FAT: Filesystem mounted, base path: /sdcard
Name: SD64G
Type: SDHC
Speed: 40.00 MHz (limit: 40.00 MHz)
Size: 60906MB
CSD: ver=2, sector_size=512, capacity=124735488 read_bl_len=9
SSR: bus_width=1
I (881) BOARD_MANAGER: Device fs_sdcard initialized
I (886) BOARD_DEVICE: Device handle audio_dac found, Handle: 0x3fce9a7c TO: 0x3fce9a7c
I (900) WAV_HEADER: WAV file: 48000 Hz, 2 channels, 16 bits
I (900) BMGR_PLAY_SDCARD_MUSIC: Play WAV file info: 48000 Hz, 2 channels, 16 bits
I (907) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:3
I (912) I2S_IF: TDM Mode 1 bits:16/16 channel:2 sample_rate:48000 mask:3
I (933) Adev_Codec: Open codec device OK
I (5185) BMGR_PLAY_SDCARD_MUSIC: Play WAV file completed
I (5185) BOARD_DEVICE: Deinit device audio_dac ref_count: 0 device_handle:0x3fce9a7c
I (5197) BOARD_DEVICE: Device audio_dac config found: 0x3c064ebc (size: 92)
I (5198) BOARD_PERIPH: Deinit peripheral i2s_audio_out ref_count: 0
E (5200) i2s_common: i2s_channel_disable(1217): the channel has not been enabled yet
W (5208) PERIPH_I2S: Caution: Releasing TX (0x0).
W (5212) PERIPH_I2S: Caution: RX (0x3c096e0c) forced to stop.
E (5217) i2s_common: i2s_channel_disable(1217): the channel has not been enabled yet
I (5225) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 0
I (5231) PERIPH_I2C: i2c_del_master_bus deinitialize
I (5235) BOARD_MANAGER: Device audio_dac deinitialized
I (5240) BOARD_DEVICE: Deinit device fs_sdcard ref_count: 0 device_handle:0x3fceaa98
I (5248) BOARD_MANAGER: Device fs_sdcard deinitialized
```

## Troubleshooting

### `gen-bmgr-config` option `-b` not found

Set `IDF_EXTRA_ACTIONS_PATH` to the `esp_board_manager` package directory.

```
# Linux / macOS:
echo $IDF_EXTRA_ACTIONS_PATH

# Windows PowerShell:
echo $env:IDF_EXTRA_ACTIONS_PATH

# Windows CMD:
echo %IDF_EXTRA_ACTIONS_PATH%
```

### Audio file not found

If logs show failure to open `/sdcard/test.wav`, ensure the file exists at the card root with exact name.

### Custom board

See [esp_board_manager README](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md).
