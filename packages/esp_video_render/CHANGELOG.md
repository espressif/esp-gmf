# Changelog

## v1.0.0

### Features

- Added manual compose mode to optimize resource usage
- Skip unnecessary repaint when container is invisible
- Skip overlay blend when video stream has no new frame (overlay drawn on same stream buffer)
- Switched the `esp_board_manager` dependency in test apps and examples from local `override_path` to the standalone component at [espressif/esp-board-manager](https://github.com/espressif/esp-board-manager); pinned to `^0.5.11`.
- Removed the example-local `idf_ext.py` board-manager forwarders; the recommended entry point is now [`esp-bmgr-assist`](https://pypi.org/project/esp-bmgr-assist/) (`pip install esp-bmgr-assist` then `idf.py bmgr ...`).
- Updated example READMEs accordingly.
- Replaced the weak private data queue implementation with the shared `esp_gmf_data_queue` from `gmf_core`.

### Bug Fixes

- Fixed DPI display using wrong API to get frame buffer
- Fixed RGB display not using RGB565LE as default output format
- Fixed memory not aligned when PSRAM cache line set to 128

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
