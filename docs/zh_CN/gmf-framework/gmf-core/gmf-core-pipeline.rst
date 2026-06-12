GMF 处理链与任务调度
================================================

:link_to_translation:`en:[English]`

本篇按"介绍 → 原理 → 基础示例 → 进阶 → 其他"五段递进，覆盖处理链（pipeline）、处理单元（element）、执行线程（task）三个对象的协作方式、运行时调度、错误处理与组合手段。

处理单元（element）的生命周期、能力描述（capability）、运行时方法（method）与自定义模板见 :doc:`gmf-core-element`。数据在处理单元之间的流动细节（数据载体（payload）、数据端口（port）、外部接口（IO））见 :doc:`gmf-core-data-path`。数据总线（data bus）四种实现见 :doc:`gmf-core-databus`。整体架构与各对象关系参考 :doc:`gmf-core-overview`。

介绍
-------------------------------------

阅读本篇后，您将能够：

- 理解执行线程如何调度处理单元的三个功能函数
- 知道事件与状态如何在处理链 / 执行线程 / 用户代码之间流动
- 用注册池（pool）批量管理处理单元模板，按需实例化多种处理链
- 在多处理链组合、暂停、停止、错误恢复等场景下选择正确接口
- 搭建一条基础处理链并启动运行

本篇只描述编排与控制层。要写自己的处理单元，先看 :doc:`gmf-core-element` 的处理单元模板和 :doc:`gmf-core-data-path` 的 acquire-release 协议。

原理
-------------------------------------

三个对象的分工
^^^^^^^^^^^^^^^^^^^^^^^^^^

多媒体处理通常由多个步骤串联而成，例如"读文件 → 解 MP3 → 重采样 → 写 I2S"。每个步骤都是一段独立的算法，它们必须按正确顺序启动、逐级传递数据、在出错或结束时一起收尾。GMF-Core 把这些关注点拆分到三个对象。

**处理单元是具体工作的执行者**\ ：实现 ``open`` / ``process`` / ``close`` 三个功能函数。框架保证功能函数按 open → process × N → close 顺序被调用，处理单元开发者只需关注单次调用的处理逻辑。

**处理链是编排者**\ ：把多个处理单元按顺序连接起来，对外暴露 ``run`` / ``stop`` / ``pause`` / ``resume`` / ``reset`` / ``seek`` 等控制接口。应用代码只与处理链交互，内部的事件分发、外部接口打开关闭、出错收尾由处理链自动处理。处理链本身不执行处理单元的代码。

**执行线程是执行者**\ ：本质是一个由框架封装的运行单元。执行线程维护一个 job 链表，按既定顺序调用每个处理单元的功能函数。处理链描述"连接方式"，执行线程描述"运行方式"，两者分开后同一条处理链可以绑定不同配置的执行线程（不同栈、优先级、CPU 核心）。

三者协作的总览：

.. only:: html

   .. mermaid::

      flowchart TB
          User["用户代码"]
          Pipeline["处理链"]
          Task["执行线程<br/>(task)"]
          Element["处理单元链"]

          User -->|"run / stop / pause"| Pipeline
          Pipeline -->|"控制指令"| Task
          Task -->|"状态事件"| Pipeline
          Pipeline -->|"状态回调"| User
          Task -->|"调用回调"| Element
          Element -->|"格式信息"| Pipeline

job 与事件的运行时序
^^^^^^^^^^^^^^^^^^^^^^^^^^

处理单元的 ``open`` / ``process`` / ``close`` 在执行线程内部都以 job 的形式存在。job 何时被调度、事件何时被回调，遵循一条固定的时序。下图展示一次 ``run`` 调用之后控制面与事件面的完整流动：

