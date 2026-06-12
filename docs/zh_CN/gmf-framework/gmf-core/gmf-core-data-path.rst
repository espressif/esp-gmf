数据流转
================================================

:link_to_translation:`en:[English]`

本篇介绍数据在处理单元（element）之间的流转方式，涵盖数据载体（payload）的字段与所有权、数据端口（port）的 acquire-release 协议、外部接口（IO）在处理链首尾的衔接行为，以及字节缓存（cache）。

处理单元（element）的生命周期、端口属性、能力描述（capability）与运行时方法（method）见 :doc:`gmf-core-element`。处理链（pipeline）编排、执行线程（task）调度、生命周期与控制接口见 :doc:`gmf-core-pipeline`。数据总线（data bus）四种实现及其选型见 :doc:`gmf-core-databus`。整体架构与对象关系参考 :doc:`gmf-core-overview`。

数据载体、数据端口
------------------------------------------------

每个处理单元对外只暴露两个数据接口：输入数据端口与输出数据端口。数据在数据端口之间以 :cpp:type:`esp_gmf_payload_t` 的形式传递，处理单元不负责分配缓冲区，也不负责跨线程同步，只需按一套 acquire-release 协议读写数据。

一次 ``process`` 中数据的完整流动如下，以一条 ``Decoder → Resample`` 处理链为例。

.. only:: html

   .. mermaid::

      sequenceDiagram
          participant T as 执行线程
          participant E as resample
          participant IP as in_port
          participant OP as out_port
          participant D as 下游

          T->>+E: process()
          E->>+IP: acquire_in(size)
          IP-->>-E: 输入数据载体 in
          E->>+OP: acquire_out(size)
          OP-->>-E: 输出数据载体 out
          Note over E: 处理输入并生成输出
          E->>OP: release_out(out)
          OP->>D: 提交输出数据载体
          E->>IP: release_in(in)
          E-->>-T: ok

以上序列包含两条规则。第一，\ ``acquire_in`` 返回的数据载体在 ``release_in`` 之前仍归当前处理单元使用，端口会通过引用计数或下层实现维持这段数据的有效性；处理单元不需要自行处理跨线程同步。第二，\ ``release_out`` 提交输出数据载体。对块型数据端口，提交过程通常不拷贝数据，而是把数据载体交给下游输入端口或下层数据队列，一份数据可以穿越整条处理链而不产生中间拷贝。

acquire-release 是 GMF-Core 数据通路的基础协议，下面分别展开数据载体与数据端口的细节。

数据载体
-------------------------------------

字段
^^^^^^^^^^^^^^^^^^^^^^^^^^

:cpp:type:`esp_gmf_payload_t` 定义在 ``esp_gmf_payload.h``\ ，字段如下。

.. list-table::
   :widths: 22 18 60
   :header-rows: 1

   * - 字段
     - 类型
     - 含义
   * - ``buf``
     - ``uint8_t*``
     - 指向数据缓冲区
   * - ``buf_length``
     - ``size_t``
     - 缓冲区总长度
   * - ``valid_size``
     - ``size_t``
     - 缓冲区中有效数据的长度
   * - ``is_done``
     - ``bool``
     - 流结束标志，最后一块数据置 1，触发处理链进入 ``FINISHED``
   * - ``pts``
     - ``uint64_t``
     - 显示时间戳，用于音视频同步
   * - ``needs_free``
     - ``uint8_t``
     - 1 位位域，指示 ``buf`` 是否需要在数据载体销毁时自动释放
   * - ``meta_flag``
     - ``uint8_t``
     - 7 位位域，目前定义了 ``ESP_GMF_META_FLAG_AUD_RECOVERY_PLC`` 用于标记通过丢包补偿恢复的音频帧

生命周期与所有权
^^^^^^^^^^^^^^^^^^^^^^^^^^

处理单元代码中的数据载体表现为一个 :cpp:type:`esp_gmf_payload_t` \* 指针。所有权在 acquire-release 期间属于当前处理单元，归还后回到数据端口与数据总线管理。使用时遵循三条规则：

