GMF-IO
==============

:link_to_translation:`en:[English]`

gmf_io 是 ESP-GMF 的输入输出组件，提供文件、网络、嵌入式 flash、I2S PDM、音频 codec 等常见的外部接口（IO）的具体实现，统一通过 ``esp_gmf_io_t`` 基类接入框架。所有子类共享同一套生命周期、acquire-release 协议、URL 评分、异步缓冲、done / abort / reload 控制等行为；本篇聚焦各个子类的差异、配置项和使用要点。

外部接口基类（\ ``esp_gmf_io_t`` 结构与 ``esp_gmf_io_*`` 系列 API）定义在 gmf-core，文档见 :doc:`../gmf-core/gmf-core-data-path`。

功能清单
-------------------------------------

- file io：基于 POSIX ``fopen`` / ``fread`` / ``fwrite`` 读写文件系统，支持 ``/sdcard`` 绝对路径和 ``file://`` 方案，可选 DMA 兼容缓存加速大块读写
- http io：基于 ``esp_http_client`` 读写 HTTP / HTTPS 资源，支持 Range 请求、gzip 解压、事件 hook、服务器证书、\ ``crt_bundle``
- embed_flash io：从只读的嵌入式 flash 资源表读数据，URL 形式 ``embed://group/<index>_name.ext``\ ，适合固件内置提示音
- i2s_pdm io：包装 ESP-IDF 的 ``i2s_pdm`` 驱动通道，用于 PDM 麦克风采集或 PDM 扬声器播放
- codec_dev io：直接对接 ``esp_codec_dev`` 设备句柄读写音频样本，典型用于板载 codec 的录音 / 放音
- URL 评分：每个子类实现 ``get_score``\ ，注册池（pool）可以按 URL 给候选外部接口打分并自动选择
- 同步 / 异步两种工作模式：配置了 buffer 与执行线程时，框架自动派生一条 io_process 执行线程解耦生产消费；否则所有 acquire / release 在调用方线程同步执行
- 速度监控：基类在同步与异步模式下均可开启，提供即时与平均传输速率（Kbps）
- 复用与切换：\ ``esp_gmf_io_reload`` 在不重建实例的前提下切换 URI，常用于 HLS 切片与播放列表
- 流结束与中止：统一的 ``done`` / ``abort`` 语义，会对内部数据总线与执行线程做对应处理

技术拆解
-------------------------------------

外部接口在 GMF 中的位置
^^^^^^^^^^^^^^^^^^^^^^^^^^

gmf_io 的每个子类位于一条三层继承链的末端：\ ``esp_gmf_obj_t`` 提供 tag、cfg、new、del 等通用对象能力；\ ``esp_gmf_io_t`` 在此基础上定义外部接口的公共行为（open / seek / acquire / release / reset / reload / done / abort / 速度统计）；\ ``elements/gmf_io`` 下的五个子类只需实现回调并填写子类配置。处理链（pipeline）通过 ``esp_gmf_port`` 将外部接口的数据总线接入相邻处理单元（element），相邻单元经由统一的数据端口读写。

.. only:: html

   .. mermaid::

      flowchart TB
          Obj["esp_gmf_obj_t"]
          IoBase["外部接口基类 (esp_gmf_io_t)"]
          IoImpl["五类外部接口子类 (file/http/embed_flash/i2s_pdm/codec_dev)"]
          Port["数据端口 (esp_gmf_port)"]

          Obj --> IoBase
          IoBase --> IoImpl
          IoBase -. 数据总线 .-> Port

实际开发只需面对子类提供的 ``xxx_io_cfg_t`` 与 ``esp_gmf_io_xxx_init``\ ，运行期所有控制都使用基类 ``esp_gmf_io_*`` 系列 API。扩展自定义外部接口时，新增一个继承自 ``esp_gmf_io_t`` 的子类并填好函数指针表即可。

同步与异步两种工作模式
^^^^^^^^^^^^^^^^^^^^^^^^^^

