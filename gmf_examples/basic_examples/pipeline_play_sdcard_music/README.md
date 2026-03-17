# Play Music from microSD Card

- [中文版](./README_CN.md)
- Basic Example: ⭐

## Example Brief

This example demonstrates reading music files from a microSD card via FatFs IO, decoding them with a decoder element, applying audio effects, and playing through CODEC_DEV_TX IO.

- Single pipeline: `io_file` → decoder → audio effects → `io_codec_dev`.
- Supports MP3, WAV, FLAC, AAC, M4A, TS, AMRNB, AMRWB; default is MP3.

### Typical Scenarios

- Local music playback, voice prompts, or any scenario that reads audio from an SD card.

## Environment Setup

### Hardware Required

- **Board**: ESP32-S3-Korvo V3 by default; other ESP audio boards are also supported.
- **Resource requirements**: microSD card, Audio DAC, speaker.

### Default IDF Branch

This example supports IDF release/v5.4 (>= v5.4.3) and release/v5.5 (>= v5.5.2).

### Software Requirements

- Prepare a microSD card: rename your audio file to `test` with the correct extension (e.g. `test.mp3`) and put it on the card. You can change the path via `DEFAULT_PLAY_URL`.

## Build and Flash

### Build Preparation

Before building this example, ensure the ESP-IDF environment is set up. If it is already set up, skip this paragraph and go to the project directory and run the pre-build script(s) as follows. If not, run the following in the ESP-IDF root directory to complete the environment setup. For full steps, see the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/index.html).

```
./install.sh
. ./export.sh
```

Short steps:

- Go to this example's project directory:

```
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_play_sdcard_music
```

