GMF-Core 概览
================================================

:link_to_translation:`en:[English]`

GMF-Core 是 ESP-GMF 的核心框架，定义了处理单元（element）、数据载体（payload）、数据端口（port）、数据总线（data bus）、执行线程（task）以及生命周期状态机。解码器、编码器、重采样器、混音器等多媒体算法均以处理单元的形式接入框架。

本篇介绍整体架构与主要对象；详细内容见各专题章节：:doc:`gmf-core-pipeline` 介绍处理链（pipeline）与执行线程（task）的协作与调度；:doc:`gmf-core-element` 介绍处理单元（element）的生命周期、能力与方法；:doc:`gmf-core-data-path` 介绍数据载体、数据端口与外部接口（IO）的数据流转；:doc:`gmf-core-databus` 展开数据总线实现与流控；:doc:`gmf-core-fourcc` 与 :doc:`gmf-core-utils` 可按需查阅。

上层应用可通过 ``esp_audio_simple_player``\ 、\ ``esp_capture`` 等封装组件间接使用 GMF-Core；开发自定义处理单元时，才需要直接面向 GMF-Core 编程。

组件分层
--------------------------

GMF-Core 把多媒体处理过程划分为四个层次，每一层解决一个独立的问题。

.. list-table::
   :widths: 12 24 64
   :header-rows: 1

   * - 层
     - 模块
     - 职责
   * - 注册层
     - 注册池（pool）
     - 集中注册处理单元和外部接口模板。应用启动时一次性登记所有可用处理单元，构建处理链时按名称复制实例
   * - 编排层
     - 处理链（pipeline）、执行线程（task）
     - 处理链按顺序串接多个处理单元；执行线程按既定规则驱动链上的处理单元运行
   * - 处理层
     - 处理单元（element）
     - 每个处理单元实现 ``open`` 初始化、\ ``process`` 处理一块数据、\ ``close`` 清理三个阶段。派生出音频、视频、图片三类子类
   * - 数据层
     - 数据载体（payload）、数据端口（port）、数据总线（data bus）
     - 数据以数据载体形式在处理单元之间传递，数据端口为处理单元提供读写接口，数据总线在数据端口下层负责排队、缓冲与同步

.. only:: html

   .. mermaid::

      flowchart TB
          subgraph registry ["注册层"]
              Pool["注册池"]
          end

          subgraph orchestrate ["编排层"]
              Pipeline["处理链"]
              Task["执行线程"]
              Pipeline -->|"绑定"| Task
          end

          subgraph process ["处理层"]
              ElemA["处理单元 A"] -->|"数据载体"| ElemB["处理单元 B"]
          end

          subgraph data ["数据层"]
              direction LR
              Port["数据端口"] --> DataBus["数据总线"] --> Payload["数据载体"]
          end

          Pool -->|"实例化"| Pipeline
          Pipeline -->|"链接"| ElemA
          Task -->|"驱动"| ElemA
          ElemA -->|"读写"| Port

这种分层让各部分可以独立扩展。新增音频算法时只需实现处理单元接口；构建新的播放流程时，从注册池中选择处理单元并组合成处理链；调整数据缓冲策略时，替换对应的数据总线即可。三类工作彼此解耦，可分别进行开发与调整。

基础对象模型
--------------------------

框架内所有可以被注册、复制、销毁的对象都继承自同一个基类 :cpp:type:`esp_gmf_obj_t`\ 。基类不负责业务逻辑，只定义了对象如何被创建（\ ``new_obj`` 函数指针）、如何被销毁（\ ``del_obj`` 函数指针），以及一个字符串标签 ``tag`` 和配置数据指针 ``cfg``\ 。

直接派生类有三个：\ ``esp_gmf_io_t`` 表示外部接口对象（如文件外部接口、I2S 外部接口），\ ``esp_gmf_task_t`` 表示执行线程，\ ``esp_gmf_element_t`` 表示处理单元。处理单元又进一步派生出 audio、video、picture 三个子类，分别携带对应类别的格式信息。

