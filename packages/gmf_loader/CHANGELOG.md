# Changelog

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