- **不要自行 free**\ ：acquire 得到的数据载体由数据端口管理，处理单元只能读写 ``buf`` 内容，不能调用 ``esp_gmf_payload_delete``\ 。
- **valid_size 由写方设置**\ ：申请到一块输出数据载体后，处理单元写入数据并把实际写入的字节数填到 ``valid_size``\ ，下游据此判断有多少有效数据。
- **acquire 与 release 成对**\ ：即使处理过程出错也必须 release，否则数据端口进入泄漏状态，后续调用会耗尽缓冲区。

处理单元自己分配数据载体的场景（较少见，典型是源头外部接口处理单元）可以用 :cpp:func:`esp_gmf_payload_new` 或 :cpp:func:`esp_gmf_payload_new_with_len` 显式创建，在不再引用时用 :cpp:func:`esp_gmf_payload_delete` 释放。需要满足 PSRAM cache 对齐时用 :cpp:func:`esp_gmf_payload_realloc_aligned_buf`\ 。

is_done 与 pts
^^^^^^^^^^^^^^^^^^^^^^^^^^

``is_done`` 是处理链从 ``RUNNING`` 自然过渡到 ``FINISHED`` 的信号。外部接口处理单元读到文件末尾时把 ``is_done`` 置 1 随数据载体下发，下游处理单元依次处理完带结束标记的数据载体后，逐级触发 close。

``pts`` 是显示时间戳，通常由源头处理单元填充。中间处理单元按需更新或直接透传。接近播放端的处理单元据此做音视频同步。

数据端口
-------------------------------------

方向与类型
^^^^^^^^^^^^^^^^^^^^^^^^^^

每个数据端口由两个属性定义。

**方向**\ ：\ ``ESP_GMF_PORT_DIR_IN`` 表示输入数据端口（从上游读取数据载体），\ ``ESP_GMF_PORT_DIR_OUT`` 表示输出数据端口（向下游写入数据载体）。每个处理单元有一个输入数据端口和一个输出数据端口。

**类型**\ ：字节型和块型两种，对应两种数据交换策略。

- ``ESP_GMF_PORT_TYPE_BYTE``\ （字节型）让处理单元按任意字节数读写，框架负责必要时的内部拷贝与拼接。适合解码器解析协议头这类需要精确定长访问的场景。
- ``ESP_GMF_PORT_TYPE_BLOCK``\ （块型）一次访问一整块，不做拷贝，只在数据端口之间传递缓冲区地址。适合处理整帧视频或批量 PCM，性能更高但可配置性较低。

acquire-release 协议
^^^^^^^^^^^^^^^^^^^^^^^^^^

数据端口对外暴露四个 API，成对使用形成 acquire-release 访问协议。以 ``acquire_in`` 为例说明完整签名：

.. code:: c

    esp_gmf_err_io_t esp_gmf_port_acquire_in(
        esp_gmf_port_handle_t   handle,
        esp_gmf_payload_t     **load,
        uint32_t                wanted_size,
        int                     wait_ticks);

四个参数的作用：

- ``handle`` 是输入数据端口句柄。
- ``load`` 是输出参数，调用返回后 ``*load`` 指向一块准备好供读取的数据载体。
- ``wanted_size`` 是处理单元期望获取的数据长度。对源头外部接口或绑定数据总线的数据端口，底层实现会按该值准备数据；对已经由上游处理单元提交到输入数据端口的数据载体，端口会直接返回当前数据载体，实际有效长度以 ``(*load)->valid_size`` 为准。
- ``wait_ticks`` 是等待超时时间，单位为系统 tick。\ ``ESP_GMF_MAX_DELAY`` 表示无限等待，\ ``0`` 表示立即返回。

返回值类型是 :cpp:type:`esp_gmf_err_io_t`\ ：

.. list-table::
   :widths: 28 14 58
   :header-rows: 1

   * - 返回码
     - 值
     - 含义
   * - ``ESP_GMF_IO_OK``
     - ``>= 0``
     - 成功，值表示实际读到的字节数
   * - ``ESP_GMF_IO_FAIL``
     - -1
     - 操作失败
   * - ``ESP_GMF_IO_TIMEOUT``
     - -2
     - 等待超时
   * - ``ESP_GMF_IO_ABORT``
     - -3
     - 被 ``esp_gmf_db_abort`` 或执行线程 stop 主动中止

