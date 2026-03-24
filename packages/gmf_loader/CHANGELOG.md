# Changelog

## v0.8.1

### Features

- Aligned ai audio supported target with `esp_sr`
- Update `esp-dsp` dependency to v1.7.0

## v0.8.0

### Features

- Added support to configure frame decoder usage in the audio decoder via `CONFIG_GMF_AUDIO_CODEC_DEC_USE_FRAME_DEC`
- Added configurable `DRC` (Dynamic Range Control) and `MBC` (Multi-Band Compressor) audio effects support in `gmf_loader_setup_audio_effects`
- Added `VORBIS` and `ALAC` decoder configuration support in `gmf_loader_setup_audio_codec`
- Added GMF codec type selection depending on the enabled codec support options
- Added configuration of task，data_bus and speed_monitor in `gmf_loader_setup_io_default` for io

## v0.7.3~1

### Bug Fixes

- Removed CODEC_I2C_BACKWARD_COMPATIBLE in sdkconfig.defaults
- Fixed API call order description in README

## v0.7.3

### Features

- Support C++ build

## v0.7.2

### Features

- Add `crt_bundle_attach` to use when not enable `CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY`
- Add io_file cache size configuration to improve read and write performance

## v0.7.1

### Bug Fixes

- Fixed amrnb and amrwb bitrate default value in `Kconfig.audio_codec.enc`

## v0.7.0

### Features

- Add initial implementation of `gmf_loader` with official element registration and I/O setup
