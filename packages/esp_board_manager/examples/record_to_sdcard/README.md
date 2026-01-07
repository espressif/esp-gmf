# Record Audio to microSD Card

- [中文版](./README_CN.md)

## Example Brief

This example demonstrates how to use `esp_board_manager` to initialize the codec and sdcard, obtain device handles, and use the `esp_codec_dev` API to record audio data and save it as a WAV file to a microSD card.

## Example Set Up

### IDF Default Branch

This example supports IDF release/v5.5 (>=5.5.2) and IDF release/v5.4 (>=5.4.3) branches.

### Preparation

This example requires a microSD card. The recorded audio file will be named `record.wav` and stored on the card.

### Build and Flash

Before compiling this example, ensure that the ESP-IDF environment is properly set up. If not, run the following script in the root directory of ESP-IDF to set up the build environment. For detailed steps on configuring and using ESP-IDF, please refer to the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/index.html):

```shell
./install.sh
. ./export.sh
```

Here are the summarized compilation steps:

- Navigate to the test project directory for recording audio to a microSD card:

```shell
cd $YOUR_GMF_PATH/packages/esp_board_manager/example/dev_audio_codec/record_to_sdcard
```

- Configure the `esp_board_manager` path to activate the environment (only needs to be executed once under the current terminal):

```shell
export IDF_EXTRA_ACTIONS_PATH=$YOUR_GMF_PATH/packages/esp_board_manager
```

- Select the development board to use, for example, esp32_s3_korvo2_v3:

```shell
idf.py gen-bmgr-config -b esp32_s3_korvo2_v3
```

- You can also run the following command to see a list of supported development boards:

```shell
idf.py gen-bmgr-config -l
```

- Compile the example code:

```shell
idf.py build
```

- Flash the program and run the monitor tool to view serial output (replace PORT with your port name):

```shell
idf.py -p PORT flash monitor
```

- To exit the debugging interface, use `Ctrl-]`.

## How to Use the Example

### Functionality and Usage

- After the example starts, it will automatically begin recording 10 seconds of audio and save it to the `record.wav` file on the microSD card. The output will be as follows:

```c
I (732) BOARD_MANAGER_RECORD_TO_SDCARD: Record to /sdcard/record.wav
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
I (991) BOARD_MANAGER_RECORD_TO_SDCARD: Record WAV file info: 48000 Hz, 1 channels, 32 bits
I (995) BOARD_MANAGER_RECORD_TO_SDCARD: Starting I2S recording...
I (11012) BOARD_MANAGER_RECORD_TO_SDCARD: I2S recording completed. Total bytes recorded: 1925120
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

## Troubleshooting

1. If you encounter the following error message, please run `echo $IDF_EXTRA_ACTIONS_PATH` to check if the `esp_board_manager` path is configured correctly:

```c
Usage: idf.py gen-bmgr-config [OPTIONS]
Try 'idf.py gen-bmgr-config --help' for help.

Error: No such option: -b
```

2. If you need to use a custom development board, please refer to the instructions on **custom boards** in [README.md](../../../README.md).
