# Changelog

## v1.1.0

### Features

- Added high-precision APIs `esp_gmf_fft_forward_hp` / `esp_gmf_fft_inverse_hp`.

### Bug Fixes

- Fixed PIE bit-reverse for real `N=4096` and `N=8192`. Hardware `ESP.FFT.BITREV` / `EE.BITREV` only reverse 3–10 bits (real `N` <= 2048); those sizes now use a software bit-reverse while PIE butterflies are unchanged.

### Breaking Changes

- On `ESP32-S31`, added `CONFIG_GMF_FFT_S31_USE_ASM` (default `n`) to choose between PIE assembly and the portable C implementation, because PIE exists only on Core 1: when enabled, related FFT/IFFT APIs must run on Core 1; when disabled, they can run on any core at lower performance.

## v1.0.0

### Features

- Added scalar C fallback for targets without PIE assembly, extending support to all ESP32 series.
- Added ESP32-S31 PIE assembly support

## v0.1.0

### Features
- Q15 real FFT / IFFT (`esp_gmf_fft_*`), PIE assembly for ESP32-S3 and ESP32-P4.
- Forward outputs half-spectrum + 1 (`fftr` packed, `N/2 + 1` bins).
- Supports ESP32-S3 and ESP32-P4.
