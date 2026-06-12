GMF-Audio
====================

:link_to_translation:`en:[English]`

gmf_audio 是 ESP-GMF 的音频处理组件，提供编解码、格式转换、效果处理、通道布局、音频封装五类共 17 个处理单元（element）。所有处理单元都继承自 ``esp_gmf_audio_element_t``\ ，对外暴露统一的生命周期功能函数、acquire-release 数据接口与运行时方法（method）调用，底层算法由 ``esp_audio_codec``\ 、\ ``esp_audio_effects`` 与 ``esp_muxer`` 三个库实现。本篇按五个类别展开每个处理单元的用途、配置与运行时控制；处理单元基类与运行时方法机制见 :doc:`/gmf-framework/gmf-core/gmf-core-element`，数据通路见 :doc:`/gmf-framework/gmf-core/gmf-core-data-path`。

功能清单
-------------------------------------

- aud_dec：多格式音频解码，支持 MP3、AAC、AMRNB、AMRWB、FLAC、WAV、M4A、TS、OPUS、SBC、LC3、ADPCM、ALAC、G711、VORBIS、OGG、G722，可在不重建处理单元的前提下按新格式重新配置
- aud_enc：多格式音频编码，支持 AAC、AMRNB、AMRWB、ADPCM、OPUS、PCM、ALAC、SBC、LC3、G711、G722，可运行中调整 bitrate
- aud_rate_cvt：采样率转换，运行中可通过 ``set_dest_rate`` 切换目标采样率
- aud_asrc：硬件辅助采样率转换，\ ``perf_type`` 支持 AUTO / HW_ONLY / SW_MEMORY / SW_SPEED，\ ``complexity`` 0–3 控制精度，与 ``aud_rate_cvt`` 共用同一套运行时方法接口
- aud_ch_cvt：声道数转换，支持单声道、立体声、多声道之间映射与下混
- aud_bit_cvt：位深转换，覆盖 8 / 16 / 24 / 32 bit PCM 之间相互转换
- aud_eq：多段均衡器，每段支持 peak、low-shelf、high-shelf、low-pass、high-pass 等滤波器类型，可逐段开关
- aud_alc：自动电平控制（automatic level control），按声道独立调整增益
- aud_fade：淡入淡出，支持线性与曲线两种过渡，可在运行中重置
- aud_sonic：变速变调（time-stretch / pitch-shift），speed 与 pitch 范围 [0.5, 2.0]
- aud_mixer：多路混音器，每路输入独立设置过渡模式，运行中可重设音频信息
- aud_drc：单段动态范围控制，参数化 attack / release / hold / makeup / knee 与拐点曲线
- aud_mbc：多段动态范围压缩，每段独立 solo / 旁路（bypass）/ 交叉频率
- aud_howl：啸叫抑制（howling suppression），FFT 频谱分析配合 PAPR / PHPR / PNPR / IMSD 多判据，逐帧插入动态 notch 滤波
- aud_intlv / aud_deintlv：多通道交错与解交错，把若干个独立单声道流拼成一路多声道流或反向拆分
- aud_muxer：音频复用封装，将编码后的音频数据打包为容器格式（TS、MP4、FLV、WAV、CAF、OGG、AVI），支持流式输出与分段文件写入两种模式
- 共性：自动格式探测、动态重配（\ ``need_reopen``\ ）、旁路优化、effects buffer in-place、按处理单元加锁的线程安全

技术拆解
-------------------------------------

元素层次与共性机制
^^^^^^^^^^^^^^^^^^^^^^^^^^

每个具体处理单元都按"包装一个 ``esp_ae_*`` 算法句柄"的方式构造：派生结构以 ``esp_gmf_audio_element_t`` 为首字段，绑定算法 handle、配置块和一个内部互斥锁；\ ``open`` 创建算法句柄并填回处理单元的 ``ops``\ ，\ ``process`` 在每次调度时获取一对输入 / 输出 数据载体（payload），调用算法的 ``process`` 函数，\ ``close`` 销毁句柄。

