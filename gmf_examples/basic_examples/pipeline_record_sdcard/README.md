# Recording Audio to microSD Card

- [中文版](./README_CN.md)
- Basic Example: ⭐

## Example Brief

This example demonstrates capturing audio from the codec via CODEC_DEV_RX IO, encoding it with an encoder element, and saving the encoded data to a microSD card via File IO.

- Single pipeline: `io_codec_dev` → `aud_enc` → `io_file`.
- Supports AAC, G711A, G711U, AMRNB, AMRWB, OPUS, ADPCM, PCM; default is AAC.
- CPU frequency is fixed at a lower value to reduce power, suitable for low-power voice recorders.

> [!NOTE]
> 1. Change `DEFAULT_CPU_FREQ_MHZ` for different CPU frequencies; see `rtc_clk_cpu_freq_mhz_to_config` for supported values per chip.
> 2. For power at different frequencies, see "Power consumption in other power modes" in the chip datasheet, e.g. [ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf).
> 3. To use default power management instead of fixing CPU frequency, see [ESP32 Power Management](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/power_management.html#esp32).

### Typical Scenarios

- Local recording to file: low-power voice recorder, meeting notes, etc.

## Environment Setup

### Hardware Required

- **Board**: ESP32-S3-Korvo V3 by default; other ESP audio boards are also supported.
- **Resource requirements**: microSD card, microphone, Audio ADC.

### Default IDF Branch

This example supports IDF release/v5.4 (>= v5.4.3) and release/v5.5 (>= v5.5.2).

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
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_record_sdcard
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

You can configure the example by editing the following in the source:

- `DEFAULT_RECORD_OUTPUT_URL`: output file path
- `DEFAULT_RECORD_SAMPLE_RATE`: sample rate (e.g. 16000 Hz)
- `DEFAULT_RECORD_BITS`: bit depth (e.g. 16 bits)
- `DEFAULT_RECORD_CHANNEL`: channel count
- `DEFAULT_MICROPHONE_GAIN`: recording microphone gain
- `DEFAULT_RECORD_DURATION_MS`: recording duration (e.g. 10000 ms)

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

- After power-on the example mounts the microSD card and initializes the recording codec, then starts recording.
- After the configured duration it stops and writes the encoded data to the microSD card (e.g. `/sdcard/esp_gmf_rec001.aac`), then releases resources.

### Log Output

- Normal run: peripheral init, register elements, create pipeline, set record URL, bind task, listen events, start pipeline, wait for stop and destroy resources. You can check loading at different CPU frequencies to confirm encoding runs correctly.

```c
I (853) main_task: Calling app_main()
I (855) pm: Frequency switching config: CPU_MAX: 80, APB_MAX: 80, APB_MIN: 80, Light sleep: DISABLED
I (864) REC_SDCARD: [ 1 ] Setup peripheral
I (1089) REC_SDCARD: [ 2 ] Register all the elements and set audio information to record codec device
I (1118) REC_SDCARD: [ 3 ] Create audio pipeline
I (1123) REC_SDCARD: [ 3.1 ] Set audio url to record
I (1127) REC_SDCARD: [ 3.2 ] Reconfig audio encoder type by url and audio information and report information to the record pipeline
I (1149) REC_SDCARD: [ 3.3 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (1174) REC_SDCARD: [ 3.4 ] Create envent group and listening event from pipeline
I (1181) REC_SDCARD: [ 4 ] Start audio_pipeline
I (1185) ESP_GMF_FILE: Open, dir:2, uri:/sdcard/esp_gmf_rec001.aac
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
I (7034) : ├───────────────────┼──────────┼─────────────┼─────────┼──────────┼───────────┼────────────┼───────┤
I (7062) : │ Tmr Svc           │ 7fffffff │ 0           │   0.00% │ 1        │ 1360      │ Blocked    │ Intr  │
I (7072) : └───────────────────┴──────────┴─────────────┴─────────┴──────────┴───────────┴────────────┴───────┘
I (7105) MONITOR: Func:sys_monitor_task, Line:25, MEM Total:7349652 Bytes, Inter:318963 Bytes, Dram:318963 Bytes

I (11283) ESP_GMF_FILE: CLose, 0x3c12b2c8, pos = 120509/0
I (11292) REC_SDCARD: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c12b0b0, type:8192, sub:ESP_GMF_EVENT_STATE_STOPPED, payload:0x0, size:0,0x0
I (11320) REC_SDCARD: [ 6 ] Destroy all the resources
I (11416) main_task: Returned from app_main()
```

## Troubleshooting

### microSD card not mounted or write failed

- Ensure the microSD card is inserted and formatted as FAT32.
- If the log shows file open failure, check capacity and write-protect.

### No recording or encode failed

- Ensure the board is configured for the recording codec (e.g. ES7210) and I2S.
- If encoder init fails, check in menuconfig that the chosen encode format is enabled.

### Reference

- To support encapsulate more audio containers or want video support refer [esp_capture](../../../packages/esp_capture/README_CN.md)

## Technical Support

For technical support, use the links below:

- Technical support: [esp32.com](https://esp32.com/viewforum.php?f=20) forum
- Issue reports and feature requests: [GitHub issue](https://github.com/espressif/esp-gmf/issues)

We will reply as soon as possible.
