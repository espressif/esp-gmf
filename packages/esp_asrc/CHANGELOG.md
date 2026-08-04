# Changelog

## v1.1.0

### Break Change

- Requires `ESP32-P4` chip version >= 3.0 for assembly optimization support
- When using the software `rate_cvt` on `ESP32-S31` with `ESP_AUDIO_EFFECTS_S31_USE_ASM` enabled, the task is pinned to core 1 by default. To customize the task core affinity, please disable `ESP_AUDIO_EFFECTS_S31_USE_ASM`

### Changes

- Updated `esp_audio_effects` dependency to `~1.4`

## v1.0.1

### Features

- Added ESP32-P4 and S31 CI target support
- Changed partition offset for IDF v6.1 compatibility

## v1.0.0

### Features

- Initial version of `esp_asrc`
