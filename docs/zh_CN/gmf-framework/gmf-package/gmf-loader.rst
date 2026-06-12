GMF Loader
======================

:link_to_translation:`en:[English]`

GMF 加载器（GMF Loader）是 GMF 注册池（pool）的初始化与配置辅助组件，通过 menuconfig 一次性选择需要的处理单元（element）与外部接口（IO），并在加入注册池之前设置默认参数。覆盖外部接口（codec_dev、file、http、flash 读取）、音频编解码器、音频效果、视频编解码与图像效果等常见组合，让应用代码不再重复编写大段注册逻辑。

主要特性
-------------------------------------

- 一键初始化：menuconfig 勾选后由 ``gmf_loader_setup_*`` 把对应处理单元批量注册到用户注册池
- IO 选择：reader 端覆盖 codec_dev RX、file、http、flash；writer 端覆盖 codec_dev TX、file、http
- 音频编解码器：解码器支持 MP3、AAC、AMR-NB/WB、FLAC、WAV、M4A、TS、OGG、OPUS、G711、PCM、ADPCM、LC3、SBC、ALAC、VORBIS、G722；编码器支持 AAC、AMR-NB/WB、G711、OPUS、ADPCM、PCM、ALAC、LC3、SBC、G722
- 音频效果：ALC、EQ、Mixer、Sonic、Fade、DRC、MBC、声道 / 位深 / 采样率转换等可独立启用，以及 ASRC、啸叫抑制（HOWL）、声道交织/解交织
- 视频编解码与图像效果：H.264 / MJPEG 软硬件编解码、PPA 加速、缩放、旋转、裁剪、叠加、帧率转换、颜色转换可按需选入
- AI 音频：唤醒词检测（WakeNet）、AEC、完整 AFE 前端按 menuconfig 自动接入注册池；独立 VAD、NS、DOA 元素可按 menuconfig 注册
- 配置而非代码：所有默认参数通过 Kconfig 暴露，无需改 C 源码即可裁剪二进制体积
- 音频复用（Muxer）：支持 TS、MP4、FLV、WAV、CAF、OGG 等容器封装，可选流式或文件输出
- 其他元素：Copier 支持在 element 间复制数据
