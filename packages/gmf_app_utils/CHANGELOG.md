# Changelog

## v0.7.8

### Features

- Updated `esp_board_manager` to version v0.5.0
- Removed obsolete config `Target Board` (breaking change)

## v0.7.7

### Bug Fixes

- Fixed the esp32_lyrat_mini_v1_1 SD card mount failure issue

## v0.7.6

### Features

- Updated dependency `esp_board_manager` instead of `codec_board`

## 0.7.5

### Bug Fixes

- Fixed build issue on windows for path error

## v0.7.4

### Features

- Support C++ build

## 0.7.3

### Features

- Optimized memory usage by utilizing PSRAM for `sys_monitor` task stack

### Bug Fixes

- Fixed resource leakage in `esp_gmf_app_wifi_disconnect()`

## 0.7.2

- Updated `codec_board` dependency to v1.0.0

## 0.7.1

- Add board support for `ESP32_S3_KORVO_2L`, `ESP32_S3_EchoEar`, `ATOMS3_ECHO_BASE`

## v0.7.0

### Features

- Initial version of `gmf_app_utils`
