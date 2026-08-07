.. _index-sec-00:

乐鑫通用多媒体框架指南
====================================

:link_to_translation:`en:[English]`

.. image:: ../_static/Banner_GMF.svg
   :alt: ESP-GMF Banner
   :width: 800px
   :align: center

|

.. list-table::
   :widths: 33 33 34

   * - |快速开始|_
     - |GMF 框架|_
     - |最佳实践|_
   * - `快速开始`_
     - `GMF 框架`_
     - `最佳实践`_

.. |快速开始| image:: ../_static/index/get-started.png
.. _快速开始: get-started/index.html

.. |GMF 框架| image:: ../_static/index/gmf-framework.png
.. _GMF 框架: gmf-framework/index.html

.. |最佳实践| image:: ../_static/index/gmf-best-practices.png
.. _最佳实践: gmf-best-practices/index.html

.. image:: ../_static/GMF_System_Diagram.png
   :alt: ESP-GMF System Diagram
   :width: 800px
   :align: center

|

.. _index-sec-01:

.. rubric:: 概述

`ESP-GMF <https://github.com/espressif/esp-gmf>`__\ （Espressif General Multimedia Framework）是乐鑫为 IoT 多媒体应用打造的轻量级通用软件框架。框架以模块化的处理单元（element）为基本单元，把数据流式处理拆解为可独立开发、可任意组合的工作单元；运行时由处理链（pipeline）串接处理单元、由执行线程（task）调度执行，整套 GMF-Core 在 IoT 芯片上的运行内存仅需约 7 KB。除音频处理外，框架同时支持图像、视频与任意流式数据的处理场景。

.. rubric:: 主要特性

- **轻量适配 IoT**\ ：面向资源受限的 ESP32 系列芯片设计，GMF-Core 运行时内存约 7 KB
- **多领域覆盖**\ ：在同一框架内统一音频、视频、图像与通用流式数据的处理
- **模块化组合**\ ：以处理单元为单元，按需拼装功能或扩展自定义处理单元与外部接口（IO）组件
- **易于开发**\ ：既能快速完成简单播放链路，也能扩展到多路混音、AI 语音、视频合成等复杂场景
- **组件丰富**\ ：内置音频编解码、视频编解码、AI 前端等高级应用组件
- **生态友好**\ ：构建在 ESP-IDF 与 Espressif Component Registry 之上，与既有组件生态互通

.. _index-sec-02:

.. rubric:: 系统模块

ESP-GMF 包含以下四个主要模块，按依赖层级自底向上排列：

- **GMF-Core**\ ：框架基础，提供处理链管理、任务调度、数据流控制等基础设施。包含处理单元、处理链、执行线程、数据总线（data bus）、注册池（pool）等基本对象。绝大多数应用通过更上层的组件间接使用 GMF-Core，扩展框架或编写自定义处理单元时直接面向 GMF-Core 编程
- **Elements**\ ：基于 GMF-Core 实现具体功能的中间层组件，包含以下子模块：

  - ``gmf_audio``\ ：音频编解码与效果
  - ``gmf_video``\ ：视频编解码与图像效果
  - ``gmf_io``\ ：文件 / 网络 / 闪存 / 编解码设备 IO
  - ``gmf_ai_audio``\ ：唤醒词、命令词、AEC 等 AI 前端处理算法
  - ``gmf_misc``\ ：杂项工具

- **Packages**\ ：面向特定应用场景的高级封装，把多个处理单元组装为常见多媒体业务流程，包含以下子模块：

  - ``esp_audio_simple_player``\ ：简易音频播放器
  - ``esp_capture``\ ：多媒体采集
  - ``esp_audio_render``\ ：多路混音渲染
  - ``esp_video_render``\ ：视频合成与显示 / 多路视频 / UI 合成
  - ``esp_bt_audio``\ ：经典蓝牙与 LE Audio 音频
  - ``esp_asrc``\ ：音频采样率 / 位深 / 声道转换
  - ``esp_board_manager``\ ：板级管理
  - ``gmf_loader``\ ：GMF 加载器
  - ``gmf_app_utils``\ ：应用辅助工具
  - ``gmf_fft``\ ：FFT 运算组件

- **GMF-Examples**\ ：演示如何使用 ESP-GMF 实现多种示例工程，覆盖音视频播放与录制、蓝牙音频、AI Agent 等多种场景

.. _index-sec-03:

.. rubric:: 与 ESP-ADF 的关系

ESP-GMF 演化自 `ESP-ADF <https://github.com/espressif/esp-adf>`_\ （Espressif Audio Development Framework）中的 ``audio_pipeline`` 模块，把处理链架构独立出来并扩展到视频、图像与通用流式数据。两者定位上的差异：

- ESP-ADF 是面向多媒体应用的功能性仓库，``master`` 分支作为后续主线，基于 ESP-GMF 提供组件，为客户提供产品级方案案例
- ESP-GMF 提供处理链架构，灵活支持音频、视频、图像与任意流式数据

.. toctree::
    :hidden:

    get-started/index
    gmf-framework/index
    gmf-best-practices/index
    resources
    contributions-guide
    glossary
    about
