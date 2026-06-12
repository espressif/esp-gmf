# Recording Audio to a microSD Card with Muxer

- [中文版](./README_CN.md)
- Example Difficulty: ⭐

## Example Overview

This example captures audio from the codec through CODEC_DEV_RX IO, encodes it with `aud_enc`, then multiplexes the encoded data into multiple container formats with the muxer element and saves it to a microSD card.

- Pipeline architecture: `io_codec_dev` → `aud_enc` → `aud_muxer` (file output).
- Supports multiple container formats
- The muxer can be configured to write files or output through databus; this example uses **file output** mode.

### Typical Scenarios

- Local recording archives that require container encapsulation (MP4/TS/WAV, etc.), such as meeting recording and voice recorder scenarios.
- For **lossless or high-fidelity archiving**, you can use combinations such as **ALAC + MP4/CAF** (ALAC encoder and corresponding muxer support must be enabled in the project first).

## Feature Description

### Containers and Codec Formats

- **Container formats supported by `aud_muxer`**: TS, MP4, FLV, WAV, CAF, OGG, AVI. You can switch by modifying `DEFAULT_RECODER_MUXER_TYPE`
- **Codec-container compatibility is required**: each container supports only part of codec tracks (for example, OGG commonly pairs with OPUS). See [Muxer documentation](https://github.com/espressif/esp-adf-libs/blob/master/esp_muxer/README.md)
- **Lossless recording with ALAC**: with ALAC encoder enabled, set codec to ALAC and select a container that supports this track (such as **MP4**, **CAF**, depending on actual component support) to get **compressed lossless** archives; compared with **uncompressed PCM**, ALAC takes less space.
- **Lossy / lossless (brief)**:
  - **Lossy**: AAC, OPUS, AMR, ADPCM, SBC, LC3, G711, etc. (irreversible approximation after compression).
  - **Lossless**: **ALAC** (compressed lossless), **PCM** / linear PCM encapsulation (such as some WAV cases, with no perceptual codec quality loss).

### Output Modes: Streaming Output vs File Output

`aud_muxer` uses `output_type` (type `esp_gmf_audio_muxer_output_type_t`) to distinguish two output paths:

| Mode | Enum Value | Behavior |
|------|------------|----------|
| **Streaming output** | `ESP_GMF_AUDIO_MUXER_OUTPUT_STREAMING` | Muxed data is sent to pipeline **downstream** through **databus**; output IO **must be attached** when creating the pipeline (such as `io_file`, `io_http`), and this IO reads the stream from databus and writes to file or network. **Setting only `output_type` without attaching output IO cannot complete local write/stream push.** |
| **File output** | `ESP_GMF_AUDIO_MUXER_OUTPUT_FILE` | The muxer writes to storage directly through the **`url_pattern` callback**; output-side IO can be **`NULL`** when creating the pipeline (this example uses `esp_gmf_pool_new_pipeline(..., NULL)`). |

### File Slicing (File Output Mode Only)

- For long-time recording, writing only one huge file is not convenient for copy and abnormal recovery. With slicing enabled, the muxer generates multiple files by time length. The file name/path is decided by the registered **`url_pattern` callback** (this example uses `muxer_file_pattern_cb`; users can modify this function to support custom file names and paths), and usually includes an **incremental `slice_index`** (for example, `esp_gmf_muxer_000.mp4`, `esp_gmf_muxer_001.mp4`).
- **Meaning of `slice_duration`**: the **target duration** of each segment, in **milliseconds (ms)**. When this duration is reached (or component-defined slicing condition is met), the current file is closed and the next segment starts. For example, `60000` means about 60 seconds per file.

## Environment Configuration

### Hardware Requirements

- **Development board**: ESP32-S3-Korvo V3 is used by default; other ESP audio boards are also applicable.
- **Required resources**: microSD card, microphone, Audio ADC.

### Default IDF Branch

This example supports IDF release/v5.4 (>= v5.4.3) and release/v5.5 (>= v5.5.2).

## Build and Flash

### Build Preparation

Before building this example, make sure the ESP-IDF environment is configured. If it is already configured, you can skip this section and directly enter the project directory. If not, run the following scripts in the ESP-IDF root directory to complete environment setup. For full steps, see the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/index.html).