基类有两种运行姿态，子类通过 ``esp_gmf_io_cfg_t.thread.stack`` 与 ``esp_gmf_io_cfg_t.buffer_cfg.buffer_size`` 选择。

**同步模式**\ ：\ ``thread.stack == 0`` 且没有配置数据总线。\ ``acquire_read`` / ``release_read`` / ``acquire_write`` / ``release_write`` 在调用方线程直接执行子类回调。RAM 开销小，适合 codec_dev、embed_flash、i2s_pdm 这类读写本身属于阻塞 DMA 或零拷贝访存的设备。

**异步模式**\ ：\ ``thread.stack > 0`` 且 ``buffer_size > 0``\ 。基类在 ``esp_gmf_io_open`` 阶段创建一条 ``io_process`` 执行线程与一个 block 类型数据总线，执行线程内部循环：读方向从设备 ``acquire_read`` 读取数据并 ``db_release_write`` 提交到数据总线；写方向从数据总线 ``db_acquire_read`` 获取数据并 ``release_write`` 下发到设备。应用侧 ``esp_gmf_io_acquire_*`` 实际操作数据总线，与设备 IO 解耦。适合 http、抖动较大的 file 等"速率不稳、需要预取"的场景。

下图以读取方向对比两种模式。

.. only:: html

   .. mermaid::

      flowchart TD
          subgraph sync ["同步模式"]
              User_A["调用者线程"]
              DevA["设备 acquire_read"]
              User_A -->|"直接调用"| DevA
          end
          subgraph async ["异步模式"]
              User_B["调用者线程"]
              DbB["block 数据总线"]
              TaskB["io_process 执行线程"]
              DevB["设备 acquire_read"]
              User_B -->|"db_acquire_read"| DbB
              TaskB -->|"读设备"| DevB
              TaskB -->|"db_release_write"| DbB
          end

io_http 默认即为异步，其余子类默认同步，应用可按需修改配置。

URL 评分与自动选择
^^^^^^^^^^^^^^^^^^^^^^^^^^

注册池支持同时注册多个外部接口，通过 ``esp_gmf_io_get_score`` 接口判断"哪个外部接口能处理这个 URL"。评分有三档：

- ``ESP_GMF_IO_SCORE_NONE``\ （0）：不支持
- ``ESP_GMF_IO_SCORE_STANDARD``\ （50）：匹配 scheme（\ ``http://``\ 、\ ``file://``\ 、\ ``embed://``\ ）
- ``ESP_GMF_IO_SCORE_PERFECT``\ （100）：精准匹配（scheme + 扩展名 / 专用外部接口）

五个子类的内置评分规则：

.. list-table::
   :widths: 24 76
   :header-rows: 1

   * - 子类
     - 匹配条件
   * - io_file
     - URL 以 ``/`` 开头，或以 ``file://`` 开头（推荐写作 ``file:///sdcard/xxx``\ ）→ ``STANDARD``
   * - io_http
     - URL 以 ``http://`` 或 ``https://`` 开头 → ``STANDARD``
   * - io_embed_flash
     - URL 以 ``embed://`` 开头 → ``STANDARD``
   * - io_i2s_pdm / io_codec_dev
     - 无 scheme 维度的匹配，通过注册池的 tag 直接选中（\ ``io_codec_dev``\ 、\ ``io_i2s_pdm``\ ）

自定义外部接口想抢占默认实现（例如 HLS 之于 HTTP）时，返回 ``PERFECT`` 即可。

io_file
^^^^^^^^^^^^^^^^^^^^^^^^^^

基于 POSIX stdio。:cpp:func:`esp_gmf_io_file_init` 按 :cpp:type:`file_io_cfg_t` 分配实例；open 时通过 ``esp_gmf_io_get_uri`` 获取 URI，支持 ``/sdcard/xxx`` 绝对路径和 ``file:///sdcard/xxx`` 文件 URI。读方向用 ``"rb"``\ ，写方向用 ``"wb"``\ ；读方向会把 ``stat`` 得到的文件大小写回基类，用于进度上报与 seek 合法性校验。

