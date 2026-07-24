# Changelog

## 1.0.2

### Bug Fixes

- Fixed bypass error when dual sink one sink match source exactly
- Fixed build issue

## 1.0.1

### Features

- Added support for fullspeed decode through `esp_capture_cfg_t::full_speed_decode` (video source is encoded and need decoded/re-encoded)
- Added `CONFIG_ESP_CAPTURE_VIDEO_DEC_OUT_POOL_SIZE` (default 2) for full-speed decode pool size

### Bug Fixes

- Enhanced error handler for pipeline error
  Disable share input when one path error, other path can continue to work
- Fixed dual sink audio pts and possible audio data wrongly used last frame
- Use atomic operation to load and set status variable to avoid voilation
- Fixed video raw bypass flag is not cleared in video path

## v1.0.0~1

### Docs

- Updated example README files to use the latest ESP Board Manager board selection commands and board names

## v1.0.0

### Changes

- Replaced the private data queue implementation with the shared `esp_gmf_data_queue` from `gmf_core`

### Bug Fixes

- Fixed video bypass release too early in restart case
- Fixed UT fails due to `esp32s31` related macro not enabled

## v0.8.4

### Features

- Added multiple overlay region support
- Added `share_overlay` configuration so that sinks can reuse frames after overlay

## v0.8.3

### Features

- Added support for `esp32s31` v4l2 source
- Added decode support when input format from camera is encoded (MJPEG) but wanted raw format (RGB565) data
- Added test cases to cover decoder support

### Bug Fixes

- Fixed print error log when acquire data when aborted by stop

## v0.8.2

### Features

- Update dependency `esp-sr` to v2.4

### Bug Fixes

- Fixed `v4l2_src` fixed negotiate may fail use un-saved results
- Fixed `esp_capture_sink_enable_muxer` wrong return value if not started yet
- Fixed `v4l2_src` fd leakage if open multiple times

## v0.8.1

### Features

- Updated dependency `esp_video` to v2.0
- Supported RGB565BE and RGB565LE convert in `v4l2` video source
- Added storage support for overlay in `video_render` example

### Bug Fixes

- Fixed map of `v4l2` format and `esp_capture` format
- Fixed `README` format error

## v0.8.0

### Bug Fixes

- Fixed setting crash for audio encoder and video encoder after stop
- Fixed dynamic enable muxer and sink setup streaming output not as expected

## v0.7.11

### Features

- Updated dependency `esp_muxer` to v1.2

### Bug Fixes

- Fixed user muxer url pattern context is overwrote

## v0.7.10

### Features

- Allowed reconfiguration for muxer after stop
- Added RGB565BE support for v4l2 video source

### Bug Fixes

- Fixed not mux any more after stop

## v0.7.9

### Features

- Added `esp_board_manager` support for examples
- Added more target support for `v4l2` video source
- Fixed muxer stopped instantly after start for stop message received pending

### Bug Fixes

- Fixed build warnings

## v0.7.8~1

### Bug Fixes

- Removed CONFIG_AUDIO_BOARD and CODEC_I2C_BACKWARD_COMPATIBLE in sdkconfig.defaults

## v0.7.8

### Features

- Added dependency `codec_board` in video_capture example yaml to adapt `gmf_app_utils` use `esp_board_manager`

## v0.7.7

### Features

- Fixed failed to set bitrate
- Added QP and GOP setting for H264

### Bug Fixes

- Fixed share queue release possible issue
- Fixed wrong received muxer data when audio/video path has error

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