.. only:: html

   .. mermaid::

      sequenceDiagram
          participant U as 用户
          participant P as 处理链
          participant T as 执行线程
          participant E1 as 处理单元 1
          participant E2 as 处理单元 2

          U->>P: run()
          P->>T: 启动执行线程
          T-->>P: CHANGE_STATE: OPENING
          P-->>U: 状态回调

          T->>E1: open
          T->>E1: process(首轮)
          E1-->>P: REPORT_INFO（格式）
          P->>E2: 写入格式信息 + 注册 open/process job
          T->>E2: open
          T->>E2: process(首轮)

          T-->>P: CHANGE_STATE: RUNNING
          P-->>U: 状态回调

          loop running 循环
              T->>E1: process
              T->>E2: process
          end

          Note over T: 触发 STOP / FINISH / ERROR
          T->>E1: close
          T->>E2: close
          T-->>P: CHANGE_STATE: STOPPED/FINISHED/ERROR
          P-->>U: 状态回调

时序包含三条规则：\ ``CHANGE_STATE`` 由执行线程上报、处理链转发到用户回调；\ ``REPORT_INFO`` 由处理单元上报，处理链据此写入下游依赖型处理单元的格式信息；非依赖型处理单元在 ``loading_jobs`` 时注册 open/process job，依赖型处理单元在收到上游格式信息后再注册 open/process job。

处理单元生命周期与三类派生
^^^^^^^^^^^^^^^^^^^^^^^^^^

每个处理单元必须实现三个功能函数，分别对应初始化、数据处理与资源回收三个阶段，框架按固定顺序调用。

**open**\ ：一次性初始化。处理单元在这里根据上游提供的格式信息配置自己，例如 MP3 解码器初始化解码器上下文、分配工作缓冲区、根据采样率配置滤波器系数。open 成功后等待 process 被调度；返回失败则处理链立即进入 ``ERROR``\ ，后续处理单元的 open 不再执行。

**process**\ ：数据处理主循环。这个回调会被反复调用，每次执行一轮从输入数据端口申请数据载体、处理、写入输出数据端口的操作。process 的返回值决定执行线程下一步动作（继续循环、报告完成、报错），具体返回码在下一节列出。

**close**\ ：资源回收。无论处理链因正常结束、用户 stop 还是 error 而退出，框架都会依次调用每个已 open 过的处理单元的 close。close 实现应当与 open 中申请的资源一一对应；即使遇到错误，也建议只记录日志，让框架继续执行后续处理单元的 close，避免资源泄漏。

按处理的数据类别，处理单元派生出三个子类，区别在于各自携带一个专用的格式信息结构。

.. list-table::
   :widths: 36 30 34
   :header-rows: 1

   * - 派生类
     - 信息结构
     - 字段
   * - ``esp_gmf_audio_element_t``
     - ``esp_gmf_info_sound_t``
     - 采样率、声道、位宽、FourCC
   * - ``esp_gmf_video_element_t``
     - ``esp_gmf_info_video_t``
     - 分辨率、帧率、FourCC
   * - ``esp_gmf_pic_element_t``
     - ``esp_gmf_info_pic_t``
     - 宽、高

格式用 FourCC 标识，全部宏定义与按类别整理的表格见 :doc:`gmf-core-fourcc`。

执行线程三阶段调度
^^^^^^^^^^^^^^^^^^^^^^^^^^

执行线程启动后按 opening → running → cleanup 三阶段执行 job 链表。

**opening**\ ：执行线程遍历 job 链表，对每个处理单元依次调用 open，成功后立即调用一次 process。首次 process 让处理单元尽早上报格式信息，下游依赖型处理单元才能被唤醒。某个处理单元的 open 返回失败时，处理链立即进入 ``ERROR``\ 。

**running**\ ：按处理单元连接顺序循环调用 process。一轮结束立即进入下一轮，直到触发下列任一退出条件：

- 某个处理单元的 process 返回 ``ESP_GMF_JOB_ERR_DONE``\ ，该处理单元永久完成，从 job 链表移除；所有处理单元都 DONE 后执行线程进入 FINISH 流程。
- 某个处理单元的 process 返回 ``ESP_GMF_JOB_ERR_FAIL``\ ，执行线程立即进入 ERROR 流程。
- 某个处理单元的 process 返回 ``ESP_GMF_JOB_ERR_ABORT``\ ，执行线程进入 ABORT 流程。
- 用户调用 :cpp:func:`esp_gmf_pipeline_stop`\ ，执行线程在下一个 job 边界检测到 ``STOP`` 标志位后进入 STOP 流程。

