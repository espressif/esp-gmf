ESP Audio Simple Player
================================

:link_to_translation:`en:[English]`

ESP Audio Simple Player 是基于 ESP-GMF 的简易音频播放器，把解码器与音频变换器拼成一条音频处理链（pipeline），对外暴露同步与异步播放接口。URI 驱动：scheme 决定外部接口（IO）类型，扩展名决定解码器；对后缀不可靠的流，组件结合 URL 后缀预判与输入数据内容探测选择解码器；通过事件回调把播放状态与解码信息送给应用层；支持通过 menuconfig 按需裁剪变换器与外部接口。

主要特性
-------------------------------------

- 支持的音频格式：AAC、MP3、AMR、M4A、PCM、WAV、ADPCM、G711、OGG、VORBIS、OPUS、ALAC、FLAC、SBC、LC3、TS
- 可配置音频变换器：Bit Depth 转换、Channel 转换、Sample Rate 转换（采样率转换默认开启）
- 支持同步与异步两种播放接口
- 内置三类 IO Stream：HTTP、File、嵌入式 Flash，按 URI scheme 自动选择
- 解码器按 URI 扩展名自动选择
- 事件回调机制：播放状态变化与音频解码信息上报应用层
- 注册自定义 IO 与处理元素：在不修改组件源码的前提下扩展功能
- menuconfig 裁剪：按硬件资源与应用需求选用变换器和 IO，降低内存与处理器负载
- 解码器格式按需启用：通过 menuconfig 勾选实际用到的格式，减小固件体积
- 智能格式识别：后缀预判结合内容探测，适配扩展名缺失或不可靠的流
- 位深转换目标可配置：通过 menuconfig 指定输出位深