**断点续传**\ 。open 阶段若基类记录的 ``info.pos`` 非零，子类会调用 ``fseek(SEEK_SET)`` 跳到该位置。配合处理链的 ``seek`` / 应用层 ``esp_gmf_io_set_pos``\ ，可以实现"从指定字节开始播放"。

**缓存配置**\ 。\ ``cache_size`` 控制 ``setvbuf`` 分配的用户缓冲区。值 ``<= 512`` 视为禁用；大于该阈值时按 512 字节向上对齐后分配。\ ``cache_caps`` 选择缓存的 heap 标签，零值退化为 ``MALLOC_CAP_DMA``\ 。在支持 PSRAM DMA 的芯片上（例如 esp32p4），建议改为 ``MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA`` 节省内部 SRAM。

**seek 与 reset**\ 。\ ``seek`` 直接调用 ``fseek(SEEK_SET)``\ ，\ ``reset`` 把游标置 0。两者在同步模式下立刻生效；异步模式下由基类转成执行线程层延迟处理。

.. code:: c

    file_io_cfg_t cfg = FILE_IO_CFG_DEFAULT();
    cfg.dir        = ESP_GMF_IO_DIR_READER;
    cfg.cache_size = 8 * 1024;
    cfg.cache_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA;

    esp_gmf_io_handle_t io = NULL;
    esp_gmf_io_file_init(&cfg, &io);
    esp_gmf_io_set_uri(io, "/sdcard/test.mp3");

io_http
^^^^^^^^^^^^^^^^^^^^^^^^^^

基于 ``esp_http_client``\ 。默认异步：栈 6 KiB、优先级 10、core 0、数据总线 20 KiB、每次 IO 块 3 KiB，可通过 ``http_io_cfg_t.io_cfg`` 覆盖。

**请求构造**\ 。open 阶段建立 client、设置 ``timeout_ms = 30000``\ 、可选 ``cert_pem`` 或 ``crt_bundle_attach``\ 。若基类已经记录了非零 ``info.pos``\ （表示断点续传），open 自动添加 ``Range: bytes=<pos>-`` 头。收到 301 / 302 会自动重定向并重新握手。

**gzip 自动解压**\ 。若响应头 ``Content-Encoding: gzip``\ ，io_http 内部用 ``gzip_miniz`` 边读边解压；应用获取的是解压后的字节流。其他 ``Content-Encoding`` 值视为不支持并返回失败。

**事件 hook**\ 。应用通过 ``http_io_cfg_t.event_handle`` 或 :cpp:func:`esp_gmf_io_http_set_event_callback` 注册回调，在连接前、请求过程中、响应过程中、请求结束、track 切换等节点被调用。回调返回正值可以让 io_http 跳过默认的读写动作，适合实现自定义认证、数据改写、多段上传等场景。事件枚举见 ``http_stream_event_id_t``\ 。

**reset 与 reload**\ 。:cpp:func:`esp_gmf_io_http_reset` 复位 HTTP 状态，常用于事件回调里"当前资源读完，切到下一段"。基类的 ``esp_gmf_io_reload`` 在设置新 URI 后复用已有连接降低握手开销，适合同一 host 的顺序切换。

.. code:: c

    http_io_cfg_t cfg = HTTP_STREAM_CFG_DEFAULT();
    cfg.dir               = ESP_GMF_IO_DIR_READER;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.event_handle      = my_http_hook;
    cfg.user_data         = &app_ctx;

    esp_gmf_io_handle_t io = NULL;
    esp_gmf_io_http_init(&cfg, &io);
    esp_gmf_io_set_uri(io, "https://example.com/track.mp3");

io_embed_flash
^^^^^^^^^^^^^^^^^^^^^^^^^^

读取链接进固件的只读资源，只支持读方向。典型用途是固件内置提示音、按键反馈、开机声。

