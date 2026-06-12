GMF-AI-Audio
====================

:link_to_translation:`en:[English]`

gmf_ai_audio 是 ESP-GMF 的 AI 语音前端组件，把乐鑫 `esp-sr <https://github.com/espressif/esp-sr>`__ 语音算法库（唤醒词、命令词、AEC、NS、VAD、DOA）包装成可连接到处理链（pipeline）的处理单元（element）。组件提供六个处理单元（ai_afe / ai_aec / ai_wn / ai_ns / ai_vad / ai_doa）和一个内部管理器（\ ``esp_gmf_afe_manager``\ ）。其中 ai_afe 是综合接口，封装完整语音前端能力，适合直接作为统一入口；ai_aec / ai_wn / ai_ns / ai_vad / ai_doa 则面向单项能力。六个处理单元都遵循统一的处理单元接口，可按业务需求在同一处理链中任意串接并组合。本篇按管理器、综合处理单元与独立算法处理单元的顺序展开原理、配置与事件系统；处理单元基类与运行时方法（method）机制见 :doc:`/gmf-framework/gmf-core/gmf-core-element`，数据通路见 :doc:`/gmf-framework/gmf-core/gmf-core-data-path`。

功能清单
-------------------------------------

- ai_afe：完整语音前端处理单元，连接在 codec_dev IO 之后即可输出 16-bit 单声道 PCM，并通过事件回调上报唤醒、VAD、命令词
- ai_aec：独立回声消除处理单元，对输入 PCM 做 AEC 后输出 16-bit 单声道 PCM，适合只需要回声消除后麦克风信号、不需要唤醒 / VAD / 命令词的场景
- ai_wn：独立唤醒词检测处理单元，不创建 feed / fetch 执行线程，直接在处理单元 ``process`` 中同步检测，并将输入 PCM 透传到输出数据端口
- ai_ns：独立降噪处理单元，输入格式为 16 kHz、16-bit 单声道 PCM，可使用 NSNet2 模型或 WebRTC NS 后端
- ai_vad：独立人声活动检测处理单元，支持 WebRTC VAD 与 VADNet 后端，通过回调上报 VAD 状态，并可透传输入 PCM
- ai_doa：独立声源方向估计处理单元，要求输入格式中包含两个麦克风通道，通过回调输出角度估计结果
- esp_gmf_afe_manager：内部管理器，封装 feed / fetch 两个执行线程，负责 ``esp-sr`` AFE 的数据输入、结果获取、特性开关与暂停 / 恢复
- NS / VAD / SE：ai_afe 通过 AFE 管理器使用 ``esp-sr`` 的降噪（NS）、人声活动检测（VAD）与语音增强（SE）能力，是否启用由 ``afe_config_t`` 与运行时特性控制决定
- 通道格式约定：用字符串描述输入声道排列，\ ``M`` 麦克风、\ ``R`` 回采、\ ``N`` 空通道，如 ``MMNR`` 表示前两路麦克风加一路回采
- 唤醒与 VAD 状态机：支持"仅唤醒"、"仅 VAD"、"唤醒 + VAD" 三种组合，自动维护 IDLE / WAKEUP / SPEECHING / WAIT_FOR_SLEEP 状态
- 命令词检测（VCMD）：基于 ``MultiNet``\ ，独立于唤醒状态机，由应用调用 ``esp_gmf_afe_vcmd_detection_begin`` 启动
- 手动唤醒控制：\ ``esp_gmf_afe_keep_awake`` 保持唤醒态、\ ``esp_gmf_afe_trigger_wakeup`` / ``..._trigger_sleep`` 手动切换，适合按键唤醒等不依赖语音的触发
- 事件系统：6 种事件覆盖唤醒开始 / 结束、VAD 开始 / 结束、命令词识别、命令词超时

技术拆解
-------------------------------------

组件层次
^^^^^^^^^^^^^^^^^^^^^^^^^^

