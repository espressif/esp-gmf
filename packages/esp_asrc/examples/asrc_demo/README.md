# ASRC Audio Format Conversion Demo

- [中文版本](./README_CN.md)
- Difficulty: ⭐

## Overview

This example demonstrates how to use the ASRC (Audio Sample Rate Converter) module for audio format conversion, including sample rate conversion, channel conversion, and bit depth conversion.

- The demo reads an embedded PCM file, performs format conversion through ASRC, and counts the total output bytes.
- Supports software ASRC (all chip series) and hardware ASRC.

### Typical Scenarios

- Upsampling low-sample-rate mono PCM data from a recording device to a standard playback format (e.g., 48 kHz stereo).
- Real-time audio format alignment between systems operating at different sample rates.

## Environment Setup

### Hardware Requirements

- **Development Board**: ESP32-S3 by default; other ESP series chips are also supported.
- **Resource Requirements**: No special peripherals required; uses only on-chip SRAM and optional PSRAM.

### Default IDF Branch

This example supports IDF release/v6.1 or later.

## Build and Flash

### Prerequisites

Before building this example, ensure that the ESP-IDF environment is configured. If not configured, run the following scripts in the ESP-IDF root directory. For complete steps on configuring and using ESP-IDF, please refer to the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/index.html).

```bash
./install.sh
. ./export.sh
```

### Enter Project Directory

```bash
cd $YOUR_ESP_ASRC_PATH/example/asrc_demo
```

### Set Target Chip

```bash
idf.py set-target esp32s3
```

### Project Configuration

This example requires no additional menuconfig settings. It demonstrates converting **16 kHz mono** to **48 kHz stereo** by default. To adjust conversion parameters, modify the `asrc_cfg` structure in `main/asrc_example.c` directly:

| Parameter | Source Format | Destination Format |
|-----------|---------------|--------------------|
| Sample Rate | 16000 Hz | 48000 Hz |
| Channels | 1 (Mono) | 2 (Stereo) |
| Bit Depth | 16 bit | 16 bit |

### Build and Flash

- Build the example:

```bash
idf.py build
```

- Flash and monitor (replace `PORT` with the actual serial port):

```bash
idf.py -p PORT flash monitor
```

- Use `Ctrl-]` to exit the monitor.

## How to Use the Example

### Functionality and Usage

- After power-on, the example runs automatically: initializes ASRC, reads the embedded PCM file (16 kHz, 1ch, 16bit), loops through `esp_asrc_process` to perform format conversion, prints statistics on completion, and releases all resources.
- No user interaction is required — monitor the serial log to confirm the result.

### Log Output

A normal run produces the following log in sequence: open ASRC, allocate buffers, process frames, and finish:

```
I (836) main_task: Calling app_main()
I (926) ASRC_EXAMPLE: ASRC process finished
I (926) main_task: Returned from app_main()
```

## Performance Modes

| Mode | Description |
|------|-------------|
| `ESP_ASRC_PERF_TYPE_AUTO` | Automatically select optimal mode (recommended) |
| `ESP_ASRC_PERF_TYPE_HW_ONLY` | Use hardware ASRC only |
| `ESP_ASRC_PERF_TYPE_SW_MEMORY` | Software ASRC, memory optimized |
| `ESP_ASRC_PERF_TYPE_SW_SPEED` | Software ASRC, speed optimized |

## Quality Levels

The `complexity` parameter controls the conversion quality of software ASRC:

| Level | Description |
|-------|-------------|
| 1 | Lowest quality, fastest speed |
| 2 | Medium quality, balanced performance |
| 3 | Highest quality, slowest speed |

> **Note**: The `complexity` parameter is ignored when using hardware ASRC.

## Supported Chips

| Chip Model | Software ASRC | Hardware ASRC |
|------------|---------------|---------------|
| ESP32 | ✅ | ❌ |
| ESP32-S2 | ✅ | ❌ |
| ESP32-S3 | ✅ | ❌ |
| ESP32-C2 | ✅ | ❌ |
| ESP32-C3 | ✅ | ❌ |
| ESP32-C5 | ✅ | ❌ |
| ESP32-C6 | ✅ | ❌ |
| ESP32-P4 | ✅ | ❌ |
| ESP32-H4 | ✅ | ✅ |
| ESP32-S31 | ✅ | ✅ |

## Troubleshooting

- **Memory allocation failed**: Check available heap memory; reduce `in_samples` (frames per call) or lower the `complexity` level.
- **Hardware ASRC unavailable**: Confirm the target is support hardware asrc and `perf_type` is set to `ESP_ASRC_PERF_TYPE_AUTO` or `ESP_ASRC_PERF_TYPE_HW_ONLY`.
- **Abnormal output byte count**: Verify that the `asrc_cfg` source format matches the embedded PCM file (16 kHz, 1ch, 16bit).
- **Timeout error**: Increase the `timeout_ms` parameter or check system load.

## Technical Support

Please use the following links for technical support:

- Technical support forum: [esp32.com](https://esp32.com/viewforum.php?f=20)
- Bug reports and feature requests: [GitHub issue](https://github.com/espressif/esp-gmf/issues)

We will get back to you as soon as possible.
