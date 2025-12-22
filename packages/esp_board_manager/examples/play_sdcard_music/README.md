# Play Music from microSD Card

- [中文版](./README_CN.md)

## Example Brief

This example demonstrates how to use `esp_board_manager` to initialize the codec and sdcard, obtain device handles, and play music from a microSD card using the `esp_codec_dev` API.

## Example Set Up

### IDF Default Branch

This example supports IDF release/v5.5 (>=5.5.2) and IDF release/v5.4 (>=5.4.3) branches.

### Preparation

This example requires a microSD card with a WAV file named `test.wav` in the root directory.

### Build and Flash

Before compiling this example, ensure that the ESP-IDF environment is properly set up. If not, run the following script in the root directory of ESP-IDF to set up the build environment. For detailed steps on configuring and using ESP-IDF, please refer to the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/index.html):

```shell
./install.sh
. ./export.sh
```

Here are the summarized compilation steps:

- Navigate to the test project directory for playing music from a microSD card:

```shell
cd $YOUR_GMF_PATH/packages/esp_board_manager/example/dev_audio_codec/play_sdcard_music
```

- Configure the `esp_board_manager` path to activate the environment (only needs to be executed once under the current terminal):

```shell
export IDF_EXTRA_ACTIONS_PATH=$YOUR_GMF_PATH/packages/esp_board_manager
```

- Select the development board to use:

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

- After the example starts, it will automatically play the `test.wav` file from the microSD card. The output will be as follows:

```c
I (732) BOARD_MANAGER_PLAY_SDCARD_MUSIC: Playing music from /sdcard/test.wav
I (739) PERIPH_I2S: I2S[0] TDM,  TX, ws: 45, bclk: 9, dout: 8, din: 10
I (745) PERIPH_I2S: I2S[0] initialize success: 0x3c096c58
I (750) DEV_AUDIO_CODEC: DAC is ENABLED
I (754) DEV_AUDIO_CODEC: Init audio_dac, i2s_name: i2s_audio_out, i2s_rx_handle:0x0, i2s_tx_handle:0x3c096c58, data_if: 0x3fcea804
I (765) PERIPH_I2C: i2c_new_master_bus initialize success
I (775) ES8311: Work in Slave mode
I (778) DEV_AUDIO_CODEC: Successfully initialized codec: audio_dac
I (779) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fceaa68, chip:es8311
I (787) BOARD_MANAGER: Device audio_dac initialized
I (791) DEV_FATFS_SDCARD: slot_config: cd=-1, wp=-1, clk=15, cmd=7, d0=4, d1=-1, d2=-1, d3=-1, d4=-1, d5=-1, d6=-1, d7=-1, width=1, flags=0x0
I (873) DEV_FATFS_SDCARD: Filesystem mounted, base path: /sdcard
Name: SD64G
Type: SDHC
Speed: 40.00 MHz (limit: 40.00 MHz)
Size: 60906MB
CSD: ver=2, sector_size=512, capacity=124735488 read_bl_len=9
SSR: bus_width=1
I (881) BOARD_MANAGER: Device fs_sdcard initialized
I (886) BOARD_DEVICE: Device handle audio_dac found, Handle: 0x3fce9a7c TO: 0x3fce9a7c
I (900) WAV_HEADER: WAV file: 48000 Hz, 2 channels, 16 bits
I (900) BOARD_MANAGER_PLAY_SDCARD_MUSIC: Play WAV file info: 48000 Hz, 2 channels, 16 bits
I (907) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:3
I (912) I2S_IF: TDM Mode 1 bits:16/16 channel:2 sample_rate:48000 mask:3
I (933) Adev_Codec: Open codec device OK
I (5185) BOARD_MANAGER_PLAY_SDCARD_MUSIC: Play WAV file completed
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
I (5248) BOARD_MANAGER: Device fs_sdcard deinitialized```

## Troubleshooting

1. If you encounter the following error message, please run `echo $IDF_EXTRA_ACTIONS_PATH` to check if the `esp_board_manager` path is configured correctly:

```c
Usage: idf.py gen-bmgr-config [OPTIONS]
Try 'idf.py gen-bmgr-config --help' for help.

Error: No such option: -b
```

2. If you need to use a custom development board, please refer to the instructions on **custom boards** in [README.md](../../../README.md).