六个处理单元与管理器的关系如下图。ai_afe 是管理器的上层封装，把管理器回调适配为 GMF 处理单元接口；ai_aec、ai_wn、ai_ns、ai_vad、ai_doa 直接调用 ``esp-sr`` 的对应算法，不经过管理器。

.. only:: html

   .. mermaid::

      classDiagram
          direction TB

          class esp_gmf_audio_element_t
          class ai_afe {
              feed/fetch 执行线程
              唤醒状态机
              命令词检测
              事件回调
          }
          class ai_aec {
              独立 AEC
              参考 + 麦克风通道
          }
          class ai_wn {
              独立 WakeNet
              唤醒词检测
          }
          class ai_ns {
              独立 NS
              单声道降噪
          }
          class ai_vad {
              独立 VAD
              状态回调
          }
          class ai_doa {
              独立 DOA
              声源定位
          }
          class esp_gmf_afe_manager {
              feed_task
              fetch_task
              特性控制
          }

          esp_gmf_audio_element_t <|-- ai_afe
          esp_gmf_audio_element_t <|-- ai_aec
          esp_gmf_audio_element_t <|-- ai_wn
          esp_gmf_audio_element_t <|-- ai_ns
          esp_gmf_audio_element_t <|-- ai_vad
          esp_gmf_audio_element_t <|-- ai_doa
          ai_afe ..> esp_gmf_afe_manager : uses

需要处理单元行为时，按场景选择 ai_afe / ai_aec / ai_wn / ai_ns / ai_vad / ai_doa 之一接入处理链；需要绕过 GMF 框架直接使用 ``esp-sr`` 时，可单独使用 ``esp_gmf_afe_manager``\ 。

input_format 通道字符串
^^^^^^^^^^^^^^^^^^^^^^^^^^

使用多通道输入的处理单元通过 ``input_format`` 字符串说明每个通道的角色：\ ``M`` 表示麦克风采集、\ ``R`` 表示扬声器回采（用于 AEC 参考）、\ ``N`` 表示该通道未使用。通道字符串的详细规则见 `esp-sr AFE 输入通道说明 <https://docs.espressif.com/projects/esp-sr/zh_CN/latest/esp32/audio_front_end/README.html#id5>`__。

例如 ``"MMNR"`` 表示输入是 4 通道交错 PCM，第 1 / 2 通道为麦克风、第 3 通道未使用、第 4 通道为参考。组件根据字符串自动从输入 PCM 中抽取需要的通道送给底层算法。ai_afe 的输入采样率固定 16 kHz、位宽 16-bit；ai_aec 使用 ``AFE_TYPE_VC_8K`` 时额外支持 8 kHz；ai_doa 要求输入格式中恰好包含两个 ``M`` 通道。

AFE 管理器
^^^^^^^^^^^^^^^^^^^^^^^^^^

``esp_gmf_afe_manager`` 把 ``esp-sr`` 的 AFE 接口封装成"数据输入 → 算法处理 → 结果输出"的回调模型，自动创建 feed 与 fetch 两个执行线程。

.. only:: html

   .. mermaid::

      flowchart LR
          ReadCb[("read_cb<br/>(应用提供)")] --> Feed["feed_task"]
          Feed --> Core["AFE 处理模块<br/>esp-sr"]
          Core --> Fetch["fetch_task"]
          Fetch --> ResultCb[("result_cb<br/>(应用接收)")]

应用通过 :cpp:type:`esp_gmf_afe_manager_cfg_t` 提供 ``read_cb`` 与 ``result_cb``\ ，feed_task 周期性调用 ``read_cb`` 获取一帧多通道 PCM，并输入 ``esp-sr`` 的 AFE；fetch_task 取出处理结果（降噪 / AEC 后的单声道 PCM + 唤醒 / VAD / 命令词事件），调用 ``result_cb``\ 。两个执行线程默认分配到不同 core（core 0 / core 1），栈 3 KiB、优先级 5，\ ``DEFAULT_GMF_AFE_MANAGER_CFG`` 给出默认值。

运行中可单独开关特性：