**cleanup**\ ：无论以何种方式退出 running，执行线程都会把每个已 open 过的处理单元的 close 追加到链表尾部并执行。每个 open 都对应一个 close。cleanup 完成后执行线程进入 ``STOPPED`` / ``FINISHED`` / ``ERROR`` 之一，通过事件回调通知处理链。

job 返回码对调度的影响
^^^^^^^^^^^^^^^^^^^^^^^^^^

处理单元的 open / process / close 在框架内部都以 job 的形式存在，返回值决定执行线程下一步如何调度。

.. list-table::
   :widths: 32 12 56
   :header-rows: 1

   * - 返回码
     - 值
     - 调度行为
   * - ``ESP_GMF_JOB_ERR_OK``
     - 0
     - 正常完成。一次性 job 从链表移除，无限次 job 保留等待下一轮
   * - ``ESP_GMF_JOB_ERR_CONTINUE``
     - 1
     - 立即跳转到第一个处理单元的 job 开始执行，当前处理单元之后的 job 跳过这一轮。常用于处理单元发现数据不足、需要再次获取数据的场景
   * - ``ESP_GMF_JOB_ERR_DONE``
     - 2
     - job 永久完成，立即从链表移除。即使是无限次 job 也不再被调度
   * - ``ESP_GMF_JOB_ERR_TRUNCATE``
     - 3
     - 当前 job 暂存到 job stack，下一轮从返回 ``ESP_GMF_JOB_ERR_TRUNCATE`` 的处理单元 job 继续执行。一般用于输出缓冲区已满、需要先让下游消费数据再继续写入的场景
   * - ``ESP_GMF_JOB_ERR_FAIL``
     - -1
     - 执行失败，触发执行线程的 ERROR 流程
   * - ``ESP_GMF_JOB_ERR_ABORT``
     - -3
     - 被主动中止，按 abort 策略决定后续进入 STOP 或 RESET 流程

``TRUNCATE`` 结合 job stack 允许处理单元在"尚未处理完成，但需要让下游先执行"的场景下暂停自身，等待下游消耗数据后再恢复。例如块型数据端口的上游处理单元在输出缓冲区已满时返回 TRUNCATE，让下游先消费，再恢复当前写入流程。

基础示例
-------------------------------------

下面用注册池批量管理处理单元模板，搭建一条"文件 → 解码 → 重采样 → I2S"的最小处理链。注册池适合应用启动时把所有支持的处理单元注册一次，之后按名称组合出多种处理链。

第一步，应用启动时把要用到的处理单元与外部接口注册到注册池：

.. code:: c

    esp_gmf_pool_handle_t pool;
    esp_gmf_pool_init(&pool);

    esp_gmf_pool_register_element(pool, audio_decoder_handle, NULL);
    esp_gmf_pool_register_element(pool, audio_rate_cvt_handle, NULL);
    esp_gmf_pool_register_io(pool, file_io_handle, NULL);
    esp_gmf_pool_register_io(pool, i2s_io_handle, NULL);

第二步，按名称构建处理链，绑定执行线程，加载 job，然后 run：

.. code:: c

    const char *el_names[] = {"aud_dec", "aud_rate_cvt"};
    esp_gmf_pipeline_handle_t pipe = NULL;
    esp_gmf_pool_new_pipeline(pool, "io_file", el_names, 2, "io_i2s", &pipe);

    esp_gmf_task_handle_t task = NULL;
    esp_gmf_task_cfg_t task_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    task_cfg.thread.stack = 8 * 1024;
    task_cfg.name = "player_task";
    esp_gmf_task_init(&task_cfg, &task);

    esp_gmf_pipeline_bind_task(pipe, task);
    esp_gmf_pipeline_loading_jobs(pipe);
    esp_gmf_pipeline_set_event(pipe, my_event_cb, my_ctx);

    esp_gmf_pipeline_set_in_uri(pipe, "/sdcard/song.mp3");
    esp_gmf_pipeline_run(pipe);