处理单元代码处理返回值的模板：

.. code:: c

    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_err_io_t ret = esp_gmf_port_acquire_in(in_port, &in_load, 1024, ESP_GMF_MAX_DELAY);
    if (ret == ESP_GMF_IO_ABORT) {
        return ESP_GMF_JOB_ERR_ABORT;
    } else if (ret < 0) {
        return ESP_GMF_JOB_ERR_FAIL;
    }
    /* 使用 in_load->buf, in_load->valid_size */

四个 API 成对工作：

- 读入侧：\ ``acquire_in`` 获取可读数据载体，处理单元消费后 ``release_in`` 归还
- 写出侧：\ ``acquire_out`` 获取可写数据载体，处理单元填充后 ``release_out`` 提交

process 中的典型调用顺序是先 ``acquire_in`` 获取输入，再 ``acquire_out`` 准备输出缓冲区，处理完后先 ``release_out`` 提交下游，最后 ``release_in`` 归还输入。先申请 out 再处理，可以在下游阻塞时提前等待，避免处理完成后才发现输出端口不可用。

acquire 与 release 必须成对出现，即使处理过程出错也要 release。框架提供了几个辅助宏简化错误路径：

.. code:: c

    ESP_GMF_PORT_ACQUIRE_IN_CHECK(TAG, ret, err, goto ACQ_FAIL);
    ESP_GMF_PORT_RELEASE_IN_CHECK(TAG, ret, err, goto REL_FAIL);

这些宏把返回值检查和错误分支收敛到一行。

共享缓冲区管理
^^^^^^^^^^^^^^^^^^^^^^^^^^

GMF 通过两种机制减少内存占用，避免不必要的数据拷贝。

**引用计数**

旁路或原地处理的处理单元，可将同一份数据载体直接传递给下游处理单元或用户应用。每增加一个使用者，数据端口的 ``ref_count`` 加一；对应 ``release_in`` / ``release_out`` 完成后减一。计数归零时，端口调用下层 ``release`` 回调回收缓冲区。共享行为由数据端口的 ``is_shared`` 标志控制，可通过 :cpp:func:`esp_gmf_port_enable_payload_share` 开启或关闭（默认可共享）。

.. only:: html

   .. mermaid::

      flowchart LR
          Source --> Bypass
          Bypass --> User["用户"]
          Bypass --> Downstream["下游处理单元"]

上述流程中始终共享同一份数据，无需额外拷贝。

**缓冲区复用（AB Buffer）**

需要生成新输出数据的处理单元，框架通过 ``is_shared`` 在各处理单元之间复用数据载体缓冲区。以处理链 ``A → B → C`` 为例：

.. code:: text

    A → B → C

- A 输出 Buffer A。
- B 以 Buffer A 为输入，处理后写入 Buffer B。
- B 完成后，Buffer A 可被回收并再次分配。
- C 以 Buffer B 为输入，其输出可复用已释放的 Buffer A。

数据沿处理链推进时，Buffer A 与 Buffer B 交替复用。处理链包含多个处理单元时，通常只需两块工作缓冲区即可完成流转，降低内存消耗。

处理单元在初始化时会声明输入输出数据端口的约束，例如端口类型、对齐要求和每次 acquire 推荐的数据量。端口属性属于处理单元配置，完整说明见 :doc:`gmf-core-element`。

数据端口对应的数据队列由数据总线负责。GMF-Core 提供四种数据总线（ringbuffer / fifo / block / pbuf），数据端口在创建时绑定其中一种，处理单元代码看到的接口完全一致。四种实现的取舍与流控接口见 :doc:`gmf-core-databus`。

外部接口（IO）
-------------------------------------