.. only:: html

   .. mermaid::

      classDiagram
          direction TD

          class esp_gmf_audio_element_t
          class Codec {
              aud_dec
              aud_enc
          }
          class FormatConversion {
              aud_rate_cvt
              aud_asrc
              aud_ch_cvt
              aud_bit_cvt
          }
          class AudioEffects {
              aud_eq
              aud_alc
              aud_fade
              aud_sonic
              aud_mixer
              aud_drc
              aud_mbc
              aud_howl
          }
          class ChannelLayout {
              aud_intlv
              aud_deintlv
          }
          class Muxer {
              aud_muxer
          }

          esp_gmf_audio_element_t <|-- Codec
          esp_gmf_audio_element_t <|-- FormatConversion
          esp_gmf_audio_element_t <|-- AudioEffects
          esp_gmf_audio_element_t <|-- ChannelLayout
          esp_gmf_audio_element_t <|-- Muxer

所有处理单元共享三条公共机制，理解后可以避免反复对照源码：

- **effects buffer in-place**\ ：效果类处理单元（\ ``eq`` / ``alc`` / ``fade`` 等）在 ``process`` 中检测 ``in_port->is_shared``\ ；若置位，直接令 ``out_load = in_load``\ ，输入输出共用同一数据载体缓冲区，底层 ``esp_ae_*`` 算法以原地写入方式处理，避免额外分配。
- **旁路优化**\ ：转换类处理单元（\ ``rate_cvt`` / ``ch_cvt`` / ``bit_cvt`` / ``asrc``\ ）在 ``open`` 阶段比较源 / 目标参数，若一致进入旁路；旁路模式下若 ``in_port->is_shared`` 置位，同样令 ``out_load = in_load`` 无拷贝透传，否则一次 memcpy 透传。
- **细粒度互斥**\ ：所有算法 handle 调用都包在处理单元自带的 mutex 中，允许在运行态从其他线程调用 setter（例如运行中调音量）。
- **格式 notify-and-update**\ ：上游处理单元解析到当前样本格式后通知下游，下游据此重建底层算法句柄。下一小节展开。

格式 notify-and-update 流程
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

音频处理链中常出现"上游解码出真实采样率后下游算法才能正确配置"这类前后依赖。gmf_audio 将该流程划分为 notify 与 update 两个动作：

**notify（上游）**\ ：上游处理单元（解码器、采样率转换、声道转换等）在 ``open`` 或 ``process`` 中确认本帧的实际 sample_rate / channel / bit_depth 后，调用 ``GMF_AUDIO_UPDATE_SND_INFO(self, rate, bits, ch)`` 宏完成两件事：把新的 ``esp_gmf_info_sound_t`` 写到自身的 audio info 字段，并通过 ``esp_gmf_element_notify_snd_info`` 上报 ``REPORT_INFO`` 事件，处理链（pipeline）接收后顺着拓扑投递给后继处理单元。

**update（下游）**\ ：下游处理单元在事件回调里把新 info 写到自己的 audio info，同时把 ``need_reopen`` 置位。在下一次 ``process`` 边界，处理单元检测到该标志，调用一次 ``close`` 销毁旧 handle，再 ``open`` 用新参数重建。reopen 过程由框架自动完成，应用代码只需保证拓扑里下游处理单元与上游有事件订阅关系（注册池创建出的处理链默认满足）。

setter 调用（\ ``set_dest_rate`` / ``set_para`` 等）使用同一条 update 路径：setter 改动后同样置 ``need_reopen``\ ，在下一次 ``process`` 边界重建算法句柄。

格式信息事件与依赖型处理单元启动流程见 :doc:`/gmf-framework/gmf-core/gmf-core-pipeline` 的 REPORT_INFO 说明。

运行时方法（method）调用模式
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

每个处理单元通过 ``esp_gmf_audio_methods_def.h`` 暴露一组宏化方法名，应用层用 ``AMETHOD(MODULE, METHOD)`` 拼出字符串后调用 ``esp_gmf_element_exe_method``\ ：

