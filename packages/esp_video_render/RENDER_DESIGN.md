# Video Render Design

This document describes the current implemented design of `esp_video_render`.

## Architecture

### 1. Render

One `esp_video_render` instance owns:

- one backend
- one blender
- one stream list
- one render mutex
- one compose mutex
- one event group for VSYNC / exit coordination

The render is the top-level composition manager. It calculates dirty regions, fills background when needed, blends active streams, and sends the final result to the backend.

### 2. Backend

A backend abstracts the display target.

Current backend behavior supports:

- direct framebuffer access through `get_fb()` / `lock_fb()`
- write-back path through `write_fb()`
- optional GRAM-oriented path
- one or two real panel framebuffers
- per-framebuffer dirty-state tracking
- background color or decoded background image

The render keeps dirty information per physical framebuffer, so dual-framebuffer displays can reuse the previous frame's dirty result when buffers swap.

### 3. Stream

A stream is the base video composition unit.

Each stream contains:

- input frame information
- display rectangle and source rectangle
- z-order, alpha, visibility, opaque state
- current frame buffer or cached frame buffer
- optional overlay
- one internal video proc handle

A stream can be used in three main ways:

- video only
- video + overlay
- overlay only

Each stream has at most one overlay.

### 4. Overlay

An overlay is attached to one stream and contains a linked list of regions.

Overlay behavior:

- may exist even when the stream has no video frames yet
- blends on top of the stream when video exists
- can also be used as a pure UI layer for that stream
- tracks removed-region dirty rectangles separately

### 5. Region / Container

The basic overlay node is `esp_vui_overlay_rgn_t`.
The most common region type is the container.

A container:

- is an overlay region with compose information
- owns a widget list
- may have its own off-screen buffer cache
- may also draw directly to the destination framebuffer

When a container has a local buffer, widget redraw is written into that cache first.
When it has no local buffer, widgets draw directly into the render target with dirty clipping.

### 6. Widget

Widgets live inside a container.

Implemented widget model:

- widgets update their own dirty rectangles
- dirty changes are merged into the parent container compose state
- widget redraw is driven by container redraw
- widgets can be reordered or removed through the container list management

The public UI API uses the `esp_vui_*` prefix, not only `esp_video_render_*`.

## Data Flow

### 1. Video input path

Stream input can come from:

- `esp_video_render_stream_write()` for normal frames
- `esp_video_render_stream_write_fb()` when the caller already owns a framebuffer from the backend

For normal writes, the stream proc is built or rebuilt based on the input info, output format, display size, crop, scale, and rotation requirements.

### 2. Proc path

Each stream owns one internal `esp_video_render_proc` helper.
The proc dynamically inserts only required stages, such as:

- decode
- color convert
- crop
- scale
- rotate
- FPS-related processing

The proc output is normally sent to the stream writer callback.
That callback either:

- references the returned frame directly, or
- copies it into stream cache when `stream_info.cached = true`

### 3. Cached vs non-cached stream output

Non-cached stream:

- keeps `stream->fb.data` pointing to the current processed frame
- avoids one extra copy
- is best when decode/render timing already matches

Cached stream:

- allocates `cached_data`
- copies proc output into the cache
- decouples decode timing from display timing
- is useful when render FPS and input FPS do not match
- is also used by dual-eyes async flows

### 4. Overlay redraw path

During blending:

1. Overlay removed-region dirty rectangles are merged first.
2. Overlay region dirty rectangles are calculated.
3. Stream dirty rectangles are calculated.
4. Containers redraw only the intersected dirty area.
5. Final dirty rectangles are blended to the target framebuffer.

## Render Execution Model

### 1. Synchronous render

When there is only one active stream and async render was not enabled, the write path directly calls the blend execution in the caller task.

This is the low-resource fast path.

### 2. Asynchronous render

A dedicated render thread is used when:

- more than one stream is active, or
- `esp_video_render_stream_render_async()` is enabled

The render thread periodically executes the blend flow and signals VSYNC / frame-done events through the event group.

## Dirty Region Model

Dirty-region tracking is central to the current design.

### 1. Compose-level dirty tracking

Each compose node tracks:

- current display rectangle
- previous display rectangle
- visible state
- fresh state
- empty state
- opaque state
- up to `VIDEO_RENDER_COMPOSE_MAX_DIRTY_AREA` dirty rectangles

Current limit:

- compose dirty rectangles: `2`

This applies to streams and overlay regions.

### 2. Display-level dirty tracking

The backend framebuffer state keeps the final dirty list for each physical framebuffer.

Current limit:

- framebuffer dirty rectangles: `16`

If merging exceeds the limit, regions are coalesced into a larger area.

### 3. Opaque-aware merge

Dirty rectangles also carry an `opaque` flag.
This is used to skip background fill when the final dirty region is fully covered by opaque content.

## Background Handling

The render supports:

- solid background color
- decoded background image

Background behavior:

- full background fill when the target framebuffer is not initialized yet
- partial background update only for non-opaque dirty regions
- background state tracked per real framebuffer

## GRAM and Non-GRAM Paths

The blend flow has two main runtime branches:

### 1. Non-GRAM path

- calculates dirty regions
- reuses previous framebuffer dirty state when buffers switch
- fills background only when needed
- blends stream and overlay content into the framebuffer directly

### 2. GRAM path

- has a special fast path for video-only full-screen cases
- otherwise merges dirty regions into a write-friendly area before panel transfer
- keeps overlay redraw and background handling consistent with the panel write model

## Overlay and Compose Synchronization

The render owns a dedicated `compose_mutex`.

It is shared by:

- stream compose lock / unlock
- overlay compose lock / unlock
- cached stream frame updates

This keeps multi-widget and multi-region updates consistent with the active blend pass.

## Dual-eyes Helper

`esp_video_render_dual_stream` is a higher-level helper built on top of the same stream and proc design.

It adds:

- two eye streams
- per-eye decode path
- queued input buffering
- optional async render + cached stream mode
- optional direct framebuffer acquisition flow

It is not a separate composition engine; it reuses the same render, stream, framebuffer, and proc concepts.

## Current Design Rules

- One render owns one backend.
- One render can have multiple streams.
- One stream can have zero or one overlay.
- One overlay can have multiple regions or containers.
- One container can have multiple widgets.
- Overlay/UI APIs use `esp_vui_*`.
- Stream write may render directly in caller context for the single-stream case.
- Proc stages are built dynamically according to current input/output requirements.
- Cached stream mode trades memory for smoother async timing.
- Dirty tracking is bounded: `2` per compose node, `16` per framebuffer.

## Typical Use Cases

### 1. Video only

- decode and process one stream
- render directly to panel framebuffer
- optionally use framebuffer write path for zero-copy style integration

### 2. Video with UI overlay

- stream holds video frame
- overlay redraw is clipped to dirty areas
- final blend updates only changed regions when possible

### 3. UI only stream

- stream overlay exists without active video frames
- containers draw cached or direct content into the final framebuffer

### 4. Dual-eyes video

- one or two renders
- one stream per eye
- optional async decode/render parallelism

