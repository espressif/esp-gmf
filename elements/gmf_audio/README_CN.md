# ESP-GMF-Audio

- [![Component Registry](https://components.espressif.com/components/espressif/gmf_audio/badge.svg)](https://components.espressif.com/components/espressif/gmf_audio)

- [English](./README.md)

ESP GMF Audio 是 GMF 音频处理相关元素的集合，包括音频编码，解码和音频效果处理算法。目前已支持的 Audio 模块参见下表。

| 名称 | 标签 | 功能 |  函数方法   | 输入端口 | 输出端口 |  输入阻塞时间 | 输出阻塞时间 |依赖音频信息 |
|:----:| :-----: | :----: | :----: |:----: |:----: |:----: |:----: |:---- |
|  AUDIO_DEC | aud_dec | 音频解码：MP3,AAC,AMRNB,<br>AMRWB,FLAC,WAV,M4A,TS  | 无 |  单个 |  单个  | 可用户配置，默认是最大延迟 |可用户配置，默认是最大延迟 |否 |
|  AUDIO_ENC | aud_enc | 音频编码：AAC,AMRNB,AMRWB,<br>ADPCM,OPUS,PCM  | 无 |  单个 |  单个  | 可用户配置，默认是最大延迟 |可用户配置，默认是最大延迟 |是 |
|  RATE_CVT| aud_rate_cvt | 音频采样率调节  | `set_dest_rate` |  单个 |  单个  |最大延迟 |最大延迟| 是 |
|  BIT_CVT | aud_bit_cvt | 音频比特位转换  | `set_dest_bits`| 单个 |  单个  |最大延迟 |最大延迟| 是 |
|  CH_CVT  | aud_ch_cvt | 音频声道数转换   | `set_dest_ch`|  单个 |  单个  |最大延迟 |最大延迟| 是 |
|  ALC     | aud_alc | 音频音量调节    | `set_gain`<br>`get_gain`| 单个 |  单个  |最大延迟 |最大延迟| 是 |
|  EQ      | aud_eq | 音频均衡器调节  |`set_para`<br>`get_para`<br>`enable_filter`<br>`disable_filter`  |单个 |单个|最大延迟 |最大延迟|是 |
|  FADE    | aud_fade | 音频淡入淡出效果    |`set_mode`<br>`get_mode`<br>`reset_weight` | 单个 |  单个  |最大延迟 |最大延迟 |是 |
|  SONIC   | aud_sonic | 音频变速变调效果    |`set_speed`<br>`get_speed`<br>`set_pitch`<br>`get_pitch`| 单个 | 单个 |最大延迟 |最大延迟| 是 |
|  MIXER   | aud_mixer | 音频混音效果  |`set_info`<br>`set_mode`|  多个 |  单个  | 第一路阻塞时间为0，其他路阻塞时间为最大延迟 |最大延迟| 否 |
|INTERLEAVE| aud_intlv | 数据交织    | 无 | 多个 |  单个  | 可用户配置，默认是最大延迟 |最大延迟| 是 |
|DEINTERLEAVE| aud_deintlv | 数据解交织 | 无| 单个 |  多个  |最大延迟|可用户配置，默认是最大延迟 |是 |

## 示例
ESP GMF Audio 常常组合成管道使用，示例代码请参考 [test_app](../test_apps/main/elements/gmf_audio_play_el_test.c)。

此外，也可通过以下命令创建并编译工程，以 `pipeline_play_embed_music` 示例工程为例。开始之前需要有可运行的 [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html) 环境。

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
