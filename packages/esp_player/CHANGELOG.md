# Changelog

## v1.0.3

### Changes

- Moved default player task stacks to external memory
- Updated audio and video performance data

### Bug Fixes

- Fixed playback time after seeking when no frame is output
- Fixed fill-mode frame submission timeout handling
- Fixed extractor busy loops while waiting for output

## v1.0.2

### Features

- Added `esp_player_enable_id3_parse()` to make ID3 parsing optional
- Added `esp_player_get_id3_info()` for bare MP3 sources
- Added tests for config/sync/ID3 APIs, seek and event paths, error recovery, and concurrency

### Bug Fixes

- Fixed `esp_player_frame_t.pts` unit documentation to milliseconds
- Fixed redundant `esp_codec_dev` codec chip options restriction from audio/video player example `sdkconfig.defaults`

## v1.0.1~1

### Docs

- Updated example README files to use the latest ESP Board Manager board selection commands and board names

## v1.0.1

### Changes

- Updated GMF component dependencies from `^0.8` to `^1.0` for the ESP-GMF 1.0 release

### Bug Fixes

- Removed extra done_write in audio decoder stop
- Added input state enum to avoid io retry open when failed
- Fixed video decoder pause timeout on seek

## v1.0.0

### Features

- Initial version of `esp_player`
