
# Recording Audio File to microSD Card

- [中文版](./README_CN.md)

## Example Brief

This example demonstrates how to capture audio using CODEC_DEV_RX IO from the codec, encode the recorded sound using an encoder element, and save the encoded data back to the microSD card via File IO.

This example supports encoding the recording into AAC, G711A, G711U, AMRNB, AMRWB, OPUS, ADPCM, and PCM audio formats, with AAC as the default format.

This example achieves lower overall system power consumption by fixing the CPU frequency at a lower level, making it well-suited for low-power voice recorder projects.

> [!NOTE]
> 1. You can modify `DEFAULT_CPU_FREQ_MHZ` to achieve different CPU frequencies. For the actual supported frequencies, refer to the optional frequencies in `rtc_clk_cpu_freq_mhz_to_config` for the corresponding chip.
> 2. For power consumption differences at different frequencies, refer to the "Power Consumption in Other Power Modes" section in the corresponding chip's datasheet, such as the [ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf).
> 3. If you don't want to directly fix the CPU frequency, you can refer to the [ESP32 Power Management](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/power_management.html#esp32) to implement the default power management strategy.

## Example Set Up

### Default IDF Branch

This example supports IDF release/v5.3 and later branches.

### Configuration

This example requires a microSD card. The recorded and encoded audio will be automatically stored on the microSD card. Users can modify the encoding format and audio parameter configuration using the `esp_gmf_audio_helper_reconfig_enc_by_type` function.

### Build and Flash

Before compiling this example, ensure that the ESP-IDF environment is properly configured. If it is already set up, you can proceed to the next configuration step. If not, run the following script in the root directory of ESP-IDF to set up the build environment. For detailed steps on configuring and using ESP-IDF, please refer to the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/index.html)

```
./install.sh
. ./export.sh
```

Here are the summarized steps for compilation:

- Enter the location where the audio recording and storage on the microSD card test project is stored

```
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_record_sdcard
```

- Execute the prebuild script, select the target chip, automatically setup IDF Action Extension

On Linux / macOS, run following command:
```bash/zsh
source prebuild.sh
```

On Windows, run following command:
```powershell
.\prebuild.ps1
```

- Use `esp_board_manager` to select supported board and custom board, be sure to see [ESP Board Manager](https://components.espressif.com/components/espressif/esp_board_manager), taking ESP32-S3-Korvo2 V3.1 as an example:

```
idf.py gen-bmgr-config -b esp32_s3_korvo2_v3
```

- Build the Example

```
idf.py build
```

- Flash the program and run the monitor tool to view serial output (replace PORT with the port name):

```
idf.py -p PORT flash monitor
```

- Exit the debugging interface using ``Ctrl-]``

## How to use the Example

### Functionality and Usage

- After the example starts running, the recorded data will be encoded and automatically stored on the microSD card, stop and exit after recording is complete, with the following output:

```c
I (853) main_task: Calling app_main()
I (855) pm: Frequency switching config: CPU_MAX: 80, APB_MAX: 80, APB_MIN: 80, Light sleep: DISABLED
I (864) REC_SDCARD: [ 1 ] Setup peripheral
I (868) DEV_FATFS_SDCARD: slot_config: cd=-1, wp=-1, clk=15, cmd=7, d0=4, d1=-1, d2=-1, d3=-1, d4=-1, d5=-1, d6=-1, d7=-1, width=1, flags=0x0
I (881) gpio: GPIO[15]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (889) gpio: GPIO[7]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (897) gpio: GPIO[4]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (953) DEV_FATFS_SDCARD: Filesystem mounted, base path: /sdcard
I (954) BOARD_MANAGER: Device fs_sdcard initialized
I (954) DEV_AUDIO_CODEC: ADC is ENABLED
W (957) i2s_tdm: the current mclk multiple cannot perform integer division (slot_num: 3, slot_bits: 16)
HINT: Please adjust the mclk multiple to get the accurate sample rate.
For example, if you're using 24-bit slot width or enabled 3 slots, then the mclk multiple should be a multiple of 3, otherwise the sample rate will be inaccurate.
W (966) i2s_tdm: the current mclk multiple cannot perform integer division (slot_num: 3, slot_bits: 16)
HINT: Please adjust the mclk multiple to get the accurate sample rate.
For example, if you're using 24-bit slot width or enabled 3 slots, then the mclk multiple should be a multiple of 3, otherwise the sample rate will be inaccurate.
I (975) PERIPH_I2S: I2S[0] TDM, RX, ws: 45, bclk: 9, dout: 8, din: 10
I (981) PERIPH_I2S: I2S[0] initialize success: 0x3c12a734
I (986) DEV_AUDIO_CODEC: Init audio_adc, i2s_name: i2s_audio_in, i2s_rx_handle:0x3c12a734, i2s_tx_handle:0x0, data_if: 0x3fcb1fec
I (998) PERIPH_I2C: i2c_new_master_bus initialize success
I (1007) ES7210: Work in Slave mode
I (1015) ES7210: Enable ES7210_INPUT_MIC1
I (1019) ES7210: Enable ES7210_INPUT_MIC2
I (1023) ES7210: Enable ES7210_INPUT_MIC3
I (1028) ES7210: Enable TDM mode
I (1032) DEV_AUDIO_CODEC: Successfully initialized codec: audio_adc
I (1032) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fcb2254, chip:es7210
I (1034) BOARD_MANAGER: Device audio_adc initialized
I (1039) BOARD_DEVICE: Device handle audio_adc found, Handle: 0x3fcad8cc TO: 0x3fcad8cc
I (1050) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:1
I (1052) I2S_IF: TDM Mode 0 bits:16/16 channel:2 sample_rate:48000 mask:1
I (1060) ES7210: Bits 8
I (1070) ES7210: Enable ES7210_INPUT_MIC1
I (1074) ES7210: Enable ES7210_INPUT_MIC2
I (1078) ES7210: Enable ES7210_INPUT_MIC3
I (1082) ES7210: Enable TDM mode
I (1089) ES7210: Unmuted
I (1089) Adev_Codec: Open codec device OK
I (1089) REC_SDCARD: [ 2 ] Register all the elements and set audio information to record codec device
I (1094) ESP_GMF_POOL: Registered items on pool:0x3c12ae28, app_main-127
I (1100) ESP_GMF_POOL: IO, Item:0x3c12aedc, H:0x3c12ae3c, TAG:io_codec_dev
I (1106) ESP_GMF_POOL: IO, Item:0x3c12af9c, H:0x3c12aeec, TAG:io_file
I (1112) ESP_GMF_POOL: EL, Item:0x3c12b0a0, H:0x3c12afac, TAG:aud_enc
I (1118) REC_SDCARD: [ 3 ] Create audio pipeline
I (1123) REC_SDCARD: [ 3.1 ] Set audio url to record
I (1127) REC_SDCARD: [ 3.2 ] Reconfig audio encoder type by url and audio information and report information to the record pipeline
W (1139) ESP_GMF_PIPELINE: There is no thread for add jobs, pipe:0x3c12b0b0, tsk:0x0, [el:aud_enc-0x3c12b0f4]
I (1149) REC_SDCARD: [ 3.3 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (1160) ESP_GMF_TASK: Waiting to run... [tsk:gmf_rec-0x3fcb0aac, wk:0x0, run:0]
I (1166) ESP_GMF_TASK: Waiting to run... [tsk:gmf_rec-0x3fcb0aac, wk:0x3c12b3f8, run:0]
I (1174) REC_SDCARD: [ 3.4 ] Create envent group and listening event from pipeline
I (1181) REC_SDCARD: [ 4 ] Start audio_pipeline
I (1185) ESP_GMF_FILE: Open, dir:2, uri:/sdcard/esp_gmf_rec001.aac
I (1192) REC_SDCARD: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c12b0b0, type:8192, sub:ESP_GMF_EVENT_STATE_OPENING, payload:0x0, size:0,0x0
I (1207) REC_SDCARD: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c12b0f4, type:12288, sub:ESP_GMF_EVENT_STATE_INITIALIZED, payload:0x3c13a6b4, size:16,0x0
I (1219) ESP_GMF_AENC: Open, type:AAC, acquire in frame: 2048, out frame: 968
I (1225) REC_SDCARD: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c12b0f4, type:8192, sub:ESP_GMF_EVENT_STATE_RUNNING, payload:0x0, size:0,0x0
I (1239) ESP_GMF_TASK: One times job is complete, del[wk:0x3c12b3f8, ctx:0x3c12b0f4, label:aud_enc_open]
I (1248) ESP_GMF_PORT: ACQ IN, new self payload:0x3c12b3f8, port:0x3c12b288, el:0x3c12b0f4-aud_enc
I (1263) REC_SDCARD: [ 5 ] Wait for a while to stop record pipeline
I (6872) : ┌───────────────────┬──────────┬─────────────┬─────────┬──────────┬───────────┬────────────┬───────┐
I (6888) : │ Task              │ Core ID  │ Run Time    │ CPU     │ Priority │ Stack HWM │ State      │ Stack │
I (6905) : ├───────────────────┼──────────┼─────────────┼─────────┼──────────┼───────────┼────────────┼───────┤
I (6927) : │ IDLE0             │ 0        │ 506963      │  25.26% │ 0        │ 720       │ Ready      │ Intr  │
I (6940) : │ gmf_rec           │ 0        │ 492176      │  24.52% │ 5        │ 37072     │ Blocked    │ Extr  │
I (6950) : │ sys_monitor       │ 0        │ 4469        │   0.22% │ 1        │ 3180      │ Running    │ Extr  │
I (6963) : │ main              │ 0        │ 0           │   0.00% │ 1        │ 1368      │ Blocked    │ Intr  │
I (6972) : │ ipc0              │ 0        │ 0           │   0.00% │ 24       │ 528       │ Suspended  │ Intr  │
I (6984) : ├───────────────────┼──────────┼─────────────┼─────────┼──────────┼───────────┼────────────┼───────┤
I (7011) : │ IDLE1             │ 1        │ 1004007     │  50.02% │ 0        │ 792       │ Ready      │ Intr  │
I (7024) : │ ipc1              │ 1        │ 0           │   0.00% │ 24       │ 536       │ Suspended  │ Intr  │
I (7034) : ├───────────────────┼──────────┼─────────────┼─────────┼────────────┼──────────┼────────────┼───────┤
I (7062) : │ Tmr Svc           │ 7fffffff │ 0           │   0.00% │ 1        │ 1360      │ Blocked    │ Intr  │
I (7072) : └───────────────────┴──────────┴─────────────┴─────────┴──────────┴───────────┴────────────┴───────┘
I (7105) MONITOR: Func:sys_monitor_task, Line:25, MEM Total:7349652 Bytes, Inter:318963 Bytes, Dram:318963 Bytes

I (11283) ESP_GMF_CODEC_DEV: CLose, 0x3c12b1e8, pos = 1028096/0
I (11283) ESP_GMF_FILE: CLose, 0x3c12b2c8, pos = 120509/0
I (11292) ESP_GMF_TASK: One times job is complete, del[wk:0x3c12b41c, ctx:0x3c12b0f4, label:aud_enc_close]
I (11292) REC_SDCARD: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c12b0b0, type:8192, sub:ESP_GMF_EVENT_STATE_STOPPED, payload:0x0, size:0,0x0
I (11305) ESP_GMF_TASK: Waiting to run... [tsk:gmf_rec-0x3fcb0aac, wk:0x0, run:0]
I (11313) ESP_GMF_TASK: Waiting to run... [tsk:gmf_rec-0x3fcb0aac, wk:0x0, run:0]
I (11320) REC_SDCARD: [ 6 ] Destroy all the resources
W (11325) GMF_SETUP_AUD_CODEC: Unregistering default encoder
I (11330) BOARD_DEVICE: Deinit device audio_adc ref_count: 0 device_handle:0x3fcad8cc
I (11342) BOARD_DEVICE: Device audio_adc config found: 0x3c0ca4a4 (size: 88)
I (11344) BOARD_PERIPH: Deinit peripheral i2s_audio_in ref_count: 0
E (11350) i2s_common: i2s_channel_disable(1200): the channel has not been enabled yet
W (11358) PERIPH_I2S: Caution: Releasing RX (0x0).
I (11362) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 0
I (11368) PERIPH_I2C: i2c_del_master_bus deinitialize
I (11373) BOARD_MANAGER: Device audio_adc deinitialized
I (11378) BOARD_DEVICE: Deinit device fs_sdcard ref_count: 0 device_handle:0x3fcad354
I (11386) gpio: GPIO[15]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (11394) gpio: GPIO[7]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (11402) gpio: GPIO[4]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (11411) BOARD_MANAGER: Device fs_sdcard deinitialized
I (11416) main_task: Returned from app_main()
```