各步骤的含义：

- :cpp:func:`esp_gmf_pool_new_pipeline` 内部按 tag 字符串查找模板，调用 ``esp_gmf_obj_dupl`` 复制出全新实例，按顺序串接并绑定首尾外部接口。
- :cpp:func:`esp_gmf_task_init` 创建执行线程。\ :cpp:type:`esp_gmf_task_cfg_t` 可以配置栈大小、优先级、CPU 核心，以及是否把栈放在外部 PSRAM。\ ``DEFAULT_ESP_GMF_TASK_CONFIG()`` 给出 4 KB 栈、优先级 5、core 0 的默认值。
- :cpp:func:`esp_gmf_pipeline_bind_task` 建立处理链与执行线程的关联，并把执行线程的事件回调设为处理链内部的事件处理器。这一步之后，执行线程运行中的任何状态变化都会被处理链感知。
- :cpp:func:`esp_gmf_pipeline_loading_jobs` 把处理单元的 open 与 process 注册到执行线程的 job 链表。只有处于 ``INITIALIZED`` 的处理单元会被注册；依赖型处理单元在收到上游格式信息之前不会注册，处理链收到 REPORT_INFO 后再为它注册 open/process job。
- :cpp:func:`esp_gmf_pipeline_run` 启动执行线程，job 链表开始被依次执行。

需要调整某个处理单元的参数时，通过 :cpp:func:`esp_gmf_pipeline_get_el_by_name` 取出句柄再修改：

.. code:: c

    esp_gmf_element_handle_t dec = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec);
    mp3_decoder_cfg_t *cfg = (mp3_decoder_cfg_t *)OBJ_GET_CFG(dec);
    cfg->out_rb_size = 8 * 1024;

不通过注册池也可以手动搭建：依次调用 ``esp_gmf_pipeline_create`` → ``register_el`` → ``set_io`` → ``bind_task`` → ``loading_jobs`` → ``run``\ 。注册池内部执行相同步骤，可省去重复编写注册代码的工作。

进阶
-------------------------------------

依赖型处理单元的延后启动
^^^^^^^^^^^^^^^^^^^^^^^^^^

多媒体处理中常见的一类问题：某些处理单元的初始化参数要到上游处理单元开始处理之后才能得到。典型例子是重采样器，只有知道输入采样率才能配置转换参数，但采样率要等 MP3 解码器解析出文件头之后才能得到。如果在处理链启动时一并 open 所有处理单元，重采样器要么用错误的默认值初始化、要么直接 open 失败。

GMF-Core 用 ``dependency`` 标志和 REPORT_INFO 事件解决这类前后依赖，流程如下：

1. 开发者在处理单元的配置里设置 ``dependency = true``\ ，声明该处理单元需要上游信息才能初始化。
2. 处理链启动后，执行线程先对非依赖型处理单元调用 open 与 process，对依赖型处理单元暂不调度。
3. 上游处理单元在自己的 open 或 process 中解析到格式信息后，通过 REPORT_INFO 事件上报。
4. 处理链收到事件后，把信息写入下游依赖型处理单元，然后触发该处理单元进入 ``INITIALIZED`` 状态。
5. 处理链为该处理单元注册 open 和 process job，执行线程随后按 job 链表调度该处理单元。

依赖关系可以级联：一条处理链里可以有多个依赖型处理单元前后相接，每个处理单元在上游报告格式信息后才注册 open/process job。应用代码不需要编写协调逻辑，处理链按上述规则完成注册与调度。

格式信息与 REPORT_INFO
^^^^^^^^^^^^^^^^^^^^^^^^^^

