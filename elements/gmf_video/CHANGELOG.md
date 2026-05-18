# Changelog

## v0.8.3

### Features

- Added support for `esp32s31` v4l2 source
- Fixed decoder overwrote fps to 0 when decoded fps not set
- Added software color convert support for `vid_ppa`

### Bug Fixes

- Fixed print error log when acquire data when aborted by stop

## v0.8.4

### Features

- Updated `esp_image_effects` dependency to `~1.1.0`

## v0.8.3

### Bug Fixes

- Use aligned buffer alignment for all video elements
- Fixed can not decode when video output not 16 pixels alignment
- Added global lock `vid_ppa` to avoid racing condition on PPA access
- Fixed rotate not take effect if angle is 180 for `vid_ppa`

## v0.8.2

### Bug Fixes

- Fixed `gmf_video` wrongly report video info when open (either non-bypass or failed)

## v0.8.1

### Features

- Added `esp_gmf_video_param_get_dst_fmts_by_codec` function

### Bug Fixes

- Added fine-grained mutex protection for internal handle calls across `gmf_video` elements to improve thread safety

## v0.8.0

No further changes. Version updated to align with other components.

## v0.7.1

### Features

- Add QP and GOP setting support for video encoder element

## v0.7.0

### Features

This is the initial release of the `gmf_video` component, introducing the following features:

* Core video processing elements: decoder, encoder, frame rate converter, overlay mixer, and hardware-accelerated Pixel Processing Accelerator (PPA)
* Software-based video effects: cropper, scaler, color converter, and rotator
