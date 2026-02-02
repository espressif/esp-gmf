# Changelog

## v0.7.5

### Features

- Added 8k sample rate support for the AEC element.

## v0.7.4~1

### Bug Fixes

- Removed CONFIG_AUDIO_BOARD and CODEC_I2C_BACKWARD_COMPATIBLE in sdkconfig.defaults

## v0.7.4

### Bug Fixes

- Fixed a crash issue that occurred when the AFE manager was destroyed.

## v0.7.3

### Features

- Added default sdkconfig for esp32p4 in wwe example

### Bug Fixes

- Fixed build error for esp32p4

## v0.7.2

### Features

- Added `esp_gmf_afe_trigger_wakeup` and `esp_gmf_afe_trigger_sleep` APIs to trigger wakeup and sleep events manually

## v0.7.1

### Features

- Increased `esp-sr` dependency to version 2.1.5

## v0.7.0

### Features

- Added `esp_gmf_afe_set_event_cb` API to register AFE element event callbacks
- Replaced the interface for decoder reconfiguration
- Introduced `esp_gmf_wn` element to support wake word detection
- Corrected return value validation for *acq_write* and *acq_release* callback function implementations
- Added `esp_gmf_afe_keep_awake` API to enable/disable the keep awake feature
- Increased `esp-sr` dependency to version 2.1.4

### Bug Fixes

- Fixed an issue where the output attribute of the `esp_gmf_afe` element was not set correctly
- Fixed a build error occurring when SPIRAM is disabled
- Integrated gmf_app_utils package for peripheral and system management
- Migrated common utilities to gmf_app_utils package
- Standardize TAG identifier format across all elements with `ai` prefix
- Fixed examples TAGs issue because of updating TAGs gmf_audio gmf_io

## v0.6.2

### Features

- Limit the version of `esp_audio_codec` and `esp_codec_dev` in `aec_rec` example

## v0.6.1

### Bug Fixes

- Fixed a build issue caused by a variable type mismatch

## v0.6.0

### Features

- Initial version of GMF AI Audio Elements
