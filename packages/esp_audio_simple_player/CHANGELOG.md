# Changelog

## v0.9.5

### Features

- Support C++ build

## v0.9.4

### Features

- Replaced the interface for decoding reconfig
- Supported placing the audio processing task stack in RAM or SPI-RAM using `stack_in_ext` in `esp_asp_cfg_t`
- Fixed `esp_audio_simple_player_destroy` random crash for event group not created
- Fixed include file missing for `esp_gmf_pipeline.h`

## v0.9.3

### Features
- Added `esp_audio_simple_player_get_pipeline` API
- Added a configurable callback between pipeline setup and execution
- Added test case for repeated playback
- Added test case for embedded flash URI

### Bug Fixes

- Fixed an issue where raw stream playback repeatedly failed


## v0.9.2

### Features
- Updated the dependencies to version 0.6.0

## v0.9.1

### Bug Fixes

- Fixed the component requirements


## v0.9.0

### Features

- Initial version of `esp_audio_simple_player`
