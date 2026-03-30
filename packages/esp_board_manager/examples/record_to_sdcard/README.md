# Record Audio to microSD Card

- [中文版](./README_CN.md)
- Example difficulty: ⭐⭐

## Example Brief

- This example captures audio from the microphone (Audio ADC), encodes it as PCM in a WAV container, and saves to `/sdcard/test.wav` for a fixed duration (see `DEFAULT_DURATION_SECONDS` in source).
- Technically it demonstrates `esp_board_manager` for Audio ADC and `fs_sdcard`, plus `esp_codec_dev` read path and `write_wav_header` from the shared `common` component.

### Typical Scenarios

- Voice memos, acoustic logging to SD, or verifying microphone and SD paths on a board.

### Run Flow

init ADC → mount SD → write WAV header → loop `esp_codec_dev_read` and `fopen` write → close file → deinit.

### File Structure

```
├── common
│   ├── CMakeLists.txt
│   ├── wav_header.c
│   └── include/wav_header.h
├── main
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   └── record_to_sdcard.c
├── CMakeLists.txt               Project: bmgr_record_to_sdcard
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

- Board with Audio ADC (`audio_adc`) and SD card support (`fs_sdcard`).

### Default IDF Branch

This example supports IDF release/v5.4 (>= v5.4.3) and release/v5.5 (>= v5.5.2).

### Software Requirements

- Sufficient free space on the SD card for the WAV clip.

## Build and Flash

### Build Preparation

```
./install.sh
. ./export.sh
```

```
cd $YOUR_GMF_PATH/packages/esp_board_manager/examples/record_to_sdcard
```

```
# Linux / macOS:
export IDF_EXTRA_ACTIONS_PATH=$YOUR_GMF_PATH/packages/esp_board_manager

# Windows PowerShell:
$env:IDF_EXTRA_ACTIONS_PATH = "$YOUR_GMF_PATH/packages/esp_board_manager"

# Windows CMD:
set IDF_EXTRA_ACTIONS_PATH=$YOUR_GMF_PATH/packages/esp_board_manager
```

```
idf.py gen-bmgr-config -b esp32_s3_korvo2_v3
idf.py gen-bmgr-config -l
```

Custom boards: [Custom board](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md#custom-board).

### Project Configuration

- Board and defaults from `gen-bmgr-config`.
- FatFs options preset for SD in `sdkconfig.defaults`.
- Recording duration, sample rate, path, and gain: edit macros in `record_to_sdcard.c` (`DEFAULT_DURATION_SECONDS`, `DEFAULT_SAMPLE_RATE`, `DEFAULT_REC_URL`, `DEFAULT_REC_GAIN`, etc.).

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

- Insert SD card, flash, and run; after boot the device records for the configured duration and writes `/sdcard/test.wav`.

### Log Output

```text
I (732) BMGR_RECORD_TO_SDCARD: Record to /sdcard/record.wav
I (738) DEV_AUDIO_CODEC: ADC is ENABLED
I (760) PERIPH_I2S: I2S[0] TDM, RX, ws: 45, bclk: 9, dout: 8, din: 10
I (766) PERIPH_I2S: I2S[0] initialize success: 0x3c096ebc
I (788) DEV_AUDIO_CODEC: Init audio_adc, i2s_name: i2s_audio_in, i2s_rx_handle:0x3c096ebc, i2s_tx_handle:0x3c096d08, data_if: 0x3fcee2dc
I (800) PERIPH_I2C: i2c_new_master_bus initialize success
I (808) ES7210: Work in Slave mode
I (814) ES7210: Enable ES7210_INPUT_MIC1
I (817) ES7210: Enable ES7210_INPUT_MIC2
I (820) ES7210: Enable ES7210_INPUT_MIC3
I (823) ES7210: Enable TDM mode
I (826) DEV_AUDIO_CODEC: Successfully initialized codec: audio_adc
I (828) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fcee514, chip:es7210
I (835) BOARD_MANAGER: Device audio_adc initialized
I (840) DEV_FS_FAT: slot_config: cd=-1, wp=-1, clk=15, cmd=7, d0=4, d1=-1, d2=-1, d3=-1, d4=-1, d5=-1, d6=-1, d7=-1, width=1, flags=0x0
I (922) DEV_FS_FAT: Filesystem mounted, base path: /sdcard
Name: SD64G
Type: SDHC
Speed: 40.00 MHz (limit: 40.00 MHz)
Size: 60906MB
CSD: ver=2, sector_size=512, capacity=124735488 read_bl_len=9
SSR: bus_width=1
I (930) BOARD_MANAGER: Device fs_sdcard initialized
I (935) BOARD_DEVICE: Device handle audio_adc found, Handle: 0x3fce9a7c TO: 0x3fce9a7c
I (949) I2S_IF: channel mode 2 bits:32/32 channel:2 mask:1
I (949) I2S_IF: TDM Mode 0 bits:32/32 channel:2 sample_rate:48000 mask:1
I (954) I2S_IF: channel mode 2 bits:32/32 channel:2 mask:1
I (959) I2S_IF: TDM Mode 1 bits:32/32 channel:2 sample_rate:48000 mask:1
I (966) ES7210: Bits 16
I (975) ES7210: Enable ES7210_INPUT_MIC1
I (977) ES7210: Enable ES7210_INPUT_MIC2
I (980) ES7210: Enable ES7210_INPUT_MIC3
I (983) ES7210: Enable TDM mode
I (988) ES7210: Unmuted
I (988) Adev_Codec: Open codec device OK
I (991) BMGR_RECORD_TO_SDCARD: Record WAV file info: 48000 Hz, 1 channels, 32 bits
I (995) BMGR_RECORD_TO_SDCARD: Starting I2S recording...
I (11012) BMGR_RECORD_TO_SDCARD: I2S recording completed. Total bytes recorded: 1925120
I (11018) BOARD_DEVICE: Deinit device audio_adc ref_count: 0 device_handle:0x3fce9a7c
I (11021) BOARD_DEVICE: Device audio_adc config found: 0x3c064f04 (size: 92)
I (11024) BOARD_PERIPH: Deinit peripheral i2s_audio_in ref_count: 0
E (11030) i2s_common: i2s_channel_disable(1217): the channel has not been enabled yet
W (11038) PERIPH_I2S: Caution: Releasing RX (0x0).
I (11042) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 0
I (11048) PERIPH_I2C: i2c_del_master_bus deinitialize
I (11053) BOARD_MANAGER: Device audio_adc deinitialized
I (11058) BOARD_DEVICE: Deinit device fs_sdcard ref_count: 0 device_handle:0x3fcee544
I (11065) BOARD_MANAGER: Device fs_sdcard deinitialized
```

(Exact sample rate or channel count may match your codec/board configuration.)

## Troubleshooting

### `gen-bmgr-config` has no `-b`

Configure `IDF_EXTRA_ACTIONS_PATH` to `esp_board_manager`.

```
# Linux / macOS:
echo $IDF_EXTRA_ACTIONS_PATH

# Windows PowerShell:
echo $env:IDF_EXTRA_ACTIONS_PATH

# Windows CMD:
echo %IDF_EXTRA_ACTIONS_PATH%
```

### SD or recording failures

Check card is FAT, mounted at `/sdcard`, and not write-protected. Verify the board supported `audio_adc` and `fs_sdcard`.

### Custom board

See [esp_board_manager README](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md).