.. code:: c

    esp_gmf_afe_manager_enable_features(mgr, ESP_AFE_FEATURE_AEC, true);
    esp_gmf_afe_manager_enable_features(mgr, ESP_AFE_FEATURE_VAD, false);

调用 :cpp:func:`esp_gmf_afe_manager_suspend` 并将挂起开关设为 ``true`` 后，可同时挂起 feed 与 fetch 两个执行线程，适合低功耗场景。初始化完成后，可通过 :cpp:func:`esp_gmf_afe_manager_get_chunk_size` 与 ``get_input_ch_num`` 查询每次处理的样本数和输入声道总数，便于应用调整自身的 IO 缓冲。

ai_afe 完整语音前端
^^^^^^^^^^^^^^^^^^^^^^^^^^

ai_afe 把 ``esp_gmf_afe_manager`` 包装成可连接到处理链的处理单元：将 codec_dev IO 读取的多通道 PCM 输入管理器，将 fetch 输出的单声道 PCM 写到输出数据端口（port），并把唤醒 / VAD / 命令词转换为事件回调发送给应用。

ai_afe 的输出为 16-bit 单声道 PCM。根据 ``esp-sr`` AFE 配置，输出音频可叠加 AEC、NS、SE、AGC 等处理结果；唤醒、VAD 与命令词检测结果通过事件回调上报，不放入音频数据载体。

**配置**\ 。:cpp:type:`esp_gmf_afe_cfg_t` 至少要提供 ``afe_manager``\ 、\ ``models``\ （已加载的 ``esp-sr`` 模型）、\ ``event_cb``\ 。AFE 的底层类型由 ``afe_config_init`` 的 ``type`` 参数决定；最新 ``esp-sr`` 常用类型包括 ``AFE_TYPE_SR``\ （语音识别）、\ ``AFE_TYPE_VC`` / ``AFE_TYPE_VC_8K``\ （语音通信）与 ``AFE_TYPE_FD``\ （全双工，适合同时播放与采集的语音交互场景）：

.. code:: c

    afe_config_t *afe_cfg = afe_config_init("MMNR", models, AFE_TYPE_FD, AFE_MODE_LOW_COST);
    afe_cfg->wakenet_init = true;
    afe_cfg->vad_init     = true;
    afe_cfg->aec_init     = true;

    esp_gmf_afe_manager_cfg_t mgr_cfg = DEFAULT_GMF_AFE_MANAGER_CFG(afe_cfg,
        my_read_cb, &io_ctx, NULL, NULL);
    esp_gmf_afe_manager_handle_t mgr = NULL;
    esp_gmf_afe_manager_create(&mgr_cfg, &mgr);

    esp_gmf_afe_cfg_t afe_el_cfg = DEFAULT_GMF_AFE_CFG(mgr, my_event_cb, &app_ctx, models);
    afe_el_cfg.vcmd_detect_en = true;
    esp_gmf_element_handle_t ai_afe = NULL;
    esp_gmf_afe_init(&afe_el_cfg, &ai_afe);

四个时序参数（默认值在 ``esp_gmf_afe.h`` 给出）调节状态机行为：

.. list-table::
   :widths: 24 18 58
   :header-rows: 1

   * - 参数
     - 默认
     - 含义
   * - ``wakeup_time``
     - 10000 ms
     - 唤醒后若一直没有 VAD 事件，多久后触发 ``WAKEUP_END``
   * - ``wakeup_end``
     - 2000 ms
     - VAD 结束后再静音多久触发 ``WAKEUP_END``
   * - ``vcmd_timeout``
     - 5760 ms
     - 命令词检测超时，超时后须重新调用 begin
   * - ``delay_samples``
     - 2048 samples
     - 输出 PCM 延迟，补偿 VAD 检测滞后；时间换算后应大于 ``afe_config_t.vad_min_speech_ms``

**唤醒与 VAD 状态机**\ 。三种组合自动切换。仅启用唤醒：

.. only:: html

   .. mermaid::

      stateDiagram-v2
          [*] --> IDLE
          IDLE --> WAKEUP : 唤醒词 / WAKEUP_START
          WAKEUP --> IDLE : wakeup_time 超时 / WAKEUP_END

