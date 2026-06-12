数据总线
================================================

:link_to_translation:`en:[English]`

数据总线（data bus）是数据端口（port）下层承载数据的队列实现。数据端口提供统一的 acquire-release 协议，数据总线负责缓冲区管理、跨线程同步，以及生产者与消费者之间的数据载体（payload）排队。GMF-Core 提供 ringbuffer、fifo、block、pbuf 四种实现，分别面向不同的拷贝策略、阻塞语义和数据粒度。处理单元（element）通过数据端口访问数据，不需要直接感知具体数据总线实现。

数据端口与 acquire-release 协议见 :doc:`gmf-core-data-path`，处理链（pipeline）与执行线程（task）的关系见 :doc:`gmf-core-pipeline`。

设计维度
-------------------------------------

选择数据总线实现时，主要看数据如何进入缓冲区、读写是否需要等待，以及每次传递的是字节流或整块数据。下表先按拷贝策略、阻塞语义和数据粒度对四种实现做对比，后文再分别说明各自的缓冲区来源与适用场景。

.. list-table::
   :widths: 24 26 26 24
   :header-rows: 1

   * - 维度
     - ringbuffer
     - fifo / block
     - pbuf
   * - 拷贝策略
     - 有 memcpy
     - 零拷贝（按地址传递）
     - 零拷贝（指针队列）
   * - 阻塞语义
     - 读写均阻塞
     - 读写均阻塞
     - 非阻塞
   * - 数据粒度
     - 任意字节
     - 整块
     - 整块

选型时需要先判断三件事：上下游速率差异有多大，数据是否能按整块边界传递，调用方是否允许在缓冲不可用时等待。

四种实现
-------------------------------------

ringbuffer
^^^^^^^^^^^^^^^^^^^^^^^^^^

ringbuffer 维护一个固定大小的循环缓冲区，每次按字节读写。\ ``acquire_read`` 会等待到有数据可读，\ ``acquire_write`` 会等待到有空间可写。数据写入和读出都会经过 ringbuffer 内部缓冲区，因此每次读写各有一次 memcpy。

.. code:: c

    esp_gmf_db_handle_t db;
    esp_gmf_db_new_ringbuffer(num, buf_size, &db);

适合上下游处理速率差异较大、需要缓冲解耦的场景。例如 MP3 解码器按帧输出 PCM，而 I2S 按固定节奏消费数据时，ringbuffer 可以吸收解码输出的突发性。

fifo
^^^^^^^^^^^^^^^^^^^^^^^^^^

fifo 是按地址传递的先进先出队列。\ ``acquire_write`` 返回一块空闲数据载体容器，处理单元写入后通过 ``release_write`` 入队；\ ``acquire_read`` 从队首取出已填充的数据载体供下游读取。数据在队列中按地址传递，不经过内部 memcpy。

.. code:: c

    esp_gmf_db_new_fifo(capacity, &db);

适合上下游速率相近、访问粒度一致、需要阻塞语义的场景。fifo 不支持"任意字节数读取"，每次 acquire 获取的是完整数据块。fifo 的数据载体可以来自外部提供的内存；需要指定写侧缓冲区对齐时，可在首次 ``acquire_write`` 前调用 :cpp:func:`esp_gmf_fifo_set_align` 设置对齐值。

block
^^^^^^^^^^^^^^^^^^^^^^^^^^

block 强调"一次一整块"的语义。创建时预分配 ``num`` 块 ``buf_size`` 字节的缓冲区；写侧通过 ``acquire_write`` 获取空闲块，填充后通过 ``release_write`` 提交给读侧。

.. code:: c

    esp_gmf_db_new_block(num, buf_size, &db);

适合整帧传递（如一帧视频）、对象大小固定、需要零拷贝的场景。与 fifo 的区别在缓冲区管理：fifo 的数据载体内存可由外部提供，block 的缓冲区由数据总线创建时预分配。

pbuf
^^^^^^^^^^^^^^^^^^^^^^^^^^

pbuf 是数据载体指针队列。它不分配缓冲区，只排队数据载体指针，\ ``acquire`` 和 ``release`` 都是非阻塞调用。

.. code:: c

    esp_gmf_db_new_pbuf(depth, &db);

适合数据已经位于固定内存区（例如 DMA 采集缓冲区）的场景。生产者把带有缓冲区地址的数据载体指针提交给 pbuf，下游随后获取同一数据载体指针。由于 pbuf 不阻塞，生产者必须自行保证队列不溢出。

block 与 pbuf 都按整块数据载体传递。如果下游处理单元需要按任意字节数读取，可以在处理单元内部使用字节缓存（cache）：先把 acquire 到的整块数据载体加载进 ``esp_gmf_cache``\ ，再通过 ``esp_gmf_cache_acquire`` 按固定长度或任意长度取出数据。这样既保留 block / pbuf 的零拷贝传递方式，又把处理单元内部的访问粒度转换为字节级读取。

选择指南
-------------------------------------

