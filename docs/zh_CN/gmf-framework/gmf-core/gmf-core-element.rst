GMF 处理单元
================================================

:link_to_translation:`en:[English]`

处理单元（element）是 GMF-Core 中承载具体处理逻辑的对象。解码、编码、重采样、混音、图像缩放等步骤都以处理单元形式接入处理链；处理链负责编排连接关系，执行线程负责调度处理单元的功能函数，处理单元只实现自身的初始化、处理与资源回收逻辑。

数据载体（payload）、数据端口（port）与 acquire-release 协议见 :doc:`gmf-core-data-path`。处理链（pipeline）编排、执行线程（task）调度、状态与错误处理见 :doc:`gmf-core-pipeline`。整体对象关系参考 :doc:`gmf-core-overview`。

生命周期与对象结构
------------------------------------------------

处理单元继承自对象基类 ``esp_gmf_obj_t``\ ，基类提供 tag、配置数据、复制与销毁接口；处理单元基类 :cpp:type:`esp_gmf_element_t` 在此基础上增加生命周期功能函数、输入输出数据端口、能力描述、运行时方法和事件接收接口。

处理单元必须实现三个主要功能函数：

- ``open``\ ：一次性初始化资源，例如创建算法句柄、读取配置、根据上游格式信息计算内部参数。
- ``process``\ ：执行一次数据处理，通常从输入数据端口获取数据载体，处理后写入输出数据端口。
- ``close``\ ：释放 ``open`` 阶段申请的资源。无论处理链正常结束、停止还是出错，框架都会尝试为已 open 的处理单元执行 close。

处理单元还可以实现 ``reset`` 与 ``event_receiver``\ 。前者用于重置内部状态，后者用于接收上游格式信息、处理链事件或应用侧事件。需要依赖上游格式信息才能 open 的处理单元，应在配置中设置 ``dependency = true``\ ，由处理链在收到 REPORT_INFO 事件后再注册它的 open/process job，流程见 :doc:`gmf-core-pipeline` 的依赖型处理单元章节。

按媒体类别，GMF-Core 提供三类派生处理单元。

.. list-table::
   :widths: 34 34 32
   :header-rows: 1

   * - 派生类
     - 格式信息
     - 典型字段
   * - :cpp:type:`esp_gmf_audio_element_t`
     - ``esp_gmf_info_sound_t``
     - 采样率、声道、位深、FourCC
   * - :cpp:type:`esp_gmf_video_element_t`
     - ``esp_gmf_info_video_t``
     - 分辨率、帧率、FourCC
   * - :cpp:type:`esp_gmf_pic_element_t`
     - ``esp_gmf_info_pic_t``
     - 宽、高、FourCC

数据端口属性
------------------------------------------------

处理单元在初始化时声明输入输出数据端口的约束，包括端口数量、缓冲区对齐、端口类型和每次 acquire 推荐的数据量。这些约束由 :cpp:type:`esp_gmf_element_port_attr_t` 描述，通常通过宏设置。

.. code:: c

    esp_gmf_element_cfg_t el_cfg = { 0 };

    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(
        el_cfg.in_attr,
        ESP_GMF_EL_PORT_CAP_SINGLE,
        16,
        16,
        ESP_GMF_PORT_TYPE_BYTE,
        768);

    ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(
        el_cfg.out_attr,
        ESP_GMF_EL_PORT_CAP_SINGLE,
        16,
        16,
        ESP_GMF_PORT_TYPE_BYTE,
        768);

``ESP_GMF_EL_PORT_CAP_SINGLE`` 表示端口只能连接一个对端，\ ``ESP_GMF_EL_PORT_CAP_MULTI`` 表示允许多个对端共享。端口类型、数据载体所有权和 acquire-release 调用顺序见 :doc:`gmf-core-data-path`。

能力描述（capability）
------------------------------------------------

能力描述（capability）是处理单元对外声明自身能力的机制。它不是单一的格式字段，而是一组可以被外部查询的能力节点：每个 :cpp:type:`esp_gmf_cap_t` 用 ``cap_eightcc`` 标识能力类别，可附带性能信息 ``perf`` 和属性迭代函数 ``attr_fun``\ 。属性由 :cpp:type:`esp_gmf_cap_attr_t` 描述，支持离散值、步进范围、倍数范围和常量值等形式。EIGHTCC 能力标识的定义见 :doc:`gmf-core-utils`，媒体格式的 FourCC 定义见 :doc:`gmf-core-fourcc`。

处理单元通过 ``ops.load_caps`` 延迟加载能力列表。外部代码第一次调用 :cpp:func:`esp_gmf_element_get_caps` 时，框架会调用该回调并把结果缓存到处理单元对象中。典型实现如下：

.. code:: c

    static esp_gmf_err_t load_caps(esp_gmf_element_handle_t handle)
    {
        esp_gmf_cap_t *caps = NULL;
        esp_gmf_cap_t cap = { 0 };

        cap.cap_eightcc = ESP_GMF_CAPS_VIDEO_SCALE;
        cap.attr_fun = scale_attr_iter;
        esp_gmf_cap_append(&caps, &cap);

        cap.cap_eightcc = ESP_GMF_CAPS_VIDEO_CROP;
        cap.attr_fun = crop_attr_iter;
        esp_gmf_cap_append(&caps, &cap);

        ESP_GMF_ELEMENT_GET(handle)->caps = caps;
        return ESP_GMF_ERR_OK;
    }

