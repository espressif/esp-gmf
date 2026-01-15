# Play embedded WAV music

- [中文版](./README_CN.md)

## Example Brief

This example demonstrates how to use `esp_board_manager` to initialize the codec, obtain device handles, and play music in WAV format from flash using the `esp_codec_dev` API.

## Example Set Up

### IDF Default Branch

This example supports IDF release/v5.5 (>=5.5.2) and IDF release/v5.4 (>=5.4.3) branches.

### Preparation

A wav audio file required for this example have been stored in the `main/audio_files` folder.

### Build and Flash

Before compiling this example, ensure that the ESP-IDF environment is properly set up. If not, run the following script in the root directory of ESP-IDF to set up the build environment. For detailed steps on configuring and using ESP-IDF, please refer to the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/index.html):

```shell
./install.sh
. ./export.sh
```

Here are the summarized compilation steps:

- Navigate to the test project directory for playing music from a microSD card:

```shell
cd $YOUR_GMF_PATH/packages/esp_board_manager/examples/play_embed_music
```

- Configure the `esp_board_manager` path to activate the environment (only needs to be executed once under the current terminal):

```shell
# Ubuntu and Mac:
export IDF_EXTRA_ACTIONS_PATH=$YOUR_GMF_PATH/packages/esp_board_manager

# Windows PowerShell:
$env:IDF_EXTRA_ACTIONS_PATH = "$YOUR_GMF_PATH/packages/esp_board_manager"

# Windows Command Prompt (CMD):
set IDF_EXTRA_ACTIONS_PATH=$YOUR_GMF_PATH/packages/esp_board_manager
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

- After the example starts, it will automatically play the `test.wav` file from flash. The output will be as follows:

```c
I (918) BMGR_EMBED_SDCARD_MUSIC: Playing embedded music
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
I (983) BMGR_EMBED_SDCARD_MUSIC: Embedded WAV file size: 818920 bytes
I (989) BMGR_EMBED_SDCARD_MUSIC: WAV file info: 48000 Hz, 2 channels, 16 bits
I (996) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:3
I (1002) I2S_IF: TDM Mode 1 bits:16/16 channel:2 sample_rate:48000 mask:3
I (1023) Adev_Codec: Open codec device OK
I (5273) BMGR_EMBED_SDCARD_MUSIC: Embedded WAV file playback completed
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
I (5329) main_task: Returned from app_main()
```

## Troubleshooting

1. If you encounter the following error message, please run `echo $IDF_EXTRA_ACTIONS_PATH` to check if the `esp_board_manager` path is configured correctly:

```c
Usage: idf.py gen-bmgr-config [OPTIONS]
Try 'idf.py gen-bmgr-config --help' for help.

Error: No such option: -b
```

2. If you need to use a custom development board, please refer to the instructions on **custom boards** in [README.md](../../../README.md).