.. code:: c

    /* 调整 ALC 第 0 声道增益到 -6 dB */
    uint8_t buf[2] = { 0 /* idx */, (uint8_t)(-6) };
    esp_gmf_element_exe_method(alc_el, AMETHOD(ALC, SET_GAIN), buf, sizeof(buf));

也可以直接调用处理单元自己的具名 setter（如 :cpp:func:`esp_gmf_alc_set_gain`\ ），两条路径使用相同实现，区别只在调用形式。本组件按处理单元提供完整的具名 API，运行期可任选其一。

运行时方法的另一个优点是接口与实现解耦。以 ``aud_rate_cvt`` 为例，应用层调用 ``AMETHOD(RATE_CVT, SET_DEST_RATE)`` 时只依赖方法名，不依赖具体处理单元。若需要换用硬件 ASRC（\ ``aud_asrc``\ ），只需在注册池中将 ``aud_rate_cvt`` 替换为 ``aud_asrc``\ ，后者实现了相同的 ``RATE_CVT::SET_DEST_RATE`` 接口，应用代码无需改动。同样，用户自己的重采样代码也可以按相同运行时方法名注册为自定义处理单元，直接接入已有处理链，不必修改上层调用逻辑。

音频编解码
^^^^^^^^^^^^^^^^^^^^^^^^^^

aud_dec 与 aud_enc 是播放与录音处理链的起点或终点处理单元，典型组装如下。

.. only:: html

   .. mermaid::

      flowchart LR
          subgraph play ["播放链"]
              direction LR
              FileIn(("file/http")) --> Dec["aud_dec"]
              Dec --> Rate["aud_rate_cvt"]
              Rate --> EQ["aud_eq"]
              EQ --> Out(("codec_dev"))
          end
          subgraph rec ["录音链"]
              direction LR
              CodecIn(("codec_dev")) --> Ch["aud_ch_cvt"]
              Ch --> AGC["aud_alc"]
              AGC --> Enc["aud_enc"]
              Enc --> FileOut(("file/http"))
          end

**aud_dec**\ ：把压缩流解码成 PCM。\ :cpp:func:`esp_gmf_audio_dec_init` 接受 ``esp_audio_simple_dec_cfg_t``\ ，\ ``dec_type`` 指定具体格式；\ ``use_frame_dec`` 设为 true 时直接使用帧级解码，绕过 simple decoder 的容器解析层。运行时通过 :cpp:func:`esp_gmf_audio_dec_reconfig` 或按 ``esp_gmf_info_sound_t`` 重新配置，适用于在 IO 返回流头之后再配置编码器的场景。aud_dec 内置 ``audio_dec_detect_type`` 自动检测常见容器头（\ ``#!AMR``\ 、\ ``RIFF``\ 、\ ``fLaC``\ 、\ ``OggS``\ 、\ ``ID3``\ ），未指定 ``dec_type`` 时可识别 MP3 / AAC / FLAC / OGG 等格式。

.. code:: c

    esp_audio_simple_dec_cfg_t cfg = DEFAULT_ESP_GMF_AUDIO_DEC_CONFIG();
    cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;

    esp_gmf_element_handle_t dec = NULL;
    esp_gmf_audio_dec_init(&cfg, &dec);
    /* 获取流头后切换到 AAC */
    esp_gmf_info_sound_t info = { .format_id = ESP_FOURCC_AAC, ... };
    esp_gmf_audio_dec_reconfig_by_sound_info(dec, &info);

**aud_enc**\ ：把 PCM 编码成压缩格式。\ :cpp:func:`esp_gmf_audio_enc_init` 接受 ``esp_audio_enc_config_t``\ ，\ ``type`` 指定输出格式（AAC、OPUS、SBC、LC3、G722 等）。运行中可调用 :cpp:func:`esp_gmf_audio_enc_set_bitrate` 改 bitrate；\ :cpp:func:`esp_gmf_audio_enc_reconfig` / ``..._by_sound_info`` 在元素处于 ``NONE`` 或 ``INITIALIZED`` 状态时切换格式或参数。