仅启用 VAD：

.. only:: html

   .. mermaid::

      stateDiagram-v2
          [*] --> IDLE
          IDLE --> SPEECHING : 检测到人声 / VAD_START
          SPEECHING --> IDLE : 静音 / VAD_END

唤醒 + VAD 联动：

.. only:: html

   .. mermaid::

      stateDiagram-v2
          [*] --> IDLE
          IDLE --> WAKEUP : 唤醒词 / WAKEUP_START
          WAKEUP --> SPEECHING : 人声 / VAD_START
          WAKEUP --> IDLE : wakeup_time 超时 / WAKEUP_END
          SPEECHING --> WAIT_FOR_SLEEP : 静音 / VAD_END
          WAIT_FOR_SLEEP --> SPEECHING : 人声 / VAD_START
          WAIT_FOR_SLEEP --> IDLE : wakeup_end 超时 / WAKEUP_END

联动模式适合"说出唤醒词 → 语音输入 → 静音后回到待机"的交互流程，避免 VAD 事件在唤醒间隔外被频繁触发。

**手动唤醒控制**\ 。除自动状态机外，三个 API 提供脱离语音的触发方式：

- :cpp:func:`esp_gmf_afe_trigger_wakeup` ：模拟唤醒词命中，立即进入 WAKEUP 状态并广播 ``WAKEUP_START``\ ；用于按键唤醒、外部传感器触发等场景
- :cpp:func:`esp_gmf_afe_trigger_sleep` ：手动切换到 IDLE
- :cpp:func:`esp_gmf_afe_keep_awake` ：启用后会关闭自动休眠计时（``wakeup_time`` 与 ``wakeup_end``）。设置后，处理单元不会因超时自动退出 WAKEUP，必须调用 :cpp:func:`esp_gmf_afe_trigger_sleep` 才会回到 IDLE

**命令词检测（VCMD）**\ ：独立于唤醒状态机。典型流程是在收到 ``WAKEUP_START`` 后调用 :cpp:func:`esp_gmf_afe_vcmd_detection_begin`\ ，命中后通过事件回调获取 :cpp:type:`esp_gmf_afe_vcmd_info_t`\ （包含 ``phrase_id``\ 、\ ``prob`` 与命令字符串），命中或超时后再次调用 begin 继续检测。\ ``vcmd_detection_cancel`` 清除当前检测状态、保留功能使能，下次仍可再次 begin。

事件系统
^^^^^^^^^^^^^^^^^^^^^^^^^^

ai_afe 通过 :cpp:type:`esp_gmf_afe_event_cb_t` 上报六类事件，事件枚举值有正有负，便于把命令词 ID 直接放在枚举里：

.. list-table::
   :widths: 36 16 48
   :header-rows: 1

   * - 事件
     - 值
     - 数据载体（payload）
   * - :c:macro:`ESP_GMF_AFE_EVT_WAKEUP_START`
     - -100
     - :cpp:type:`esp_gmf_afe_wakeup_info_t`\ （音量、唤醒词索引、模型索引）
   * - :c:macro:`ESP_GMF_AFE_EVT_WAKEUP_END`
     - -99
     - NULL
   * - :c:macro:`ESP_GMF_AFE_EVT_VAD_START`
     - -98
     - NULL
   * - :c:macro:`ESP_GMF_AFE_EVT_VAD_END`
     - -97
     - NULL
   * - :c:macro:`ESP_GMF_AFE_EVT_VCMD_DECT_TIMEOUT`
     - -96
     - NULL
   * - :c:macro:`ESP_GMF_AFE_EVT_VCMD_DECTECTED`
     - ``>= 0``
     - :cpp:type:`esp_gmf_afe_vcmd_info_t`\ ，枚举值等于 phrase ID

回调在 fetch_task 上下文执行，应用层建议只做轻量分发（更新状态、入消息队列）；耗时逻辑应在主线程或独立执行线程中执行。

