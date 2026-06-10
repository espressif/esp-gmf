GMF-Misc
====================

:link_to_translation:`en:[English]`

gmf_misc 收纳 ESP-GMF 中功能单一、不归属编解码 / 外部接口（IO）/ 效果三大类的工具型处理单元（element）。当前仅含 ``copier``\ ：单输入多输出处理单元，把上游送来的一帧数据载体（payload）一次性广播给 N 路下游，配合处理链（pipeline）的级联机制即可实现一进多出拓扑。处理单元基类见 :doc:`/gmf-framework/gmf-core/gmf-core-element`，数据通路见 :doc:`/gmf-framework/gmf-core/gmf-core-data-path`，处理链组合见 :doc:`/gmf-framework/gmf-core/gmf-core-pipeline`。

功能清单
-------------------------------------

- copier：把单一输入端口的数据载体广播到多个输出端口；第一路输出与输入共享同一份数据（零拷贝），其余路通过 ``esp_gmf_payload_copy_data`` 复制一份
- 端口类型：输入与输出数据端口（port）都同时支持 ``ESP_GMF_PORT_TYPE_BYTE`` 与 ``ESP_GMF_PORT_TYPE_BLOCK``\ ，自动适配上下游
- 透传 ``is_done`` 标志：输入数据载体的流结束标记会传到第一路输出，让下游处理链自然收尾

技术拆解
-------------------------------------

copier 的工作机制
^^^^^^^^^^^^^^^^^^^^^^^^^^

copier 是处理链中实现"一进多出"拓扑的工具。在 ``esp_gmf_element_t`` 基类上，把输入端口属性设为 ``ESP_GMF_EL_PORT_CAP_SINGLE``\ （仅 1 路输入），输出端口属性设为 ``ESP_GMF_EL_PORT_CAP_MULTI``\ （允许多路连接）。注册到注册池（pool）后，应用通过 ``esp_gmf_pipeline_connect_pipe`` 或 ``esp_gmf_pipeline_reg_el_port`` 把多个处理链 / 数据端口连到 copier 的输出端，运行时 copier 在每次 ``process`` 中：

1. ``acquire_in`` 取一帧输入数据载体，长度按 ``data_length`` 决定。
2. 遍历输出数据端口链表，对每一路：

   - 第一路（链表头）特殊处理，让 ``out_load`` 指向同一份输入数据载体，仅同步 ``valid_size`` 与 ``is_done``\ ，\ ``release_out`` 时按引用计数交给下游，避免内存拷贝；
   - 其余路单独 ``acquire_out`` 一块容量 ≥ 输入有效字节的输出数据载体，调用 ``esp_gmf_payload_copy_data`` 把输入数据复制过去再 ``release_out``\ 。

3. 全部输出 release 完后 ``release_in`` 归还输入数据载体。

每一路输出的 ``release_out`` 都用各自数据端口的 ``wait_ticks``\ ，应用可在添加数据端口时通过框架接口为不同下游设置不同的阻塞时间，避免某路下游处理慢时整个 copier 阻塞。返回值方面：输入数据载体的 ``is_done`` 触发时直接返回 ``ESP_GMF_JOB_ERR_DONE``\ ，让执行线程把 copier 从 job 链表中移除；正常情况下返回当前帧的 ``valid_size``\ 。

配置
^^^^^^^^^^^^^^^^^^^^^^^^^^

:cpp:type:`esp_gmf_copier_cfg_t` 只有一个字段：

.. code:: c

    typedef struct {
        uint8_t copy_num;  /* 复制路数 */
    } esp_gmf_copier_cfg_t;

``copy_num`` 在初始化时声明计划连接的输出路数。实际连接到 copier 的路数由处理链创建时绑定的输出数据端口数量决定，\ ``copy_num`` 主要用于资源预估与日志。

代码示例
^^^^^^^^^^^^^^^^^^^^^^^^^^

把 copier 注册到注册池后即可按名 ``"copier"`` 出现在处理链字符串里：

.. code:: c

    esp_gmf_copier_cfg_t cfg = { .copy_num = 2 };
    esp_gmf_obj_handle_t copier = NULL;
    esp_gmf_copier_init(&cfg, &copier);
    esp_gmf_pool_register_element(pool, copier, NULL);

    /* 主 pipeline：File → Decoder → Copier → Resample → I2S */
    const char *p1_els[] = {"aud_dec", "copier", "aud_rate_cvt"};
    esp_gmf_pipeline_handle_t p1 = NULL;
    esp_gmf_pool_new_pipeline(pool, "io_file", p1_els, 3, "io_codec_dev", &p1);

    /* 旁路 pipeline：Copier 的第二路 → Resample → File */
    const char *p2_els[] = {"aud_rate_cvt"};
    esp_gmf_pipeline_handle_t p2 = NULL;
    esp_gmf_pool_new_pipeline(pool, NULL, p2_els, 1, "io_file", &p2);

    /* 把 p1 中 copier 的输出 port 连到 p2 的 resample 输入 port */
    esp_gmf_pipeline_connect_pipe(p1, "copier", NULL, p2, "aud_rate_cvt", NULL);

完整的"一分多"拓扑组合方式（事件级联、prev_run 协调）见 :doc:`/gmf-framework/gmf-core/gmf-core-pipeline`。

应用示例
-------------------------------------

- ``elements/test_apps/main/elements/gmf_audio_play_el_test.c``\ ：综合用例，演示 copier 与音频处理单元组合
- :doc:`/gmf-framework/gmf-core/gmf-core-overview` 的"一分多分流"典型拓扑：音乐播放同时把同一份音频送到 AEC 参考路

FAQ
-------------------------------------

**Q：** copier 的第一路输出与其他路有什么差别？

第一路输出与输入共享同一份数据载体，是零拷贝；其余路在 ``process`` 中通过 ``esp_gmf_payload_copy_data`` 各自分配并复制一份数据。因此应将延迟与吞吐要求最高的下游连接至第一路，可避免一次 memcpy。

**Q：** copier 接多少路输出有上限？

源码层面无固定上限，\ ``copy_num`` 仅用于估算资源。实际限制取决于内存：每多接一路就多一份数据载体缓冲。建议根据下游处理单元的吞吐和缓冲一起规划。

**Q：** 某一路下游处理较慢会影响其他路吗？

会受影响。copier 在 ``process`` 中按链表顺序串行 ``release_out``\ ，某一路阻塞时其他路也会等待。对延迟敏感的下游建议放在链表头部（第一路），并为处理较慢的数据端口设置较短的 ``wait_ticks``\ ，超时时 copier 会跳过该次释放并记录警告。

**Q：** copier 是否可置于处理链末尾？

可以使用，但置于末尾时无法向后续处理单元分流。常见用法是把 copier 放在中间，输出端口的一部分连接到本处理链后续处理单元，另一部分通过 ``esp_gmf_pipeline_connect_pipe`` 连接到旁路处理链。

API 参考
-------------------------------------

本组件涉及的头文件：

- ``esp_gmf_copier.h``\ ：copier 配置与初始化

.. include-build-file:: inc/esp_gmf_copier.inc