能力查询可以发生在注册池阶段，也可以发生在处理链构建后。应用或上层封装先遍历注册池中的处理单元模板，再读取每个处理单元的能力描述，按能力类别或属性范围选择合适的处理单元。例如视频处理链可以通过 ``cap_eightcc`` 查找具备缩放、裁剪、旋转等能力的处理单元；AI 音频处理链可以检查 ai_afe 是否同时声明 AEC、NS、AGC、VAD 等能力。

常用查询步骤如下：

1. 调用 :cpp:func:`esp_gmf_element_get_caps` 获取能力链表。
2. 调用 :cpp:func:`esp_gmf_cap_fetch_node` 按 ``cap_eightcc`` 找到某类能力。
3. 调用 :cpp:func:`esp_gmf_cap_iterate_attr` 或 :cpp:func:`esp_gmf_cap_find_attr` 获取能力属性。
4. 调用 :cpp:func:`esp_gmf_cap_attr_check_value` 判断目标值是否被支持。

属性类型用于描述不同的协商方式。离散属性适合列出若干个支持值，例如采样率或像素格式集合；步进属性适合描述最小值、最大值和步进，例如缩放后的宽高范围；倍数属性适合描述按因子变化的取值；常量属性适合描述固定能力。上层构建处理链时可据此做动态选择，减少写死处理单元名称和参数的逻辑。

运行时方法（method）
------------------------------------------------

运行时方法（method）是处理单元对外暴露运行时控制动作的机制，用于 open / process / close 之外的参数设置或查询，例如设置目标采样率、调整音量、切换 EQ 预设、修改目标分辨率。每个 :cpp:type:`esp_gmf_method_t` 包含方法名、执行函数和参数描述链表。

处理单元通过 ``ops.load_methods`` 延迟加载方法列表。外部代码调用 :cpp:func:`esp_gmf_element_get_method` 或 :cpp:func:`esp_gmf_element_exe_method` 时，框架会在需要时调用该回调。

.. code:: c

    static esp_gmf_err_t set_volume(void *handle,
                                    esp_gmf_args_desc_t *args,
                                    uint8_t *buf, int len)
    {
        uint8_t volume = *buf;
        /* 更新处理单元内部音量状态 */
        return ESP_GMF_ERR_OK;
    }

    static esp_gmf_err_t load_methods(esp_gmf_element_handle_t handle)
    {
        esp_gmf_method_t *methods = NULL;
        esp_gmf_args_desc_t *args = NULL;

        esp_gmf_args_desc_append(&args, "volume",
                                 ESP_GMF_ARGS_TYPE_UINT8,
                                 sizeof(uint8_t), 0);
        esp_gmf_method_append(&methods, "set_volume", set_volume, args);

        ESP_GMF_ELEMENT_GET(handle)->method = methods;
        return ESP_GMF_ERR_OK;
    }

调用方按方法名执行：

.. code:: c

    uint8_t volume = 80;
    esp_gmf_element_exe_method(el, "set_volume", &volume, sizeof(volume));

方法名应保持稳定，参数描述用于告诉调用方参数名称、类型、大小和偏移。多个处理单元实现相同运行时方法名时，上层应用可以只依赖方法名而不依赖具体处理单元类型；例如软件重采样与硬件 ASRC 可以暴露相同的目标采样率设置方法，处理链替换处理单元后，上层调用逻辑保持不变。

自定义处理单元模板
------------------------------------------------

下面给出一个最小音频处理单元骨架，把处理单元生命周期、数据端口属性和 acquire-release 调用串起来。该处理单元把输入 PCM 乘以增益因子后输出。