.. only:: html

   .. mermaid::

      classDiagram
          direction TB

          class esp_gmf_obj_t
          class esp_gmf_io_t
          class esp_gmf_task_t
          class esp_gmf_element_t
          class esp_gmf_audio_element_t
          class esp_gmf_video_element_t
          class esp_gmf_pic_element_t

          esp_gmf_obj_t <|-- esp_gmf_io_t
          esp_gmf_obj_t <|-- esp_gmf_task_t
          esp_gmf_obj_t <|-- esp_gmf_element_t
          esp_gmf_element_t <|-- esp_gmf_audio_element_t
          esp_gmf_element_t <|-- esp_gmf_video_element_t
          esp_gmf_element_t <|-- esp_gmf_pic_element_t

统一继承让注册池只需维护一条 :cpp:type:`esp_gmf_obj_t` \* 链表，就能存放任意类型的模板。使用时通过 :cpp:func:`esp_gmf_obj_dupl` 按模板复制出全新实例。框架中"先注册模板、后按需实例化"的使用模式依赖于这套统一的对象接口实现。

主要对象一览
--------------------------

处理链（pipeline）
^^^^^^^^^^^^^^^^^^^^^^^^^^

处理链是任务编排者，把若干个处理单元按顺序连接起来，作为一个整体对外提供 ``run`` / ``stop`` / ``pause`` / ``resume`` / ``reset`` / ``seek`` 等控制接口。对应用代码而言，只需操作处理链这一个对象，就能启动、暂停、停止整条处理流程。

处理链还负责三类内部协调工作：打开与关闭首尾的外部接口（文件、I2S 等）、把处理单元上报的格式信息转发到下游处理单元、在出错或结束时协调所有处理单元完成清理。处理链本身不执行处理单元的代码，具体执行由绑定的执行线程完成。搭建与控制方式见 :doc:`gmf-core-pipeline`。

执行线程（task）
^^^^^^^^^^^^^^^^^^^^^^^^^^

执行线程是处理流程的执行者，本质是一个由框架封装的运行单元。执行线程维护一个 job 链表，按既定顺序调用处理单元的 ``open`` / ``process`` / ``close``\ 。每条处理链在运行时绑定一个执行线程，两者一一对应。

处理链和执行线程划分为两个对象，分别承载不同职责。处理链描述"连接关系"，执行线程描述"运行时资源"（栈大小、优先级、在哪个 CPU 核心）。同一条处理链可以绑定不同配置的执行线程，同一个执行线程模板也可以被反复使用。调度模型见 :doc:`gmf-core-pipeline`。

处理单元（element）
^^^^^^^^^^^^^^^^^^^^^^^^^^

处理单元是具体工作的执行者，一个处理单元对应一种具体的工作（如 MP3 解码、重采样、混音）。每个处理单元实现三个固定阶段的功能函数：\ ``open`` 做一次性初始化，\ ``process`` 处理一块数据，\ ``close`` 释放资源。执行线程先调用 open，运行期间按 job 配置反复调用 process（常见为 ``ESP_GMF_JOB_TIMES_INFINITE``\ ），直到处理单元返回结束、错误或收到停止请求，最后调用 close。

按处理的数据类别，处理单元派生出音频（\ ``esp_gmf_audio_element_t``\ ）、视频（\ ``esp_gmf_video_element_t``\ ）、图片（\ ``esp_gmf_pic_element_t``\ ）三类子类，分别携带采样率/声道、分辨率/帧率、宽/高这些格式信息。生命周期细节、接口实现与自定义处理单元模板见 :doc:`gmf-core-element`。

数据载体（payload）
^^^^^^^^^^^^^^^^^^^^^^^^^^