外部接口（\ :cpp:type:`esp_gmf_io_t`\ ）是处理链首尾的特殊处理单元，承担文件、网络、编解码器等外部数据源的读写。除基础的 ``open`` / ``acquire`` / ``release`` / ``close`` 外，还支持异步模式：由 ``io_process`` 执行线程将数据写入内部数据总线，调用方从缓存读取，以平滑 HTTP 下载等速率波动；可通过 :cpp:type:`esp_gmf_io_speed_stats_t` 获取即时与平均传输速率（Kbps）。此外还提供数据源切换、中断恢复等高级控制接口。

同步与异步两种工作模式
^^^^^^^^^^^^^^^^^^^^^^^^^^

外部接口在创建时通过 :cpp:type:`esp_gmf_io_cfg_t` 配置工作模式。

**同步模式**\ ：\ ``thread.stack = 0`` 且不配置 ``buffer_cfg``\ 。\ ``esp_gmf_io_acquire_read`` 直接在调用线程上下文中执行底层外部接口操作（例如 ``fread``\ ）。优点是延迟低、内存占用小；缺点是调用方必须能容忍外部接口操作存在较长阻塞（例如网络读）。

**异步模式**\ ：\ ``thread.stack > 0`` 且配置 ``buffer_cfg.buffer_size``\ 。框架内部创建一个 ``io_process`` 执行线程，独立完成底层外部接口操作并把数据缓存到数据总线。用户调用 ``acquire_read`` 时实际从数据总线中读取，与底层外部接口解耦。这种模式适合"读固定字节数"或"生产消费速率差异大"的场景，例如解码器需要稳定的字节流而网络下载存在突发。

.. code:: c

    esp_gmf_io_cfg_t cfg = {
        .thread = {
            .stack = 4 * 1024,
            .prio  = 5,
            .core  = 0,
        },
        .buffer_cfg = {
            .io_size     = 4096,   /* 每次底层 IO 读取大小 */
            .buffer_size = 16 * 1024,
            .read_filter = NULL,
        },
        .enable_speed_monitor = true,
    };

URL 评分策略
^^^^^^^^^^^^^^^^^^^^^^^^^^

:cpp:type:`esp_gmf_io_t` 的 ``get_score`` 用于获取匹配度，便于从注册池中的多个外部接口里找到最匹配的外部接口。注册池函数 ``esp_gmf_pool_get_io_tag_by_url`` 遍历所有已注册外部接口的 ``get_score``\ ，返回评分最高者。三档评分定义：

.. list-table::
   :widths: 36 14 50
   :header-rows: 1

   * - 评分
     - 值
     - 含义
   * - ``ESP_GMF_IO_SCORE_NONE``
     - 0
     - 外部接口不支持该 URL
   * - ``ESP_GMF_IO_SCORE_STANDARD``
     - 50
     - 协议头匹配，例如 ``http://`` 由 HTTP 外部接口处理
   * - ``ESP_GMF_IO_SCORE_PERFECT``
     - 100
     - 协议加扩展名都匹配，或专用 IO 高优先级匹配

自定义外部接口实现 ``get_score`` 时可以返回任意中间值，便于在多个候选外部接口中区分优先级。

无缝衔接与重置
^^^^^^^^^^^^^^^^^^^^^^^^^^

:cpp:func:`esp_gmf_io_reload` 用一个新 URI 重新开启已有外部接口，不销毁底层连接。主要面向 HLS 切片这类"同主机连续下载多个分片"的场景，复用 HTTP 连接以避免反复握手的开销。重新加载内部对异步外部接口做了三件事：清 ``data_bus`` 的 EOF 标志、清 abort 标志、调用外部接口的 ``reload`` 回调重新打开新 URI。\ ``reload`` 必须在前一次读取完成后调用。

:cpp:func:`esp_gmf_io_reset` 做更全面的清理：把位置和文件大小清零、调用外部接口的 ``reset`` 回调、重置执行线程并重新加载外部接口 process job。常用于处理链 reset 之后对外部接口做配套清理。

done / abort 控制
^^^^^^^^^^^^^^^^^^^^^^^^^^

