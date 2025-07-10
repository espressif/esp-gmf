# Changelog

## v0.7.6

### Features

- Support C++ build

## v0.7.5

### Bug Fixes

- Fixed the issue where idf_build_set_property is not a valid function for component

## v0.7.4

### Bug Fixes

- Fixed data bus init when direction is write in `io_http`

## v0.7.3

### Bug Fixes

- Fixed cache caps issues for esp32 in `io_file`

## v0.7.2

### Features

- Added io_file cache size configuration to enhance read and write performance

### Bug Fixes

- Delay task creation to open stage in `io_http` to avoid task resource waste if no used

## v0.7.1

- Updated `esp-codec-dev` dependency to v1.4.0

## v0.7.0

### Features

- Added `esp_gmf_io_codec_dev_set_dev` API to set the audio codec device handle
- Added `esp_gmf_io_http_set_event_callback` API to set an HTTP event callback
- Added basic reset functionality to each IO to properly clean up resources and reset state

### Bug Fixes
- Fixed an issue where HTTP IO did not reset the thread and databus properly
- Fixed file open check for treat return error as valid fd
- Corrected return value validation for *acq_write/read and *acq_release_write/read callback function implementations
- Fixed HTTP connection timeout handling during _http_close operations
- Standardize TAG identifier format across all I/O elements with `io` prefix


## v0.6.3

### Bug Fixes

- Changed the tag for embedded flash IO from `embed_flash` to `embed`
- Refactor the script to make the generated files follow our coding style

## v0.6.2

### Features

- Limit the version of `esp_codec_dev`

## v0.6.1

- Fixed memory leaks in I/O flash destroy API
- Corrected default values for I/O file and I/O I2S elements to ensure stable initialization
- Enforced esp_gmf_err_t return type for all element initialization functions (I/O flash, HTTP)

## v0.6.0

### Features
- Added support for NULL object configuration
- Renamed component to `gmf_io`
- Updated the License

### Bug Fixes
- Standardized return values of port and data bus acquire/release APIs to esp_gmf_err_io_t only

## v0.5.1

### Bug Fixes

- Fixed the component requirements


## v0.5.0

### Features

- Initial version of `esp-gmf-io`