**资源表**\ 。上下文是一个 ``embed_item_info_t`` 数组，每项包含 ``address`` 与 ``size`` 两个字段。通过 :cpp:func:`esp_gmf_io_embed_flash_set_context` 设置；\ ``max_files`` 给上限保护，可以比实际条目数大。

**URL 形式**\ 。\ ``embed://<group>/<index>_<name>.<ext>``\ 。open 时解析 URL 里 ``_`` 之前的最后一段数字作为 index，在资源表中查 ``address`` 与 ``size``\ 。读出的数据是 raw 字节；若为 mp3 / wav 等编码，需配合解码处理单元。

.. code:: c

    extern const embed_item_info_t g_my_tones[];

    embed_flash_io_cfg_t cfg = EMBED_FLASH_CFG_DEFAULT();
    cfg.max_files = ESP_MY_TONE_URL_MAX;

    esp_gmf_io_handle_t io = NULL;
    esp_gmf_io_embed_flash_init(&cfg, &io);
    esp_gmf_io_embed_flash_set_context(io, g_my_tones, ESP_MY_TONE_URL_MAX);
    esp_gmf_io_set_uri(io, "embed://tone/0_startup.mp3");

仓库提供 ``mk_flash_embed_tone.py`` 辅助脚本，把指定目录下的音频文件生成资源表与对应 URI 枚举。

io_i2s_pdm
^^^^^^^^^^^^^^^^^^^^^^^^^^

把 ESP-IDF 的 ``i2s_pdm`` 驱动通道包装成外部接口。读写方向由 ``i2s_pdm_io_cfg_t.dir`` 决定，通道句柄 ``pdm_chan`` 由应用预先按 ESP-IDF 流程创建。

open 阶段调用 ``i2s_channel_enable`` 启用通道；写方向额外注册 ``on_send_q_ovf`` 事件回调用于感知发送队列溢出，并通过事件组的 ``PDM_TX_DONE_BIT`` 在 close 时等待最后一次发送完成，确保尾部数据已发送。读方向用 ``i2s_channel_read`` 阻塞读取，直到填满一个数据载体（payload）；写方向用 ``i2s_channel_write`` 阻塞写完整个数据载体。

.. code:: c

    i2s_chan_handle_t tx_handle = NULL;
    /* 按 ESP-IDF 流程创建 I2S PDM TX 通道 */

    i2s_pdm_io_cfg_t cfg = ESP_GMF_IO_I2S_PDM_CFG_DEFAULT();
    cfg.dir      = ESP_GMF_IO_DIR_WRITER;
    cfg.pdm_chan = tx_handle;

    esp_gmf_io_handle_t io = NULL;
    esp_gmf_io_i2s_pdm_init(&cfg, &io);

io_codec_dev
^^^^^^^^^^^^^^^^^^^^^^^^^^

直接把 ``esp_codec_dev`` 设备句柄当成外部接口。构造时通过 ``codec_dev_io_cfg_t.dev`` 绑定设备；运行中可通过 :cpp:func:`esp_gmf_io_codec_dev_set_dev` 热替换（例如蓝牙切回本地 codec 时）。

``acquire_read`` 调用 ``esp_codec_dev_read`` 同步读取指定字节数；\ ``release_write`` 调用 ``esp_codec_dev_write`` 写入数据载体的有效字节。open / close 只做状态标记，硬件启停由应用通过 ``esp_codec_dev_open`` / ``esp_codec_dev_close`` 负责。codec_dev io 不做数据格式转换，输入样本需由上游处理单元（\ ``aud_rate_cvt``\ 、\ ``aud_bit_cvt``\ 、\ ``aud_ch_cvt``\ ）对齐到设备的当前采样率、位宽与声道。

.. code:: c

    codec_dev_io_cfg_t cfg = ESP_GMF_IO_CODEC_DEV_CFG_DEFAULT();
    cfg.dir = ESP_GMF_IO_DIR_WRITER;
    cfg.dev = playback_handle;

    esp_gmf_io_handle_t io = NULL;
    esp_gmf_io_codec_dev_init(&cfg, &io);
    /* 运行中切换设备 */
    esp_gmf_io_codec_dev_set_dev(io, new_playback_handle);

