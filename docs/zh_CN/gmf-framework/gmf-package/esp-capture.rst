ESP Capture
=======================

:link_to_translation:`en:[English]`

ESP Capture 是基于 ESP-GMF 的轻量级多媒体采集组件，按 "capture source → capture path → capture sink" 模型把输入设备采集到的数据处理为目标格式输出。集成音视频编码、图像旋转缩放、回声消除、图层叠加等能力，支持多路 sink 并行处理，每路 sink 内置 Muxer 和本地存储功能。处理链按源与目标格式自动协商，简化配置；常见应用包括音视频录制、AI 大模型输入、WebRTC、RTMP 推流、本地存储与远程监控。

主要特性
-------------------------------------

- 低内存开销，模块化 pipeline 结构
- 与 ESP-GMF 深度集成，复用框架的高级音视频处理能力
- 多种输入设备：V4L2 摄像头、DVP 摄像头、音频 codec
- 并行流式传输与本地存储：一份采集数据同时推流与录文件
- 自动源 / 目标协商：根据输入格式与输出要求自动构造处理链
- 可定制的处理链：支持自定义 source、path、sink 与协商策略
- 多 overlay 区域：每个 sink 可通过链表挂载多个 overlay 区域