格式信息用于处理单元之间传递音视频参数，相关结构定义在 ``esp_gmf_info.h``\ 。音频处理单元常用 ``esp_gmf_info_sound_t`` 描述采样率、声道、位深和 FourCC；视频处理单元使用 ``esp_gmf_info_video_t`` 描述宽高、帧率和像素格式。处理单元在 open 或 process 中解析到格式后，通过 REPORT_INFO 事件上报给处理链。

.. code:: c

    esp_gmf_info_sound_t snd = {
        .format_id    = ESP_FOURCC_PCM,
        .sample_rates = 44100,
        .bitrate      = 0,
        .channels     = 2,
        .bits         = 16,
    };
    esp_gmf_audio_el_set_snd_info(handle, &snd);
    esp_gmf_element_notify_snd_info(handle, &snd);

处理链收到 REPORT_INFO 后，把格式信息写入下游依赖型处理单元，并为已满足依赖条件的处理单元注册 open/process job。因此，REPORT_INFO 不只是通知用户的事件，也是依赖型处理单元延后启动的触发条件。应用也可以通过 :cpp:func:`esp_gmf_pipeline_report_info` 主动上报格式信息，用于没有上游处理单元自动上报的场景。

进阶注册池 API
^^^^^^^^^^^^^^^^^^^^^^^^^^

除了基本的注册与构建接口，注册池还提供三类进阶用法。

**插入高优先模板**\ ：\ :cpp:func:`esp_gmf_pool_register_element_at_head` 与 :cpp:func:`esp_gmf_pool_register_element` 行为一致，区别在于把处理单元插入到链表头部。注册池查找模板按链表顺序遍历，先注册的优先匹配，因此用 ``_at_head`` 注册的自定义处理单元可以覆盖同名的默认实现。

**按 URL 自动选外部接口**\ ：\ :cpp:func:`esp_gmf_pool_get_io_tag_by_url` 遍历注册池中所有外部接口模板，调用每个外部接口的 ``get_score`` 回调对 URL 评分，返回评分最高的外部接口 tag。

.. code:: c

    char *io_tag = NULL;
    esp_gmf_pool_get_io_tag_by_url(pool, "http://server/song.mp3",
                                   ESP_GMF_IO_DIR_READER, &io_tag);
    /* io_tag 通常为 "io_http"，用作 esp_gmf_pool_new_pipeline 的 in_name */

评分档位的详细定义见 :doc:`gmf-core-data-path` 的"URL 评分策略"小节。

**遍历已注册模板**\ ：\ :cpp:func:`esp_gmf_pool_iterate_element` 与 :cpp:func:`esp_gmf_pipeline_iterate_element` 提供基于迭代器的遍历方式，常用于日志输出、能力探测或调试。把 ``*iterator`` 初始化为 ``NULL`` 后逐次调用，直到返回 ``ESP_GMF_ERR_NOT_FOUND`` 结束。

.. code:: c

    const void *it = NULL;
    esp_gmf_element_handle_t el = NULL;
    while (esp_gmf_pool_iterate_element(pool, &it, &el) == ESP_GMF_ERR_OK) {
        const char *tag = NULL;
        esp_gmf_obj_get_tag((esp_gmf_obj_handle_t)el, &tag);
        ESP_LOGI("APP", "registered element: %s", tag);
    }

执行线程策略与 abort 流程
^^^^^^^^^^^^^^^^^^^^^^^^^^

执行线程的默认行为是：所有 job DONE 后进入 ``FINISHED``\ ；某个 job FAIL 进入 ``ERROR``\ ；某个 job ABORT 进入 ``STOPPED``\ 。对需要特殊行为的场景，通过 :cpp:func:`esp_gmf_task_set_strategy_func` 注册策略函数覆盖默认行为。

策略函数在两种触发点被回调：

- ``GMF_TASK_STRATEGY_TYPE_FINISH``\ ：所有 job 返回 DONE 时
- ``GMF_TASK_STRATEGY_TYPE_ABORT``\ ：某个 job 返回 ABORT 时

策略函数的返回值决定执行线程下一步进入哪条路径：

