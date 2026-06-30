# Changelog

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