aud_enc 内部用字节缓存（cache）处理"输入数据量不一定整帧对齐"的情况，调度上配合 job 返回码与执行线程协作：

- 输入还不足一帧时累积到字节缓存，返回 ``ESP_GMF_JOB_ERR_CONTINUE``\ ，执行线程在同一处理单元上再输入一块数据；
- 输入大于一帧时只消费字节缓存中的完整帧，剩余尾部留在字节缓存，返回 ``ESP_GMF_JOB_ERR_TRUNCATE``\ ，执行线程会跳到下游处理单元把已编码的输出消费掉，下次再回到本处理单元继续处理剩余字节；
- 凑够整帧后调用底层 ``esp_audio_enc_process`` 产生输出。

PTS 也由处理单元在字节缓存维度维护：当字节缓存中已累积时长超过当前 origin 的 ``pts`` 时，自动 clamp 到 0 避免回退。aud_dec 使用同样的 ``CONTINUE`` / ``TRUNCATE`` 模式，应用层不需要关心；编写自定义处理单元并连接到 codec 后方时，也需遵循同一约定。

格式转换
^^^^^^^^^^^^^^^^^^^^^^^^^^

格式转换节共四个处理单元：\ ``rate_cvt`` / ``ch_cvt`` / ``bit_cvt`` 包装 ``esp_audio_effects`` 中的对应算法，对外只暴露一个 ``set_dest_*`` 的 setter；\ ``asrc`` 使用独立的 ``esp_asrc`` 库，提供硬件辅助或软件后备两条处理路径。

**aud_rate_cvt**\ ：把输入 PCM 重采样到目标采样率。\ ``DEFAULT_ESP_GMF_RATE_CVT_CONFIG`` 默认 44100 → 48000、立体声、16 bit；\ ``complexity`` 取 0–5，越高音质越好开销越大；\ ``perf_type`` 在 ``ESP_AE_RATE_CVT_PERF_TYPE_SPEED`` 与 ``QUALITY`` 之间选择。运行中切换目标采样率：

.. code:: c

    esp_gmf_rate_cvt_set_dest_rate(rate_el, 16000);

setter 会触发 ``need_reopen``\ ，下一次 ``process`` 边界自动重建底层 handle。

**aud_ch_cvt**\ ：转换声道数，支持任意 N → M 之间映射。若源声道等于目标声道，open 时直接置旁路。运行中改目标声道用 :cpp:func:`esp_gmf_ch_cvt_set_dest_ch`\ 。

**aud_bit_cvt**\ ：转换位深，覆盖 uint8 / int16 / int24 / int32 PCM 之间相互转换。运行中改目标位深用 :cpp:func:`esp_gmf_bit_cvt_set_dest_bits`\ 。

**aud_asrc**\ ：使用硬件 ASRC 外设（SoC 支持时）或软件后备实现采样率转换，\ ``perf_type`` 决定后端选择：

- ``ESP_ASRC_PERF_TYPE_AUTO``\ ：优先启用硬件，不可用时自动回退软件
- ``ESP_ASRC_PERF_TYPE_HW_ONLY``\ ：仅使用硬件，不支持的 SoC 上返回错误
- ``ESP_ASRC_PERF_TYPE_SW_MEMORY`` / ``ESP_ASRC_PERF_TYPE_SW_SPEED``\ ：纯软件，分别优化内存占用 / 处理速度

``complexity`` 取 0–3（仅软件模式有效），越高音质越好、开销越大。\ ``timeout_ms`` 是等待硬件完成一帧的超时阈值，软件模式忽略。\ ``DEFAULT_ESP_GMF_ASRC_CONFIG`` 默认 44100 → 48000、立体声、16 bit。

