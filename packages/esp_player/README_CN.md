# ESP Player

- [![Component Registry](https://components.espressif.com/components/espressif/esp_player/badge.svg)](https://components.espressif.com/components/espressif/esp_player)

- [English Version](./README.md)

## 概述

`esp_player` 是乐鑫提供的嵌入式多媒体播放组件，在单实例内完成**解封装 → 解码 → 渲染**的完整链路，支持本地文件、HTTP(S) 流、HLS 以及无容器的外部帧喂入等场景，面向资源受限的 IoT 与多媒体应用。

## 特性

- **多种输入源**：本地文件（`file:///`）、HTTP/HTTPS 流、HLS（`.m3u8` 自动识别）、外部帧模式（`fill:///`、`block:///`）
- **常见封装容器**：WAV、MP4、M4A、TS、OGG、AVI、FLV、CAF；裸 ES 流文件（`.mp3`/`.aac`/`.flac`/`.amr`，无容器头）
- **音频解码格式**：AAC、MP3、Vorbis、Opus、FLAC、AMR-NB/WB、G.711 A-law/μ-law、ALAC、ADPCM、SBC、LC3
- **视频解码格式**：H.264、MJPEG
- **音视频同步**：可配置系统时钟、以音频为主钟、以视频为主钟、无同步（freerun）四种模式
- **播放控制**：播放、暂停、继续、停止、Seek（毫秒）、倍速
- **音视频轨选择**：可分别开关音频/视频，支持多轨容器的轨道枚举与选择
- **网络缓冲**：基于队列水位的启动预缓冲与运行期重缓冲
- **事件通知**：支持同步回调与异步事件队列两种模式，覆盖播放状态、缓冲、错误、轨道信息等事件
- **自定义解码器**：通过工厂回调注册自定义 GMF 音视频解码元素，与内置解码器共存
- **按实例调参**（高级）：多 player 时可对每路 handle 单独覆盖 GMF task 与 buffer（内置默认见 `player_defaults_cfg.h`，API 见 `esp_player_advance.h`）

## 架构

`esp_player` 作为应用与 GMF 管线之间的协调层，负责状态机、命令分发与音视频同步，底层数据由 GMF Pipeline / Task / DataBus 驱动。

```mermaid
flowchart TB
  app["应用层"]
  api["esp_player_* API"]
  event["事件回调 / event_queue"]
  core["ESP Player Core\n(命令任务、状态机、A/V 同步)"]
  extractor["Extractor\n(解封装)"]
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

## API 概览

### 头文件分类

| 头文件 | 用途 |
|--------|------|
| `esp_player.h` | 模块 init/deinit + 核心播放控制 |
| `esp_player_types.h` | 类型定义（事件、错误码、轨道信息、帧结构等） |
| `esp_player_advance.h` | 高级能力：自定义解码器、解码子配置（`esp_player_set_dec_cfg`）、无容器喂帧、**按实例覆盖 GMF task / buffer**（`esp_player_set_task_config` / `esp_player_set_buffer_config`） |

### 生命周期 API

| API | 说明 |
|-----|------|
| `esp_player_init(config, handle)` | 初始化播放器，分配内部资源 |
| `esp_player_deinit(handle)` | 释放所有资源，handle 失效 |
| `esp_player_set_data_src(handle, src)` | 一次设置 AV mask、URL、同步模式（便捷接口） |
| `esp_player_set_av_mask(handle, mask)` | 单独配置音视频侧开关 |
| `esp_player_set_url(handle, url)` | 单独配置播放 URL，换源时调用 |
| `esp_player_set_sync_mode(handle, mode)` | 设置 A/V 同步模式（仅 `ESP_PLAYER_MASK_AV` 时有效） |

### 播放控制 API

| API | 说明 |
|-----|------|
| `esp_player_run(handle)` | 开始播放（非阻塞） |
| `esp_player_run_to_end(handle)` | 阻塞播放直到结束或出错 |
| `esp_player_pause(handle)` | 暂停 |
| `esp_player_resume(handle)` | 从暂停恢复 |
| `esp_player_stop(handle)` | 停止 |
| `esp_player_seek(handle, time_ms)` | 跳转到指定时间（毫秒） |
| `esp_player_set_speed(handle, speed)` | 设置倍速 |

### 状态与信息 API

| API | 说明 |
|-----|------|
| `esp_player_get_duration(handle, duration)` | 获取媒体总时长（毫秒） |
| `esp_player_get_play_time(handle, current_time)` | 获取当前播放进度（毫秒） |
| `esp_player_get_track_num(handle, type, track_num)` | 获取指定类型的轨道数 |
| `esp_player_get_track_info(handle, type, track_idx, track_info)` | 获取轨道信息（codec、采样率、分辨率等） |
| `esp_player_enable_track(handle, type, track_idx, enable)` | 选择或禁用指定轨道 |

### 事件

`esp_player_set_event_cb()` 注册同步回调，或用 `esp_player_set_event_queue()` 接收异步事件（元素大小为 `sizeof(esp_player_event_msg_t)`）。

| 事件 | 含义 |
|------|------|
| `ESP_PLAYER_EVENT_PLAYED` | 开始播放 |
| `ESP_PLAYER_EVENT_PAUSED` | 已暂停 |
| `ESP_PLAYER_EVENT_STOPPED` | 已停止 |
| `ESP_PLAYER_EVENT_SEEK_DONE` | Seek 完成 |
| `ESP_PLAYER_EVENT_FINISHED` | 播放结束 |
| `ESP_PLAYER_EVENT_BUFFERING` | 正在缓冲 |
| `ESP_PLAYER_EVENT_BUFFERED` | 缓冲完成，恢复播放 |
| `ESP_PLAYER_EVENT_ERROR` | 播放出错，`data` 字段携带 `esp_player_error_source_t` |
| `ESP_PLAYER_EVENT_TRACK_INFO_PARSED` | 轨道信息解析完成 |
| `ESP_PLAYER_EVENT_AUDIO_INFO_PARSED` | 音频信息解析完成 |
| `ESP_PLAYER_EVENT_VIDEO_INFO_PARSED` | 视频信息解析完成 |

## URL 格式

### 标准 URI（`esp_gmf_uri_parse`，RFC 3986 风格）

| scheme | 示例 | 说明 |
|--------|------|------|
| `file` | `file:///sdcard/music/test.mp3` | 本地文件（三斜杠标准形式） |
| `file` | `/sdcard/music/test.mp3` | 裸 VFS 路径，内部规范为 `file:///` |
| `file` | `file://sdcard/music/test.mp3` | 兼容双斜杠形式（`sdcard` 为路径的一部分） |
| `file` | `file:///sdcard/test.pcm?sr=48000&ch=2&bits=16` | 无头裸 PCM，须带 `?query` |
| `http` / `https` | `https://example.com/audio/test.mp4` | HTTP(S) 流 |
| `http` | `http://192.168.1.10:8080/stream.aac` | 指定端口的 HTTP |
| `https` | `https://user:pass@example.com/audio/test.mp4` | 可选 `user:pass@` 基本认证 |
| `https`（HLS） | `https://example.com/live/playlist.m3u8` | 路径含 `.m3u8` 自动识别为 HLS |
| `http` | `http://example.com/audio/test.pcm?sr=48000&ch=2&bits=16` | 网络裸 PCM，须带 `?query` |

scheme 语法、authority 规则及 `?query` 参数：见 `esp_player_set_url()`。

### 外部帧模式（无容器）

适用于蓝牙 A2DP 裸帧、麦克风 PCM、自定义编码帧等场景。URL 格式为 `fill:///name.codec[?params]` 或 `block:///name.codec[?params]`。

- `fill`：每次调用完整拷贝一帧
- `block`：零拷贝，调用方须保证 buffer 在解码完成前有效
- `name` 无实际语义，`.codec` 扩展名决定解码器

`esp_player_run()` 之后通过 `esp_player_submit_frame()` 喂帧（见 `esp_player_advance.h`）。`fill:///`、`block:///` 不支持 `ESP_PLAYER_MASK_AV`。

| 场景 | URL 示例 |
|------|----------|
| 裸 PCM | `fill:///test.pcm?sr=16000&ch=1&bits=16` |
| PCM（零拷贝） | `block:///test.pcm?sr=16000&ch=1&bits=16` |
| AAC（标准 ADTS） | `fill:///test.aac` |
| AAC（无 ADTS 头，如 BT A2DP） | `fill:///test.aac?no_adts=1` |
| HE-AAC 裸帧 | `fill:///test.aac?no_adts=1&aac_plus=1` |
| OPUS 裸帧 | `fill:///test.opus?sr=16000&ch=2&frame_dms=20` |

常用 query 参数：`sr`（采样率）、`ch`（声道数）、`bits`（位宽）、`no_adts`（AAC 无头）、`aac_plus`（HE-AAC）、`frame_dms`（OPUS/LC3 帧时长）、`plc`（SBC/LC3 丢包补偿）等；完整列表见 `esp_player_set_url()`。

## 快速开始

### 环境要求

- **ESP-IDF**：`>= 5.3`（见 `idf_component.yml`）

### 集成到工程

通过 ESP-IDF Component Manager：

```yaml
dependencies:
  espressif/esp_player:
    version: "^1.0"
```

### 配置

通过 `menuconfig` → **ESP Player**：

- **Enable Audio / Video Playback Path**：编译期音/视频通路
- **Input IO sources**：按需开启 `file://`、`http(s)://`、HLS（`.m3u8`）；未开启的 scheme 在 `esp_player_set_url()` 返回 `ESP_PLAYER_ERR_INVALID_ARG`。

task、buffer 及网络缓冲开关的内置默认值见 `player_defaults_cfg.h`。多实例或各路资源不同时，在 `esp_player_init()` 之后、`esp_player_run()` 之前对 handle 调用 `esp_player_set_task_config()` / `esp_player_set_buffer_config()` 覆盖即可。

### 初始化

按 `esp_player_set_av_mask()` 启用的通路配置 render handle（详见 `esp_player_config_t` 注释）：

| av_mask | 必填 handle |
|---------|-------------|
| `ESP_PLAYER_MASK_AUDIO` | `audio_render_hd` |
| `ESP_PLAYER_MASK_VIDEO` | `video_render_hd` |
| `ESP_PLAYER_MASK_AV` | 两者均必填 |

不使用的通路可传 NULL（如纯音频时 `video_render_hd = NULL`）。

```c
#include "esp_player.h"

esp_player_config_t config = ESP_PLAYER_CONFIG_DEFAULT();
config.audio_render_hd = audio_render_handle;   /* esp_audio_render_stream_handle_t，由 esp_audio_render_stream_get() 获取 */
config.video_render_hd = video_render_handle;   /* esp_video_render_handle_t，由 esp_video_render_create() 创建；纯音频可为 NULL */

esp_player_handle_t player = NULL;
esp_player_init(&config, &player);
```

### 播放音频文件

```c
static esp_player_err_t player_event_cb(esp_player_event_msg_t *msg, void *ctx)
{
    if (msg->event_type == ESP_PLAYER_EVENT_FINISHED) {
        /* 播放结束处理 */
    }
    if (msg->event_type == ESP_PLAYER_EVENT_ERROR) {
        /* 错误处理，msg->data 携带 esp_player_error_source_t */
    }
    return ESP_PLAYER_ERR_OK;
}

esp_player_set_event_cb(player, player_event_cb, NULL);

esp_player_data_src_t src = ESP_PLAYER_DATA_SRC("file:///sdcard/test.mp3", ESP_PLAYER_MASK_AUDIO);
esp_player_set_data_src(player, &src);
esp_player_run(player);
```

### 播放音视频文件

```c
esp_player_data_src_t src = ESP_PLAYER_DATA_SRC("file:///sdcard/test.mp4", ESP_PLAYER_MASK_AV);
esp_player_set_data_src(player, &src);
esp_player_run(player);
```

### 换源

换源时只需调用 `esp_player_set_url()`（mask 不变则无需重新设置），player 内部会自动完成旧管线的拆除与新源的重建：

```c
esp_player_stop(player);
esp_player_set_url(player, "file:///sdcard/next.mp3");
esp_player_run(player);
```

### 销毁

```c
esp_player_deinit(player);
```

## 性能

测试环境：

| 芯片 | CPU | SPI RAM | Flash |
|------|-----|---------|-------|
| ESP32-P4 | 360 MHz | 200 MHz | QIO |

播放本地 SD 卡文件， MP3 和 AAC 对比：

| 格式 | 内存 (KB) | CPU (%) | 启播延时 (ms) | 复播延时 (ms) |
|------|-----------|---------|--------------|---------------|
| MP3 | 95 | 3.1 | 188 | 124 |
| AAC | 105 | 3.6 | 185 | 126 |

- **启播延时**：`esp_player_run()` 到第一帧音频输出的时间
- **复播延时**：`esp_player_stop()` + `esp_player_run()`（不换 URL）到第一帧音频输出的时间

### 内存优化

在 menuconfig 中按实际格式和硬件裁剪功能：

| 区域 | Menuconfig | 建议（本地纯音频） |
|------|------------|-------------------|
| Player | `Enable Audio Playback Path` / `Enable Video Playback Path` | 开音频、关视频 |
| Player | `Network Playback Tuning` | 仅本地文件时可关 |
| Player | `Player Pipeline GMF Tasks` | 栈余量允许时可下调（默认 5120） |
| Player | 多实例 | 各路差异大时用 `esp_player_set_task_config()` / `esp_player_set_buffer_config()`（`esp_player_advance.h`）；默认值见 `player_defaults_cfg.h` |
| Extractor | `Espressif Extractor Configuration`，各容器 extractor | 只开实际使用的格式 |
| Audio codec | `Audio Codec Configuration`，decoders | 只开所需 decoder，encoder 全关 |
| Codec device | `Audio Codec Device Configuration` | 只开板载 codec |

开启 PSRAM（`SPIRAM`、`FREERTOS_TASK_CREATE_ALLOW_EXT_MEM`）可将大栈和缓冲放到片外，降低 internal RAM 占用。

## 示例

示例代码请参阅 [示例](./examples) 文件夹，包含音频播放与音视频播放程序。
