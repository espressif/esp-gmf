# GMF FFT

- [![Component Registry](https://components.espressif.com/components/espressif/gmf_fft/badge.svg)](https://components.espressif.com/components/espressif/gmf_fft)
- [中文版](README_CN.md)

`gmf_fft` is an ESP-IDF component for fixed-point Q15 real FFT / IFFT processing on ESP-series chips. It includes a generic C implementation and PIE vector assembly optimizations for Xtensa / RISC-V targets, delivering lower latency than a pure software DIT implementation at typical FFT sizes. By using the Hermitian symmetry of real input signals, the forward transform keeps only `N/2 + 1` frequency bins, reducing frequency-domain storage and computation cost for no-FPU or latency-sensitive embedded scenarios.

## Features

- Supports real FFT / IFFT sizes from 32 to 8192 points, power-of-two only. On PIE targets, hardware bit-reverse covers real `N` up to 2048 (10-bit `ESP.FFT.BITREV` / `EE.BITREV`); `N=4096` and `N=8192` fall back to a software bit-reverse while butterflies stay in PIE.
- Uses Q15 `int16_t` data to reduce compute and memory cost.
- A handle can be shared by multiple threads when each thread uses its own `data` buffer.
- Optional HP APIs (`esp_gmf_fft_forward_hp` / `inverse_hp`) use block floating point internally. Inverse output is Q15 and does not need an `N/4` multiply.

## Supported Targets

- v0.1.0 supports ESP32-S3 and ESP32-P4.
- v1.0.0 supports all ESP32 series chips.

## Breaking changes since v1.1.0

- ESP32-S31 adds `CONFIG_GMF_FFT_S31_USE_ASM` (default `n`) to choose between PIE assembly and the portable C implementation. PIE on ESP32-S31 exists only on Core 1, so this option lets FFT/IFFT run on other cores when assembly is disabled.
  - Enabled: related FFT/IFFT APIs must run on **Core 1**.
  - Disabled: APIs can run on any core, at lower performance than PIE assembly; see the table below.

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

The forward input is `N` real samples. The `data` buffer must contain `ESP_GMF_FFT_BUFFER_SIZE(N)` `int16_t` elements. Both API pairs use the same packed layout: `data[0]` for DC real, `data[1]` for Nyquist real, and `data[2k]`, `data[2k + 1]` for the real and imaginary parts of bin `k`.

The same `handle` can be used with either pair. Do not mix them: `inverse` only accepts a half-spectrum from `forward`, and `inverse_hp` only accepts a half-spectrum from `forward_hp` on the same handle.

| Usage | `esp_gmf_fft_forward` / `inverse` | `esp_gmf_fft_forward_hp` / `inverse_hp` |
|-------|-----------------------------------|-----------------------------------------|
| Arguments | `handle` and `data` | Same |
| Per-stage scale | Fixed `>>1` | `>>1` only when `max_abs >= 8192` |
| Round-trip | About `4/N`; multiply by `N/4` for identity | Q15 output, approximately the original input |
| Typical use | Faster; sparse spectral peaks | Higher SNR; restore time-domain amplitude |

HP example (no `N/4` multiply):

```c
esp_gmf_fft_forward_hp(handle, data);
esp_gmf_fft_inverse_hp(handle, data);
/* data is Q15, approximately the original input */
```

## Memory Usage

- User data buffer: `ESP_GMF_FFT_BUFFER_SIZE(N)` `int16_t` elements, about `2 * (N + 2)` bytes.
- Internal handle twiddle tables: about `3 * N` bytes, plus a small plan structure.

## Accuracy And Performance

Test conditions: `-O2`, Unity test framework. The forward reference is a floating-point DFT with the same half-spectrum scaling as the fixed-point implementation. ESP32-S3 / ESP32-P4 use the PIE assembly implementation; ESP32-S31 is measured for both portable C (`GMF_FFT_S31_USE_ASM=n`) and PIE assembly (`GMF_FFT_S31_USE_ASM=y`). HP columns are a single `esp_gmf_fft_forward_hp` / `inverse_hp` call. HP inverse includes folding leftover BFP bits back to Q15.

| N_real | ESP32-S3 forward | ESP32-S3 inverse | ESP32-S3 HP forward | ESP32-S3 HP inverse | ESP32-P4 forward | ESP32-P4 inverse | ESP32-P4 HP forward | ESP32-P4 HP inverse |
|-------:|-----------------:|-----------------:|--------------------:|--------------------:|-----------------:|-----------------:|--------------------:|--------------------:|
|     32 |            17 us |            21 us |              77 us |              33 us |             6 us |             2 us |               4 us |               8 us |
|    512 |            22 us |            23 us |              95 us |              76 us |             8 us |             8 us |              27 us |              24 us |
|   1024 |            28 us |            36 us |             148 us |             124 us |            16 us |            16 us |              53 us |              45 us |

| N_real | ESP32-S31 C forward | ESP32-S31 C inverse | ESP32-S31 C HP forward | ESP32-S31 C HP inverse | ESP32-S31 PIE forward | ESP32-S31 PIE inverse | ESP32-S31 PIE HP forward | ESP32-S31 PIE HP inverse |
|-------:|--------------------:|--------------------:|-----------------------:|-----------------------:|----------------------:|----------------------:|-------------------------:|-------------------------:|
|     32 |              21 us |              12 us |                 20 us |                 36 us |                  7 us |                  2 us |                   16 us |                    9 us |
|    512 |             277 us |             271 us |                491 us |                507 us |                 10 us |                 10 us |                   44 us |                   32 us |
|   1024 |             601 us |             595 us |               1085 us |               1124 us |                 21 us |                 21 us |                   78 us |                   60 us |

Regular round-trip tests scale the inverse output by `N/4`. With peak input 10000, worst scaled errors for `N=32` / `512` / `1024` were 6 / 115 / 200 on ESP32-P4 and ESP32-S31 PIE, and 11 / 183 / 528 on ESP32-S3 and ESP32-S31 C. HP round-trip compares Q15 with no `N/4`; worst errors were 2 / 4 / 4 on P4 and S31 PIE, 4 / 39 / 78 on S3, and 4 / 58 / 96 on S31 C.

## Comparison With dl_fft And ESP-DSP (ESP32-P4)

Board: ESP32-P4 rev v3.2, 400 MHz, ESP-IDF v6.1, `-O2`. Golden reference is an unnormalized double-precision real DFT of `x/32768`. Forward SNR is least-squares gain-matched. Round-trip SNR uses Q15 for `gmf_fft_hp` and `N/4` for `gmf_fft`. Signals: cosine (bin 8, amplitude 10000), two-tone (8000+4000), random (~0.25 FS). Timing is 20-loop average including memcpy. `dsps_fft2r_sc16` is ESP-DSP's N-point complex Q15 FFT (imaginary part set to 0).

```bash
cd examples/fft_p4_compare_dl
idf.py set-target esp32p4
idf.py -p PORT flash
# wait for FFT_COMPARE_DONE
```

Full tables: [`examples/fft_p4_compare_dl/TEST_REPORT_CN.md`](examples/fft_p4_compare_dl/TEST_REPORT_CN.md).

**N=1024 accuracy (forward SNR / round-trip SNR, dB)**

| Signal | `gmf_fft_hp` | `dl_rfft_s16_hp` | `dsps_fft2r_sc16` |
|--------|----------------|------------------|-------------------|
| cosine | 82.28 / **76.60** | **84.66** / 73.18 | 70.78 / 17.20 |
| two_tone | **78.80 / 74.64** | 77.91 / 73.44 | 68.30 / 14.65 |
| random | **69.46 / 67.64** | 67.10 / 65.59 | 19.55 / 14.57 |

**Latency (forward / inverse, µs, cosine)**

| N | `gmf_fft` | `gmf_fft_hp` | `dl_rfft_s16_hp` | `dsps_fft2r_sc16` |
|---|-----------|----------------|------------------|-------------------|
| 128 | **2 / 2** | 7 / 7 | 7 / 7 | 10 / 12 |
| 256 | **4 / 4** | **12 / 12** | 14 / 14 | 21 / 25 |
| 512 | **8 / 9** | **23 / 23** | 28 / 28 | 44 / 51 |
| 1024 | **17 / 18** | **50 / 47** | 57 / 57 | 91 / 104 |

At N=1024, `gmf_fft` remains the fastest path at about 17/18 µs. `gmf_fft_hp` is about 50/47 µs (0.88× `dl_rfft_s16_hp` on both forward and inverse). Inverse folds leftover BFP bits to Q15 in PIE; sparse cosine inverse is 47 µs vs 57 µs for `dl_rfft_s16_hp`. Broadband random inverse stays about 43 µs.

## Build And Test

```bash
cd test_apps

idf.py set-target esp32p4
idf.py build flash monitor

idf.py set-target esp32s3
idf.py build flash monitor
```