.. code:: c

    esp_asrc_cfg_t cfg = DEFAULT_ESP_GMF_ASRC_CONFIG();
    esp_gmf_element_handle_t asrc_el = NULL;
    esp_gmf_asrc_init(&cfg, &asrc_el);
    /* 运行中切换目标采样率 */
    esp_gmf_asrc_set_dest_rate(asrc_el, 16000);

setter 触发 ``need_reopen``\ ，与 ``aud_rate_cvt`` 行为一致。\ ``aud_asrc`` 注册了 ``RATE_CVT::SET_DEST_RATE`` 方法，与 ``aud_rate_cvt`` 方法接口相同，可在注册池中直接互换而不修改应用代码。

三个转换处理单元（\ ``rate_cvt`` / ``ch_cvt`` / ``bit_cvt``\ ）都遵循同一条数据流：在 ``process`` 中 acquire 一块输入，按 src / dest 比例计算需要的输出字节数，acquire 输出，调用 ``esp_ae_*_process`` 处理一帧后双双 release。进入旁路时，若 ``in_port->is_shared`` 置位，则直接令输出数据载体指向输入数据载体，无拷贝透传。

效果处理
^^^^^^^^^^^^^^^^^^^^^^^^^^

**aud_eq**\ ：多段均衡器。\ ``DEFAULT_ESP_GMF_EQ_CONFIG`` 在 ``filter_num = 0`` 时使用内置 10 段默认参数（覆盖 31 Hz–16 kHz）。每段独立配置：

.. code:: c

    esp_ae_eq_filter_para_t para = {
        .filter_type = ESP_AE_EQ_FILTER_PEAK,
        .fc          = 1000,
        .q           = 1.0f,
        .gain        = 6.0f,
    };
    esp_gmf_eq_set_para(eq_el, 0, &para);
    esp_gmf_eq_enable_filter(eq_el, 0, true);

EQ 在 ``open`` 时根据传入的 enable 状态决定哪些段参与处理，\ ``process`` 调用 ``esp_ae_eq_process`` 对当前 PCM 做整体响应调整。

**aud_alc**\ ：自动电平控制，按声道独立设置增益，范围 [-64, 63] dB，\ ``-64`` 以下视为静音。

.. code:: c

    esp_gmf_alc_set_gain(alc_el, 0, -6);  /* 左 -6 dB */
    esp_gmf_alc_set_gain(alc_el, 1, -6);  /* 右 -6 dB */

**aud_fade**\ ：淡入淡出，\ ``mode`` 选 ``FADE_IN`` 或 ``FADE_OUT``\ ；\ ``curve`` 选 ``LINE`` 或 ``CURVE``\ ；\ ``transit_time`` 单位 ms。\ :cpp:func:`esp_gmf_fade_reset` 把当前 fade 进度重置到初始权重，可用于"切歌时复用同一个 fade 处理单元"。

**aud_sonic**\ ：变速变调。speed 与 pitch 取值范围 [0.5, 2.0]，1.0 表示原样。time-stretch 会改变输出样本数，处理单元内部维护一个可变长度的输出缓冲处理这种放大 / 缩小关系。

.. code:: c

    esp_gmf_sonic_set_speed(sonic_el, 1.25f);  /* 1.25 倍速 */
    esp_gmf_sonic_set_pitch(sonic_el, 0.9f);   /* 略低沉 */

**aud_mixer**\ ：多路混音，多个输入数据端口（port）由 ``src_num`` 决定。每路通过 :cpp:func:`esp_gmf_mixer_set_mode` 单独设置 ``FADE_BY_SAMPLES`` / ``MUTE`` / ``NONE`` 等过渡模式，运行中可通过 :cpp:func:`esp_gmf_mixer_set_audio_info` 重新设置共同的采样率 / 位宽 / 声道。第一路输入阻塞时间为 0、其余路为最大延迟：保证有主路时立即出声，副路只在有数据时叠加。

**aud_drc**\ ：单段动态范围控制。除了 attack / release / hold / makeup / knee 五个时间与曲线参数，还可以通过 :cpp:func:`esp_gmf_drc_set_points` 设置输入 / 输出折点拐点曲线（最多支持若干个 ``esp_ae_drc_point_t``\ ）。配合 ``aud_eq`` 可以构造典型的主路总线压缩链路。

