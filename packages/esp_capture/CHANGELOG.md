# Changelog

## v0.7.3

### Bug Fixes

- Fixed incorrect handling of `RGB565` and `YUV422P` formats in DVP video source
- Fixed `codec_dev` handle wrongly cleared
- Fixed audio source read hangup if read from device failed
- Fixed AEC source crash for default microphone layout not set

## 0.7.2

- Remove the dependency on the `codec_board` component.

## v0.7.1

### Features

- Updated esp-sr dependency to v2.1.5

## v0.7.0

### Features

- Initial version of `esp_capture`
