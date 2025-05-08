# ESP Audio Simple Player

- [![Component Registry](https://components.espressif.com/components/espressif/esp_audio_simple_player/badge.svg)](https://components.espressif.com/components/espressif/esp_audio_simple_player)

[English](./README.md)

ESP Audio Simple Player 是由 Espressif 基于 ESP-GMF 设计的一个简单易用的音频播放器。它具有模块化、可定制、低资源占用等设计特点，能够简化音频处理开发流程，非常适用于物联网设备的音频应用场景，为开发者提供了一种高效、灵活的音频播放解决方案。

## 特性
- 支持常见音频格式，包括 AAC、MP3、AMR、FLAC、WAV、M4A、RAW_OPUS 和 TS
- 支持可配置音频变换器，包括 Bit Depth 转换、Channel 转换和 Sample Rate 转换
- 提供同步和异步播放接口
- 支持定制化的 IO stream 和音频处理元素

## 功能详解
ESP Audio Simple Player 通过将解码器和音频变换器构建为一个音频处理 Pipeline，实现模块化的音频处理。音频输入源由预置的 IO Stream 提供，数据依次经过解码器和音频变换器处理后，通过回调函数输出给用户层，方便用户根据具体场景进行适配，实现灵活的音频播放功能。

播放器采用 URI 作为资源输入路径，其中 URI 的 scheme 部分用于自动选择 IO Stream 类型（如 HTTP 流、本地文件或嵌入式闪存），URI 文件后缀则用于判断音频格式，确保播放器自动选择正确的解码器格式（如 MP3、WAV 和 AAC）。播放器的运行状态和音频解码信息以事件形式传递给应用层，便于用户及时处理。

ESP Audio Simple Player 不仅功能全面，还特别注重资源的高效利用。其支持音频变换器功能包括 Bit Depth 转换、Channel 转换和 Sample Rate 转换（Sample Rate 转换默认开启），支持 IO Stream 有 HTTP、File 和嵌入式 Flash 三种。用户通过 Menuconfig 配置选项，可根据硬件资源和应用需求灵活裁剪音频变换器功能和 IO Stream，从而优化资源利用率，降低设备的内存和处理器负载。

此外，Audio Simple Player 提供了接口，允许用户注册自定义的 IO 和音频处理元素。开发者可以轻松集成自己的定制化模块，进一步扩展播放器功能，满足特定的音频处理需求。

## 配置优化
在终端中执行以下命令，打开 Menuconfig 界面进行配置：
```
idf.py menuconfig
```

### 音频变换器和 IO Stream
在 Menuconfig 界面中，进入 `Component config` -> `ESP Audio Simple Player`，根据需要选择音频变换器和 IO stream，同时配置音频变换器的输出参数 Bit Depth、Channel 和 Sample Rate 等。

### 音频格式
在 Menuconfig 界面中，进入 `Component config` -> `Audio Codec Configuration` -> `Audio Decoder Configuration` and `Audio Simple Decoder Configuration`, 根据需要选择支持的音频解码格式。该选项可以大幅减小编译后的二进制文件大小，节省 Flash 资源，也可减少一定 RAM 资源。

## 使用示例
Audio Simple Player 的使用示例可参考 [test_apps](./test_apps/main/aud_simp_player_test.c)。