系统工具
================================================

:link_to_translation:`en:[English]`

GMF-Core 在框架主体之外提供两组通用模块：操作系统抽象层（OAL）和辅助工具（helpers）。前者把操作系统 / IDF 相关的系统调用收敛到统一接口，便于兼容不同版本；后者提供 URI 解析、EIGHTCC 能力标识等小工具，供处理单元与上层应用复用。

本篇覆盖这两组模块的使用接口。框架主要对象（处理链（pipeline）、处理单元（element）、数据载体（payload）、数据端口（port））见 :doc:`gmf-core-pipeline`、:doc:`gmf-core-element` 与 :doc:`gmf-core-data-path`。

操作系统抽象层
-------------------------------------

位于 ``oal/`` 目录。任何 GMF 代码涉及内存、互斥锁、线程等系统调用都通过 OAL 访问，而不是直接调用 FreeRTOS 或 IDF 的 API。OAL 内部按"目标平台 + 编译开关"路由到具体实现，因此同一份框架代码既可以运行在 ESP-IDF 上，也为后续迁移到其他 RTOS 留出抽象层。

内存分配
^^^^^^^^^^^^^^^^^^^^^^^^^^

``esp_gmf_oal_malloc`` / ``esp_gmf_oal_calloc`` / ``esp_gmf_oal_realloc`` 与标准 C 接口同名同义，区别在于：开启 PSRAM 时框架会按策略选择内部 RAM 或 PSRAM 分配，而不需要应用代码做判断。\ :cpp:func:`esp_gmf_oal_calloc_inner` 强制使用内部 RAM，用于对延迟敏感的场景。

.. code:: c

    void *buf = esp_gmf_oal_calloc(1, size);
    if (!buf) {
        return ESP_GMF_ERR_MEMORY_LACK;
    }
    /* ... */
    esp_gmf_oal_free(buf);

带对齐的分配用 :cpp:func:`esp_gmf_oal_malloc_align`\ ，\ ``align`` 必须是 2 的幂或 0。

PSRAM 状态
^^^^^^^^^^^^^^^^^^^^^^^^^^

三个查询接口让代码能感知 PSRAM 的可用性与对齐要求：

- :cpp:func:`esp_gmf_oal_mem_spiram_is_enabled`\ ：当前固件是否启用了 PSRAM
- :cpp:func:`esp_gmf_oal_mem_spiram_stack_is_enabled`\ ：是否允许把任务栈放在 PSRAM
- :cpp:func:`esp_gmf_oal_get_spiram_cache_align`\ ：PSRAM cache 的对齐字节数

自定义处理单元需要跨 PSRAM 边界 DMA 时，用 :cpp:func:`esp_gmf_oal_get_spiram_cache_align` 取出对齐值，填入 ``ESP_GMF_ELEMENT_*_PORT_ATTR_SET`` 的 ``buf_addr_aligned`` / ``buf_size_aligned`` 字段，避免缓存一致性问题。

互斥锁与线程
^^^^^^^^^^^^^^^^^^^^^^^^^^

OAL 把 FreeRTOS 的 mutex 与 task 接口包成更简洁的 API：

- ``esp_gmf_oal_mutex_create`` / ``lock`` / ``unlock`` / ``destroy``\ ：递归互斥锁
- ``esp_gmf_oal_thread_create`` / ``delete``\ ：任务创建（\ ``delete`` 接受 NULL 句柄）

线程接口接收一个 :cpp:type:`esp_gmf_oal_thread_t` 配置（栈大小、优先级、CPU 核心、是否使用外部 SPI RAM 栈），与 ``esp_gmf_task_cfg_t`` 中嵌套使用的 ``esp_gmf_task_config_t`` 字段一致。执行线程与处理链集成时一般不直接调 OAL 线程接口，而是通过 :cpp:func:`esp_gmf_task_init` 间接使用。

调试辅助
^^^^^^^^^^^^^^^^^^^^^^^^^^

``ESP_GMF_MEM_SHOW(tag)`` 在当前位置打印堆内存状态。\ ``esp_gmf_oal_sys_get_real_time_stats`` 输出运行时任务统计，便于排查栈不足或 CPU 占用偏高。这两个接口都用宏或函数把"调用点信息"自动带进日志，调试时不需要手填行号。

URI 解析
-------------------------------------

``esp_gmf_uri_parser`` 将 ``scheme://host[:port]/path?query`` 格式的字符串解析为各个字段，外部接口（IO）处理单元据此判断需要选择的具体实现。典型调用：

