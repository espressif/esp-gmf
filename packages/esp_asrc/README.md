# ESP-ASRC

- [![Component Registry](https://components.espressif.com/components/espressif/esp_asrc/badge.svg)](https://components.espressif.com/components/espressif/esp_asrc)
- [中文](./README_CN.md)

The ASRC (Audio Sample Rate Converter) module is a high-performance audio data format conversion unit that supports sample rate, bit depth, and channel count conversion, enabling format alignment and adaptation between different audio systems. It uses a hardware–software cooperative architecture: on chips with an ASRC peripheral, the module can schedule hardware resources according to configuration for low-latency, high-efficiency real-time conversion; when hardware is unavailable, an optimized software path maintains functionality. Based on current chip capabilities and the configured strategy, the module selects a processing path aimed at optimal resource use. The interface is clear, configuration is flexible, and the module integrates cleanly into embedded audio frameworks, audio middleware, or higher-level applications that require audio format adaptation.

## Features

- Sample rate: `src_info.sample_rate` / `dest_info.sample_rate` are integer multiples of **4000 Hz** or **11025 Hz**, each **≤ 192000 Hz** (subject to driver validation).
- Bit depth: **8** (unsigned PCM), **16 / 24 / 32** (signed, interleaved).
- Channels: any `channel` in `esp_asrc_aud_info_t`; optional `weight` when the channel layout changes, with `weight_len == src_ch × dest_ch`.
- Modes: `ESP_ASRC_PERF_TYPE_AUTO`, `ESP_ASRC_PERF_TYPE_HW_ONLY`, `ESP_ASRC_PERF_TYPE_SW_SPEED`, `ESP_ASRC_PERF_TYPE_SW_MEMORY` (see `esp_asrc_types.h`).
- Software path: when not `ESP_ASRC_PERF_TYPE_HW_ONLY`, `complexity` is **1–3**.
- Hardware path: `timeout_ms` must be **> 0** or **−1** (long wait); **`0` is invalid** and `esp_asrc_open()` returns `ESP_ASRC_ERR_INVALID_PARAMETER`.
- Buffers: call `esp_asrc_get_buffer_alignment()` first, then allocate input/output buffers with `esp_asrc_align_alloc()` using the returned alignment values.

## Directory layout

| Path | Description |
|------|---------------|
| `include/` | Public API: `esp_asrc.h`, `esp_asrc_types.h` |
| `src/`, `inc/` | Core scheduling and internal utilities |
| `hw/` | Peripheral driver, capability checks, HW op tables |
| `sw/` | Software conversion op tables |
| `examples/asrc_demo/` | Example project (build/flash: see README in that folder) |
| `test_apps/` | Unity tests and pytest entry point |

## Quick start

- **ESP-IDF**: use an IDF / toolchain version supported by your esp-gmf branch (performance numbers below were taken on ESP32-H4 with **IDF v6.1**).
- **Dependency**: `espressif/esp_audio_effects` from the root `idf_component.yml` (a monorepo may set `override_path`).
- **Example**: open `examples/asrc_demo/README.md`, select the target chip, then build and flash; reference code: [`examples/asrc_demo/main/asrc_example.c`](./examples/asrc_demo/main/asrc_example.c).

## Notes

- **PSRAM + hardware acceleration**: input/output buffers in PSRAM must satisfy cache-line alignment and size rules; call `esp_asrc_get_buffer_alignment()` first and pass `inbuf_*` / `outbuf_*` to `esp_asrc_align_alloc()`.
- **No hot parameter updates**: to change sample rate, bit depth, or channel count, use `esp_asrc_close()` → update `esp_asrc_cfg_t` → `esp_asrc_open()`.
- **Non-integer rate ratio**: when the rate ratio is not an integer, `esp_asrc_process()` may return a slightly varying output sample count per call (internal residual buffering).
- **Stereo**: CPU load is about **2×** the mono figures in the table below.

## Performance

Test setup:

| Chip     | IDF   | CPU    | SPI RAM | Flash |
|----------|-------|--------|---------|-------|
| ESP32-H4 | v6.1  | 96 MHz | 64 MHz  | QIO   |

Mono **16-bit** PCM, hardware vs software ASRC (representative sweep):

| Input           | Output          | Mode    | Memory (KB) | CPU (%) |
|-----------------|-----------------|---------|-------------|---------|
| 8kHz/1ch/16bit  | 16kHz/1ch/16bit | Use HW  | 0.6         | 0.8     |
|                 |                 | Only SW | 0.4         | 2.0     |
| 8kHz/1ch/16bit  | 24kHz/1ch/16bit | Use HW  | 0.5         | 0.9     |
|                 |                 | Only SW | 0.5         | 2.9     |
| 8kHz/1ch/16bit  | 32kHz/1ch/16bit | Use HW  | 0.5         | 1.1     |
|                 |                 | Only SW | 0.5         | 3.8     |
| 8kHz/1ch/16bit  | 44kHz/1ch/16bit | Use HW  | 0.5         | 1.3     |
|                 |                 | Only SW | 15.9        | 11.6    |
| 8kHz/1ch/16bit  | 48kHz/1ch/16bit | Use HW  | 0.5         | 1.4     |
|                 |                 | Only SW | 0.6         | 5.6     |
| 16kHz/1ch/16bit | 8kHz/1ch/16bit  | Use HW  | 0.5         | 1.0     |
|                 |                 | Only SW | 0.4         | 1.7     |
| 16kHz/1ch/16bit | 24kHz/1ch/16bit | Use HW  | 0.5         | 1.4     |
|                 |                 | Only SW | 0.5         | 6.3     |
| 16kHz/1ch/16bit | 32kHz/1ch/16bit | Use HW  | 0.5         | 1.5     |
|                 |                 | Only SW | 0.4         | 4.0     |
| 16kHz/1ch/16bit | 44kHz/1ch/16bit | Use HW  | 0.5         | 1.8     |
|                 |                 | Only SW | 15.9        | 11.6    |
| 16kHz/1ch/16bit | 48kHz/1ch/16bit | Use HW  | 0.5         | 1.8     |
|                 |                 | Only SW | 0.5         | 5.8     |
| 24kHz/1ch/16bit | 8kHz/1ch/16bit  | Use HW  | 0.5         | 1.4     |
|                 |                 | Only SW | 0.5         | 2.5     |
| 24kHz/1ch/16bit | 16kHz/1ch/16bit | Use HW  | 0.5         | 1.6     |
|                 |                 | Only SW | 0.5         | 3.7     |
| 24kHz/1ch/16bit | 32kHz/1ch/16bit | Use HW  | 0.5         | 2.0     |
|                 |                 | Only SW | 0.5         | 8.4     |
| 24kHz/1ch/16bit | 44kHz/1ch/16bit | Use HW  | 0.5         | 2.2     |
|                 |                 | Only SW | 5.6         | 11.6    |
| 24kHz/1ch/16bit | 48kHz/1ch/16bit | Use HW  | 0.5         | 2.3     |
|                 |                 | Only SW | 0.4         | 5.9     |
| 32kHz/1ch/16bit | 8kHz/1ch/16bit  | Use HW  | 0.5         | 1.8     |
|                 |                 | Only SW | 0.5         | 3.3     |
| 32kHz/1ch/16bit | 16kHz/1ch/16bit | Use HW  | 0.5         | 2.0     |
|                 |                 | Only SW | 0.4         | 3.5     |
| 32kHz/1ch/16bit | 24kHz/1ch/16bit | Use HW  | 0.5         | 2.2     |
|                 |                 | Only SW | 0.5         | 5.0     |
| 32kHz/1ch/16bit | 44kHz/1ch/16bit | Use HW  | 0.5         | 2.6     |
|                 |                 | Only SW | 15.9        | 11.7    |
| 32kHz/1ch/16bit | 48kHz/1ch/16bit | Use HW  | 0.5         | 2.7     |
|                 |                 | Only SW | 0.5         | 12.6    |
| 44kHz/1ch/16bit | 8kHz/1ch/16bit  | Use HW  | 0.5         | 2.4     |
|                 |                 | Only SW | 8.2         | 5.8     |
| 44kHz/1ch/16bit | 16kHz/1ch/16bit | Use HW  | 0.5         | 2.6     |
|                 |                 | Only SW | 11.0        | 6.5     |
| 44kHz/1ch/16bit | 24kHz/1ch/16bit | Use HW  | 0.5         | 2.8     |
|                 |                 | Only SW | 5.7         | 7.1     |
| 44kHz/1ch/16bit | 32kHz/1ch/16bit | Use HW  | 0.5         | 3.0     |
|                 |                 | Only SW | 11.6        | 7.5     |
| 44kHz/1ch/16bit | 48kHz/1ch/16bit | Use HW  | 0.5         | 3.4     |
|                 |                 | Only SW | 6.0         | 12.8    |
| 48kHz/1ch/16bit | 8kHz/1ch/16bit  | Use HW  | 0.5         | 2.6     |
|                 |                 | Only SW | 0.5         | 4.9     |
| 48kHz/1ch/16bit | 16kHz/1ch/16bit | Use HW  | 0.5         | 2.8     |
|                 |                 | Only SW | 0.5         | 5.0     |
| 48kHz/1ch/16bit | 24kHz/1ch/16bit | Use HW  | 0.5         | 3.0     |
|                 |                 | Only SW | 0.4         | 5.2     |
| 48kHz/1ch/16bit | 32kHz/1ch/16bit | Use HW  | 0.5         | 3.2     |
|                 |                 | Only SW | 0.5         | 7.5     |
| 48kHz/1ch/16bit | 44kHz/1ch/16bit | Use HW  | 0.5         | 3.5     |
|                 |                 | Only SW | 5.6         | 11.6    |

- **Use HW**: on-chip ASRC handles applicable rate conversion; for the same row, CPU is lower than **Only SW**.
- **Only SW**: pure software path; CPU can be higher for some rate ratios.
- **CPU (%)**: average CPU time share over **10 s** of continuous processing (mono).
- **Stereo**: CPU is about **2×** the mono values in the table above.

## SoC compatibility

| Chip      | v1.0.0 | Notes               |
|-----------|--------|---------------------|
| ESP32     | ✅     | Software only       |
| ESP32-S2  | ✅     | Software only       |
| ESP32-S3  | ✅     | Software only       |
| ESP32-C2  | ✅     | Software only       |
| ESP32-C3  | ✅     | Software only       |
| ESP32-C5  | ✅     | Software only       |
| ESP32-C6  | ✅     | Software only       |
| ESP32-P4  | ✅     | Software only       |
| ESP32-H4  | ✅     | Hardware + software |
| ESP32-S31 | ✅     | Hardware + software |

## FAQ

1) **Does ASRC impose buffer or alignment requirements?**

- With **hardware acceleration** and buffers in **PSRAM**: length and address must satisfy DMA **cache-line** alignment rules.
- **Software** path: `malloc()`-style allocation is generally acceptable; throughput may differ from the hardware path.
- Prefer **`esp_asrc_get_buffer_alignment()` + `esp_asrc_align_alloc()`** so allocations match the active mode.

2) **Can sample rate, bit depth, or channel count be changed while the handle is open?**

- **No.** Call `esp_asrc_close()`, update `esp_asrc_cfg_t`, then `esp_asrc_open()`.

3) **What does each `ESP_ASRC_PERF_TYPE_*` mean?**

| Symbol | Behavior |
|--------|----------|
| `ESP_ASRC_PERF_TYPE_AUTO` | Selects HW/SW based on capability and load. |
| `ESP_ASRC_PERF_TYPE_SW_SPEED` | Software only; internal buffers prefer **IRAM**. |
| `ESP_ASRC_PERF_TYPE_SW_MEMORY` | Software only; internal buffers prefer **PSRAM**. |
| `ESP_ASRC_PERF_TYPE_HW_ONLY` | Hardware only; `esp_asrc_open()` fails if ASRC is not supported. |

For multi-target builds, **`ESP_ASRC_PERF_TYPE_AUTO`** is recommended by default.
