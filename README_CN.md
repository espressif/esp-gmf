# ESP-GMF
[English](./README.md)

ESP-GMF 全称 Espressif General Multimedia Framework，是乐鑫开发的应用于 IoT 多媒体领域的轻量级通用软件框架。它灵活性高，可扩展性强，专为 IoT 芯片量身打造，RAM 资源占用只有 7 KB。ESP-GMF 可应用于音频、图像、视频等产品，还可应用于任何流式处理数据的产品中。

ESP-GMF 包含 GMF-Core、 Elements、Packages 和 GMF-Examples 四个模块。

- **GMF-Core** 是 ESP-GMF 软件框架的核心，有 GMF-Element、GMF-Pipeline、GMF-Task 等主要部件。
- **Elements** 是基于 GMF-Core 实现的各种具体功能 element，比如音视频编解码算法、音效处理算法 和 AI 算法等。
- **Packages** 基于 GMF-Pipeline 实现的 High Level 功能的组件示例，比如 **ESP Audio Simple Player** 是一个简单音频解码播放器。
- **GMF-Examples** 提供如何使用 GMF-Pipeline 实现简单功能的示例，比如播放 flash 或 SD 卡中的音乐。

# ESP-GMF 组件介绍

ESP-GMF 各个模块以组件的形式存在，组件又按功能分为**原子组件**、**基础组件**和**高级组件**。在开发项目时，推荐使用官方仓库的 elements 和 IOs 组件进行开发，也可以自行创建 element 和 IO 组件来扩展其应用场景。

## 原子组件

原子组件是 ESP-GMF 不可或缺的、核心的基础构建单元。

|  组件名称 |  功能 | 依赖的组件  |
| :------------: | :------------:|:------------ |
|  [gmf_core](./gmf_core) | GMF 基础框架  |  无 |

## 基础组件

基础组件是 ESP-GMF 中的中间层模块，承担数据处理和音视频流编解码等核心能力。这些组件具备清晰的输入/输出接口，专注于一个具体的任务，具有复用性和可组合性。它们既可以单独使用，也常用于构建复杂应用程序。

|  组件名称 |  功能 | 依赖的组件  |
| :------------: | :------------:|:------------ |
|  [gmf_audio](./elements/gmf_audio) | GMF 音频编解码和<br>音效处理 element  | - `gmf_core`<br>- `esp_audio_effects`<br> - `esp_audio_codec` |
|  [gmf_misc](./elements/gmf_misc) | 工具类 element   | 无  |
|  [gmf_io](./elements/gmf_io) | 文件、flash、HTTP 输入输出  | - `gmf_core`<br>- `esp_codec_dev`  |
|  [gmf_ai_audio](./elements/gmf_ai_audio) | 智能语音算法和<br>语音识别 element | - `esp-sr`<br>- `gmf_core` |
|  [gmf_video](./elements/gmf_video) | GMF 视频编解码和<br>视频效果处理 element  | - `gmf_core`<br>- `esp_video_codec` |

## 高级组件

高级组件是 ESP-GMF 中面向特定应用场景的封装模块，通常由多个基础功能组件甚至原子组件组合而成。它们封装了常见的多媒体业务流程，隐藏了底层的 pipeline 构建和组件配置逻辑，提供简单易用的接口，帮助用户快速实现复杂的功能，以简化用户开发流程，便于快速集成。该分类还包括一些工具类模块和示例集合。

|  组件名称 |  功能 | 依赖的组件  |
| :------------: | :------------:|:------------ |
|  [esp_audio_simple_player](./packages/esp_audio_simple_player) | 简单的音频播放器 | - `gmf_audio`<br>- `gmf_io` |
|  [gmf_loader](./packages/gmf_loader) | 使用 `Kconfig` 选择的配置<br>设置给定的 GMF Pool | - `gmf_core`<br>- `gmf_io`<br>- `gmf_audio`<br>- `gmf_misc`<br>- `gmf_video`<br>- `gmf_ai_audio`<br>- `esp_codec_dev`<br>- `esp_audio_codec`<br>- `esp_video_codec` |
|  [gmf_app_utils](./packages/gmf_app_utils) | 常用外设配置，单元测试工具<br>内存泄漏检测工具 | - `gmf_core`<br>- `protocol_examples_common`<br>- `codec_board` |
|  [esp_capture](./packages/esp_capture) | 易用的音视频采集器 | - `gmf_core`<br>- `gmf-audio`<br>- `gmf-video`<br>- `esp_muxer`<br>- `esp_codec_dev`<br>- `esp-sr`<br>- `esp_video`<br>- `esp32-camera`|

# ESP-GMF 使用说明

GMF-Core API 的简单示例代码请参考 [test_apps](./gmf_core/test_apps/main/cases/gmf_pool_test.c)，Elements 实际应用示例请参考 [ examples ](./gmf_examples/basic_examples/)。

# 常见问题

- **ESP-GMF 和 ESP-ADF 有什么区别？**

  ESP-ADF 是一个包含很多模块的功能性仓库，比如 `audio_pipeline`、`services`、`peripherals` 和 `audio boards` 等，它常应用与比较复杂的项目中。ESP-GMF 是将 `audio_pipeline` 独立出来并进行功能扩展，使其支持音频、视频、图像等流式数据的应用场景。ESP-GMF 按功能分为不同的组件，灵活性优于 ESP-ADF 的 `audio_pipeline`，例如用于简单流式数据的处理，从 SD 卡/flash 播放一个音频，以及多个组件结合使用提供较复杂的功能模块（如音频播放器 `esp_audio_simple_player `）。ESP-ADF 的后期版本会使用 ESP-GMF 替代 `audio_pipeline` 模块。
