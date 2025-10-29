# Changelog

## v0.7.6

### Features

- Support C++ build

## v0.7.5

### Bug Fixes

- Fixed AEC audio source not support multiple microphone
- Fixed can not create task stack in RAM

### Features

- Added `data_on_vad` option for AEC audio source to support only send data when VAD active
- Added `data_q_rewind` to support resend from old position

## v0.7.4

### Bug Fixes

- Fixed failed to start audio capture with PCM format
- Fixed dynamically enable disable sink failed after started
- Added get sink handle with same configuration after started
- Fixed sync lost if dynamical enable and disable sink
- Added test cases for audio bypass
- Added test cases for dynamically enable and disable sink
- Added multiple start and stop test cases

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