ai_aec 独立回声消除
^^^^^^^^^^^^^^^^^^^^^^^^^^

ai_aec 仅完成回声消除：从多通道输入 PCM 中按 ``input_format`` 抽出麦克风 + 参考通道，经 AEC 算法处理后输出单声道 PCM。不需要模型分区，比 ai_afe 占用资源小，适合只需要消除回声、不需要唤醒 / VAD / 命令词的录音链路。

:cpp:type:`esp_gmf_aec_cfg_t` 三个调优字段：

- ``filter_len``\ ：滤波器长度，ESP32-S3 / P4 推荐 4，ESP32-C5 推荐 2，值越大 CPU 占用越高
- ``type``\ ：\ ``AFE_TYPE_VC``\ （语音通信）或 ``AFE_TYPE_SR``\ （语音识别）
- ``mode``\ ：\ ``AFE_MODE_LOW_POWER`` 或 ``AFE_MODE_HIGH_PERF``

.. code:: c

    esp_gmf_aec_cfg_t cfg = {
        .filter_len   = 4,
        .type         = AFE_TYPE_SR,
        .mode         = AFE_MODE_HIGH_PERF,
        .input_format = "MMNR",
    };
    esp_gmf_obj_handle_t aec = NULL;
    esp_gmf_aec_init(&cfg, &aec);

ai_aec 内部维护参考信号与麦克风信号的同步缓存：每次 ``process`` 累积一帧的对齐数据后调用底层 AEC，输出 16-bit 单声道 PCM。输入采样率通常为 16 kHz；配置 ``AFE_TYPE_VC_8K`` 时输入采样率为 8 kHz。位深必须为 16-bit PCM；任何不匹配会在 ``open`` 阶段被拒绝。

ai_wn 独立唤醒词检测
^^^^^^^^^^^^^^^^^^^^^^^^^^

ai_wn 是 WakeNet 的轻量封装：\ ``process`` 中对输入 PCM 同步运行检测，命中时回调用户 ``detect_cb`` 并把当前帧透传到输出数据端口；未命中也透传，由下游决定如何处理。

与 ai_afe 的区别：

- 不创建 feed / fetch 执行线程，处理直接发生在 GMF 执行线程上下文
- 不依赖 AFE 管理器与完整模型集，仅加载 WakeNet 模型
- 资源占用小，适合内存资源有限或只需要唤醒词检测的场景

.. code:: c

    esp_gmf_wn_cfg_t cfg = {
        .models       = models,
        .det_mode     = DET_MODE_2CH_90,
        .input_format = "MMNR",
        .detect_cb    = my_wakeup_cb,
        .user_ctx     = &ctx,
    };
    esp_gmf_element_handle_t wn = NULL;
    esp_gmf_wn_init(&cfg, &wn);

支持采样率 8 kHz 或 16 kHz、16-bit PCM。\ ``det_mode`` 在初始化 WakeNet 时已选定通道数（如 ``DET_MODE_90``\ ），\ ``input_format`` 中的 ``M`` 数量需与之匹配，否则模型拒绝运行。

ai_ns 独立降噪
^^^^^^^^^^^^^^^^^^^^^^^^^^

ai_ns 对单通道 PCM 做噪声抑制后输出同样格式的 PCM。它适合只需要降噪、不需要完整 AFE 状态机的录音或语音前处理链路。

:cpp:type:`esp_gmf_ns_cfg_t` 的主要字段：

- ``sample_rate``\ ：采样率，当前支持 16 kHz
- ``channel``\ ：通道数，当前仅支持单声道
- ``frame_ms``\ ：WebRTC NS 每帧时长，支持 10 / 20 / 30 ms
- ``model_name`` 与 ``partition_label``\ ：NSNet2 模型名与模型分区标签

.. code:: c

    esp_gmf_ns_cfg_t cfg = ESP_GMF_NS_CFG_DEFAULT();
    esp_gmf_obj_handle_t ns = NULL;
    esp_gmf_ns_init(&cfg, &ns);