- ``GMF_TASK_STRATEGY_ACTION_DEFAULT``\ ：按默认流程进入终止状态
- ``GMF_TASK_STRATEGY_ACTION_RESET``\ ：加载每个处理单元的 reset job，恢复为 ``INITIALIZED``\ 。适合"播放完一首自动切下一首"的场景，避免整条处理链重建
- ``GMF_TASK_STRATEGY_ACTION_STOP``\ ：加载 close job 进入 STOP 流程

策略函数内部不能调用任何执行线程或处理链的控制 API，否则会触发超时错误。如果需要等待外部事件，使用信号量之类的阻塞机制。

stop 与 abort 的语义差别如下。\ ``stop`` 由用户主动调用 :cpp:func:`esp_gmf_pipeline_stop` / :cpp:func:`esp_gmf_task_stop` 发起，执行线程在下一个 job 边界检测到 ``STOP`` 标志位后进入 cleanup，属于软停止；正在执行的 job 完成当前一次调用后退出。\ ``abort`` 由处理单元在 ``process`` 中返回 ``ESP_GMF_JOB_ERR_ABORT`` 或由数据层调用 ``esp_gmf_db_abort`` / ``esp_gmf_io_abort`` 触发；网络中断时 HTTP 外部接口上报 ``ESP_GMF_IO_ABORT``\ ，处理单元把这个外部接口错误向上转换为 job 的 ABORT 返回码。abort 出现后执行线程由策略函数决定进入 STOPPED 或 RESET，配合 RESET 动作可以实现无需销毁处理链的恢复流程，例如"重连后从同一音频继续播放"或"切换到下一首"。

错误处理路径
^^^^^^^^^^^^^^^^^^^^^^^^^^

任一处理单元的 job 返回 ``FAIL`` 时，执行线程按如下顺序清理并通知：

1. 执行线程把 job 链表中所有未执行的 process job 从链表移除，阻止新的数据处理动作。
2. 执行线程把每个已 open 过的处理单元的 close 追加到链表尾部。
3. 执行线程依次执行 close，让处理单元回收自己申请的资源。
4. 处理链的事件处理器收到 ``CHANGE_STATE = ERROR``\ ，调用输入外部接口与输出外部接口的 close 关闭外设。
5. 处理链把 ERROR 事件转发给用户注册的回调。

这一路径是幂等的：即使某个 close 自身也失败，执行线程也会继续处理剩余的 close 直到链表清空，避免单个处理单元的失败阻塞整个清理流程。进入 ERROR 后处理链停留在该状态，必须调用 :cpp:func:`esp_gmf_pipeline_reset` 清理状态后才能重新启动。

输入 / 输出外部接口的 open 失败也会触发同样的路径。例如输入文件打不开时，处理链的事件处理器检测到外部接口 open 返回非 OK，直接把状态切到 ERROR 并启动 cleanup。

组合多条处理链
^^^^^^^^^^^^^^^^^^^^^^^^^^

一个应用常由多条处理链组合而成（例如解码处理链与渲染处理链通过 ringbuffer 桥接）。GMF-Core 提供以下接口支持处理链组合。

**事件级联**\ ：\ :cpp:func:`esp_gmf_pipeline_reg_event_recipient` 把 connectee 加入 connector 的 ``evt_conveyor`` 链。之后 connector 接收到的任何 REPORT_INFO 事件都会转发一份给 connectee，下游处理链的依赖型处理单元就能在上游处理链获取格式信息时被唤醒。

:cpp:func:`esp_gmf_pipeline_connect_pipe` 在事件级联的基础上，还把 connector 中某个处理单元的 out 数据端口直接连到 connectee 中某个处理单元的 in 数据端口。这在需要细粒度连接两条处理链的特定处理单元时使用。

**预 / 后置回调**\ ：\ :cpp:func:`esp_gmf_pipeline_set_prev_run_cb` 与 :cpp:func:`esp_gmf_pipeline_set_prev_stop_cb` 允许在 run 和 stop 之前插入自定义动作。一个常见用途是依赖关系管理：上游处理链 stop 之前通知下游先 stop，避免下游在上游停止之后继续等待数据。这两个回调也可以通过 :cpp:func:`esp_gmf_pipeline_prev_run` / :cpp:func:`esp_gmf_pipeline_prev_stop` 手动触发，触发之后不会被 run / stop 再次调用。

