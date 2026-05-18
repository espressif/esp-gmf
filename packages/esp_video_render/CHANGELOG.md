# Changelog

## Unreleased

- Switched the `esp_board_manager` dependency in test apps and examples from local `override_path` to the standalone component at [espressif/esp-board-manager](https://github.com/espressif/esp-board-manager); pinned to `^0.5.11`.
- Removed the example-local `idf_ext.py` board-manager forwarders; the recommended entry point is now [`esp-bmgr-assist`](https://pypi.org/project/esp-bmgr-assist/) (`pip install esp-bmgr-assist` then `idf.py bmgr ...`).
- Updated example READMEs accordingly.

## v0.8.1

### Bug Fixes

- Fixed build error of `dual_eyes` example on IDFv6.x

## v0.8.0

### Features

- Initial version of `esp_video_render`
  - Support basic video render (decode, display, display rectangle, source rectangle, multiple streams, z-order, alpha, blend etc)
  - Support multiple render backend
  - Support for dual stream, video processor
  - Support overlay and basic widget system (image widget and text widget)
  - Support PC emulation based on gstream CLI
