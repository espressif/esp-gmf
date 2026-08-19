# Changelog

## v1.0.6

### Bug Fixes

- Fixed Vorbis playback failing when the same source is run again
- Fixed fill/block AV dropping early frames while the other decoder pipeline was still starting

## v1.0.5

### Breaking Changes

- Prepended `esp_player_frame_t::track_type` for fill/block AV routing;
  Omitting it (or `{0}`) yields `NONE` and still works for single-track `av_mask`;
  for `MASK_AV`, set AUDIO or VIDEO on every frame
- Changed `esp_player_track_type_t` values to `NONE=0`, `AUDIO=1`, `VIDEO=2`, `MAX=3`

### Features

- Unified `data_queue` as the decoder front-end input buffer
- Added `esp_player_submit_frame()` support for `ESP_PLAYER_MASK_AV` to push audio and video streams
- Allowed `esp_player_seek()` in `IDLE` after a source is set to bookmark the next-run start position

### Bug Fixes

- Fixed `esp_player_run_to_end()` returning before the player entered the `FINISHED` state
- Fixed `esp_player_run_to_end()` blocking when playback failed after it had already started

## v1.0.4~1

### Changes

- Changed dependency version constraints from `^` (automatic minor updates) to `~` (automatic patch updates) to improve compatibility

## v1.0.4

### Features

- Added `esp_player_get_state()` and public `esp_player_state_t` for authoritative playback state queries

### Bug Fixes

- Fixed crash and seek stall from incorrect extractor frame release
- Fixed video decoder reopen with wrong decoded pixel format
- Fixed `player_cmd` task abort on deinit
- Fixed stale input IO after stop/finish when the same URI is replayed
- Fixed brief leftover audio from the previous track after pause, stop, and play a new URI

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
