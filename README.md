# ESP-GMF
[中文版](./README_CN.md)

The Espressif General Multimedia Framework (ESP-GMF) is a lightweight and versatile software framework developed by Espressif specifically for IoT multimedia applications. It is highly flexible and scalable and tailored for IoT chips, with RAM usage as low as 7 KB. ESP-GMF supports applications in audio, image, and video processing, as well as any data-streaming product.

ESP-GMF consists of four modules: GMF-Core, Elements, Packages, and GMF-Examples.

- **GMF-Core** is the core of the ESP-GMF software framework and includes the main components such as GMF-Element, GMF-Pipeline, and GMF-Task.
- **Elements** implements various functional elements based on GMF-Core, such as codec algorithms, audio processing algorithms, and more.
- **Packages** implements high-level functional components based on GMF-Pipeline, such as the **ESP Audio Simple Player**, which is a basic audio decoding player.
- **GMF-Examples** provides examples of how to use GMF-Pipeline to implement simple functionalities, such as playing music from flash or an SD card.

# ESP-GMF Component Overview

ESP-GMF modules exist as components, which are categorized by functionality into **atomic components**, **basic components** and **advanced components**. When developing a project, it is recommended to use the elements and IOs components from the official repository. You can also create your own elements and IO components to extend its application scenarios.

## Atomic Components

Atomic components are essential, core building of ESP-GMF.

| Component Name | Function | Dependent Components |
| :------------: | :------------: | :------------ |
| [gmf_core](./gmf_core) | Core framework for GMF | None |

## Basic Components

Basic components are intermediate-layer modules in ESP-GMF that handle data processing and core capabilities like audio/video stream encoding/decoding. These components have clear input/output interfaces, focus on specific tasks, and are reusable and composable. They can be used independently or commonly combined to build complex applications.

| Component Name | Function | Dependent Components |
| :------------: | :------------: | :------------ |
| [gmf_audio](./elements/gmf_audio) | Elements for audio encoding, decoding,<br>and sound effects processing | - `gmf_core`<br>- `esp_audio_effects`<br>- `esp_audio_codec` |
| [gmf_misc](./elements/gmf_misc) | Utility elements | None |
| [gmf_io](./elements/gmf_io) | Input/output for files, flash, and HTTP | - `gmf_core`<br>- `esp_codec_dev` |
| [gmf_ai_audio](./elements/gmf_ai_audio) | Elements for intelligence audio processing<br>and speech recognition | - `esp-sr`<br>- `gmf_core` |
| [gmf_video](./elements/gmf_video) | Elements for video encoding, decoding,<br>and video effects processing | - `gmf_core`<br>- `esp_video_codec` |

## Advanced Components

Advanced components are encapsulation modules in ESP-GMF that target specific application scenarios, typically composed of multiple basic components or even atomic components. They encapsulate common multimedia business processes, hide the underlying pipeline construction and component configuration logic, provide simple and easy-to-use interfaces to help users quickly implement complex functions, simplify user development processes, and facilitate rapid integration. This category also includes utility modules and example collections.

| Component Name | Function | Dependent Components |
| :------------: | :------------: | :------------ |
| [esp_audio_simple_player](./packages/esp_audio_simple_player) | Simple audio player | - `gmf_audio`<br>- `gmf_io` |
| [gmf_loader](./packages/gmf_loader) | Set up the given GMF pool using the <br> configuration selected via `Kconfig` | - `gmf_core`<br>- `gmf_io`<br>- `gmf_audio`<br>- `gmf_misc`<br>- `gmf_video`<br>- `gmf_ai_audio`<br>- `esp_codec_dev`<br>- `esp_audio_codec`<br>- `esp_video_codec` |
| [gmf_app_utils](./packages/gmf_app_utils) | Common peripheral configuration, unit <br>testing tools Memory leak detection tools | - `gmf_core`<br>- `protocol_examples_common`<br>- `codec_board`|
| [esp_capture](./packages/esp_capture) | High-level multimedia capture module | - `gmf_core`<br>- `gmf-audio`<br>- `gmf-video`<br>- `esp_muxer`<br>- `esp_codec_dev`<br>- `esp-sr`<br>- `esp_video`<br>- `esp32-camera`|


# ESP-GMF Usage Guide

For a basic example of GMF-Core API usage, see [test_apps](./gmf_core/test_apps/main/cases/gmf_pool_test.c). For practical application examples of elements, see the [ examples ](./gmf_examples/basic_examples/).

# Frequently Asked Questions

- **What is the difference between ESP-GMF and ESP-ADF?**

   ESP-ADF is a functional repository that includes many modules, such as `audio_pipeline`, `services`, `peripherals`, and `audio boards`. It is commonly used for more complex projects. ESP-GMF is an extension of the `audio_pipeline` module, designed to support applications for streaming audio, video, and image data. ESP-GMF offers a more modular structure than ESP-ADF's `audio_pipeline` by organizing functionality into different components, making it more flexible. For example, it is suitable for simple data streaming, such as playing audio from SD card/flash, or combining multiple components to provide more complex modules (e.g., audio player `esp_audio_simple_player`). Future versions of ESP-ADF will replace the `audio_pipeline` module with ESP-GMF.
