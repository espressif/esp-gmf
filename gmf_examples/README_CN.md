# ESP-GMF-Examples

- [![Component Registry](https://components.espressif.com/components/espressif/gmf_examples/badge.svg)](https://components.espressif.com/components/espressif/gmf_examples)

- [English](./README.md)

ESP GMF Examples 是一个汇集了 GMF 相关 example 的组件，其主要目的是方便用户通过 [ESP Registry](https://components.espressif.com/) 快速获取 GMF example。它是一个虚拟的组件，不能被任何组件或工程依赖，也就是说不能使用`idf.py add-dependency "espressif/gmf_examples"` 命令。本集合示例列表如下：

| 示例名称 | 功能描述 | 主要组件 | 数据流向 |
|---------|---------|---------|---------|
| [pipeline_play_embed_music](./basic_examples/pipeline_play_embed_music) | 播放嵌入到 Flash 中的音乐 | - aud_dec<br>- aud_bit_cvt<br>- aud_rate_cvt<br>- aud_ch_cvt | Flash -> 解码器 -> 音频处理 -> 输出设备 |
| [pipeline_play_sdcard_music](./basic_examples/pipeline_play_sdcard_music) | 播放 SD 卡中的音乐 | - aud_dec<br>- aud_rate_cvt<br>- aud_ch_cvt<br>- aud_bit_cvt | SD卡 -> 解码器 -> 音频处理 -> 输出设备 |
| [pipeline_record_sdcard](./basic_examples/pipeline_record_sdcard) | 录制音频到 SD 卡 | - aud_enc | 输入设备 -> 编码器 -> SD卡 |

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
