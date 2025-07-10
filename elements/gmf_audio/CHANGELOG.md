# Changelog

## v0.7.2

### Features

- Support C++ build

## v0.7.1

### Bug Fixes
- Fixed sonic bypass failed for first payload

### Features

- Added audio param setting for `aud_alc`, `aud_fade`, `aud_sonic`

## v0.7.0

### Features

- Updated `esp_audio_codec` to version v2.3.0
- Updated `esp_audio_effects` to version v1.1.0
- Added `esp_gmf_audio_enc_get_frame_size`, `esp_gmf_audio_enc_set_bitrate`, `esp_gmf_audio_enc_get_bitrate`, `esp_gmf_audio_enc_reconfig` and `esp_gmf_audio_enc_reconfig_by_sound_info` functions to `gmf_audio_enc`
- Added `SBC` and `LC3` encoders
- Added `esp_gmf_audio_dec_reconfig` and `esp_gmf_audio_dec_reconfig_by_sound_info` functions to `gmf_audio_dec`
- Added `SBC` and `LC3` decoders
- Added `meta_flag` in `gmf_audio_dec` to support audio decoder recovery status tracking
- Added common audio parameter setting interface
- Redefined audio methods name
- Removed the audio encoder and decoder reconfig interface in `esp_gmf_audio_helper.c`
- Used the `esp_gmf_element_handle_t` type handle in the `gmf_audio` module
- Use default configuration for `esp_gmf_audio_xxx_init` internally if configuration set to NULL
- Added TRUNCATE support for SONIC element to enable its flexible placement at any position within the pipeline
- Supported scenarios where the input data for audio encoding is not a complete frame
- Added PTS (Presentation Time Stamp) calculation and propagation for the `aud_alc`, `aud_eq`, `aud_fade`, `aud_enc`
- Supported user-configurable input frame length and load acquisition timeout for the mixer

### Bug Fixes

- Fixed `gmf_audio_enc` process blocked due to forget release of in_load when truncate is returned
- Fixed parameter mismatch in `audio_dec_reconfig_dec_by_sound_info`
- Standardize TAG identifier format across all audio elements with `aud` prefix
- Fixed a crash caused by passing a NULL encoder config to the init function and subsequently reporting info
- Fixed bugs in the implementation of methods for sonic, fade, and mixer
- Fixed the issue where the encoder modified the configuration during runtime
- Fixed the issue where the mixer did not forward the out_load parameter to downstream components when all input stream were no data
- Fixed bugs that not check validation of in and out buffer for mixer which will cause crash
- Fixed bugs where setting parameters before opening had no effect for `aud_fade` and `aud_mixer`

## v0.6.3

### Bug Fixes

- Fixed decoder not releasing input port on failure
- Fixed missing input size check in gmf_audio_enc when input is insufficient to encode a frame and stream has ended
- Fixed missing zero size check for acquire_in in gmf_audio_dec
- Fixed unmatched sub_cfg when reconfigured by esp_gmf_audio_helper_reconfig_dec_by_uri

## v0.6.2

### Features

- Limit the version of `esp_audio_codec` and `esp_audio_effects`


## v0.6.1

- Added missing #include "esp_gmf_audio_element.h" across all audio elements
- Fixed out-of-range parameter handling for mixer and EQ elements
- Resolved rate/bit/channel converter bypass errors caused by asynchronous modification of obj->cfg between set/event callbacks and process functions
- Deleted one unreasonable log from esp_gmf_audio_helper.c


## v0.6.0

### Features
- Updated the logic for GMF capabilities and method handling
- Added support for NULL object configuration
- Added GMF port attribution for elements
- Added gmf_cache and optimized the loop path for the audio encoder and decoder
- Renamed component to `gmf_audio`
- Updated the License


## v0.5.2

### Bug Fixes

- Fixed an issue where the decoder's output done flag was repeatedly set after the input was marked as done
- Fixed an issue where the input port was not released when the input data size was 0


## v0.5.1

### Bug Fixes

- Fixed the component requirements


## v0.5.0

### Features

- Initial version of `esp-gmf-audio`