**aud_mbc**\ ：多段动态压缩。每段独立调 threshold / ratio / attack / release / hold / knee / makeup，再通过 ``set_fc`` 设置段间交叉频率；调试时用 ``set_solo`` 与 ``set_bypass`` 单独听某一段或绕过某一段。

**aud_howl**\ ：啸叫抑制（howling suppression），检测并衰减由麦克风、放大器、扬声器环路产生的反馈啸叫。算法对每个 FFT 频率 bin 同时评估三个判据：PAPR（峰值 / 均值功率比）、PHPR（峰值 / 谐波功率比）、PNPR（峰值 / 噪声功率比）。满足任一阈值的频率 bin 被标记为啸叫候选，算法在该频率动态插入 biquad notch 滤波器并适当降低总增益，以抑制新的啸叫峰。

可选第四判据 IMSD（帧间频谱幅度偏差）通过 ``enable_imsd`` 开启，对持续稳定频率敏感度更高，适合语音和音乐混合内容；纯语音场景关闭可节省内存和 CPU。

处理帧长度由 ``esp_ae_howl_get_frame_size`` 在 open 时根据采样率决定（sample_rate < 32000 Hz 时 512 样本/通道，否则 1024 样本/通道）。算法支持原地处理，输入输出可指向同一 buffer。

.. note::

   ``aud_howl`` 的 GMF 处理单元封装（\ ``esp_gmf_howl.h``\ ）尚在开发中，具体初始化 API 以实现合入后为准。

通道布局
^^^^^^^^^^^^^^^^^^^^^^^^^^

**aud_intlv**\ ：把 N 个单声道输入拼成一路 N 声道交错 PCM，\ ``src_num`` 决定输入数据端口数。常用于把 AEC 参考路、麦克风路合并成立体声写文件。

**aud_deintlv**\ ：反向操作，把一路交错 PCM 按声道分解到 N 个输出数据端口。常用于将立体声分解为左右两路分别后处理。

两者只做内存重排，不修改样本值，\ ``process`` 中按 sample 步长循环 memcpy。

音频封装
^^^^^^^^^^^^^^^^^^^^^^^^^^

**aud_muxer**\ ：把编码后的音频数据打包为容器格式输出。底层由 ``esp_muxer`` 库驱动，目前支持的容器格式包括 TS、MP4、FLV、WAV、CAF、OGG、AVI。aud_muxer 通常接在 aud_enc 之后，负责在录制或转码处理链末端完成格式封装。

``esp_gmf_audio_muxer_cfg_t`` 的主要字段：

.. list-table::
   :widths: 28 72
   :header-rows: 1

   * - 字段
     - 说明
   * - ``muxer_type``
     - 容器类型，取 ``ESP_MUXER_TYPE_TS / MP4 / FLV / WAV / CAF / OGG / AVI``
   * - ``codec``
     - 输入音频的编码类型（与 aud_enc 的输出格式对应）
   * - ``output_type``
     - 输出模式：\ ``STREAMING``\ （经 databus 流式输出）或 ``FILE``\ （按文件写入）
   * - ``slice_duration``
     - 仅文件模式有效，每段文件的时长（毫秒），默认 60000
   * - ``url_pattern`` / ``url_ctx``
     - 仅文件模式有效，分段文件路径回调，决定每段写入哪个文件
   * - ``get_codec_spec_info_cb``
     - 可选。为 AAC 等需要 codec-specific 信息的格式提供 SPS 等额外元数据的回调

**流式输出模式**\ ：\ ``output_type = ESP_GMF_AUDIO_MUXER_OUTPUT_STREAMING`` 时，处理单元在 ``open`` 阶段创建一个内部 block databus，muxer 通过回调把封装后的数据写入该 databus，再由 out_port 转发给下游（如外部接口处理单元写文件或网络发送）。

