# ESP-GMF-Examples

- [English](./README.md)

ESP GMF Examples 是一个汇集了 GMF 相关 example 的组件，其主要目的是方便用户通过 [ESP Registry](https://components.espressif.com/) 快速获取 GMF example。它是一个虚拟的组件，不能被任何组件或工程依赖，也就是说不能使用`idf.py add-dependency "espressif/gmf_examples"` 命令。本集合示例列表如下：
| 示例名称 | 功能描述 | 主要组件 | 数据流向 |
|---------|---------|---------|---------|
| pipeline_play_embed_music | 播放嵌入到 Flash 中的音乐 | - aud_dec<br>- aud_bit_cvt<br>- aud_rate_cvt<br>- aud_ch_cvt | Flash -> 解码器 -> 音频处理 -> 输出设备 |
| pipeline_play_sdcard_music | 播放 SD 卡中的音乐 | - aud_dec<br>- aud_rate_cvt<br>- aud_ch_cvt<br>- aud_bit_cvt | SD卡 -> 解码器 -> 音频处理 -> 输出设备 |
| pipeline_record_sdcard | 录制音频到 SD 卡 | - encoder | 输入设备 -> 编码器 -> SD卡 |
