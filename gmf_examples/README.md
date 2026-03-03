# ESP-GMF-Examples

- [![Component Registry](https://components.espressif.com/components/espressif/gmf_examples/badge.svg)](https://components.espressif.com/components/espressif/gmf_examples)

- [中文版](./README_CN.md)

ESP GMF Examples is a component that collects GMF-related examples, mainly designed to help users quickly access GMF examples through [ESP Registry](https://components.espressif.com/). It is a virtual component that cannot be depended on by any component or project, which means you cannot use `idf.py add-dependency "espressif/gmf_examples"`. The related example list is as follows:

**Basic Examples**

| Example Name | Description | Typical Scenarios | Resource Requirements |
|---------|---------|---------|---------|
| [pipeline_play_embed_music](./basic_examples/pipeline_play_embed_music) | Play audio embedded in Flash | Boot tone, built-in ringtone, no external storage | Audio DAC, speaker |
| [pipeline_play_sdcard_music](./basic_examples/pipeline_play_sdcard_music) | Play SD card music | Local music playback, play from storage card | SD card, Audio DAC, speaker |
| [pipeline_play_http_music](./basic_examples/pipeline_play_http_music) | Play HTTP/network music (online streaming, no m3u8) | Network music playback, online streaming | Wi-Fi, Audio DAC, speaker |
| [pipeline_record_sdcard](./basic_examples/pipeline_record_sdcard) | Record and encode audio to SD card | Low-power voice recorder, offline voice recording | Microphone, Audio ADC, SD card |
| [pipeline_http_download_to_sdcard](./basic_examples/pipeline_http_download_to_sdcard) | Download HTTP file and write to SD card (with overall speed optimization) | Firmware/audio download, offline cache, download speed test | Wi-Fi, SD card |
| [pipeline_record_http](./basic_examples/pipeline_record_http) | Record from mic, encode and upload to HTTP server | Voice upload, online recording, stream to server | Microphone, Audio ADC, Wi-Fi |
| [pipeline_audio_effects](./basic_examples/pipeline_audio_effects) | Play audio with various effects and mixing | Effect simulation, EQ/DRC, multi-channel mixing | Audio DAC, speaker |
| [pipeline_loop_play_no_gap](./basic_examples/pipeline_loop_play_no_gap) | Seamless loop playback of multiple files | Background music loop, continuous playback | SD card, Audio DAC, speaker |
| [play_embed_music](../packages/esp_board_manager/examples/play_embed_music) | Play Flash-embedded WAV with `esp_board_manager` (no GMF) | Board-level playback, quick board verification | Audio DAC, speaker |
| [play_sdcard_music](../packages/esp_board_manager/examples/play_sdcard_music) | Play SD card music with `esp_board_manager` (no GMF) | Board-level SD card music playback | SD card, Audio DAC, speaker |
| [record_to_sdcard](../packages/esp_board_manager/examples/record_to_sdcard) | Record and save WAV to SD card with `esp_board_manager` (no GMF) | Board-level recording and WAV save | Microphone, Audio ADC, SD card |

**Advanced Examples**

| Example Name | Description | Typical Scenarios | Resource Requirements |
|---------|---------|---------|---------|
| [audio_capture](../packages/esp_capture/examples/audio_capture) | Use `esp_capture` for audio capture (basic/AEC/file recording/custom processing) | Mic capture, AEC, record to file (no pipeline setup required) | Microphone, Audio ADC, SD card |
| [video_capture](../packages/esp_capture/examples/video_capture) | Use `esp_capture` for audio/video capture (basic/single-frame/recording/overlay/dual-path) | Camera capture, recording, overlay and save to SD card (no pipeline setup required) | Camera (DVP, MIPI), Microphone, Audio ADC, JPEG Encoder, H264 Encoder, SD card |
| [audio_render](../packages/esp_audio_render/examples/audio_render) | Use `esp_audio_render` for PCM rendering, mixing, and network audio decode playback | Network audio, mixing output | Wi-Fi, Audio DAC, speaker |
| [simple_piano](../packages/esp_audio_render/examples/simple_piano) | Use `esp_audio_render` for polyphonic piano synthesizer, multi-track and real-time rendering | Music creation, piano demo | Audio DAC, speaker |
| [aec_rec](../elements/gmf_ai_audio/examples/aec_rec) | Use `gmf_ai_audio` for playing SD card music while AEC echo-cancelled recording | Hands-free, meeting recording, quick AEC verification | Microphone, Audio ADC, Audio DAC, speaker, SD card |
| [wwe](../elements/gmf_ai_audio/examples/wwe) | Use `gmf_ai_audio` for speech recognition (wake word, VAD, command) | Voice wake-up, command recognition | Microphone, Audio ADC, Audio DAC, speaker, SD card |
| [simple_player](../packages/esp_audio_simple_player/test_apps) | Use `esp_audio_simple_player` for simple audio player example (SD card/Flash/HTTP playback) | Quick verification of multiple sources (no pipeline setup required) | SD card or Wi-Fi, Audio DAC, speaker |
| [baidu_rtc](https://github.com/espressif/esp-adf/tree/master/examples/ai_agent/baidu_rtc) | Use Baidu RTC for real-time voice dialogue, ASR/TTS, network music, video call, and 3A audio processing | Smart speaker, voice interaction device, smart home, audio/video call | Microphone, Audio ADC, Audio DAC, speaker, Wi-Fi |
| [coze_ws_app](https://github.com/espressif/esp-adf/tree/master/examples/ai_agent/coze_ws_app) | Use Coze WebSocket OpenAPI for bidirectional streaming voice dialogue with agent (direct/wake/key-trigger, 3A) | Voice assistant, agent conversation | Microphone, Audio ADC, Audio DAC, speaker, Wi-Fi |

# Usage

Below is an example of how to get and compile the project using `pipeline_play_embed_music`. Before starting, make sure you have a working [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html) environment.

### 1. Create the Example Project

Create the `pipeline_play_embed_music` example project based on the `gmf_examples` component (using version v0.7.0 as an example; update the version as needed):

```shell
idf.py create-project-from-example "espressif/gmf_examples=0.7.0:pipeline_play_embed_music"
```

### 2. Build and Flash the Project Using an ESP32-S3 Board

```shell
cd pipeline_play_embed_music
idf.py set-target esp32s3
idf.py -p YOUR_PORT flash monitor
```