IO 层对 done 与 abort 的控制接口与数据总线一一对应，但在异步模式下还会额外管理 IO 执行线程的 hold 状态。

- :cpp:func:`esp_gmf_io_done` 标记 EOF：abort 异步外部接口的数据总线并把执行线程 hold 住，下游 ``acquire`` 会获取带 ``is_done`` 的数据载体。调用方在切换到下一个数据源前\ **必须**\ 调用 :cpp:func:`esp_gmf_io_clear_done` 释放被 hold 的执行线程，否则后续外部接口不会推进。
- :cpp:func:`esp_gmf_io_abort` 立即中止当前操作：数据总线 abort、执行线程 hold，所有 ``acquire`` / ``release`` 返回 ``ESP_GMF_IO_ABORT``\ 。配套的 :cpp:func:`esp_gmf_io_clear_abort` 恢复正常工作。

播放列表切换曲目的典型流程：

.. code:: c

    /* 当前曲目播放完毕 */
    esp_gmf_io_done(io_handle);

    /* 等待处理链处理完剩余数据载体 */
    ...

    /* 切换到下一首 */
    esp_gmf_io_set_uri(io_handle, next_uri);
    esp_gmf_io_clear_done(io_handle);
    /* 处理链继续运行，无需重建 */

自定义数据载体处理
^^^^^^^^^^^^^^^^^^^^^^^^^^

:cpp:type:`esp_gmf_io_buffer_cfg_t` 的 ``read_filter`` 回调允许在外部接口读到原始数据后、写入数据总线之前插入自定义处理。常见用途包括解密、字节序转换、协议解封装。回调签名：

.. code:: c

    esp_gmf_err_t (*read_filter)(esp_gmf_io_handle_t obj,
                                  void *payload,
                                  uint32_t wanted_size,
                                  int block_ticks);

读到的数据载体在回调中可以原地修改 ``buf`` 与 ``valid_size``\ 。

速度监控
^^^^^^^^^^^^^^^^^^^^^^^^^^

把 :cpp:type:`esp_gmf_io_cfg_t` 的 ``enable_speed_monitor`` 置 true 后，框架会维护当前与平均传输速度（Kbps），通过 :cpp:func:`esp_gmf_io_get_speed_stats` 取出 :cpp:type:`esp_gmf_io_speed_stats_t`\ 。运行中也可以用 :cpp:func:`esp_gmf_io_enable_speed_monitor` 动态开关。常用于网络状况监测与传输速率自适应。

字节缓存（cache）
-------------------------------------

块型数据端口一次 acquire 返回一整块数据，而解码器这类处理单元常常需要"从数据流中精确读 N 个字节"。\ ``esp_gmf_cache`` 模块提供字节级缓存能力：

.. code:: c

    esp_gmf_cache_handle_t cache;
    esp_gmf_cache_new(1024, &cache);

    /* 每次 acquire 到一块 block 数据载体后，提交给 cache */
    esp_gmf_cache_load(cache, in_load->buf, in_load->valid_size);

    /* 然后按任意字节数读 */
    int read;
    esp_gmf_cache_acquire(cache, 7, out_buf, &read);
    esp_gmf_cache_release(cache, 7);

字节缓存内部维护一个可扩展的滑动缓冲区。处理单元可以连续把多块数据载体中的字节加载到字节缓存中，再按需要读取任意长度的数据；即使一次读取跨越两块数据载体的边界，字节缓存也会从内部缓冲区中连续返回。典型使用场景是音频解码器解析协议头或帧头。

API 参考
-------------------------------------

本篇涉及的头文件：

- ``esp_gmf_payload.h``\ ：数据载体结构与生命周期
- ``esp_gmf_port.h``\ ：数据端口的配置与访问 API
- ``esp_gmf_cache.h``\ ：字节缓存
- ``esp_gmf_io.h``\ ：外部接口对象的公共接口

.. include-build-file:: inc/esp_gmf_payload.inc

.. include-build-file:: inc/esp_gmf_port.inc

.. include-build-file:: inc/esp_gmf_cache.inc

.. include-build-file:: inc/esp_gmf_io.inc
