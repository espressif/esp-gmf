# GMF FFT

- [![Component Registry](https://components.espressif.com/components/espressif/gmf_fft/badge.svg)](https://components.espressif.com/components/espressif/gmf_fft)
- [中文版](README_CN.md)

`gmf_fft` is an ESP-IDF component for fixed-point Q15 real FFT / IFFT processing on ESP-series chips. It includes a generic C implementation and PIE vector assembly optimizations for Xtensa / RISC-V targets, delivering lower latency than a pure software DIT implementation at typical FFT sizes. By using the Hermitian symmetry of real input signals, the forward transform keeps only `N/2 + 1` frequency bins, reducing frequency-domain storage and computation cost for no-FPU or latency-sensitive embedded scenarios.

## Features

- Supports real FFT / IFFT sizes from 32 to 8192 points, power-of-two only.
- Uses Q15 `int16_t` data to reduce compute and memory cost.
- A handle can be shared by multiple threads when each thread uses its own `data` buffer.

## Supported Targets

v0.1.0 supports ESP32-S3 and ESP32-P4.

## API

```c
#include "esp_gmf_fft.h"

int16_t data[ESP_GMF_FFT_BUFFER_SIZE(256)] = {0};
esp_gmf_fft_handle_t handle = NULL;
const esp_gmf_fft_cfg_t cfg = {
    .n_fft = 256,
    .fft_type = ESP_GMF_FFT_TYPE_REAL_Q15,
};

esp_gmf_fft_init(&cfg, &handle);
esp_gmf_fft_forward(handle, data);
esp_gmf_fft_inverse(handle, data);
esp_gmf_fft_deinit(&handle);
```

The forward input is `N` real samples. The `data` buffer must contain `ESP_GMF_FFT_BUFFER_SIZE(N)` `int16_t` elements. The packed output layout is `data[0]` for DC real, `data[1]` for Nyquist real, and `data[2k]`, `data[2k + 1]` for the real and imaginary parts of bin `k`.

A `forward -> inverse` round trip is scaled by about `4/N`; the library does not compensate this gain. Multiply the inverse output by `N/4` when identity scaling is required.

## Memory Usage

- User data buffer: `ESP_GMF_FFT_BUFFER_SIZE(N)` `int16_t` elements, about `2 * (N + 2)` bytes.
- Internal handle twiddle tables: about `3 * N` bytes, plus a small plan structure.

## Accuracy And Performance

Test conditions: ESP32-S3 / ESP32-P4, PIE assembly implementation, `-O2`, Unity test framework. The forward reference is a floating-point DFT with the same half-spectrum scaling as the fixed-point implementation.

| N_real | ESP32-S3 forward | ESP32-S3 inverse | ESP32-P4 forward | ESP32-P4 inverse |
|-------:|-----------------:|-----------------:|-----------------:|-----------------:|
|     32 |             9 us |            22 us |             3 us |             7 us |
|    512 |            40 us |            23 us |            21 us |            22 us |
|   1024 |            51 us |            36 us |            43 us |            44 us |

Round-trip tests scale the inverse output by `N/4`. The observed worst scaled errors were 10 for `N=32`, 181 for `N=512`, and 528 for `N=1024` with a peak input amplitude of 10000.

## Build And Test

```bash
cd test_apps

idf.py set-target esp32p4
idf.py build flash monitor

idf.py set-target esp32s3
idf.py build flash monitor
```

## Difference From ESP-DSP FFT

| Item | gmf_fft | ESP-DSP FFT |
|------|---------|-------------|
| Data format | Q15 `int16_t` | `int16_t` and `float32` |
| Transform type | Real FFT / IFFT with half-spectrum output | General FFT APIs |
| Implementation | PIE vector assembly for ESP32-S3 / ESP32-P4 | General DSP implementation and platform optimizations |
| Output layout | Compact `N/2 + 1` half-spectrum | Depends on the selected ESP-DSP API |
| Target scenario | No-FPU, low-latency, deterministic real-time processing | General signal processing |
