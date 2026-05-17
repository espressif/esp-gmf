# ESP-GMF-Examples

- [![Component Registry](https://components.espressif.com/components/espressif/gmf_examples/badge.svg)](https://components.espressif.com/components/espressif/gmf_examples)

- [English](./README.md)

ESP GMF Examples 是一个汇集了 GMF 相关 example 的组件，其主要目的是方便用户通过 [ESP Registry](https://components.espressif.com/) 快速获取 GMF example。它是一个虚拟的组件，不能被任何组件或工程依赖，也就是说不能使用`idf.py add-dependency "espressif/gmf_examples"` 命令。相关示例列表如下：

**基础功能示例**

| 示例名称 | 功能描述 | 典型场景 | 资源要求 |
|---------|---------|---------|---------|
| [pipeline_play_embed_music](./basic_examples/pipeline_play_embed_music) | 播放内嵌 Flash 音频 | 开机提示音、内置铃声、无外存设备 | Audio DAC、扬声器 |
| [pipeline_play_sdcard_music](./basic_examples/pipeline_play_sdcard_music) | 播放 SD 卡音乐 | 本地音乐播放、从存储卡选曲 | SD 卡、Audio DAC、扬声器 |
| [pipeline_play_http_music](./basic_examples/pipeline_play_http_music) | 播放 HTTP/网络音乐（在线流式，不支持 m3u8） | 网络音乐播放、在线流媒体 | Wi-Fi、Audio DAC、扬声器 |
| [pipeline_record_sdcard](./basic_examples/pipeline_record_sdcard) | 音频录制编码后保存到 SD 卡 | 低功耗录音笔、离线语音记录 | 麦克风、Audio ADC、SD 卡 |
| [pipeline_record_audio_muxer](./basic_examples/pipeline_record_audio_muxer) | 编码后由 muxer 封装为多种容器格式并保存到 SD 卡 | 带封装本地录音、会议记录、MP4/TS 等 | 麦克风、Audio ADC、SD 卡 |
| [pipeline_http_download_to_sdcard](./basic_examples/pipeline_http_download_to_sdcard) | 下载 HTTP 文件并写入 SD 卡（整体速度优化） | 固件/音频下载、离线缓存、下载测速 | Wi-Fi、SD 卡 |
| [pipeline_record_http](./basic_examples/pipeline_record_http) | 麦克风录制并编码上传 HTTP 服务器 | 语音上传、在线录音、推流到服务端 | 麦克风、Audio ADC、Wi-Fi |
| [pipeline_audio_effects](./basic_examples/pipeline_audio_effects) | 播放音频并施加多种特效与混音 | 音效模拟、EQ/DRC、多路混音 | Audio DAC、扬声器 |
| [pipeline_howl](./basic_examples/pipeline_howl) | SD 卡伴奏解码后与麦克风（`aud_howl` 啸叫抑制）混音播放 | KTV/扩声啸叫抑制、多路 pipeline 与 ringbuf 互连参考 | SD 卡、麦克风、Audio ADC、Audio DAC、扬声器 |
| [pipeline_loop_play_no_gap](./basic_examples/pipeline_loop_play_no_gap) | 多文件无缝循环播放 | 背景音乐循环、单曲连续播放 | SD 卡、Audio DAC、扬声器 |
| [pipeline_play_multi_source_music](./basic_examples/pipeline_play_multi_source_music) | 多源音频播放器，支持从 HTTP、SD 卡、Flash 三种音频源播放音乐 | 提示音插入播放、多源音乐切换 | SD 卡、Wi-Fi、Audio DAC、扬声器 |

> 说明：板级例程 `play_embed_music`、`play_sdcard_music`、`record_to_sdcard`（不依赖 GMF）已迁至 [esp-board-manager 仓库](https://github.com/espressif/esp-board-manager/tree/main/esp_board_manager/examples)。

**高级功能示例**

| 示例名称 | 功能描述 | 典型场景 | 资源要求 |
|---------|---------|---------|---------|
| [audio_capture](../packages/esp_capture/examples/audio_capture) | 使用 `esp_capture` 实现音频采集（基础/AEC/文件录制/自定义处理） | 麦克风采集、AEC、录制为文件（无需自行搭建 pipeline） | 麦克风、Audio ADC、SD 卡 |
| [video_capture](../packages/esp_capture/examples/video_capture) | 使用 `esp_capture` 实现音视频采集（基础/单帧/录制/叠加/双路） | 摄像头采集、录像、叠加并保存到 SD 卡（无需自行搭建 pipeline） | Camera（DVP，MIPI）、麦克风、Audio ADC、JPEG Encoder、H264 Encoder、SD 卡 |
| [video_render](../packages/esp_video_render/examples/video_render) | 使用 `esp_video_render` 实现 MJPEG 视频渲染、双路并排播放与轻量 overlay | LCD 视频播放、双路预览、视频 UI 叠加 | LCD、SD 卡、PSRAM |
| [video_player](../packages/esp_video_render/examples/video_player) | 使用 `esp_video_render` 构建本地视频播放器，集成媒体提取、解码、渲染和 overlay UI | SD 卡本地视频播放、嵌入式播放器验证 | LCD、SD 卡、PSRAM |
| [dual_eyes](../packages/esp_video_render/examples/dual_eyes) | 使用 `esp_video_render` 实现左右眼双流同步渲染，支持单屏并排或双屏输出 | 机器人眼睛、AI 伴侣表情、双路同步播放 | LCD、SD 卡、PSRAM |
| [audio_render](../packages/esp_audio_render/examples/audio_render) | 使用 `esp_audio_render` 实现渲染 PCM、混音，网络音频解码播放 | 网络音频、混音输出 | Wi-Fi、Audio DAC、扬声器 |
| [simple_piano](../packages/esp_audio_render/examples/simple_piano) | 使用 `esp_audio_render` 实现复音钢琴合成器，多轨创作与实时渲染 | 音乐创作、钢琴演示 | Audio DAC、扬声器 |
| [asrc_demo](../packages/esp_asrc/examples/asrc_demo) | 使用 `esp_asrc` 实现音频采样率、声道数和位深转换 | 音频格式对齐、录音数据上采样 | SRAM、PSRAM（可选） |
| [aec_rec](../elements/gmf_ai_audio/examples/aec_rec) | 使用 `gmf_ai_audio` 实现播放 SD 卡音乐的同时完成 AEC 回声消除录制 | 免提通话、会议录音、快速验证 AEC 效果 | 麦克风、Audio ADC、Audio DAC、扬声器、SD 卡 |
| [wwe](../elements/gmf_ai_audio/examples/wwe) | 使用 `gmf_ai_audio` 实现语音识别（唤醒词、VAD、指令） | 语音唤醒、指令识别 | 麦克风、Audio ADC、Audio DAC、扬声器、SD 卡 |
| [simple_player](../packages/esp_audio_simple_player/test_apps) | 使用 `esp_audio_simple_player` 实现简单音频播放器示例（SD 卡/Flash/HTTP 播放） | 快速验证多种不同播放源（无需自行搭建 pipeline） | SD 卡或 Wi-Fi、Audio DAC、扬声器 |
| [audio_player](../packages/esp_player/examples/audio_player) | 使用 `esp_player` 实现完整音频播放（解复用、解码、播放控制；支持多路混音，无需自行搭建 pipeline） | 音频播放、多路音频混音 | SD 卡或 Wi-Fi、Audio DAC、扬声器 |
| [video_player](../packages/esp_player/examples/video_player) | 使用 `esp_player` 实现完整音视频播放（解复用、解码、A/V 同步、LCD 与扬声器输出，无需自行搭建 pipeline） | 带屏设备媒资播放、验证完整音视频链路 | SD 卡、LCD、Audio DAC、扬声器 |
| [baidu_rtc](https://github.com/espressif/esp-adf/tree/master/adf_examples/ai_agent/baidu_rtc/solutions/voice_assistant_app) | 使用百度 RTC 实现实时语音对话、ASR/TTS、网络音乐、视频对讲与 3A 音频处理 | 智能音箱、语音交互设备、智能家居、音视频通话 | 麦克风、Audio ADC、Audio DAC、扬声器、Wi-Fi |
| [coze_ws_app](https://github.com/espressif/esp-adf/tree/master/adf_examples/ai_agent/coze_ws_app) | 使用扣子 WebSocket OpenAPI 实现与智能体双向流式语音对话（直接对话/唤醒/按键打断、3A） | 语音助手、智能体对话 | 麦克风、Audio ADC、Audio DAC、扬声器、Wi-Fi |
| [esp_audio_analyzer_app](https://github.com/espressif/esp-adf/tree/master/adf_examples/checks/esp_audio_analyzer_app) | 使用 ESP 音频分析工具全面测试麦克风、扬声器及 AEC 功能 | 音频测试、声学调试、AEC 效果验证 | 麦克风、Audio ADC、Audio DAC、扬声器、Wi-Fi |
| [services_hub](https://github.com/espressif/esp-adf/tree/master/adf_examples/services_hub) | 通过 `adf_event_hub` 与 `esp_service_manager` 集成 Wi-Fi、OTA、CLI 与按键服务，可选支持 MCP | 多服务编排、OTA 验证、设备服务管理 | Wi-Fi、按键、OTA HTTP Server |
| [bt_audio](../packages/esp_bt_audio/examples/bt_audio) | 使用 `esp_bt_audio` 与 GMF pipeline 实现蓝牙音频（音频接收、音频发送、语音通话、媒体控制） | 蓝牙音箱、音频源 | 蓝牙、麦克风、Audio ADC、Audio DAC、扬声器、SD 卡 |

# 使用说明

下面以 `pipeline_play_embed_music` 为例，演示如何获取工程及编译。开始之前需要有可运行的 [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html) 环境。

### 1. 创建示例工程

基于 `gmf_examples` 组件创建 `pipeline_play_embed_music` 的示例（以 v0.7.0 版本为例， 请根据实际使用更新版本参数）

``` shell
idf.py create-project-from-example "espressif/gmf_examples=0.7.0:pipeline_play_embed_music"
```

### 2. 基于 ESP32S3 编译和下载

```shell
cd pipeline_play_embed_music
idf.py set-target esp32s3`
idf.py -p YOUR_PORT flash monitor
```
