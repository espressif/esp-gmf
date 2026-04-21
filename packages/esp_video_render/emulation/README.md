# ESP Video Render Emulation

## Overview

`esp_video_render` includes a Linux-based emulation environment for desktop development, UI verification, and regression testing.

The emulation layer reuses most of the component code and provides PC-side replacements for platform-specific services. This allows you to validate rendering logic, overlay behavior, widget drawing, and selected video-processing flows before running on ESP hardware.

## What the Emulation Supports

The Linux emulation currently covers:

- core video render composition
- dirty-region based redraw
- overlay, container, and widget composition
- text and image widgets
- dual-eyes rendering logic
- SDL-based display output
- optional GStreamer CLI-based video processing on Linux

## Architecture Summary

The emulation keeps the original render pipeline where practical and replaces ESP-specific pieces with Linux-friendly implementations:

- **backend**
  SDL backend for framebuffer presentation
- **processor**
  - `proc_gstcli.c` when GStreamer CLI tools are available
  - `proc_emulation.c` fallback when GStreamer support is disabled
- **stubs**
  replacement headers and lightweight implementations for ESP/GMF support APIs used by the component

For background and implementation notes, see [`Design.md`](./Design.md).

## Requirements

### Required System Packages

The emulation build requires these libraries through `pkg-config`:

- `sdl2`
- `freetype2`

On Ubuntu or Debian-based systems:

```bash
sudo apt install libsdl2-dev libfreetype6-dev pkg-config ninja-build cmake
```

### Optional GStreamer Support

GStreamer is optional. When the following tools are available, the build script automatically enables `EMU_WITH_GSTREAMER=ON`:

- `gst-launch-1.0`
- `gst-inspect-1.0`

On Ubuntu or Debian-based systems:

```bash
sudo apt install gstreamer1.0-tools
```

When GStreamer tools are not found, the emulator still builds, but video-processing paths that rely on decode/crop/scale emulation will not work anymore.

## Build

From `packages/esp_video_render/emulation`:

```bash
./build.sh
```

This produces:

- `build/video_render_emulator`

### Debug Build

To build with debug symbols and no optimization:

```bash
./build.sh -g
```

## Run

```bash
cd build
./video_render_emulator
```

## WSL and Headless Environments

When running in WSL or software-render-only environments, it can be helpful to force software rendering:

```bash
export SDL_RENDER_DRIVER=software
export LIBGL_ALWAYS_SOFTWARE=1
```

For fully headless environments such as CI:

```bash
export SDL_VIDEODRIVER=dummy
./video_render_emulator
```

The dummy SDL driver creates a hidden window and keeps the rendering path usable for automated checks.

## Automated Tests

### Build and Run All Tests

```bash
./build.sh
cd build
ctest --output-on-failure
```

### Run Tests in Headless Mode

```bash
export SDL_VIDEODRIVER=dummy
cd build
ctest --output-on-failure
```

## Available Test Targets

The emulation build includes several focused test executables.

### Always Built

- `emu_backend_sdl`
  Verifies the SDL backend and dirty-region update path.

- `emu_widgets_text`
  Verifies text widget rendering, including UTF-8 and emoji scenarios.

- `emu_widgets_image`
  Verifies image widget rendering.

- `emu_xiaozhi_panel`
  Verifies a more complete UI panel composition flow.

- `emu_player_view`
  Verifies the video player view layer used by the example UI.

### Built Only with GStreamer Enabled

- `emu_proc_matrix`
  Verifies video-processing paths such as crop, scale, color conversion, and selected decode flows.

- `emu_intercom_ui`
  Verifies the intercom-style UI composition path.

- `emu_dual_eyes`
  Verifies dual-eyes rendering behavior with MJPEG assets when available.


### Run Individual Tests

```bash
cd build
./emu_test_backend_sdl
./emu_test_widgets_text
./emu_test_widgets_image
./emu_test_xiaozhi_panel
./emu_test_player_view
```

If GStreamer support is enabled:

```bash
./emu_test_proc_matrix
./emu_test_intercom_ui
./emu_test_dual_eyes
```

## Typical Use Cases

Use the emulation environment when you want to:

- validate layout and widget rendering on a PC
- debug dirty-region and blend behavior without flashing hardware
- develop overlay-heavy applications faster
- run desktop-side regression tests for render logic
- exercise dual-eyes or UI composition flows in CI

## Limitations

- The emulation target is Linux-oriented.
- The SDL backend is not a cycle-accurate model of a real panel backend.
- GStreamer-based processing is implemented through CLI subprocesses rather than linked runtime APIs.
- Some hardware-specific behavior is intentionally simplified.

## Technical Support

- Technical support: [esp32.com](https://esp32.com/viewforum.php?f=20) forum
- Issue reports and feature requests: [GitHub issue](https://github.com/espressif/esp-gmf/issues)
