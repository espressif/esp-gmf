# Pipeline HOWL (Howling Suppression)

- [中文](./README_CN.md)
- Example difficulty: ⭐

## Overview

This example demonstrates a karaoke-style audio flow with **three GMF pipelines** and **two audio sources** (backing track and microphone). Each source is processed on its own pipeline, connected to the mix pipeline via ringbuf, then mixed by `aud_mixer` and played through the DAC.

- Backing track pipeline: `io_file` (SD card) → `aud_dec` → `aud_rate_cvt` → `aud_bit_cvt` → `aud_ch_cvt`
- Microphone pipeline: `io_codec_dev` (ADC) → `aud_howl`
- Mix output pipeline: `aud_mixer` → `io_codec_dev` (DAC)

The final mixed playback format is **16 kHz / mono / 16-bit**.

### Typical Use Cases

- Karaoke backing track + live vocal mixing
- Howling suppression validation in near-field microphone amplification
- Reference example for multi-pipeline audio connections (databus/ringbuf)

## Environment Setup

### Hardware Requirements

- **Development board**: an ESP audio board with ADC and DAC, such as ESP32-S3-Korvo V3.
- **Peripherals**: microphone, speaker, and SD card.

### Default IDF Version

As with other GMF basic examples, this example supports IDF release/v5.4 (≥ v5.4.3) and release/v5.5 (≥ v5.5.2).

### Software Requirements

- Prepare a microSD card, rename your audio file to `test` (keep the extension consistent with the format, for example `test.mp3`), and copy it to the card. You can change the playback file path with `DEFAULT_PLAY_URL`. Supported backing-track formats include MP3, WAV, FLAC, AAC, M4A, TS, AMRNB, AMRWB, and others. MP3 is the default.
- To change the final playback format, adjust the following in `main/pipeline_howl.c`:
  - `HOWL_SAMPLE_RATE`
  - `HOWL_CHANNELS`
  - `HOWL_BITS`

## Build and Download

### Build Preparation

Set up the ESP-IDF environment before building. Skip this step if it is already configured.

```bash
./install.sh
. ./export.sh
```

Short setup steps:

- Enter the example directory:

```bash
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_howl
```

This example uses [ESP Board Manager](https://github.com/espressif/esp-board-manager) to manage board-level resources. The [`esp-bmgr-assist`](https://pypi.org/project/esp-bmgr-assist/) helper tool is recommended as the default entry point.

- Install once in your activated ESP-IDF Python environment:

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

> [!NOTE]
> If you switch to another board supported by `esp_board_manager`, follow the same steps and replace the board name or index.
> For custom boards, see [How to Customize a Board](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/docs/how_to_customize_board.md).
> For more about `esp_board_manager`, see [ESP Board Manager Getting Started](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README.md).

### Project Configuration (Optional)

- To adjust audio-effect parameters such as mixer weights and HOWL detection thresholds, configure the GMF Audio options in menuconfig:

```bash
idf.py menuconfig
```

Configure the following items in menuconfig:

1) HOWL settings
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `HOWL PAPR Threshold × 10`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `HOWL PHPR Threshold × 10`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `HOWL PNPR Threshold × 10`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `HOWL IMSD Threshold × 10`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `Enable HOWL IMSD`

2) MIXER settings
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `Source1 Parameters`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `Source12 Parameters`

> After configuration, press `s` to save, then press `Esc` to exit.

### SD Card Music File

- Copy the backing-track file to the SD card and set the following in `main/pipeline_howl.c`:
  - `HOWL_MUSIC_URI`
- The default path in the current code is `/sdcard/test.mp3`

## Build and Flash

- Build the example:

```
idf.py build
```

- Flash the firmware and open the serial monitor (replace `PORT` with your serial port name):

```
idf.py -p PORT flash monitor
```

- Press `Ctrl-]` to exit the monitor.

## How to Use the Example

### Behavior

- After power-on, the example initializes the DAC, ADC, and SD card, creates three pipelines, and starts automatically:
  - Backing-track decode + resample / bit-depth / channel conversion
  - Microphone capture + HOWL processing
  - Mix and playback
- When the backing-track file finishes playing, the example stops all pipelines and releases resources.

### Log Output

- In the normal flow, logs appear in order for SD card mounting, element registration, pipeline creation, URL setup, task binding, event listening, pipeline start, end-of-playback wait, and resource teardown. Key steps are marked `[ 1 ]` through `[ 7 ]`.

