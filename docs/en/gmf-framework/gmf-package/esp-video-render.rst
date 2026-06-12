ESP Video Render
================

:link_to_translation:`zh_CN:[中文]`

ESP Video Render is a video compositing and display component for ESP chips, rendering video and UI overlays to a unified display back-end with H.264 / MJPEG decode input support. Each render instance manages one display back-end and can contain multiple streams; each stream carries video, an overlay, or both, and each overlay can hold multiple containers and widgets. The component uses dirty-region tracking for partial refresh to avoid unnecessary redraws. Suitable for video-first embedded applications such as video players, smart displays, robot eyes, video intercom, and camera preview layouts.

Key Features
------------

- Video and UI composited in a single rendering path, avoiding the need to combine multiple subsystems
- Partial refresh: dirty-region tracking reduces unnecessary redraws and background fills
- Independent per-stream control: each stream supports individual settings for position, cropping, rotation, show/hide, z-order, and update policy
- Multiple output back-ends: supports direct LCD output, LVGL integration, and frame buffer back-end
- Overlay and widget capability: each overlay supports multiple regions; each region can hold multiple containers and widgets to construct complex UI layouts
- Built-in examples: typical usage including dual-eye rendering, video player, and camera preview
- Simulation support: a Linux simulation guide is provided with the repository for debugging visual effects on the host
- Manual compose mode: the application triggers compositing and refresh on demand to reduce idle resource usage