数据载体是跨处理单元流动数据的统一容器。无论是一帧解码后的 PCM、一段压缩的 H.264，还是一个 HTTP 分片，在框架内部都以 ``esp_gmf_payload_t`` 承载。这个结构包含数据缓冲区指针、缓冲区总长度、有效数据长度、流结束标志、时间戳等字段。

统一的数据载体让框架可以在不依赖具体媒体类型的前提下完成数据传输、缓冲与线程同步，避免每个处理单元自定义数据结构。数据载体的字段、生命周期和所有权规则见 :doc:`gmf-core-data-path`。

数据端口（port）
^^^^^^^^^^^^^^^^^^^^^^^^^^

数据端口是处理单元对外暴露的数据读写接口。每个处理单元有一个输入数据端口和一个输出数据端口，分别连接上游和下游。数据端口本身不存储数据，它只提供"申请一块数据载体来读写"的接口，真实数据存放在下层的数据总线中。

数据端口采用 acquire-release（申请-释放）的访问协议：处理单元需要读数据时先 acquire 一块数据载体，读完后 release 归还；写数据时先 acquire 一块空数据载体，填满后 release 提交给下游。这种模式划定了数据的所有权边界，让处理单元在两次调用之间可以安全地读写数据载体，无需考虑线程同步。详细协议见 :doc:`gmf-core-data-path`。

数据总线（data bus）
^^^^^^^^^^^^^^^^^^^^^^^^^^

数据总线是数据端口下层的真实数据队列，负责分配缓冲区、处理跨线程同步、在生产者与消费者之间排队数据载体。GMF-Core 提供四种实现，分别针对不同的拷贝策略、阻塞语义与数据粒度。数据端口在创建时绑定其中一种，底层实现差异由框架封装，处理单元代码仅使用统一的数据端口接口。

四种实现的取舍、选择指南与流控接口见 :doc:`gmf-core-databus`。

注册池（pool）
^^^^^^^^^^^^^^^^^^^^^^^^^^

注册池是处理单元与外部接口的模板库。应用启动时把所有支持的处理单元（MP3 解码器、重采样器、文件外部接口、I2S 外部接口等）一次性注册到注册池；构建处理链时，应用只需给出名称列表，注册池就会按模板复制实例并按顺序串接。

"注册模板"和"使用实例"分开后，同一批处理单元可以组合出多种不同的处理链。系统里可以同时存在几十种处理链组合，借助注册池只需登记一次模板，而不是为每种组合编写一套构建流程。除了按名称查找，注册池还能按 URL 评分自动选择匹配的外部接口（例如 ``http://`` 匹配 HTTP 外部接口、\ ``file://`` 匹配文件外部接口），便于上层应用按需切换数据源。使用方式见 :doc:`gmf-core-pipeline`。

生命周期状态
--------------------------

处理链和执行线程共用一套生命周期状态枚举 :cpp:type:`esp_gmf_event_state_t`\ 。应用通过注册事件回调感知每一次状态迁移。

.. list-table::
   :widths: 32 10 58
   :header-rows: 1

   * - 状态
     - 值
     - 含义
   * - ``ESP_GMF_EVENT_STATE_NONE``
     - 0
     - 对象刚创建，尚未初始化
   * - ``ESP_GMF_EVENT_STATE_INITIALIZED``
     - 1
     - 已完成 init，具备启动条件
   * - ``ESP_GMF_EVENT_STATE_OPENING``
     - 2
     - 正在执行处理单元的 open 阶段
   * - ``ESP_GMF_EVENT_STATE_RUNNING``
     - 3
     - 正在循环调度 process 阶段
   * - ``ESP_GMF_EVENT_STATE_PAUSED``
     - 4
     - 已暂停，上下文保留
   * - ``ESP_GMF_EVENT_STATE_STOPPED``
     - 5
     - 用户主动停止，正在执行 close
   * - ``ESP_GMF_EVENT_STATE_FINISHED``
     - 6
     - 数据自然结束（数据载体的 ``is_done`` 触发）
   * - ``ESP_GMF_EVENT_STATE_ERROR``
     - 7
     - 任一 job 返回失败，已进入 cleanup

