# Changelog

## Unreleased

### Features

- Added `pipeline_play_http_music` example for playing online music via HTTP
- Added `pipeline_http_download_to_sdcard` example for HTTP file downloads to SD card
- Added `pipeline_record_http` example for uploading the recorded audio to HTTP server
- Added power manage support for recording example `pipeline_record_sdcard`
- Added `pipeline_audio_effects` example demonstrating audio effects (ALC, EQ, FADE, SONIC, DRC, MBC) usage in GMF pipeline
- Added `pipeline_loop_play_no_gap` example for seamless playback of music file

## v0.7.2~1

### Bug Fixes

- Removed CONFIG_AUDIO_BOARD and CODEC_I2C_BACKWARD_COMPATIBLE in sdkconfig.defaults

## v0.7.2

### Bug Fixes

- Added prebuild script to solve compilation issue

## v0.7.1

### Bug Fixes

- Fixed `pipeline_play_sdcard_music` example playback fail due to format not set
- Fixed examples codec device setting mismatch with process output

## v0.7.0

### Features

- Initial version of `gmf_examples`