**文件分段模式**\ ：\ ``output_type = ESP_GMF_AUDIO_MUXER_OUTPUT_FILE`` 时，处理单元不占用 out_port，直接通过 ``url_pattern`` 回调按 ``slice_duration`` 切分并写入一系列文件，适合连续录制场景。

.. code:: c

    esp_gmf_audio_muxer_cfg_t cfg = {
        .muxer_type  = ESP_MUXER_TYPE_MP4,
        .codec       = ESP_MUXER_AUDIO_CODEC_AAC,
        .output_type = ESP_GMF_AUDIO_MUXER_OUTPUT_STREAMING,
    };
    esp_gmf_element_handle_t muxer_el = NULL;
    esp_gmf_audio_muxer_init(&cfg, &muxer_el);

aud_muxer 依赖上游 aud_enc 上报的 ``esp_gmf_info_sound_t``\ （采样率、位深、声道）来初始化 muxer 音频流描述；确保 aud_enc 在 aud_muxer 之前完成 ``REPORT_INFO`` 事件投递，即保持 enc→muxer 的标准处理链连接顺序即可。

性能
-------------------------------------

整体上每个处理单元的耗时主要由底层 ``esp_ae_*`` 算法决定；framework 的 acquire-release 与运行时方法调度对一次 ``process`` 的相对开销小（百微秒量级）。需要权衡 CPU 与音质时：

- ``aud_rate_cvt`` 的 ``complexity`` 字段直接换音质 / CPU；\ ``perf_type = SPEED`` 在嵌入式播放器场景通常够用
- ``aud_eq`` / ``aud_drc`` / ``aud_mbc`` 开启的段越多耗时越大，可用 ``enable_filter`` / ``set_bypass`` 在运行时按需关掉
- ``aud_dec`` 与 ``aud_enc`` 的吞吐取决于具体编解码格式（OPUS、AAC、FLAC 之间差异较大），可参考 ``esp_audio_codec`` 提供的 benchmark
- 转换类处理单元在源 / 目标参数一致时自动旁路，处理链设计时不必为"可能没必要的转换"专门拆处理单元

应用示例
-------------------------------------

- ``elements/test_apps/main/elements/gmf_audio_el_test.c``\ ：每个处理单元的单独 case，涵盖默认参数、运行时 setter、reset 等
- ``elements/test_apps/main/elements/gmf_audio_play_el_test.c``\ ：拼成播放处理链的综合用例
- ``elements/test_apps/main/elements/gmf_audio_effects_test.c``\ ：效果链处理链测试
- ``elements/test_apps/main/elements/gmf_audio_rec_el_test.c``\ ：录音处理链测试，覆盖编码侧
- ``gmf_examples/basic_examples/pipeline_play_embed_music``\ ：完整应用工程

上层封装 ``esp_audio_simple_player`` / ``esp_audio_render`` 内部也通过本组件连接音频处理链，可作进阶参考。

FAQ
-------------------------------------

**Q：** 未指定 ``dec_type`` 时，``aud_dec`` 是否仍可解码？

可以。前提是 ``use_frame_dec = false``\ （默认值）。此时解码器会启用内部 parser，自动从输入流识别容器头与帧同步字来确定格式，并据此初始化解码器。若 ``use_frame_dec = true``\ ，输入被视为已完整的编码帧，不经过 parser，格式自动检测将失效。明确知道格式时建议显式设置 ``dec_type``\ ，以加快启动并避免误识别。

**Q：** ``aud_enc`` 的 ``set_bitrate`` 在什么条件下才会生效？

编码 handle 尚未创建时，\ :cpp:func:`esp_gmf_audio_enc_set_bitrate` 会校验参数并写入 ``esp_audio_enc_config_t`` 配置；handle 已创建时直接作用于运行中的编码器。\ :cpp:func:`esp_gmf_audio_enc_reconfig` 与 ``..._by_sound_info`` 仅在 state 小于 ``OPENING`` 时可替换整体配置。使用 :cpp:func:`esp_gmf_audio_enc_get_bitrate` 可读取配置或运行中的实际比特率。

