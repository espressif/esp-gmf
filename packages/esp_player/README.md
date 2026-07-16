# ESP Player

- [![Component Registry](https://components.espressif.com/components/espressif/esp_player/badge.svg)](https://components.espressif.com/components/espressif/esp_player)

- [中文版](./README_CN.md)

## Overview

**ESP Player** (`esp_player`) is Espressif's embedded multimedia playback component. A single instance runs the full **demux → decode → render** pipeline. It supports local files, HTTP(S) streams, HLS, and container-less external frame input, targeting resource-constrained IoT and multimedia applications.

## Key Features

- **Multiple input sources**: local files (`file:///`), HTTP/HTTPS streams, HLS (`.m3u8` auto-detected), external frame mode (`fill:///`, `block:///`)
- **Common containers**: WAV, MP4, M4A, TS, OGG, AVI, FLV, CAF; raw ES stream files (`.mp3`/`.aac`/`.flac`/`.amr`, no container header)
- **Audio codecs**: AAC, MP3, Vorbis, Opus, FLAC, AMR-NB/WB, G.711 A-law/μ-law, ALAC, ADPCM, SBC, LC3
- **Video codecs**: H.264, MJPEG
- **A/V synchronization**: system clock, audio master clock, video master clock, or none (freerun)
- **Playback control**: play, pause, resume, stop, seek (milliseconds), playback speed
- **Track selection**: enable or disable audio/video independently; enumerate and select tracks in multi-track containers
- **Network buffering**: startup pre-buffering and runtime re-buffering based on queue watermarks
- **Event notification**: synchronous callback or asynchronous event queue, covering playback state, buffering, errors, track info, and more
- **Custom decoders**: register your custom GMF audio/video decoder elements via factory callbacks alongside built-in decoders
- **Per-instance tuning** (advanced): override GMF task and buffer settings per handle (`player_defaults_cfg.h` built-ins; see `esp_player_advance.h`)

## Architecture

`esp_player` sits between the application and GMF pipelines. It owns the state machine, command dispatch, and A/V sync; data flow is driven by GMF Pipeline / Task / DataBus.

```mermaid
flowchart TB
  app["Application"]
  api["esp_player_* API"]
  event["Event callback / event_queue"]
  core["ESP Player Core\n(command task, state machine, A/V sync)"]
  extractor["Extractor\n(demux)"]
  audio["Audio Path\n(Decoder → Render)"]
  video["Video Path\n(Decoder → Render)"]
  gmf["GMF Pipeline / Task / DataBus"]

  app --> api --> core
  core --> event --> app
  core --> extractor
  extractor --> audio
  extractor --> video
  audio --> gmf
  video --> gmf
```

## API Overview

### Header Files

| Header | Purpose |
|--------|---------|
| `esp_player.h` | Module init/deinit and core playback control |
| `esp_player_types.h` | Types (events, error codes, track info, frame structures, etc.) |
| `esp_player_advance.h` | Advanced: custom decoders, decoder sub-config (`esp_player_set_dec_cfg`), frame submit, **per-handle GMF task / buffer overrides** (`esp_player_set_task_config` / `esp_player_set_buffer_config`) |

### Lifecycle APIs

| API | Description |
|-----|-------------|
| `esp_player_init(config, handle)` | Initialize the player and allocate internal resources |
| `esp_player_deinit(handle)` | Release all resources; handle becomes invalid |
| `esp_player_set_data_src(handle, src)` | Set AV mask, URL, and sync mode in one call (convenience API) |
| `esp_player_set_av_mask(handle, mask)` | Configure audio/video side enable mask separately |
| `esp_player_set_url(handle, url)` | Set playback URL; call when switching sources |
| `esp_player_set_sync_mode(handle, mode)` | Set A/V sync mode (only meaningful with `ESP_PLAYER_MASK_AV`) |

### Playback Control APIs

| API | Description |
|-----|-------------|
| `esp_player_run(handle)` | Start playback (non-blocking) |
| `esp_player_run_to_end(handle)` | Block until playback finishes or an error occurs |
| `esp_player_pause(handle)` | Pause |
| `esp_player_resume(handle)` | Resume from pause |
| `esp_player_stop(handle)` | Stop |
| `esp_player_seek(handle, time_ms)` | Seek to a position (milliseconds) |
| `esp_player_set_speed(handle, speed)` | Set playback speed |

### Status and Information APIs

| API | Description |
|-----|-------------|
| `esp_player_get_duration(handle, duration)` | Get total media duration (milliseconds) |
| `esp_player_get_play_time(handle, current_time)` | Get current playback position (milliseconds) |
| `esp_player_get_track_num(handle, type, track_num)` | Get the number of tracks of the given type |
| `esp_player_get_track_info(handle, type, track_idx, track_info)` | Get track info (codec, sample rate, resolution, etc.) |
| `esp_player_enable_track(handle, type, track_idx, enable)` | Enable or disable a specific track |

### Events

Register a synchronous callback with `esp_player_set_event_cb()`, or receive asynchronous events via `esp_player_set_event_queue()` (element size: `sizeof(esp_player_event_msg_t)`).

| Event | Meaning |
|-------|---------|
| `ESP_PLAYER_EVENT_PLAYED` | Playback started |
| `ESP_PLAYER_EVENT_PAUSED` | Paused |
| `ESP_PLAYER_EVENT_STOPPED` | Stopped |
| `ESP_PLAYER_EVENT_SEEK_DONE` | Seek completed |
| `ESP_PLAYER_EVENT_FINISHED` | Playback finished |
| `ESP_PLAYER_EVENT_BUFFERING` | Buffering |
| `ESP_PLAYER_EVENT_BUFFERED` | Buffering done; playback resumed |
| `ESP_PLAYER_EVENT_ERROR` | Playback error; `data` carries `esp_player_error_source_t` |
| `ESP_PLAYER_EVENT_TRACK_INFO_PARSED` | Track info parsed |
| `ESP_PLAYER_EVENT_AUDIO_INFO_PARSED` | Audio info parsed |
| `ESP_PLAYER_EVENT_VIDEO_INFO_PARSED` | Video info parsed |

## URL Schemes

### Standard URIs (`esp_gmf_uri_parse`, RFC 3986 style)

| Scheme | Example | Description |
|--------|---------|-------------|
| `file` | `file:///sdcard/music/test.mp3` | Local file (three-slash form) |
| `file` | `/sdcard/music/test.mp3` | Bare VFS path; auto-normalized to `file:///` |
| `file` | `file://sdcard/music/test.mp3` | Compat double-slash form (`sdcard` is part of the path) |
| `file` | `file:///sdcard/test.pcm?sr=48000&ch=2&bits=16` | Headerless raw PCM; `?query` required |
| `http` / `https` | `https://example.com/audio/test.mp4` | HTTP(S) stream |
| `http` | `http://192.168.1.10:8080/stream.aac` | HTTP with explicit port |
| `https` | `https://user:pass@example.com/audio/test.mp4` | Optional `user:pass@` basic auth |
| `https` (HLS) | `https://example.com/live/playlist.m3u8` | Path containing `.m3u8` is auto-detected as HLS |
| `http` | `http://example.com/audio/test.pcm?sr=48000&ch=2&bits=16` | Raw PCM over network; `?query` required |

Scheme grammar, authority rules, and `?query` parameters: see `esp_player_set_url()`.

### Frame Mode (Container-less)

For Bluetooth A2DP raw frames, microphone PCM, custom encoded frames, and similar use cases. URL format: `fill:///name.codec[?params]` or `block:///name.codec[?params]`.

- `fill`: full copy of each frame on every call
- `block`: zero-copy; caller must keep the buffer valid until decoding completes
- `name` has no semantic meaning; the `.codec` extension selects the decoder

After `esp_player_run()`, push frames via `esp_player_submit_frame()` (see `esp_player_advance.h`). `fill:///` and `block:///` do not support `ESP_PLAYER_MASK_AV`.

| Scenario | URL Example |
|----------|-------------|
| Raw PCM | `fill:///test.pcm?sr=16000&ch=1&bits=16` |
| PCM (zero-copy) | `block:///test.pcm?sr=16000&ch=1&bits=16` |
| AAC (standard ADTS) | `fill:///test.aac` |
| AAC (no ADTS header, e.g. BT A2DP) | `fill:///test.aac?no_adts=1` |
| HE-AAC raw frames | `fill:///test.aac?no_adts=1&aac_plus=1` |
| OPUS raw frames | `fill:///test.opus?sr=16000&ch=2&frame_dms=20` |

Common query parameters: `sr` (sample rate), `ch` (channels), `bits` (bit depth), `no_adts` (AAC without header), `aac_plus` (HE-AAC), `frame_dms` (OPUS/LC3 frame duration), `plc` (SBC/LC3 packet-loss concealment), and more. See `esp_player_set_url()` for the full list.

## Quick Start

### Requirements

- **ESP-IDF**: `>= 5.3` (see `idf_component.yml`)

### Add to Project

Via ESP-IDF Component Manager:

```yaml
dependencies:
  espressif/esp_player:
    version: "^1.0"
```

### Configuration

Under `menuconfig` → **ESP Player**:

- **Enable Audio / Video Playback Path**: compile-time pipeline selection
- **Input IO sources**: `file://`, `http(s)://`, and/or HLS (`.m3u8`); disabled schemes return `ESP_PLAYER_ERR_INVALID_ARG` from `esp_player_set_url()`.

Built-in task, buffer, and network-buffering defaults live in `player_defaults_cfg.h`. Override per player instance with `esp_player_set_task_config()` / `esp_player_set_buffer_config()` after `esp_player_init()` and before `esp_player_run()` when needed (e.g. multi-stream mixer).

### Initialization

Configure render handles for every path you enable via `esp_player_set_av_mask()` (see `esp_player_config_t`):

| av_mask | Required handle |
|---------|-----------------|
| `ESP_PLAYER_MASK_AUDIO` | `audio_render_hd` |
| `ESP_PLAYER_MASK_VIDEO` | `video_render_hd` |
| `ESP_PLAYER_MASK_AV` | both |

Pass NULL only for paths you will never use (e.g. `video_render_hd = NULL` for audio-only).

```c
#include "esp_player.h"

esp_player_config_t config = ESP_PLAYER_CONFIG_DEFAULT();
config.audio_render_hd = audio_render_handle;   /* esp_audio_render_stream_handle_t from esp_audio_render_stream_get() */
config.video_render_hd = video_render_handle;   /* esp_video_render_handle_t from esp_video_render_create(); NULL for audio-only */

esp_player_handle_t player = NULL;
esp_player_init(&config, &player);
```

### Play an Audio File

```c
static esp_player_err_t player_event_cb(esp_player_event_msg_t *msg, void *ctx)
{
    if (msg->event_type == ESP_PLAYER_EVENT_FINISHED) {
        /* Playback finished */
    }
    if (msg->event_type == ESP_PLAYER_EVENT_ERROR) {
        /* Error handling; msg->data carries esp_player_error_source_t */
    }
    return ESP_PLAYER_ERR_OK;
}

esp_player_set_event_cb(player, player_event_cb, NULL);

esp_player_data_src_t src = ESP_PLAYER_DATA_SRC("file:///sdcard/test.mp3", ESP_PLAYER_MASK_AUDIO);
esp_player_set_data_src(player, &src);
esp_player_run(player);
```

### Play an Audio/Video File

```c
esp_player_data_src_t src = ESP_PLAYER_DATA_SRC("file:///sdcard/test.mp4", ESP_PLAYER_MASK_AV);
esp_player_set_data_src(player, &src);
esp_player_run(player);
```

### Switch Source

When switching sources, call `esp_player_set_url()` only (no need to reset the mask if unchanged). The player tears down the old pipeline and rebuilds for the new source internally:

```c
esp_player_stop(player);
esp_player_set_url(player, "file:///sdcard/next.mp3");
esp_player_run(player);
```

### Cleanup

```c
esp_player_deinit(player);
```

## Performance

### Test Environment

| Chip | CPU | SPI RAM | Flash |
|------|-----|---------|-------|
| ESP32-S3 | 240 MHz | 80 MHz | QIO |
| ESP32-S31 | 320 MHz | 200 MHz | QIO |
| ESP32-P4 | 400 MHz | 250 MHz | QIO |

### Audio Performance

Play MP3 and AAC files from a local SD card.

| Chip | Format | Internal (KB) | PSRAM (KB) | Total (KB) | CPU (%) | Startup (ms) | Replay (ms) |
|------|--------|---------------|------------|------------|---------|--------------|-------------|
| ESP32-S3 | MP3 | 8.8 | 91.8 | 100.6 | 5 | 32 | 27 |
| ESP32-S3 | AAC | 10.1 | 106.4 | 116.5 | 3 | 19 | 6 |
| ESP32-S31 | MP3 | 7.2 | 91.8 | 99.0 | 4 | 16 | 6 |
| ESP32-S31 | AAC | 8.6 | 106.4 | 115.0 | 5 | 16 | 6 |
| ESP32-P4 | MP3 | 7.2 | 91.8 | 99.0 | 3 | 15 | 3 |
| ESP32-P4 | AAC | 10.1 | 106.4 | 116.5 | 3 | 11 | 2 |

Notes:
- **Startup latency**: time from `esp_player_run()` to first audio frame output
- **Replay latency**: time from `esp_player_stop()` + `esp_player_run()` (same URL) to first audio frame output

### ESP Player vs ESP-Audio Playback Performance Comparison

`esp_audio` is the audio playback component in v2.x version ESP-ADF. The following table compares `esp_audio` and `esp_player` on ESP32-S3 using the same formats and test conditions, covering resource overhead, startup latency, and replay latency. Parentheses in each `esp_player` row show the reduction relative to `esp_audio`.

| Format | Component | Internal (KB) | PSRAM (KB) | Total (KB) | CPU (%) | Startup (ms) | Replay (ms) |
|--------|-----------|---------------|------------|------------|---------|--------------|-------------|
| MP3 | esp_audio | 19.0 | 109.1 | 128.1 | 8 | 74 | 74 |
| MP3 | esp_player | 8.8 (53.7% lower) | 91.8 (15.9% lower) | 100.6 (21.5% lower) | 5 (37.5% lower) | 32 (56.8% lower) | 27 (63.5% lower) |
| AAC | esp_audio | 35.7 | 182.9 | 218.6 | 5 | 80 | 80 |
| AAC | esp_player | 10.1 (71.7% lower) | 106.4 (41.8% lower) | 116.5 (46.7% lower) | 3 (40.0% lower) | 19 (76.3% lower) | 6 (92.5% lower) |

### Video Performance

| Chip | Format | Maximum Performance |
|------|--------|---------------------|
| ESP32-S3 | H264 | 320x240@18fps |
| ESP32-S31 | H264 | 320x240@55fps |
| ESP32-S31 | H264 | 640x480@13fps |
| ESP32-P4 | H264 | 320x240@68fps |
| ESP32-P4 | H264 | 640x480@18fps |
| ESP32-S3 | MJPEG | 320x240@21fps |
| ESP32-S31 | MJPEG | 320x240@>120fps |
| ESP32-S31 | MJPEG | 640x480@42fps |
| ESP32-S31 | MJPEG | 1280x720@19fps |
| ESP32-P4 | MJPEG | 320x240@>120fps |
| ESP32-P4 | MJPEG | 640x480@67fps |
| ESP32-P4 | MJPEG | 1280x720@25fps |

Notes:
- H264 uses software decoding on all chips listed above.
- MJPEG uses software decoding on ESP32-S3 and hardware decoding on ESP32-S31 and ESP32-P4.
- Video playback requires substantial memory and processing resources. ESP32-S31 and ESP32-P4 with PSRAM are recommended. For higher-resolution MJPEG playback, use ESP32-S31 or ESP32-P4 with hardware MJPEG decoding.

### Memory optimization

Trim features in menuconfig to match your source formats and board:

| Area | Menuconfig | Suggestion (local audio-only) |
|------|------------|-------------------------------|
| Player | `Enable Audio Playback Path` / `Enable Video Playback Path` | Audio on, video off |
| Player | `Network Playback Tuning` | Off for local files only |
| Player | `Player Pipeline GMF Tasks` | Lower if stack usage allows (default 5120) |
| Player | Multi-instance | Use `esp_player_set_task_config()` / `esp_player_set_buffer_config()` (`esp_player_advance.h`) when streams differ; defaults are in `player_defaults_cfg.h` |
| Extractor | `Espressif Extractor Configuration`, container extractors | Enable only what you play |
| Audio codec | `Audio Codec Configuration`, decoders | Enable only needed decoders; disable encoders |
| Codec device | `Audio Codec Device Configuration` | Enable only your board codec |

Enable PSRAM (`SPIRAM`, `FREERTOS_TASK_CREATE_ALLOW_EXT_MEM`) to move large stacks and buffers off internal RAM.

## Examples

See the [examples](./examples) folder for audio-only and audio/video playback applications.