```c
I (906) main_task: Calling app_main()
I (908) PIPELINE_HOWL: [ 1 ] Init peripherals
I (913) PERIPH_I2S: I2S[0] STD,  TX, ws: 45, bclk: 9, dout: 8, din: 10
I (918) PERIPH_I2S: I2S[0] initialize success: 0x3c1709bc
I (924) DEV_AUDIO_CODEC: DAC is ENABLED
I (927) PERIPH_I2C: I2C master bus initialized successfully
I (938) ES8311: Work in Slave mode
I (941) DEV_AUDIO_CODEC: Successfully initialized codec: audio_dac
I (941) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fceb574, chip:es8311
I (949) BOARD_MANAGER: Device audio_dac initialized
I (953) BOARD_DEVICE: Device handle audio_dac found, Handle: 0x3fce9a80 TO: 0x3fce9a80
I (961) I2S_IF: No paired data, current mode: playback
I (966) I2S_IF: STD: TX, data_bit: 16, slot_bit: 16, ws_width: 16, slot_mode: MONO, slot_mask: 0x1
I (974) I2S_IF: STD: TX, sample_rate_hz: 16000, mclk_multiple: 256, clk_src: 6
I (997) Adev_Codec: Open codec device OK
I (997) PERIPH_I2S: I2S[0] STD, RX, ws: 45, bclk: 9, dout: 8, din: 10
I (997) PERIPH_I2S: I2S[0] initialize success: 0x3c170b78
I (1001) DEV_AUDIO_CODEC: ADC over I2S is enabled
I (1005) BOARD_PERIPH: Reuse periph: i2c_master, ref_count=2
I (1016) ES8311: Work in Slave mode
I (1019) DEV_AUDIO_CODEC: Successfully initialized codec: audio_adc
I (1020) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fceb0f4, chip:es8311
I (1027) BOARD_MANAGER: Device audio_adc initialized
I (1032) BOARD_DEVICE: Device handle audio_adc found, Handle: 0x3fcea6e0 TO: 0x3fcea6e0
I (1040) I2S_IF: Current mode(record) and peer mode have same sample_rate 16000, channel 2, bits_per_sample 16
I (1049) I2S_IF: STD: RX, data_bit: 16, slot_bit: 16, ws_width: 16, slot_mode: MONO, slot_mask: 0x1
I (1058) I2S_IF: STD: RX, sample_rate_hz: 16000, mclk_multiple: 256, clk_src: 6
I (1079) Adev_Codec: Open codec device OK
I (1079) DEV_FS_FAT_SUB_SDMMC: slot_config: cd=-1, wp=-1, clk=6, cmd=7, d0=4, d1=-1, d2=-1, d3=-1, d4=-1, d5=-1, d6=-1, d7=-1, width=1, flags=0x1
W (1128) SD_HOST: input line delay not supported, fallback to 0 delay
Name: SD16G
Type: SDHC
Speed: 40.00 MHz (limit: 40.00 MHz)
Size: 15238MB
CSD: ver=2, sector_size=512, capacity=31207424 read_bl_len=9
SSR: bus_width=1
I (1136) DEV_FS_FAT: Filesystem mounted, base path: /sdcard
I (1142) BOARD_MANAGER: Device fs_sdcard initialized
I (1146) PIPELINE_HOWL: [ 2 ] Register pool
I (1151) ESP_GMF_POOL: Registered items on pool:0x3c172a24, app_main-165
I (1157) ESP_GMF_POOL: IO, Item:0x3c172ab0, H:0x3c172cc4, TAG:io_codec_dev
I (1163) ESP_GMF_POOL: IO, Item:0x3c172e50, H:0x3c172d80, TAG:io_codec_dev
I (1170) ESP_GMF_POOL: IO, Item:0x3c172f6c, H:0x3c172e60, TAG:io_file
I (1176) ESP_GMF_POOL: IO, Item:0x3c173088, H:0x3c172f7c, TAG:io_file
I (1182) ESP_GMF_POOL: IO, Item:0x3c1731a8, H:0x3c173098, TAG:io_http
I (1188) ESP_GMF_POOL: IO, Item:0x3c1732b8, H:0x3c1731b8, TAG:io_embed_flash
I (1195) ESP_GMF_POOL: EL, Item:0x3c1733d8, H:0x3c1732c8, TAG:aud_dec
I (1201) ESP_GMF_POOL: EL, Item:0x3c1734d4, H:0x3c1733e8, TAG:aud_alc
I (1207) ESP_GMF_POOL: EL, Item:0x3c1735bc, H:0x3c1734e4, TAG:aud_ch_cvt
I (1214) ESP_GMF_POOL: EL, Item:0x3c1736a0, H:0x3c1735cc, TAG:aud_bit_cvt
I (1220) ESP_GMF_POOL: EL, Item:0x3c17378c, H:0x3c1736b0, TAG:aud_rate_cvt
I (1227) ESP_GMF_POOL: EL, Item:0x3c1738bc, H:0x3c17379c, TAG:aud_mixer
I (1233) ESP_GMF_POOL: EL, Item:0x3c1739d0, H:0x3c1738cc, TAG:aud_howl
I (1240) PIPELINE_HOWL: [ 3 ] Build pipelines
I (1244) PIPELINE_HOWL: [ 3.1 ] Config music source and decoder
I (1249) PIPELINE_HOWL: [ 3.2 ] Config howl parameters
I (1254) PIPELINE_HOWL: [ 3.3 ] Config mixer parameters
I (1259) PIPELINE_HOWL: [ 3.4 ] Connect music/mic pipelines to mixer
I (1265) NEW_DATA_BUS: New ringbuffer:0x3c174660, num:10, item_cnt:1024, db:0x3c176e8c
I (1273) NEW_DATA_BUS: New ringbuffer:0x3c176ef8, num:10, item_cnt:1024, db:0x3c179724
I (1280) PIPELINE_HOWL: [ 4 ] Create tasks and load jobs
I (1286) ESP_GMF_TASK: Waiting to run... [tsk:howl_music-0x3fcec480, wk:0, run:0]
I (1293) ESP_GMF_TASK: Waiting to run... [tsk:howl_music-0x3fcec480, wk:0x3c1798d0, run:0]
I (1301) ESP_GMF_TASK: Waiting to run... [tsk:howl_mic-0x3fced67c, wk:0, run:0]
W (1301) ESP_GMF_PIPELINE: Element[aud_howl-0x3c1740cc] not ready to register job, ret:0xffffdff8
I (1316) ESP_GMF_TASK: Waiting to run... [tsk:howl_mix-0x3fcee878, wk:0, run:0]
W (1323) ESP_GMF_PIPELINE: Element[aud_mixer-0x3c174350] not ready to register job, ret:0xffffdff8
I (1332) ESP_GMF_TASK: Waiting to run... [tsk:howl_mic-0x3fced67c, wk:0x3c179998, run:0]
I (1332) ESP_GMF_TASK: Waiting to run... [tsk:howl_mix-0x3fcee878, wk:0x3c179a20, run:0]
I (1332) PIPELINE_HOWL: [ 5 ] Run howl pipelines
I (1352) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: NULL-0x3c17430c, type: 2000, sub: ESP_GMF_EVENT_STATE_OPENING, payload: 0, size: 0, ctx:0x3fceeac0
I (1365) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: aud_mixer-0x3c174350, type: 3000, sub: ESP_GMF_EVENT_STATE_INITIALIZED, payload: 0x3fcaaf70, size: 16, ctx:0x3fceeac0
I (1380) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: aud_mixer-0x3c174350, type: 2000, sub: ESP_GMF_EVENT_STATE_RUNNING, payload: 0, size: 0, ctx:0x3fceeac0
I (1393) ESP_GMF_TASK: One times job is complete, del[wk:0x3c179a20, ctx:0x3c174350, label:aud_mixer_open]
I (1403) ESP_GMF_PORT: ACQ IN, new self payload:0x3c179a20, port:0x3c1797d0, el:0x3c174350-aud_mixer
I (1412) ESP_GMF_FILE: Open, dir:1, uri:/sdcard/test.mp3
I (1417) PIPELINE_HOWL: [ 6 ] Wait music end then stop all pipelines
I (1417) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: -0x3c174088, type: 2000, sub: ESP_GMF_EVENT_STATE_OPENING, payload: 0, size: 0, ctx:0x3fceea98
I (1442) ESP_GMF_PORT: ACQ IN, new self payload:0x3c17aa04, port:0x3c179860, el:0x3c174350-aud_mixer
I (1447) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: aud_howl-0x3c1740cc, type: 3000, sub: ESP_GMF_EVENT_STATE_INITIALIZED, payload: 0x3fcee590, size: 16, ctx:0x3fceea98
I (1448) ESP_GMF_FILE: File size: 3664281 byte, file position: 0
I (1459) ESP_GMF_HOWL: Howl opened, 0x3c1740cc, frame_bytes 1024
I (1465) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: -0x3c1739e0, type: 2000, sub: ESP_GMF_EVENT_STATE_OPENING, payload: 0, size: 0, ctx:0x3fceea70
I (1471) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: aud_howl-0x3c1740cc, type: 2000, sub: ESP_GMF_EVENT_STATE_RUNNING, payload: 0, size: 0, ctx:0x3fceea98
I (1483) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1798d0, ctx:0x3c173a24, label:aud_dec_open]
I (1497) ESP_GMF_TASK: One times job is complete, del[wk:0x3c179998, ctx:0x3c1740cc, label:aud_howl_open]
I (1506) ESP_GMF_PORT: ACQ IN, new self payload:0x3c1798d0, port:0x3c174048, el:0x3c173a24-aud_dec
I (1515) ESP_GMF_PORT: ACQ IN, new self payload:0x3c179998, port:0x3c1742cc, el:0x3c1740cc-aud_howl
I (1525) ESP_ES_PARSER: The version of es_parser is v1.0.1
W (1539) ESP_GMF_ASMP_DEC: Not enough memory for out, need:4608, old: 1024, new: 4608
I (1689) ESP_GMF_TASK: One times job is complete, del[wk:0x3c17ac1c, ctx:0x3c173b34, label:aud_rate_cvt_open]
I (1690) ESP_GMF_TASK: One times job is complete, del[wk:0x3c184260, ctx:0x3c173c90, label:aud_bit_cvt_open]
I (1697) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: aud_ch_cvt-0x3c173de4, type: 3000, sub: ESP_GMF_EVENT_STATE_INITIALIZED, payload: 0x3fced3a0, size: 16, ctx:0x3fceea70
I (1712) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: aud_ch_cvt-0x3c173de4, type: 2000, sub: ESP_GMF_EVENT_STATE_RUNNING, payload: 0, size: 0, ctx:0x3fceea70
I (1726) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1842b0, ctx:0x3c173de4, label:aud_ch_cvt_open]
I (230320) ESP_GMF_FILE: No more data, ret: 0
I (230322) ESP_GMF_TASK: Job is done, [tsk:howl_music-0x3fcec480, wk:0x3c17990c, job:0x3c173a24-aud_dec_proc]
I (230324) ESP_GMF_TASK: Job is done, [tsk:howl_music-0x3fcec480, wk:0x3c183f24, job:0x3c173b34-aud_rate_cvt_proc]
I (230333) ESP_GMF_TASK: Job is done, [tsk:howl_music-0x3fcec480, wk:0x3c184288, job:0x3c173c90-aud_bit_cvt_proc]
I (230376) ESP_GMF_TASK: Job is done, [tsk:howl_music-0x3fcec480, wk:0x3c1842d8, job:0x3c173de4-aud_ch_cvt_proc]
I (230376) ESP_GMF_TASK: Finish, strategy action: 0, [tsk:0x3fcec480-howl_music]
I (230382) ESP_GMF_FILE: CLose, 0x3c173f3c, pos = 3664281/3664281
I (230388) ESP_GMF_TASK: One times job is complete, del[wk:0x3c183f24, ctx:0x3c173a24, label:aud_dec_close]
I (230398) ESP_GMF_TASK: One times job is complete, del[wk:0x3c184260, ctx:0x3c173b34, label:aud_rate_cvt_close]
I (230407) ESP_GMF_TASK: One times job is complete, del[wk:0x3c184288, ctx:0x3c173c90, label:aud_bit_cvt_close]
I (230417) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1798f4, ctx:0x3c173de4, label:aud_ch_cvt_close]
I (230427) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: -0x3c1739e0, type: 2000, sub: ESP_GMF_EVENT_STATE_FINISHED, payload: 0, size: 0, ctx:0x3fceea70
I (230440) ESP_GMF_TASK: Waiting to run... [tsk:howl_music-0x3fcec480, wk:0, run:0]
I (230447) ESP_GMF_TASK: Waiting to run... [tsk:howl_music-0x3fcec480, wk:0, run:0]
I (230463) ESP_GMF_CODEC_DEV: CLose, 0x3c1741d0, pos = 7327744/0
I (230463) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1799bc, ctx:0x3c1740cc, label:aud_howl_close]
I (230470) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: -0x3c174088, type: 2000, sub: ESP_GMF_EVENT_STATE_STOPPED, payload: 0, size: 0, ctx:0x3fceea98
I (230483) ESP_GMF_TASK: Waiting to run... [tsk:howl_mic-0x3fced67c, wk:0, run:0]
I (230490) ESP_GMF_TASK: Waiting to run... [tsk:howl_mic-0x3fced67c, wk:0, run:0]
I (230496) ESP_GMF_CODEC_DEV: CLose, 0x3c174470, pos = 7328640/0
I (230503) ESP_GMF_TASK: One times job is complete, del[wk:0x3c179a44, ctx:0x3c174350, label:aud_mixer_close]
I (230513) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: NULL-0x3c17430c, type: 2000, sub: ESP_GMF_EVENT_STATE_STOPPED, payload: 0, size: 0, ctx:0x3fceeac0
I (230526) ESP_GMF_TASK: Waiting to run... [tsk:howl_mix-0x3fcee878, wk:0, run:0]
I (230533) ESP_GMF_TASK: Waiting to run... [tsk:howl_mix-0x3fcee878, wk:0, run:0]
I (230540) PIPELINE_HOWL: [ 7 ] Destroy resources
W (230545) GMF_SETUP_AUD_CODEC: Unregistering default decoder
I (230551) BOARD_DEVICE: Deinit device fs_sdcard ref_count: 0 device_handle:0x3fceb144
I (230558) BOARD_DEVICE: Device fs_sdcard config found: 0x3c1107dc (size: 84)
I (230565) DEV_FS_FAT: Sub device 'sdmmc' deinitialized successfully
I (230571) BOARD_MANAGER: Device fs_sdcard deinitialized
I (230577) I2S_IF: Pending out channel for in channel running
I (230581) BOARD_DEVICE: Deinit device audio_dac ref_count: 0 device_handle:0x3fce9a80
I (230590) BOARD_DEVICE: Device audio_dac config found: 0x3c1109b0 (size: 384)
I (230596) BOARD_PERIPH: Deinit peripheral i2s_audio_out ref_count: 0
W (230602) PERIPH_I2S: Caution: Releasing TX (0).
I (230607) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 1
W (230612) BOARD_PERIPH: Peripheral i2c_master still has 1 references, not deinitializing
I (230620) BOARD_MANAGER: Device audio_dac deinitialized
I (230630) BOARD_DEVICE: Deinit device audio_adc ref_count: 0 device_handle:0x3fcea6e0
I (230639) BOARD_DEVICE: Device audio_adc config found: 0x3c110830 (size: 384)
I (230640) BOARD_PERIPH: Deinit peripheral i2s_audio_in ref_count: 0
E (230646) i2s_common: i2s_channel_disable(1290): the channel has not been enabled yet
W (230654) PERIPH_I2S: Caution: Releasing RX (0).
I (230658) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 0
I (230664) PERIPH_I2C: I2C master bus deinitialized successfully
I (230670) BOARD_MANAGER: Device audio_adc deinitialized
I (230675) main_task: Returned from app_main()
```

## Troubleshooting

### SD Card File Not Found or Playback Fails

- Check whether the SD card mounted successfully.
- Check whether `HOWL_MUSIC_URI` matches the file path and extension.
- Use the `pipeline_play_sdcard_music` example first to verify board-level SD card and decode support.

### Microphone Path Timeout (`HOWL_mic` timeout)

- Check whether the ADC initialized successfully.
- Confirm that the microphone path reports `ESP_GMF_INFO_SOUND` with the expected capture parameters (16 kHz / 1 ch / 16-bit in this example).
- Confirm that HOWL is enabled and opened successfully.
- Confirm that the mixer input side is not blocked for a long time (ringbuf connection and wait parameters).

## Technical Support

- Forum: [esp32.com](https://esp32.com/viewforum.php?f=20)
- Issue tracker: [esp-gmf GitHub Issues](https://github.com/espressif/esp-gmf/issues)

We will get back to you as soon as possible.