启用 ``CONFIG_SR_NSN_NSNET2`` 时，ai_ns 从 ``partition_label`` 指定的模型分区加载 NSNet2 模型；启用 WebRTC NS 后端时，模型相关字段不参与处理。

ai_vad 独立人声活动检测
^^^^^^^^^^^^^^^^^^^^^^^^^^

ai_vad 对单通道 PCM 做人声活动检测，并在 VAD 状态变化时通过回调上报。处理单元可以把输入 PCM 复制到输出数据端口，便于后续处理链继续消费原始音频。

:cpp:type:`esp_gmf_vad_cfg_t` 的主要字段：

- ``sample_rate``\ ：采样率，WebRTC VAD 支持 8 kHz / 16 kHz / 32 kHz
- ``frame_ms``\ ：WebRTC VAD 每帧时长，支持 10 / 20 / 30 ms
- ``vad_mode``\ ：VAD 灵敏度模式
- ``result_callback``\ ：状态变化回调，返回底层 ``vad_state_t``
- ``model_name`` 与 ``partition_label``\ ：VADNet 模型名与模型分区标签

.. code:: c

    static void vad_cb(vad_state_t state, void *ctx)
    {
        /* 根据 VAD 状态更新应用逻辑 */
    }

    esp_gmf_vad_cfg_t cfg = ESP_GMF_VAD_CFG_DEFAULT();
    cfg.result_callback = vad_cb;
    esp_gmf_obj_handle_t vad = NULL;
    esp_gmf_vad_init(&cfg, &vad);

选择 VADNet 后端时，处理单元从模型分区加载 VADNet 模型，并使用模型要求的帧长；选择 WebRTC 后端时，\ ``frame_ms`` 控制每次处理的时长。

ai_doa 独立声源方向估计
^^^^^^^^^^^^^^^^^^^^^^^^^^

ai_doa 基于两路麦克风信号估计声源方向，处理结果通过回调返回角度值，不输出新的 PCM 数据。它适合麦克风阵列需要感知声源方向的应用。

:cpp:type:`esp_gmf_doa_cfg_t` 的主要字段：

- ``sample_rate``\ ：采样率，默认 16 kHz
- ``resolution``\ ：方向估计分辨率
- ``d_mics``\ ：两个麦克风之间的物理距离，单位为米
- ``frame_ms``\ ：每次输出一个 DOA 结果所需的音频时长，默认 64 ms
- ``input_format``\ ：输入通道排列，必须恰好包含两个 ``M`` 通道
- ``result_callback``\ ：方向估计结果回调

.. code:: c

    static void doa_cb(float angle, void *ctx)
    {
        /* angle 为声源方向估计结果 */
    }

    esp_gmf_doa_cfg_t cfg = ESP_GMF_DOA_CFG_DEFAULT();
    cfg.result_callback = doa_cb;
    esp_gmf_obj_handle_t doa = NULL;
    esp_gmf_doa_init(&cfg, &doa);

性能
-------------------------------------

AI Audio 处理单元的瓶颈集中在底层 ``esp-sr`` 算法，GMF 层的开销主要是 acquire-release 与回调分发。调优建议：

.. list-table::
   :widths: 18 22 60
   :header-rows: 1

   * - 模块
     - 主要瓶颈
     - 调优方向
   * - ai_afe
     - 唤醒模型 + AEC + NS 同时运行的 CPU
     - feed / fetch 分到不同 core（默认 0 / 1）；用 ``afe_config_init`` 关闭暂时不需要的特性
   * - ai_aec
     - 滤波器长度
     - ESP32-C5 等算力受限的 SoC 用 ``filter_len = 2``\ ；只录人声且环境安静时可关闭 AEC
   * - ai_wn
     - WakeNet 模型推理
     - ``det_mode`` 选择 1 通道版本（\ ``DET_MODE_90``\ ）减半计算量
   * - ai_ns
     - NS 模型或 WebRTC NS 运算
     - 单声道输入；按实际噪声场景选择 NSNet2 或 WebRTC 后端
   * - ai_vad
     - VAD 模型或 WebRTC VAD 运算
     - WebRTC 后端用较短帧长降低延迟；VADNet 后端需保证模型分区正确
   * - ai_doa
     - DOA 算法与双麦克风通道提取
     - 减小 ``frame_ms`` 可降低回调间隔；需按实际麦克风间距设置 ``d_mics``
   * - AFE 管理器
     - feed / fetch 队列长度与 ringbuffer 大小
     - 关注 ``afe_config_t.feed_buffer_size``\ ；read_cb 不应阻塞过久避免拖慢算法