.. list-table::
   :widths: 28 16 24 12 20
   :header-rows: 1

   * - 需求特征
     - 推荐实现
     - 缓冲区来源
     - 拷贝
     - 阻塞
   * - 字节存取，速率差异大
     - ringbuffer
     - 数据总线内部循环缓冲区
     - 是
     - 是
   * - 任意大小写入、零拷贝
     - fifo
     - 外部提供的数据载体 / buffer
     - 否
     - 是
   * - 整帧数据、零拷贝
     - block
     - 数据总线内部固定块
     - 否
     - 是
   * - DMA 缓冲区直接传递
     - pbuf
     - 生产者提供的数据载体指针
     - 否
     - 否

选型顺序建议为：先确定是否能接受 memcpy，再确定读写是否需要阻塞，最后判断数据是否按固定块传递。需要"按任意字节数读"时通常选择 ringbuffer；如果上游使用 block 或 pbuf 传递整块数据，也可以在处理单元内部配合字节缓存完成字节级读取。

公共接口
-------------------------------------

数据总线的读写、复位、状态查询等操作由 ``esp_gmf_data_bus.h`` 提供统一接口。数据端口在创建时绑定其中一种数据总线，后续通过同一组 acquire-release 接口访问。

.. list-table::
   :widths: 32 68
   :header-rows: 1

   * - API
     - 作用
   * - ``esp_gmf_db_acquire_write``
     - 申请一块可写数据载体
   * - ``esp_gmf_db_release_write``
     - 提交写完的数据载体给读侧
   * - ``esp_gmf_db_acquire_read``
     - 申请一块可读数据载体
   * - ``esp_gmf_db_release_read``
     - 归还读完的数据载体给写侧
   * - ``esp_gmf_db_reset``
     - 清空缓冲、重置内部状态
   * - ``esp_gmf_db_get_total_size`` / ``esp_gmf_db_get_filled_size``
     - 查询容量与已填充字节数
   * - ``esp_gmf_db_get_available``
     - 查询当前可写空间

通常处理单元不直接调用这些 API，而是通过数据端口的 ``esp_gmf_port_acquire_*`` 间接访问。数据端口与 acquire-release 协议见 :doc:`gmf-core-data-path`。

部分实现还提供专用配置接口。例如 fifo 可在首次写侧 acquire 前调用 :cpp:func:`esp_gmf_fifo_set_align` 设置缓冲区地址对齐值，用于 DMA 或 PSRAM cache 对齐场景。

流结束与中止控制
-------------------------------------

除了正常的读写循环，数据总线还提供四个流控接口，用来表达 EOF 与中止两种语义。

.. list-table::
   :widths: 26 30 44
   :header-rows: 1

   * - API
     - 影响
     - 典型场景
   * - :cpp:func:`esp_gmf_db_done_write`
     - 置 EOF，让 ``acquire_read`` 返回带 ``is_done`` 的数据载体
     - 数据源读到末尾
   * - :cpp:func:`esp_gmf_db_reset_done_write`
     - 清除 EOF
     - 切换到新数据源、复用数据总线
   * - :cpp:func:`esp_gmf_db_abort`
     - 唤醒所有阻塞调用，返回 ``ESP_GMF_IO_ABORT``
     - 紧急停止、错误恢复
   * - :cpp:func:`esp_gmf_db_clear_abort`
     - 仅清 abort 标志，缓冲区数据保留
     - 错误恢复后继续读写

``done`` 与 ``abort`` 的语义差别在于：\ ``done`` 表示数据自然结束，下游处理完剩余数据后进入结束流程；\ ``abort`` 表示立即中止，后续读写调用返回 ``ESP_GMF_IO_ABORT``\ 。两者都让执行线程退出 running 阶段，分别进入 ``FINISHED`` 与 ``STOPPED`` 路径（详见 :doc:`gmf-core-pipeline` 的执行线程调度章节）。

``clear_abort`` 与 ``reset`` 的差别在于：\ :cpp:func:`esp_gmf_db_reset` 会清空缓冲数据并重置内部状态，\ ``clear_abort`` 只清除 abort 状态位。若恢复后仍要继续处理缓冲区中保留的数据，使用 ``clear_abort``；若需要丢弃旧数据并重新开始，使用 ``reset``。

API 参考
-------------------------------------

四种实现的统一接口与公共类型：

- ``esp_gmf_data_bus.h``\ ：数据总线公共接口（acquire / release / done / abort / reset）
- ``esp_gmf_new_databus.h``\ ：四种实现的创建接口

具体实现的头文件按需查阅 ``esp_gmf_ringbuffer.h`` / ``esp_gmf_fifo.h`` / ``esp_gmf_block.h`` / ``esp_gmf_pbuf.h``\ 。常规读写通过数据总线公共接口完成；只有使用实现专属配置接口时，才需要查看具体实现头文件。

.. include-build-file:: inc/esp_gmf_data_bus.inc

.. include-build-file:: inc/esp_gmf_new_databus.inc

.. include-build-file:: inc/esp_gmf_ringbuffer.inc

.. include-build-file:: inc/esp_gmf_fifo.inc

.. include-build-file:: inc/esp_gmf_block.inc

.. include-build-file:: inc/esp_gmf_pbuf.inc