运行时控制
^^^^^^^^^^^^^^^^^^^^^^^^^^

所有子类共享同一套控制接口，按用途分四组。

**位置与大小**\ 。\ ``esp_gmf_io_set_uri`` / ``get_uri`` 维护 URL；\ ``set_size`` / ``get_size`` 与 ``set_pos`` / ``update_pos`` / ``get_pos`` 维护已读字节数与总字节数。子类在 open 时把总大小写入基类（file 来自 ``stat``\ ，http 来自 ``Content-Length``\ ），处理链用于进度上报与 seek 合法性判断。

**流结束与中止**\ 。\ ``esp_gmf_io_done`` 把当前数据源标为已读完，下游 ``acquire_*`` 返回的数据载体带 ``is_done = true``\ ；异步模式下同时暂停外部接口执行线程，应用调用 ``esp_gmf_io_clear_done`` 再启动。\ ``esp_gmf_io_abort`` 语义更强，后续 ``acquire_*`` / ``release_*`` 直接返回 ``ESP_GMF_IO_ABORT``\ ，需 ``esp_gmf_io_clear_abort`` 恢复。两者对应"切歌"与"用户停止"两个不同语义场景。

**复位与重载**\ 。\ ``esp_gmf_io_reset`` 把 pos 清零、调子类 reset、重置异步执行线程与 job，处理链 ``reset`` 时会自动调用。\ ``esp_gmf_io_reload`` 用于无缝切换：替换 URI 并复用连接与资源，比 ``close + set_uri + open`` 省时。

**速度统计**\ 。通过 ``esp_gmf_io_cfg_t.enable_speed_monitor`` 或 ``esp_gmf_io_enable_speed_monitor`` 开启；基类在 I/O 过程中累计传输字节与耗时，维护即时与平均 Kbps（千比特每秒）。同步与异步模式下均可使用；通过 ``esp_gmf_io_get_speed_stats`` 读取 ``esp_gmf_io_speed_stats_t``\ ，用于观察网络带宽、文件读写吞吐等。

性能
-------------------------------------

下表给出常见配置下的吞吐量级别与调优方向。实测值与文件系统、网络、PSRAM、codec 样本率强相关，仅供相对比较。

.. list-table::
   :widths: 18 22 28 32
   :header-rows: 1

   * - 外部接口
     - 典型模式
     - 主要瓶颈
     - 调优方向
   * - io_file
     - 同步
     - SD 卡控制器 / FATFS 块大小
     - 增大 ``cache_size``\ （建议 4–16 KiB）；在支持 PSRAM DMA 的芯片上用 SPIRAM 缓存
   * - io_http
     - 异步
     - TLS 握手、链路带宽
     - 扩大 ``buffer_size``\ （默认 20 KiB）与 ``io_size``\ （默认 3 KiB）；合理设置执行线程运行核心
   * - io_embed_flash
     - 同步
     - flash 读带宽 / memcpy
     - 单次 ``wanted_size`` 不宜过小；与下游处理单元的 ``io_size`` 对齐
   * - io_i2s_pdm
     - 同步
     - I2S 驱动 DMA 描述符大小
     - 保持数据载体大小为 DMA 描述符的整数倍
   * - io_codec_dev
     - 同步
     - codec 样本率决定硬上限
     - 下游处理单元对齐到 codec 的采样率 / 位宽 / 声道，避免使用软件转换

应用示例
-------------------------------------

- ``gmf_examples/basic_examples/pipeline_play_embed_music``\ ：从 embed_flash 读取 mp3，经 ``aud_dec`` 与效果链，最终通过 codec_dev 输出，覆盖注册池注册、处理链构建、外部接口绑定三阶段

上层应用组件中的 ``esp_audio_simple_player`` 等封装内部也通过本组件连接文件与 HTTP 源，可作进阶参考。