.. code:: c

    esp_gmf_uri_t info = { 0 };
    esp_gmf_uri_parse("http://server:8080/song.mp3?token=abc", &info);
    /* info.scheme = "http", info.host = "server", info.path = "/song.mp3" */
    esp_gmf_uri_free(&info);

解析结果中的字符串是从源字符串复制出的副本，使用结束后必须调用 :cpp:func:`esp_gmf_uri_free` 释放。注册池（pool）的 :cpp:func:`esp_gmf_pool_get_io_tag_by_url` 内部使用同一解析器；自定义外部接口实现 ``get_score`` 回调评估 URL 时，也建议复用此模块。

EIGHTCC 能力标识
-------------------------------------

EIGHTCC（Eight Character Code，八字符代码）用 8 个 ASCII 字符标识处理单元能力类别。能力描述（capability）通过 ``cap_eightcc`` 声明处理单元属于哪一类能力，例如音频解码、音频混音、视频缩放或视频叠加。EIGHTCC 描述"能做什么"，FourCC 描述"处理什么媒体格式"；FourCC 的定义见 :doc:`gmf-core-fourcc`。

``esp_gmf_caps_def.h`` 按音频与视频两类定义常见 EIGHTCC。

**音频能力**

- ``ESP_GMF_CAPS_AUDIO_DECODER`` / ``ESP_GMF_CAPS_AUDIO_ENCODER``\ ：音频解码 / 编码
- ``ESP_GMF_CAPS_AUDIO_RATE_CONVERT`` / ``ESP_GMF_CAPS_AUDIO_CHANNEL_CONVERT`` / ``ESP_GMF_CAPS_AUDIO_BIT_CONVERT``\ ：采样率、声道数、位深转换
- ``ESP_GMF_CAPS_AUDIO_EQUALIZER`` / ``ESP_GMF_CAPS_AUDIO_MIXER`` / ``ESP_GMF_CAPS_AUDIO_SONIC`` / ``ESP_GMF_CAPS_AUDIO_FADE``\ ：常规音频效果
- ``ESP_GMF_CAPS_AUDIO_AEC`` / ``ESP_GMF_CAPS_AUDIO_NS`` / ``ESP_GMF_CAPS_AUDIO_AGC`` / ``ESP_GMF_CAPS_AUDIO_VAD`` / ``ESP_GMF_CAPS_AUDIO_WWE`` / ``ESP_GMF_CAPS_AUDIO_VCMD``\ ：AI 语音前端能力
- ``ESP_GMF_CAPS_AUDIO_DRC`` / ``ESP_GMF_CAPS_AUDIO_MBC``\ ：动态范围处理

**视频能力**

- ``ESP_GMF_CAPS_VIDEO_DECODER`` / ``ESP_GMF_CAPS_VIDEO_ENCODER``\ ：视频解码 / 编码
- ``ESP_GMF_CAPS_VIDEO_COLOR_CONVERT``\ ：颜色格式转换
- ``ESP_GMF_CAPS_VIDEO_CROP`` / ``ESP_GMF_CAPS_VIDEO_ROTATE`` / ``ESP_GMF_CAPS_VIDEO_SCALE``\ ：裁剪、旋转、缩放
- ``ESP_GMF_CAPS_VIDEO_OVERLAY`` / ``ESP_GMF_CAPS_VIDEO_FPS_CVT``\ ：叠加与帧率转换

字符串与数值的相互转换通过 ``STR_2_EIGHTCC`` / ``EIGHTCC_2_STR`` 宏完成；新增自定义能力标识时复用这两个宏，避免直接拼接字节。

API 参考
-------------------------------------

本篇涉及的头文件：

- ``esp_gmf_oal_mem.h``\ ：内存分配与 PSRAM 状态查询
- ``esp_gmf_oal_mutex.h``\ ：互斥锁
- ``esp_gmf_oal_thread.h``\ ：线程封装
- ``esp_gmf_oal_sys.h``\ ：运行时统计与系统辅助
- ``esp_gmf_uri_parser.h``\ ：URI 解析
- ``esp_gmf_caps_def.h``\ ：EIGHTCC 能力标识

.. include-build-file:: inc/esp_gmf_oal_mem.inc

.. include-build-file:: inc/esp_gmf_oal_mutex.inc

.. include-build-file:: inc/esp_gmf_oal_thread.inc

.. include-build-file:: inc/esp_gmf_oal_sys.inc

.. include-build-file:: inc/esp_gmf_uri_parser.inc

.. include-build-file:: inc/esp_gmf_caps_def.inc