**pause-on-start**\ ：\ :cpp:func:`esp_gmf_pipeline_set_pause_on_start` 让处理链在 run 之后立即进入 ``PAUSED``\ ，不执行 process。典型用途是多条处理链需要同时启动时，让每条都先进入 PAUSED 再统一 resume，保证同步起点。

**外部接口替换**\ ：\ :cpp:func:`esp_gmf_pipeline_replace_in` / :cpp:func:`esp_gmf_pipeline_replace_out` 允许在处理链非 ``RUNNING`` 状态替换首尾外部接口，用于"换一首歌播放"之类的场景。被替换下来的旧外部接口所有权回到用户手里，需要用户自行 destroy。

其余
-------------------------------------

控制接口的合法状态
^^^^^^^^^^^^^^^^^^^^^^^^^^

处理链对外暴露一套统一的控制接口，实现上都转发到绑定的执行线程。下表列出每个 API 的合法调用状态，调用于非法状态时返回 ``ESP_GMF_ERR_NOT_SUPPORT`` 或 ``ESP_GMF_ERR_INVALID_STATE``\ 。状态的完整枚举见 :doc:`gmf-core-overview`。

.. list-table::
   :widths: 26 30 44
   :header-rows: 1

   * - API
     - 合法状态
     - 效果
   * - :cpp:func:`esp_gmf_pipeline_run`
     - ``INITIALIZED`` / ``STOPPED`` / ``FINISHED``
     - 触发 ``prev_run`` 回调（若配置），启动执行线程，进入 ``OPENING``
   * - :cpp:func:`esp_gmf_pipeline_stop`
     - ``RUNNING`` / ``PAUSED``
     - 触发 ``prev_stop`` 回调（若配置），请求执行线程停止，进入 ``STOPPED``
   * - :cpp:func:`esp_gmf_pipeline_pause`
     - ``RUNNING``
     - 挂起 process 调度，保留上下文，进入 ``PAUSED``
   * - :cpp:func:`esp_gmf_pipeline_resume`
     - ``PAUSED``
     - 释放挂起信号量，恢复为 ``RUNNING``
   * - :cpp:func:`esp_gmf_pipeline_reset`
     - 非 ``RUNNING``
     - 清空 job 链表、重置处理单元与数据端口状态，回到 ``INITIALIZED``
   * - :cpp:func:`esp_gmf_pipeline_seek`
     - ``PAUSED`` / ``STOPPED`` / ``FINISHED``
     - 调用输入外部接口的 seek 接口跳转到指定位置

几个细节值得单独说明：

- ``pause`` 设置 ``PAUSE`` 标志位，执行线程在下一个 job 边界处阻塞到信号量上；\ ``resume`` 释放信号量让执行线程继续。暂停发生在 job 边界而不是 job 内部，避免在处理单元正处理数据时被打断。
- ``reset`` 清空执行线程的 job 链表，调用每个处理单元的 ``reset_state`` 与 ``reset_port``\ 。reset 之后要再次 run，必须先调用 :cpp:func:`esp_gmf_pipeline_loading_jobs` 重新加载 job。
- ``seek`` 只对支持按帧独立解码的流式格式（如 MP3、AAC、TS）有意义。调用前处理链必须处于非运行态，以避免 seek 过程中外部接口被并发读取。

超时与同步语义
^^^^^^^^^^^^^^^^^^^^^^^^^^

所有控制 API 默认都会阻塞，最长等待时间由 ``DEFAULT_TASK_OPT_MAX_TIME_MS``\ （2000 ms）控制，通过 :cpp:func:`esp_gmf_task_set_timeout` 可以调整这一超时值。