.. only:: html

   .. mermaid::

      stateDiagram-v2
          direction TB

          [*] --> NONE
          NONE --> INITIALIZED : init
          INITIALIZED --> OPENING : run
          OPENING --> RUNNING : opens done
          OPENING --> ERROR : open fail
          RUNNING --> PAUSED : pause
          PAUSED --> RUNNING : resume
          RUNNING --> STOPPED : stop
          RUNNING --> FINISHED : is_done
          RUNNING --> ERROR : proc fail
          STOPPED --> INITIALIZED : reset
          FINISHED --> INITIALIZED : reset
          ERROR --> INITIALIZED : reset

三个终止状态 ``STOPPED`` / ``FINISHED`` / ``ERROR`` 都可以通过 ``reset`` 回到 ``INITIALIZED``\ ，意味着同一条处理链可以被反复 run 和 stop，不需要重建。

枚举中没有独立的"abort 状态"。处理单元的 ``process`` 返回 ``ESP_GMF_JOB_ERR_ABORT`` 时，执行线程默认按 ``STOPPED`` 路径完成清理；如果应用通过 ``esp_gmf_task_set_strategy_func`` 注册了策略函数，则可以把 abort 改派到 ``RESET`` 路径让处理链自动回到 ``INITIALIZED``\ 。abort 流程的详细行为见 :doc:`gmf-core-pipeline`。

事件类型
--------------------------

事件用 :cpp:type:`esp_gmf_event_pkt_t` 承载，按类型分三类：

- **状态变化事件 (CHANGE_STATE)**\ ：由执行线程上报，告知应用处理链当前处于哪个生命周期阶段。用户代码可通过这个事件更新 UI 或触发下一步操作。
- **格式信息事件 (REPORT_INFO)**\ ：由处理单元上报，告知框架自己解析到的格式（例如 MP3 解码器读完文件头后上报采样率和声道数）。处理链收到该事件后，把格式信息写入下游依赖型处理单元，并注册该处理单元的 open/process job。
- **加载 job 事件 (LOADING_JOB)**\ ：处理单元需要重新注册 job 时发出该事件，处理链收到后更新执行线程的 job 链表。用户代码不直接处理该事件。

事件在处理链内部形成双向流动：执行线程把状态变化上报给处理链，处理链转发给用户回调；处理单元把格式信息上报给处理链，处理链再写入下游依赖型处理单元并注册对应 job。

典型应用拓扑
--------------------------

下面几个拓扑展示处理链的组合能力。简单场景仅需一条处理链；复杂场景通过多条处理链级联分担任务。

单路解码播放
^^^^^^^^^^^^^^^^^^^^^^^^^^

文件外部接口作为处理链的 IN，I2S 外部接口作为 OUT，中间串接解码器与重采样器，整条链由一个执行线程驱动。本地音乐播放类应用可采用这种结构。

.. only:: html

   .. mermaid::

      flowchart LR
          File(("File")) --> Dec["Decoder"]
          Dec --> Rs["Resample"]
          Rs --> I2S(("I2S"))

回调驱动，不连接外部接口
^^^^^^^^^^^^^^^^^^^^^^^^^^

IN / OUT 不连接外部接口对象，改由用户通过 ``esp_gmf_port_set_cb`` 注册回调函数提供或获取数据。适合处理链对接蓝牙音频、HTTP 流、自定义协议栈等外部数据源，外部层只需在回调中完成数据输入与输出。

.. only:: html

   .. mermaid::

      flowchart LR
          InCb(("输入<br/>回调")) --> Dec["Decoder"]
          Dec --> Rs["Resample"]
          Rs --> OutCb(("输出<br/>回调"))

通过 ringbuffer 桥接两段处理链
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