SoC 兼容性
-------------------------------------

- io_file 的 ``cache_caps`` 在不同芯片上行为不同：

  - esp32：使用 ``MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`` 或 ``MALLOC_CAP_DMA``
  - esp32sx / esp32cx：可额外使用 ``MALLOC_CAP_INTERNAL``
  - esp32p4 等具备 ``SOC_SDMMC_PSRAM_DMA_CAPABLE`` 的芯片：可使用 ``MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA`` 节省 SRAM

- io_i2s_pdm 依赖 ``driver/i2s_pdm.h``\ ，仅在支持 PDM 的 SoC 上可用；不支持 PDM 的芯片请改用对应的 I2S Std 封装
- io_http 启用 ``crt_bundle_attach`` 需开启 ``CONFIG_MBEDTLS_CERTIFICATE_BUNDLE``

FAQ
-------------------------------------

**Q：** 何时应启用外部接口的异步模式？

当外部接口延迟存在明显抖动，或下游处理单元消费速率对外部接口抖动敏感时。典型例子是 HTTP 点播音频，网络抖动需要由预取缓冲吸收。对 codec_dev、embed_flash 这类稳定设备，同步模式即可，异步反而增加上下文切换开销。

**Q：** ``acquire_read`` 返回成功但数据载体的 ``valid_size`` 小于 ``wanted_size``\ ？

属于正常现象。文件尾部、网络分块、I2S DMA 描述符切换都可能导致单次读不满请求长度。调用方应以 ``valid_size`` 为准，并在 ``is_done == true`` 时停止继续读取。

**Q：** io_http 配置了 ``event_handle`` 但回调未触发？

确认 event id 属于 ``HTTP_STREAM_*`` 五种之一；另外回调必须通过 :cpp:type:`http_io_cfg_t` 传入配置，或在 init 之后调用 :cpp:func:`esp_gmf_io_http_set_event_callback`\ 。直接改配置结构的成员在 init 之后不生效。

**Q：** embed_flash io 读到一半返回错误 "No _ in file name"？

URL 格式要求 ``embed://<group>/<index>_<name>.<ext>``\ ，索引与文件名之间用下划线分隔。修改 URI 或资源表命名即可解决。

**Q：** codec_dev io 能直接驱动 I2S 吗？

不能直接驱动 I2S。codec_dev io 只与 ``esp_codec_dev`` 句柄交互，后者内部再连接 I2S 或 I2S_PDM 驱动。若需直接操作 I2S，请使用 io_i2s_pdm 或其他 I2S 子类封装。

**Q：** seek 在异步模式下耗时较长？

异步模式下 seek 会 abort 当前数据总线、等待外部接口执行线程在下一个 job 边界执行 ``fseek`` / Range 请求，再刷新数据。等待时长由执行线程正在进行的外部接口操作决定；必要时可通过 ``esp_gmf_io_set_task_timeout`` 调整等待上限。

API 参考
-------------------------------------

本组件涉及的头文件：

- ``esp_gmf_io_file.h``\ ：file io 的配置与初始化
- ``esp_gmf_io_http.h``\ ：http io 的配置、事件、证书、reset
- ``esp_gmf_io_embed_flash.h``\ ：embed_flash io 的配置、资源表设置
- ``esp_gmf_io_i2s_pdm.h``\ ：i2s_pdm io 的配置与初始化
- ``esp_gmf_io_codec_dev.h``\ ：codec_dev io 的配置与设备热替换

基类接口 ``esp_gmf_io.h`` 的 API 文档位于 :doc:`../gmf-core/gmf-core-data-path`。

.. include-build-file:: inc/esp_gmf_io_file.inc

.. include-build-file:: inc/esp_gmf_io_http.inc

.. include-build-file:: inc/esp_gmf_io_embed_flash.inc

.. include-build-file:: inc/esp_gmf_io_i2s_pdm.inc

.. include-build-file:: inc/esp_gmf_io_codec_dev.inc
