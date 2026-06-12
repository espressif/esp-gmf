GMF Elements
======================

:link_to_translation:`en:[English]`

处理单元（element）是 ESP-GMF 中的中间层处理单元，承担多媒体数据的具体加工：编解码、格式转换、效果处理、外部接口（IO）读写等。每个处理单元都继承自 ``esp_gmf_element_t``\ ，对外暴露统一的生命周期功能函数（open / process / close）与基于数据端口（port）的 acquire-release 数据接口。处理单元基类、生命周期与能力描述见 :doc:`/gmf-framework/gmf-core/gmf-core-element`，数据通路细节见 :doc:`/gmf-framework/gmf-core/gmf-core-data-path`。

.. list-table::
   :widths: 20 30 50
   :header-rows: 1

   * - 类别
     - 目录
     - 角色
   * - I/O Elements
     - ``elements/gmf_io``
     - 与外部数据源 / 数据汇对接：file、http、embed_flash、i2s_pdm、codec_dev
   * - Audio Elements
     - ``elements/gmf_audio``
     - 音频编解码、采样率 / 声道 / 位深转换、EQ、Mixer、Fade、ALC、DRC
   * - Video Elements
     - ``elements/gmf_video``
     - 视频编解码、PPA 加速、缩放、旋转、叠加、帧率转换
   * - AI Audio Elements
     - ``elements/gmf_ai_audio``
     - 语音算法：AEC、NS、AGC、VAD、WWE、VCMD
   * - Miscellaneous
     - ``elements/gmf_misc``
     - Copier 等工具型处理单元，以及 gmf_loader 的动态选择能力

.. toctree::
    :maxdepth: 1

    gmf-io
    gmf-audio
    gmf-video
    gmf-ai-audio
    gmf-misc