当上下游处理速率差异较大时，将流程划分为两段独立处理链，用 ringbuffer 作中间缓冲。两段各拥有一个执行线程，可以配置不同的栈大小和优先级：前段占用较大栈做 MP3 解码，后段用较小栈处理输出调度。

.. only:: html

   .. mermaid::

      flowchart LR
          subgraph p1 ["处理链 1"]
              File(("文件"))
              Dec["MP3<br/>解码器"]
          end
          subgraph p2 ["处理链 2"]
              Rs["重采样器"]
              Out(("输出"))
          end
          File --> Dec
          Dec -->|"ringbuffer"| Rs
          Rs --> Out

多路输入混音
^^^^^^^^^^^^^^^^^^^^^^^^^^

两条上游处理链分别解码不同音源，汇入一条混音处理链叠加后输出。典型应用是导航语音叠加到背景音乐：主处理链持续播放音乐，当导航触发时启动提示音处理链，两路 PCM 送到 mixer 混合后写入 I2S。停止提示音时只需 stop 对应的处理链，主播放不受影响。

.. only:: html

   .. mermaid::

      flowchart LR
          subgraph p1 ["背景音乐"]
              F1(("File"))
              D1["MP3"]
          end
          subgraph p2 ["提示音"]
              H1(("HTTP"))
              D2["MP3"]
          end
          subgraph pm ["混音输出"]
              Mix["Mixer"]
              Out(("I2S"))
          end
          F1 --> D1
          D1 --> Mix
          H1 --> D2
          D2 --> Mix
          Mix --> Out

一分多分流
^^^^^^^^^^^^^^^^^^^^^^^^^^

用 copier 处理单元把一路输入复制成多路输出，例如播放音乐的同时把同一份音频送给 AEC 算法作为回声消除的参考信号：扬声器放出的音乐会被麦克风重新采集，AEC 需要原始音乐作为参考才能从麦克风信号中消除回声。

.. only:: html

   .. mermaid::

      flowchart LR
          subgraph p1 ["解码"]
              F(("File"))
              Dec["Decoder"]
              Cp{"Copier"}
          end
          subgraph p2 ["播放"]
              R1["Resample"]
              I2S(("I2S"))
          end
          subgraph p3 ["AEC 参考"]
              R2["Resample"]
              Rec(("Recorder"))
          end
          F --> Dec
          Dec --> Cp
          Cp --> R1
          R1 --> I2S
          Cp --> R2
          R2 --> Rec

组件目录
--------------------------

.. code:: text

    gmf_core/
    ├── include/          对外头文件
    ├── src/              基础类型与框架实现
    ├── data_bus/         四种 data bus 实现
    ├── oal/              操作系统抽象层
    ├── helpers/          URI 解析、FourCC 校验工具
    ├── docs/             Doxygen 配置
    └── test_apps/        单元测试与集成用例

GMF-Core 构建在 ESP-IDF 之上，依赖事件组、原子操作、\ ``esp_common``\ 、\ ``log`` 等系统能力。本身不依赖任何音视频算法库：所有解码器、编码器、滤波器都以处理单元的形式由上层组件（如 ``gmf_audio``\ 、\ ``gmf_video``\ ）提供。

API 参考
--------------------------

本篇涉及的基础类型与对象层次：

- ``esp_gmf_obj.h``\ ：对象基类
- ``esp_gmf_event.h``\ ：状态枚举与事件包
- ``esp_gmf_err.h``\ ：错误码定义
- ``esp_gmf_info_file.h``\ ：文件信息结构

FourCC 宏定义详见 :doc:`gmf-core-fourcc`，本篇不再重复包含。

.. include-build-file:: inc/esp_gmf_obj.inc

.. include-build-file:: inc/esp_gmf_event.inc

.. include-build-file:: inc/esp_gmf_err.inc

.. include-build-file:: inc/esp_gmf_info_file.inc
