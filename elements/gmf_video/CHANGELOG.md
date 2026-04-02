# Changelog

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