应用示例
-------------------------------------

- ``elements/gmf_ai_audio/examples/wwe``\ ：唤醒词检测完整工程，覆盖 ai_afe + manager 创建、事件回调处理、命令词触发
- ``elements/gmf_ai_audio/examples/aec_rec``\ ：AEC 录音工程，演示 ai_aec 连接到处理链并输出回声消除后的 PCM
- ``elements/gmf_ai_audio/examples/wwe/README_CN.md`` 与 ``elements/gmf_ai_audio/examples/aec_rec/README_CN.md``\ ：每个工程的板级接线、Kconfig 选项与运行说明

通过 ``idf.py create-project-from-example "espressif/gmf_ai_audio=<version>:wwe"`` 可基于本组件直接生成可编译工程。

调试工具
-------------------------------------

`ESP 音频分析工具 <https://audio-tools.espressif.com.cn/>`_ 是乐鑫提供的音频测试方案，由设备端测试工程与网页端分析界面组成，经 WebSocket 连接后对麦克风、扬声器与 AEC 等功能做标准化检测，输出 THD、SNR 等指标与结构化测试报告。设备联网后于网页连接即可开始测试。

测试工程基于 gmf_ai_audio，录音链路使用 ai_afe 且默认开启 AFE 内 AEC。调试 AEC 效果时，可在播放与录音同时进行的双工场景下验证回声消除效果，无需自行抓 PCM 或编写播放脚本。网页端可实时调整 MIC gain（麦克风增益）、播放 Volume（扬声器音量）与通道格式（\ ``M``\ /\ ``R``\ /\ ``N``\ 排列，如 ``MMNR``\ ），对照硬件回采接线并观察 AEC 效果变化；原始录音可导出，结合报告中的处理前后对比，辅助排查回声残留等问题。

- 覆盖麦克风、扬声器、AEC 共 11 项标准化音频测试
- 测试工程默认启用 ai_afe 内 AEC，与本文档处理单元配置一致
- 网页端可调 MIC gain、播放 Volume 与通道格式，便于 AEC 效果对比
- 支持原始录音导出与结构化测试报告
- 配套测试工程：`esp_audio_analyzer_app <https://github.com/espressif/esp-adf/tree/master/adf_examples/checks/esp_audio_analyzer_app>`_

SoC 兼容性
-------------------------------------

不同处理单元依赖的 ``esp-sr`` 模型与硬件加速能力不同，支持矩阵如下：

.. list-table::
   :widths: 18 16 18 18 18 18 18
   :header-rows: 1

   * - 处理单元
     - ESP32
     - ESP32-S3
     - ESP32-S31
     - ESP32-C3
     - ESP32-C5
     - ESP32-P4
   * - ai_afe
     - 支持
     - 支持
     - 支持
     - 不支持
     - 不支持
     - 支持
   * - ai_aec
     - 支持
     - 支持
     - 支持
     - 不支持
     - 支持
     - 支持
   * - ai_wn
     - 支持
     - 支持
     - 支持
     - 支持
     - 支持
     - 支持
   * - ai_ns
     - 支持
     - 支持
     - 支持
     - 不支持
     - 支持
     - 支持
   * - ai_vad
     - 支持
     - 支持
     - 支持
     - 支持
     - 支持
     - 支持
   * - ai_doa
     - 不支持
     - 支持
     - 支持
     - 不支持
     - 不支持
     - 不支持