.. code:: c

    #include "esp_gmf_audio_element.h"
    #include "esp_gmf_oal_mem.h"

    typedef struct {
        esp_gmf_audio_element_t parent;
        float                   gain;
    } gain_el_t;

    typedef struct {
        float gain;
        int   data_size;
    } gain_cfg_t;

    static esp_gmf_job_err_t gain_open(void *self, void *para)
    {
        gain_el_t *el = (gain_el_t *)self;
        gain_cfg_t *cfg = (gain_cfg_t *)OBJ_GET_CFG(self);
        el->gain = cfg->gain;
        return ESP_GMF_JOB_ERR_OK;
    }

    static esp_gmf_job_err_t gain_process(void *self, void *para)
    {
        gain_el_t *el = (gain_el_t *)self;
        esp_gmf_port_handle_t in  = ESP_GMF_ELEMENT_GET_IN_PORT(self);
        esp_gmf_port_handle_t out = ESP_GMF_ELEMENT_GET_OUT_PORT(self);
        esp_gmf_payload_t *in_load = NULL;
        esp_gmf_payload_t *out_load = NULL;

        esp_gmf_err_io_t ret = esp_gmf_port_acquire_in(in, &in_load, 1024, ESP_GMF_MAX_DELAY);
        if (ret < 0) {
            return (ret == ESP_GMF_IO_ABORT) ? ESP_GMF_JOB_ERR_ABORT : ESP_GMF_JOB_ERR_FAIL;
        }

        ret = esp_gmf_port_acquire_out(out, &out_load, in_load->valid_size, ESP_GMF_MAX_DELAY);
        if (ret < 0) {
            esp_gmf_port_release_in(in, in_load, 0);
            return (ret == ESP_GMF_IO_ABORT) ? ESP_GMF_JOB_ERR_ABORT : ESP_GMF_JOB_ERR_FAIL;
        }

        int16_t *src = (int16_t *)in_load->buf;
        int16_t *dst = (int16_t *)out_load->buf;
        int samples = in_load->valid_size / sizeof(int16_t);
        for (int i = 0; i < samples; i++) {
            dst[i] = (int16_t)(src[i] * el->gain);
        }
        out_load->valid_size = in_load->valid_size;
        out_load->is_done = in_load->is_done;

        esp_gmf_port_release_out(out, out_load, 0);
        esp_gmf_port_release_in(in, in_load, 0);
        return in_load->is_done ? ESP_GMF_JOB_ERR_DONE : ESP_GMF_JOB_ERR_OK;
    }

    static esp_gmf_job_err_t gain_close(void *self, void *para)
    {
        return ESP_GMF_JOB_ERR_OK;
    }

    esp_gmf_err_t gain_el_init(gain_cfg_t *cfg, esp_gmf_element_handle_t *out)
    {
        gain_el_t *el = esp_gmf_oal_calloc(1, sizeof(gain_el_t));
        if (!el) {
            return ESP_GMF_ERR_MEMORY_LACK;
        }

        esp_gmf_element_cfg_t el_cfg = { 0 };
        ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr,
            ESP_GMF_EL_PORT_CAP_SINGLE, 16, 16,
            ESP_GMF_PORT_TYPE_BYTE, cfg->data_size);
        ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(el_cfg.out_attr,
            ESP_GMF_EL_PORT_CAP_SINGLE, 16, 16,
            ESP_GMF_PORT_TYPE_BYTE, cfg->data_size);
        el_cfg.dependency = true;

        esp_gmf_audio_el_init((esp_gmf_audio_element_handle_t)el, &el_cfg);
        esp_gmf_obj_set_tag((esp_gmf_obj_handle_t)el, "gain");
        esp_gmf_obj_set_config((esp_gmf_obj_handle_t)el, cfg, sizeof(gain_cfg_t));

        esp_gmf_element_t *base = ESP_GMF_ELEMENT_GET(el);
        base->ops.open = gain_open;
        base->ops.process = gain_process;
        base->ops.close = gain_close;

        *out = el;
        return ESP_GMF_ERR_OK;
    }

模板的要点：

- 自定义处理单元结构体把派生类基类放在第一个成员，便于在派生类与处理单元基类之间转换。
- 配置结构通过 ``esp_gmf_obj_set_config`` 绑定到对象，open 回调里通过 ``OBJ_GET_CFG`` 取出。
- 三个生命周期功能函数写在处理单元内部，构造时赋值到 ``base->ops``\ 。
- ``dependency = true`` 声明该处理单元需要上游格式信息才能启动。若处理逻辑不依赖上游格式，可保持默认值 false。
- process 中 ``acquire_in`` 与 ``release_in``\ 、\ ``acquire_out`` 与 ``release_out`` 严格成对；每个错误分支都要释放已 acquire 的数据载体，否则数据端口会泄漏。
- 输入数据载体的 ``is_done`` 需要按处理语义透传或转换；遇到流结束时返回 ``DONE``\ ，让框架把对应 job 从链表中移除。

构造好的处理单元可以注册到注册池，再由注册池按名称复制实例并构建处理链。处理链搭建与控制接口见 :doc:`gmf-core-pipeline`。

API 参考
------------------------------------------------

本篇涉及的头文件：

- ``esp_gmf_element.h``\ ：处理单元基类、生命周期、端口属性、能力描述与运行时方法访问接口
- ``esp_gmf_audio_element.h`` / ``esp_gmf_video_element.h`` / ``esp_gmf_pic_element.h``\ ：三类派生处理单元
- ``esp_gmf_cap.h``\ ：能力描述链表、属性描述与属性匹配
- ``esp_gmf_method.h`` / ``esp_gmf_args_desc.h`` / ``esp_gmf_method_helper.h``\ ：运行时方法描述、参数描述与辅助调用

.. include-build-file:: inc/esp_gmf_element.inc

.. include-build-file:: inc/esp_gmf_audio_element.inc

.. include-build-file:: inc/esp_gmf_video_element.inc

.. include-build-file:: inc/esp_gmf_pic_element.inc

.. include-build-file:: inc/esp_gmf_cap.inc

.. include-build-file:: inc/esp_gmf_method.inc

.. include-build-file:: inc/esp_gmf_args_desc.inc

.. include-build-file:: inc/esp_gmf_method_helper.inc
