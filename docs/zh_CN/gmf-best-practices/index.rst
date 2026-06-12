ESP-GMF 最佳实践
==============================

:link_to_translation:`en:[English]`

本章结合 ``gmf_examples`` 中的示例工程，介绍 ESP-GMF 在实际项目中的开发流程、典型应用方式与进阶开发技巧，帮助开发者熟悉 GMF 并将功能集成到产品方案中。

`gmf_examples <https://github.com/espressif/esp-gmf/tree/main/gmf_examples>`__ 是官方维护的 GMF 示例集合。当前基础示例位于 ``gmf_examples/basic_examples``\ ，用于演示处理链（pipeline）的搭建、播放、录音、带容器封装录音、音效、循环播放、多源切换和 HTTP 下载等基础用法。所有示例都可以通过 ESP-IDF Component Manager 直接创建：

.. code:: shell

    idf.py create-project-from-example "espressif/gmf_examples=<version>:<example_name>"

例如，基于 ``pipeline_play_embed_music`` 创建工程：

.. code:: shell

    idf.py create-project-from-example "espressif/gmf_examples=0.8.0:pipeline_play_embed_music"

基础功能示例包括：

- `pipeline_play_embed_music <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_play_embed_music>`__\ ：播放内嵌 Flash 音频
- `pipeline_play_sdcard_music <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_play_sdcard_music>`__\ ：播放 SD 卡音乐
- `pipeline_play_http_music <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_play_http_music>`__\ ：播放 HTTP/网络音乐
- `pipeline_record_sdcard <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_record_sdcard>`__\ ：音频录制编码后保存到 SD 卡
- `pipeline_record_audio_muxer <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_record_audio_muxer>`__\ ：编码后由 muxer 封装为多种容器格式并保存到 SD 卡
- `pipeline_record_http <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_record_http>`__\ ：麦克风录制并编码上传 HTTP 服务器
- `pipeline_audio_effects <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_audio_effects>`__\ ：播放音频并施加多种特效与混音
- `pipeline_howl <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_howl>`__\ ：SD 卡伴奏解码后与麦克风（\ ``aud_howl`` 啸叫抑制）混音播放
- `pipeline_loop_play_no_gap <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_loop_play_no_gap>`__\ ：多文件无缝循环播放
- `pipeline_play_multi_source_music <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_play_multi_source_music>`__\ ：多源音频播放器，支持从 HTTP、SD 卡、Flash 三种音频源播放音乐
- `pipeline_http_download_to_sdcard <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_http_download_to_sdcard>`__\ ：下载 HTTP 文件并写入 SD 卡（整体速度优化）

组件示例请参考各组件目录下的 ``test_apps/`` 或 ``examples/`` 文件夹，例如 ``packages/esp_capture/examples`` 等。

更多完整应用示例可参考 `ESP-ADF examples <https://github.com/espressif/esp-adf/tree/master/adf_examples>`__\ 。ESP-ADF 示例覆盖更完整的音频应用场景，可作为 ESP-GMF 组件组合和产品化流程的参考。
