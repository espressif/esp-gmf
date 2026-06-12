ESP Audio Render
================================

:link_to_translation:`en:[English]`

ESP Audio Render 是面向乐鑫 SoC 的高级音频渲染组件，把一路或多路 PCM 输入混音后送给用户提供的 writer 回调输出。每路输入称为 stream，可在混音前连接一条 stream processor（per-stream EQ、Sonic 速度变化等），混音后可连接一条 mixed processor（ALC、限幅器等），处理链由 ESP-GMF 处理单元（element）动态生成以优化性能。适合"音乐 + TTS + 通知音"叠加播放、多轨音乐合成等场景。

主要特性
-------------------------------------

- 多路 stream 混音：将多个 PCM 输入混音为单一输出
- 可选 per-stream 处理：每路 stream 独立连接 ESP-GMF element 链做前处理
- 可选混音后处理：在混音器之后连接统一的 mixed processor（如 ALC、限幅）
- 写入端可自定义：通过 writer 回调把最终 PCM 送给应用，可对接 I2S、蓝牙 sink、网络推流等任意 sink
- 动态处理链：按当前激活的 stream 数自动生成处理链，避免空转以节省 CPU
- 运行时控制：每路 stream 支持 ``pause`` / ``resume`` / ``flush`` / 速度变化
- 独奏播放：可指定单路 stream 独奏，其余 stream 静音
- 混音控制：每路 stream 可独立设置混音增益与淡入/淡出
- 输出格式动态切换：运行中可修改输出采样率 / 声道 / 位宽，无需重建处理链
- 帧大小可配置：每次处理的帧长可调，可与下游 sink 对齐