- Run the pre-build script: follow the prompts to select the target chip, set up the IDF Action extension, and use `esp_board_manager` to select a supported board. For a custom board, see [Custom board](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md#custom-board).

On Linux / macOS:
```bash/zsh
source prebuild.sh
```

On Windows:
```powershell
.\prebuild.ps1
```

### Project Configuration

- To adjust audio effect parameters (sample rate, channels, bit depth), configure GMF Audio options in menuconfig.

```bash
idf.py menuconfig
```

In menuconfig:

- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `Channel Convert Destination Channel`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `Bit Convert Destination Bits`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `Rate Convert Destination Rate`

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

- To exit the monitor, use `Ctrl-]`

## How to Use the Example

### Functionality and Usage

- After power-on the example mounts the microSD card and initializes audio, then plays the default file (e.g. `/sdcard/test.mp3` or `/sdcard/test.wav`). When playback finishes it stops and releases resources.

### Log Output

- Normal run: mount SD card, register elements, create pipeline, set URL, bind task, listen events, start pipeline, open file and decode/play, wait for finish and destroy resources. Key steps are marked with `[ 1 ]`–`[ 6 ]`.

```c
I (888) main_task: Calling app_main()
I (888) PLAY_SDCARD_MUSIC: [ 1 ] Mount sdcard
I (892) DEV_FS_FAT_SUB_SDMMC: slot_config: cd=-1, wp=-1, clk=15, cmd=7, d0=4, d1=-1, d2=-1, d3=-1, d4=-1, d5=-1, d6=-1, d7=-1, width=1, flags=0x1
Name: SA32G
Type: SDHC
Speed: 40.00 MHz (limit: 40.00 MHz)
Size: 29544MB
CSD: ver=2, sector_size=512, capacity=60506112 read_bl_len=9
SSR: bus_width=1
I (951) DEV_FS_FAT: Filesystem mounted, base path: /sdcard
I (956) BOARD_MANAGER: Device fs_sdcard initialized
I (961) PERIPH_I2S: I2S[0] TDM,  TX, ws: 45, bclk: 9, dout: 8, din: 10
I (967) PERIPH_I2S: I2S[0] initialize success: 0x3c14fae4
I (972) DEV_AUDIO_CODEC: DAC is ENABLED
I (976) DEV_AUDIO_CODEC: Init audio_dac, i2s_name: i2s_audio_out, i2s_rx_handle:0x0, i2s_tx_handle:0x3c14fae4, data_if: 0x3fcead9c
I (988) PERIPH_I2C: I2C master bus initialized successfully
I (998) ES8311: Work in Slave mode
I (1000) DEV_AUDIO_CODEC: Successfully initialized codec: audio_dac
I (1002) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fceaff0, chip:es8311
I (1009) BOARD_MANAGER: Device audio_dac initialized
I (1014) BOARD_DEVICE: Device handle audio_dac found, Handle: 0x3fcea024 TO: 0x3fcea024
I (1022) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:3
I (1027) I2S_IF: TDM Mode 1 bits:16/16 channel:2 sample_rate:44100 mask:3
I (1048) Adev_Codec: Open codec device OK
I (1048) PLAY_SDCARD_MUSIC: [ 2 ] Register all the elements and set audio information to play codec device
I (1051) PLAY_SDCARD_MUSIC: [ 3 ] Create audio pipeline
I (1055) PLAY_SDCARD_MUSIC: [ 3.1 ] Set audio url to play
I (1060) PLAY_SDCARD_MUSIC: [ 3.2 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (1071) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcec084-0x3fcec084, wk:0x0, run:0]
I (1078) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcec084-0x3fcec084, wk:0x3c151b34, run:0]
I (1087) PLAY_SDCARD_MUSIC: [ 3.3 ] Create envent group and listening event from pipeline
I (1095) PLAY_SDCARD_MUSIC: [ 4 ] Start audio_pipeline
I (1100) ESP_GMF_FILE: Open, dir:1, uri:/sdcard/test.mp3
I (1105) PLAY_SDCARD_MUSIC: [ 5 ] Wait stop event to the pipeline and stop all the pipeline
I (1107) ESP_GMF_FILE: File size: 231023 byte, file position: 0
I (1118) PLAY_SDCARD_MUSIC: CB: RECV Pipeline EVT: el: NULL-0x3c15131c, type: 2000, sub: ESP_GMF_EVENT_STATE_OPENING, payload: 0x0, size: 0, 0x3fced37c
I (1132) ESP_GMF_TASK: One times job is complete, del[wk:0x3c151b34, ctx:0x3c151360, label:aud_dec_open]
I (1141) ESP_GMF_PORT: ACQ IN, new self payload:0x3c151b34, port:0x3c151990, el:0x3c151360-aud_dec
I (1150) ESP_ES_PARSER: The verion of es_parser is v1.0.0
W (1155) ESP_GMF_ASMP_DEC: Not enough memory for out, need:4608, old: 1024, new: 4608
I (1165) ESP_GMF_TASK: One times job is complete, del[wk:0x3c151df8, ctx:0x3c151480, label:aud_ch_cvt_open]
I (1172) ESP_GMF_TASK: One times job is complete, del[wk:0x3c151e48, ctx:0x3c1515d8, label:aud_bit_cvt_open]
I (1181) PLAY_SDCARD_MUSIC: CB: RECV Pipeline EVT: el: aud_rate_cvt-0x3c15172c, type: 3000, sub: ESP_GMF_EVENT_STATE_INITIALIZED, payload: 0x3fcecfd0, size: 16, 0x3fced37c
I (1196) PLAY_SDCARD_MUSIC: CB: RECV Pipeline EVT: el: aud_rate_cvt-0x3c15172c, type: 2000, sub: ESP_GMF_EVENT_STATE_RUNNING, payload: 0x0, size: 0, 0x3fced37c
I (1210) ESP_GMF_TASK: One times job is complete, del[wk:0x3c151df8, ctx:0x3c15172c, label:aud_rate_cvt_open]
I (21180) ESP_GMF_FILE: No more data, ret: 0
I (21198) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcec084-0x3fcec084, wk:0x3c151b70, job:0x3c151360-aud_dec_proc]
I (21198) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcec084-0x3fcec084, wk:0x3c151e20, job:0x3c151480-aud_ch_cvt_proc]
I (21207) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcec084-0x3fcec084, wk:0x3c151e88, job:0x3c1515d8-aud_bit_cvt_proc]
I (21217) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcec084-0x3fcec084, wk:0x3c151ee0, job:0x3c15172c-aud_rate_cvt_proc]
I (21228) ESP_GMF_TASK: Finish, strategy action: 0, [tsk:0x3fcec084-TSK_0x3fcec084]
I (21235) ESP_GMF_FILE: CLose, 0x3c151888, pos = 231023/231023
I (21241) ESP_GMF_CODEC_DEV: CLose, 0x3c1519d0, pos = 3528000/0
I (21246) ESP_GMF_TASK: One times job is complete, del[wk:0x3c151b58, ctx:0x3c151360, label:aud_dec_close]
I (21256) ESP_GMF_TASK: One times job is complete, del[wk:0x3c151b80, ctx:0x3c151480, label:aud_ch_cvt_close]
I (21265) ESP_GMF_TASK: One times job is complete, del[wk:0x3c151df8, ctx:0x3c1515d8, label:aud_bit_cvt_close]
I (21275) ESP_GMF_TASK: One times job is complete, del[wk:0x3c151e38, ctx:0x3c15172c, label:aud_rate_cvt_close]
I (21285) PLAY_SDCARD_MUSIC: CB: RECV Pipeline EVT: el: NULL-0x3c15131c, type: 2000, sub: ESP_GMF_EVENT_STATE_FINISHED, payload: 0x0, size: 0, 0x3fced37c
I (21298) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcec084-0x3fcec084, wk:0x0, run:0]
I (21306) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcec084-0x3fcec084, wk:0x0, run:0]
I (21314) PLAY_SDCARD_MUSIC: [ 6 ] Destroy all the resources
W (21319) GMF_SETUP_AUD_CODEC: Unregistering default decoder
I (21330) BOARD_DEVICE: Deinit device audio_dac ref_count: 0 device_handle:0x3fcea024
I (21337) BOARD_DEVICE: Device audio_dac config found: 0x3c0de0d4 (size: 92)
I (21339) BOARD_PERIPH: Deinit peripheral i2s_audio_out ref_count: 0
E (21345) i2s_common: i2s_channel_disable(1262): the channel has not been enabled yet
W (21353) PERIPH_I2S: Caution: Releasing TX (0x0).
W (21357) PERIPH_I2S: Caution: RX (0x3c14fca0) forced to stop.
E (21363) i2s_common: i2s_channel_disable(1262): the channel has not been enabled yet
I (21370) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 0
I (21376) PERIPH_I2C: I2C master bus deinitialized successfully
I (21382) BOARD_MANAGER: Device audio_dac deinitialized
I (21387) BOARD_DEVICE: Deinit device fs_sdcard ref_count: 0 device_handle:0x3fce9a7c
I (21394) BOARD_DEVICE: Device fs_sdcard config found: 0x3c0de024 (size: 84)
I (21401) DEV_FS_FAT: Sub device 'sdmmc' deinitialized successfully
I (21407) BOARD_MANAGER: Device fs_sdcard deinitialized
I (21412) main_task: Returned from app_main()
```

## Troubleshooting

### Audio file not found or cannot play

If the log shows `FILE_IO: The error is happened in reading data` or `Bad file number`, the file is likely missing on the microSD card. Ensure the file is renamed to `test` with the correct extension (e.g. `test.mp3`, `test.wav`) in the card root and that the card is mounted correctly.

## Technical Support

For technical support, use the links below:

- Technical support: [esp32.com](https://esp32.com/viewforum.php?f=20) forum
- Issue reports and feature requests: [GitHub issue](https://github.com/espressif/esp-gmf/issues)

We will reply as soon as possible.