``stop`` 的行为稍有不同：即使超时返回 ``ESP_GMF_ERR_TIMEOUT``\ ，停止动作仍会在后台继续。执行线程内部的重试逻辑会持续等待，直到所有 job 退出，以保证资源处于稳定状态。\ ``pause`` 在超时后同样仍会生效，调用者可以选择再次等待或继续执行后续流程。

停止与超时
^^^^^^^^^^^^^^^^^^^^^^^^^^

:cpp:func:`esp_gmf_pipeline_stop` 返回 ``ESP_GMF_ERR_TIMEOUT`` 不表示停止失败，而是表示同步等待没有在限定时间内完成；停止动作仍在后台继续推进。常见原因是某个处理单元在 process 中进行了较长的阻塞操作（例如等待网络数据或互斥锁）。处理方式有两种：调大 :cpp:func:`esp_gmf_task_set_timeout` 的值，或在处理单元实现中检查执行线程的 abort 标志主动退出。

依赖未启动
^^^^^^^^^^^^^^^^^^^^^^^^^^

``loading_jobs`` 之后某个处理单元仍然没有 open，通常是它被标记为 ``dependency = true``\ ，但上游一直没有上报格式信息。确认上游处理单元在 open 或 process 中调用了 ``esp_gmf_element_notify_snd_info``\ （或对应派生类的 notify 接口），或由应用层通过 :cpp:func:`esp_gmf_pipeline_report_info` 主动上报信息。

ERROR 后恢复
^^^^^^^^^^^^^^^^^^^^^^^^^^

ERROR 状态下调用 run 会返回 ``ESP_GMF_ERR_NOT_SUPPORT``\ 。恢复步骤是 :cpp:func:`esp_gmf_pipeline_reset` 把状态回到 ``INITIALIZED``\ ，再 :cpp:func:`esp_gmf_pipeline_loading_jobs` 重新加载 job，然后才能 run。

abort 后处理链阻塞
^^^^^^^^^^^^^^^^^^^^^^^^^^

处理单元返回 ``ABORT`` 后执行线程进入策略函数。若策略函数返回 ``GMF_TASK_STRATEGY_ACTION_DEFAULT``\ ，处理链按 STOPPED 路径退出，必须 reset 才能再次 run。若希望 abort 后自动恢复，注册策略函数并返回 ``GMF_TASK_STRATEGY_ACTION_RESET``\ 。策略函数内不能调用任何处理链控制 API。

pause 之后 state 仍是 RUNNING
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

默认情况下不会发生。\ ``pause`` 会等待执行线程处理暂停请求；只有执行线程进入 ``PAUSED`` 后，才返回 ``ESP_GMF_ERR_OK``\ 。因此 ``pause`` 返回 OK 后再查询 state，应当读到 ``PAUSED``\ ，而不是 ``RUNNING``\ 。

如果执行线程迟迟没有到达下一个 job 边界，\ ``pause`` 会返回 ``ESP_GMF_ERR_TIMEOUT``\ 。这个返回值只表示本次同步等待超时，不表示暂停请求已取消；暂停请求仍会在后续 job 边界生效。因此在超时返回后立刻查询 state，state 仍可处于 ``RUNNING``\ 。

API 参考
-------------------------------------

本篇涉及的头文件：

- ``esp_gmf_pipeline.h``\ ：处理链的创建、控制与级联
- ``esp_gmf_task.h``\ ：执行线程的配置、控制与策略
- ``esp_gmf_job.h``\ ：job 结构、返回码与 job stack
- ``esp_gmf_pool.h``\ ：注册池的注册与实例化
- ``esp_gmf_info.h``\ ：音视频格式信息结构

处理单元基类与三类派生处理单元的接口见 :doc:`gmf-core-element` 的"API 参考"；外部接口的接口见 :doc:`gmf-core-data-path`。

.. include-build-file:: inc/esp_gmf_pipeline.inc

.. include-build-file:: inc/esp_gmf_task.inc

.. include-build-file:: inc/esp_gmf_job.inc

.. include-build-file:: inc/esp_gmf_pool.inc

.. include-build-file:: inc/esp_gmf_info.inc