ai_afe 与 ai_wn 都依赖 ``esp-sr`` 的模型数据分区，应用层需要在分区表中预留 ``model`` 分区并烧入对应模型。模型准备与烧录步骤请参考 ``esp-sr`` 文档，以及 ``elements/gmf_ai_audio/examples/wwe/README_CN.md`` 的模型配置说明。

FAQ
-------------------------------------

**Q：** 唤醒词检测灵敏度不足或未上报事件，如何排查？

按以下顺序排查：\ ``afe_config_t.wakenet_init`` 是否为 true、模型分区是否正确烧录、\ ``input_format`` 中 ``M`` 通道数量与硬件麦克风接线是否一致、麦克风采样电平是否过低（用示波器或 ``esp_gmf_afe_wakeup_info_t.data_volume`` 反推）。示例 ``wwe`` 的 ``README.md`` 给出完整的硬件检查清单。

**Q：** feed_task 触发了任务看门狗超时？

AFE 推理的 CPU 占用较高，\ ``feed_task`` 与 ``fetch_task`` 应分配到不同 core。在 ESP32 单核芯片上，\ ``feed_task`` 与应用其他高负载执行线程竞争时容易超时，建议提高 ``fetch_task_setting.prio`` 或使用独立定时任务向 AFE 写入输入数据。

**Q：** ai_aec 输出有明显回声残留？

确认四件事：参考信号（\ ``R`` 通道）是否连接到扬声器输出的回采、采样率是否为 16 kHz、麦克风与参考的时间对齐是否存在偏差、\ ``filter_len`` 是否过小（ESP32-S3 / P4 推荐 4）。具体调试方法见 ``esp_gmf_aec.c`` 头注与 ``esp-sr`` AEC 文档。

**Q：** 命令词检测 begin 之后没有事件？

\ ``vcmd_detect_en`` 是否在 :cpp:type:`esp_gmf_afe_cfg_t` 里置 true、\ ``mn_language`` 与模型语言是否一致（\ ``cn`` / ``en``\ ）、\ ``vcmd_timeout`` 内是否已输入命令词。超时后会返回 :c:macro:`ESP_GMF_AFE_EVT_VCMD_DECT_TIMEOUT`\ ，需要再次调用 begin。

**Q：** ai_wn 与 ai_afe 如何选择？

仅需唤醒词的轻量化场景（蓝牙音箱、传感器节点）应使用 ai_wn；需要完整语音交互（唤醒 + VAD + 命令词 / AEC / NS）应使用 ai_afe。两者都处理原始多通道 PCM，不能在同一处理链串联。

**Q：** 如何单独使用 esp_gmf_afe_manager，不连接 GMF 处理链？

:cpp:func:`esp_gmf_afe_manager_create` 不要求调用方是 GMF 处理单元，\ ``read_cb`` 与 ``result_cb`` 都是普通回调。在非 GMF 场景下可单独使用，自行管理输入 / 输出循环；不再具备 acquire-release 协议与处理链控制能力。

API 参考
-------------------------------------

本组件涉及的头文件：

- ``esp_gmf_afe_manager.h``\ ：AFE 管理器配置、特性开关、暂停 / 恢复
- ``esp_gmf_afe.h``\ ：ai_afe 处理单元初始化、命令词控制、手动唤醒、事件回调
- ``esp_gmf_aec.h``\ ：ai_aec 处理单元配置
- ``esp_gmf_wn.h``\ ：ai_wn 处理单元配置与检测回调
- ``esp_gmf_ns.h``\ ：ai_ns 处理单元配置
- ``esp_gmf_vad.h``\ ：ai_vad 处理单元配置与结果回调
- ``esp_gmf_doa.h``\ ：ai_doa 处理单元配置与方向估计回调
- ``esp_gmf_ai_audio_methods.h``\ ：运行时方法名宏

.. include-build-file:: inc/esp_gmf_afe_manager.inc

.. include-build-file:: inc/esp_gmf_afe.inc

.. include-build-file:: inc/esp_gmf_aec.inc

.. include-build-file:: inc/esp_gmf_wn.inc

.. include-build-file:: inc/esp_gmf_ai_audio_methods.inc