**Q：** ``aud_rate_cvt`` 切换目标采样率后为什么会出现短暂停顿？

``set_dest_rate`` 触发 ``need_reopen``\ ，处理单元会在下一次 process 边界销毁旧 handle 并重建新 handle。重建过程会带来短暂停顿。若需平滑切换，建议在 ``pause`` 之后执行切换。

**Q：** ``aud_mixer`` 主路无数据时其他路是否会阻塞？

mixer 的设计是"第一路阻塞 0、其余阻塞最大延迟"，主路无数据时直接返回，剩余路按零数据混合。若希望主路缺失时由副路接管，可调换接入顺序，把现在的副路作为主路连接到第 0 个输入数据端口。

**Q：** ``aud_sonic`` 设置 speed=2 后 PCM 长度为何不是输入的一半？

time-stretch 是非整除运算，处理单元输出大小按当前 speed 比例计算并保留累积误差，因此每帧输出长度不固定。下游处理单元（如 codec_dev 外部接口）应当按 ``valid_size`` 写入硬件，不应假设固定长度。

**Q：** ``aud_intlv`` 多输入间如何保证同步？

interleave 处理单元不做同步，依赖上游各路处理单元给出数量一致的样本。多路 AEC 参考的常见做法是把所有路连接在同一个处理链、用同一个执行线程串行 acquire，从而保持对齐。

API 参考
-------------------------------------

本组件涉及的头文件：

- ``esp_gmf_audio_dec.h`` / ``esp_gmf_audio_enc.h``\ ：编解码处理单元
- ``esp_gmf_audio_helper.h`` / ``esp_gmf_audio_param.h``\ ：辅助接口与参数定义
- ``esp_gmf_audio_methods_def.h``\ ：运行时方法名与参数名宏（纯宏定义，详见源码头文件）
- ``esp_gmf_rate_cvt.h`` / ``esp_gmf_ch_cvt.h`` / ``esp_gmf_bit_cvt.h``\ ：格式转换处理单元
- ``esp_gmf_asrc.h``\ ：硬件辅助采样率转换处理单元
- ``esp_gmf_eq.h`` / ``esp_gmf_alc.h`` / ``esp_gmf_fade.h`` / ``esp_gmf_sonic.h`` / ``esp_gmf_mixer.h``\ ：常规效果处理单元
- ``esp_gmf_drc.h`` / ``esp_gmf_mbc.h``\ ：动态范围处理单元
- ``esp_gmf_interleave.h`` / ``esp_gmf_deinterleave.h``\ ：通道布局处理单元
- ``esp_gmf_audio_muxer.h``\ ：音频封装处理单元

处理单元基类 ``esp_gmf_audio_element_t`` 的接口位于 :doc:`/gmf-framework/gmf-core/gmf-core-element`。

.. include-build-file:: inc/esp_gmf_audio_dec.inc

.. include-build-file:: inc/esp_gmf_audio_enc.inc

.. include-build-file:: inc/esp_gmf_audio_helper.inc

.. include-build-file:: inc/esp_gmf_audio_param.inc

.. include-build-file:: inc/esp_gmf_rate_cvt.inc

.. include-build-file:: inc/esp_gmf_ch_cvt.inc

.. include-build-file:: inc/esp_gmf_bit_cvt.inc

.. include-build-file:: inc/esp_gmf_eq.inc

.. include-build-file:: inc/esp_gmf_alc.inc

.. include-build-file:: inc/esp_gmf_fade.inc

.. include-build-file:: inc/esp_gmf_sonic.inc

.. include-build-file:: inc/esp_gmf_mixer.inc

.. include-build-file:: inc/esp_gmf_drc.inc

.. include-build-file:: inc/esp_gmf_mbc.inc

.. include-build-file:: inc/esp_gmf_interleave.inc

.. include-build-file:: inc/esp_gmf_deinterleave.inc