```
./install.sh
. ./export.sh
```

Quick steps:

- Enter this example project directory:

```
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_record_audio_muxer
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

  On first invocation, the component is downloaded automatically based on the `espressif/esp_board_manager` dependency declared in `main/idf_component.yml`.

> [!NOTE]
> To switch to a different board supported by `esp_board_manager`, repeat the same steps with the new board name or index.
> For a custom board, see [How to customize board](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/docs/how_to_customize_board.md).
> For more information about `esp_board_manager`, see the [ESP Board Manager Getting Started Guide](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README.md).

### Project Configuration

In source file `main/record_muxer.c`, behavior can be adjusted through the following macros and related muxer configuration items:

- `DEFAULT_RECORD_SAMPLE_RATE`: sample rate (for example, 48000 Hz)
- `DEFAULT_RECORD_CHANNEL`: channel count (for example, 2ch)
- `DEFAULT_RECORD_BITS`: sample bit depth (for example, 16bit)
- `DEFAULT_RECORD_BITRATE`: encoding bitrate (for example, 90000bps)
- `DEFAULT_RECODER_CODEC_TYPE`: codec type (for example, `ESP_AUDIO_TYPE_ALAC`)
- `DEFAULT_RECODER_MUXER_TYPE`: muxer container type (for example, `ESP_MUXER_TYPE_MP4`)
- `DEFAULT_RECODER_MUXER_SLICE_DURATION`: slice duration (milliseconds), assigned to muxer `slice_duration` (default in example is 60000, about 60 seconds per segment; whether `0` is supported to disable slicing depends on component behavior)

Recorded muxed files are written to microSD (path pattern can be found in `muxer_file_pattern_cb` in source code).

### Build and Flash

- Build the example

```
idf.py build
```

- Flash and run monitor to view serial output (replace PORT with your port name):

```
idf.py -p PORT flash monitor
```

- Exit monitor with `Ctrl+]`

## How to Use the Example

### Function and Usage

- After power-on, the example mounts microSD, initializes recording codec, and starts recording with configured codec and muxer type.
- After recording for a while, it stops automatically, writes encapsulated files to the card (for example, `/sdcard/esp_gmf_muxer_000.mp4`), and then releases resources.

### Log Output

- In normal flow, logs print peripheral initialization, element registration, creation of pipeline with encoder and muxer, muxer configuration, task binding, event listening, pipeline startup, muxer output path, etc.

```
I (844) main_task: Calling app_main()
I (846) REC_MUXER: [ 1 ] Setup peripheral for audio codec device and sdcard
I (853) DEV_FS_FAT_SUB_SDMMC: slot_config: cd=-1, wp=-1, clk=15, cmd=7, d0=4, d1=-1, d2=-1, d3=-1, d4=-1, d5=-1, d6=-1, d7=-1, width=1, flags=0x1
Name: SD
Type: SDHC
Speed: 40.00 MHz (limit: 40.00 MHz)
Size: 29818MB
CSD: ver=2, sector_size=512, capacity=61067264 read_bl_len=9
SSR: bus_width=1
I (913) DEV_FS_FAT: Filesystem mounted, base path: /sdcard
I (918) BOARD_MANAGER: Device fs_sdcard initialized
I (923) DEV_AUDIO_CODEC: ADC is ENABLED
I (927) PERIPH_I2S: I2S[0] TDM, RX, ws: 45, bclk: 9, dout: 8, din: 10
I (932) PERIPH_I2S: I2S[0] initialize success: 0x3c10d194
I (938) DEV_AUDIO_CODEC: Init audio_adc, i2s_name: i2s_audio_in, i2s_rx_handle:0x3c10d194, i2s_tx_handle:0x3c10cfac, data_if: 0x3fcee908
I (950) PERIPH_I2C: I2C master bus initialized successfully
I (958) ES7210: Work in Slave mode
I (965) ES7210: Enable ES7210_INPUT_MIC1
I (967) ES7210: Enable ES7210_INPUT_MIC2
I (970) ES7210: Enable ES7210_INPUT_MIC3
I (973) ES7210: Enable TDM mode
I (976) DEV_AUDIO_CODEC: Successfully initialized codec: audio_adc
I (978) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fceeb64, chip:es7210
I (985) BOARD_MANAGER: Device audio_adc initialized
I (990) BOARD_DEVICE: Device handle audio_adc found, Handle: 0x3fcea070 TO: 0x3fcea070
I (1000) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:1
I (1003) I2S_IF: TDM Mode 0 bits:16/16 channel:2 sample_rate:48000 mask:1
I (1009) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:1
I (1014) I2S_IF: TDM Mode 1 bits:16/16 channel:2 sample_rate:48000 mask:1
I (1022) ES7210: Bits 8
I (1030) ES7210: Enable ES7210_INPUT_MIC1
I (1033) ES7210: Enable ES7210_INPUT_MIC2
I (1036) ES7210: Enable ES7210_INPUT_MIC3
I (1039) ES7210: Enable TDM mode
I (1044) ES7210: Unmuted
I (1044) Adev_Codec: Open codec device OK
I (1044) REC_MUXER: [ 2 ] Register all the elements and set audio information to record codec device
I (1052) GMF_SETUP_AUD_MUXER: Muxer config: type=538989396, output_type=0, codec=541278529
I (1060) GMF_SETUP_AUD_MUXER: Audio muxer initialized, type: 538989396, codec: 541278529
I (1068) ESP_GMF_POOL: Registered items on pool:0x3c10d8dc, app_main-120
I (1074) ESP_GMF_POOL: IO, Item:0x3c10d9f4, H:0x3c10d8f0, TAG:io_codec_dev
I (1081) ESP_GMF_POOL: EL, Item:0x3c10db10, H:0x3c10da04, TAG:aud_enc
I (1087) ESP_GMF_POOL: EL, Item:0x3c10dc18, H:0x3c10db20, TAG:aud_muxer
I (1093) REC_MUXER: [ 3 ] Create audio pipeline with encoder and muxer
I (1100) REC_MUXER: [ 3.1 ] Configure muxer element
I (1104) REC_MUXER: [ 3.2 ] Reconfig audio encoder type and report information to the record pipeline
I (1113) REC_MUXER: [ 3.3 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (1123) ESP_GMF_TASK: Waiting to run... [tsk:gmf_rec_muxer-0x3fcede00, wk:0x0, run:0]
I (1131) ESP_GMF_TASK: Waiting to run... [tsk:gmf_rec_muxer-0x3fcede00, wk:0x3c10f05c, run:0]
I (1139) REC_MUXER: [ 3.4 ] Create event group and listening event from pipeline
I (1146) REC_MUXER: [ 4 ] Start audio_pipeline
I (1150) REC_MUXER: CB: RECV Pipeline EVT: el:NULL-0x3c10dc28, type:8192, sub:ESP_GMF_EVENT_STATE_OPENING, payload:0x0, size:0,0x0
I (1164) ESP_GMF_AENC: Open, type:ALAC, acquire in frame: 8192, out frame: 8200
I (1169) ESP_GMF_TASK: One times job is complete, del[wk:0x3c10f05c, ctx:0x3c10dc6c, label:aud_enc_open]
I (1178) ESP_GMF_PORT: ACQ IN, new self payload:0x3c10f05c, port:0x3c10dff4, el:0x3c10dc6c-aud_enc
I (1187) REC_MUXER: [ 5 ] Wait for a while to stop record pipeline
I (1263) ESP_GMF_MUXER: Open muxer element, type: 540299341, output_type: 1 sample_rate: 48000, channel: 1, bits: 16
I (1263) REC_MUXER: CB: RECV Pipeline EVT: el:aud_muxer-0x3c10dd78, type:8192, sub:ESP_GMF_EVENT_STATE_RUNNING, payload:0x0, size:0,0x0
I (1274) ESP_GMF_TASK: One times job is complete, del[wk:0x3c10f138, ctx:0x3c10dd78, label:aud_muxer_open]
I (1284) REC_MUXER: Muxer file pattern: /sdcard/esp_gmf_muxer_000.mp4 (slice index: 0)
I (1305) MP4_MUXER: Set track limit 6000

I (11220) ESP_GMF_CODEC_DEV: CLose, 0x3c10def0, pos = 901120/0
I (11221) ESP_GMF_TASK: One times job is complete, del[wk:0x3c10f080, ctx:0x3c10dc6c, label:aud_enc_close]
I (11242) ESP_GMF_TASK: One times job is complete, del[wk:0x3c10f0bc, ctx:0x3c10dd78, label:aud_muxer_close]
I (11242) REC_MUXER: CB: RECV Pipeline EVT: el:NULL-0x3c10dc28, type:8192, sub:ESP_GMF_EVENT_STATE_STOPPED, payload:0x0, size:0,0x0
I (11252) ESP_GMF_TASK: Waiting to run... [tsk:gmf_rec_muxer-0x3fcede00, wk:0x0, run:0]
I (11259) ESP_GMF_TASK: Waiting to run... [tsk:gmf_rec_muxer-0x3fcede00, wk:0x0, run:0]
I (11267) REC_MUXER: [ 6 ] Destroy all the resources
W (11272) GMF_SETUP_AUD_CODEC: Unregistering default encoder
I (11277) BOARD_DEVICE: Deinit device audio_adc ref_count: 0 device_handle:0x3fcea070
I (11288) BOARD_DEVICE: Device audio_adc config found: 0x3c0dd86c (size: 92)
I (11291) BOARD_PERIPH: Deinit peripheral i2s_audio_in ref_count: 0
E (11297) i2s_common: i2s_channel_disable(1262): the channel has not been enabled yet
W (11305) PERIPH_I2S: Caution: Releasing RX (0x0).
I (11309) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 0
I (11315) PERIPH_I2C: I2C master bus deinitialized successfully
I (11321) BOARD_MANAGER: Device audio_adc deinitialized
I (11326) BOARD_DEVICE: Deinit device fs_sdcard ref_count: 0 device_handle:0x3fce9a7c
I (11333) BOARD_DEVICE: Device fs_sdcard config found: 0x3c0dd818 (size: 84)
I (11340) DEV_FS_FAT: Sub device 'sdmmc' deinitialized successfully
I (11346) BOARD_MANAGER: Device fs_sdcard deinitialized
I (11351) main_task: Returned from app_main()
```

## Troubleshooting

### microSD Card Not Mounted or Not Writable

- Make sure the microSD card is inserted correctly and formatted as FAT32.
- If logs show file open failure, check card capacity and write-protect status.

### No Recording, Encoding, or Muxer Initialization Failure

- Make sure board-level recording codec and I2S are configured correctly.
- Make sure the **muxer type and codec type** combination is valid (for example, OGG requires OPUS).
- If encoder initialization fails, check whether the corresponding codec format is enabled in menuconfig.

### Related References

- If you only need encoded raw stream file output without container encapsulation, or if you need low-power features, refer to [pipeline_record_sdcard](../pipeline_record_sdcard/README.md) in the same directory.
- The current example only supports audio recording and encapsulation. If you need packaging support for video-related capability, refer to [esp_capture](../../../packages/esp_capture/README.md).

## Technical Support

Get technical support from the following links:

- Technical support forum: [esp32.com](https://esp32.com/viewforum.php?f=20)
- For issue reports and feature requests, create a [GitHub issue](https://github.com/espressif/esp-gmf/issues)

We will reply as soon as possible.
