GMF-Video
====================

:link_to_translation:`en:[English]`

gmf_video 是 ESP-GMF 的视频处理组件，提供 9 个处理单元（element）覆盖编解码、像素加速器、软件图像效果与帧率 / 叠加工具。所有处理单元都继承自 ``esp_gmf_video_element_t``\ ，对外暴露统一的生命周期功能函数、acquire-release 数据接口和运行时方法（method）调用；底层算法分别由 ESP32-P4 的硬件 PPA / 2D-DMA、\ ``esp_video_codec`` 编解码库与 ``esp_image_effects`` 软件图像效果库实现。本篇按四类展开每个处理单元的用途、配置与运行时控制；处理单元基类与运行时方法机制见 :doc:`/gmf-framework/gmf-core/gmf-core-element`，数据通路见 :doc:`/gmf-framework/gmf-core/gmf-core-data-path`。

功能清单
-------------------------------------

- vid_dec：视频解码，支持 H.264 与 MJPEG，可指定输出像素格式（如 YUV420P、RGB565LE）
- vid_enc：视频编码，支持 H.264 与 MJPEG，可运行中调整 bitrate、GOP、QP 范围（仅 H.264）
- vid_ppa：ESP32-P4 像素加速器复合处理单元，把颜色转换、缩放、裁剪、旋转合并为一次硬件处理
- vid_fps_cvt：帧率转换，按 PTS 丢帧把输入帧率降到指定输出帧率
- vid_overlay：叠加混合器，把额外画面（水印、UI、时间戳）按 alpha 混合或透明色（colorkey）叠加到原始视频
- vid_color_cvt：软件颜色转换，覆盖 YUYV、RGB565、RGB888、YUV420P 等常见格式互转
- vid_crop：软件裁剪，从原始帧中提取指定矩形区域
- vid_scale：软件缩放，按目标分辨率重采样视频帧
- vid_rotate：软件旋转，支持任意角度（单位为度）
- 共性：旁路（bypass）优化、自动对齐查询、\ ``need_recfg`` 触发的运行时重配置、PTS 透传与丢帧逻辑

技术拆解
-------------------------------------

元素层次与共性机制
^^^^^^^^^^^^^^^^^^^^^^^^^^

每个具体处理单元都按"包装一个底层视频算法 / 硬件句柄"的方式构造：派生结构以 ``esp_gmf_video_element_t`` 为首字段，绑定 codec / PPA / imgfx handle 与本处理单元的配置；\ ``open`` 创建底层 handle 并向处理链（pipeline）上报输出 ``esp_gmf_info_video_t``\ （宽、高、帧率、像素格式 FourCC）；\ ``process`` 在每次调度时从输入数据端口（port）取帧、执行转换并写出输出数据端口；\ ``close`` 释放硬件资源。

.. only:: html

   .. mermaid::

      classDiagram
          direction TD

          class esp_gmf_video_element_t
          class Codec {
              vid_dec
              vid_enc
          }
          class HwAccel {
              vid_ppa
          }
          class SwEffects {
              vid_color_cvt
              vid_crop
              vid_scale
              vid_rotate
          }
          class Utility {
              vid_fps_cvt
              vid_overlay
          }

          esp_gmf_video_element_t <|-- Codec
          esp_gmf_video_element_t <|-- HwAccel
          esp_gmf_video_element_t <|-- SwEffects
          esp_gmf_video_element_t <|-- Utility

四条公共机制：

- **旁路优化**\ ：vid_dec 在源 / 目标格式一致时进入零拷贝直通；软件效果处理单元在源 / 目标参数一致时同样置旁路，输入数据载体（payload）直接透传输出数据端口。
- **need_recfg 重配置**\ ：所有 setter（\ ``set_dst_format`` / ``set_dst_resolution`` / ``set_angle`` 等）改动后置 ``need_recfg`` 标志，下一次 ``process`` 边界销毁旧 handle、重建新 handle，运行中切换分辨率或像素格式不需要应用层重建处理单元。
- **缓冲对齐自动查询**\ ：底层硬件（PPA、H.264 编码器）通常对输入 / 输出缓冲区有对齐要求，处理单元在 ``open`` 阶段调用硬件驱动查询对齐参数，写入数据端口 attr 让框架按对齐分配数据载体，避免硬件直接拒绝处理。
- **格式信息上报**\ ：上游处理单元解析到当前帧的宽高 / 像素格式后通过 ``REPORT_INFO`` 事件上报，处理链把信息投递给下游处理单元，下游据此 open 或触发重配置。机制详情见 :doc:`/gmf-framework/gmf-core/gmf-core-pipeline` 的 REPORT_INFO 说明。

视频编解码
^^^^^^^^^^^^^^^^^^^^^^^^^^

vid_dec 与 vid_enc 是视频处理链的起点或终点处理单元，典型组装如下。

.. only:: html

   .. mermaid::

      flowchart LR
          subgraph rec ["录制链"]
              direction LR
              CapIn(("camera/raw")) --> CC1["vid_color_cvt"]
              CC1 --> Scale["vid_scale"]
              Scale --> Enc["vid_enc"]
              Enc --> FileOut(("file/http"))
          end
          subgraph play ["播放链"]
              direction LR
              FileIn(("file/http")) --> Dec["vid_dec"]
              Dec --> PPA["vid_ppa"]
              PPA --> Disp(("display"))
          end

**vid_dec**\ ：把压缩流解码成原始像素。\ :cpp:func:`esp_gmf_video_dec_init` 接受 :cpp:type:`esp_gmf_video_dec_cfg_t`\ ，\ ``codec_cc`` 字段用 FourCC 指定解码器实现；为零时框架按可用硬件 / 软件解码器自动选择。

通过 :cpp:func:`esp_gmf_video_dec_get_dst_formats` 查询指定源编码（H.264 / MJPEG 等）对应的可输出像素格式列表，再用 :cpp:func:`esp_gmf_video_dec_set_dst_format` 在处理单元启动前固定输出格式。如果源格式与目标格式一致（罕见但有用，例如纯转封装链），vid_dec 自动进入旁路模式直接转发数据载体。

.. code:: c

    esp_gmf_video_dec_cfg_t cfg = { .codec_cc = 0 };  /* 自动选 */
    esp_gmf_element_handle_t dec = NULL;
    esp_gmf_video_dec_init(&cfg, &dec);

    const uint32_t *fmts = NULL;
    uint8_t fmt_num = 0;
    esp_gmf_video_dec_get_dst_formats(dec, ESP_FOURCC_H264, &fmts, &fmt_num);
    esp_gmf_video_dec_set_dst_format(dec, fmts[0]);

**vid_enc**\ ：把原始像素编码成压缩流。\ :cpp:func:`esp_gmf_video_enc_init` 接受 :cpp:type:`esp_gmf_video_enc_cfg_t`\ ，\ ``codec_cc`` 指定编码器实现。

正常工作流是处理单元启动后从上游获取源信息（\ ``esp_gmf_info_video_t``\ ），按 ``set_dst_codec`` 指定的目标 codec 自动选具体实现并 open。如果要在启动前查询编码器能力（例如查支持的源像素格式），先用 :cpp:func:`esp_gmf_video_enc_preset` 传入源信息与目标 codec，再调用 :cpp:func:`esp_gmf_video_enc_get_src_formats` 与 :cpp:func:`esp_gmf_video_enc_get_out_size`\ 。

运行中可调三个参数（H.264 编码器额外支持后两个）：

.. code:: c

    esp_gmf_video_enc_set_bitrate(enc, 2 * 1000 * 1000);  /* 2 Mbps */
    esp_gmf_video_enc_set_gop(enc, 30);                    /* 每 30 帧一个 I 帧 */
    esp_gmf_video_enc_set_qp(enc, 20, 40);                 /* QP 区间 */

输出缓冲区大小按 :cpp:func:`esp_gmf_video_enc_get_out_size` 估算，MJPEG 按 10:1、H.264 按 2:1 的压缩比给上限，框架按此尺寸分配输出数据载体。

硬件加速 PPA
^^^^^^^^^^^^^^^^^^^^^^^^^^

**vid_ppa** 仅支持 ESP32-P4/ESP32S31，把颜色转换、缩放、裁剪、旋转合并到一次硬件处理中。\ :cpp:func:`esp_gmf_video_ppa_init` 不需要传配置；四个 setter 分别配置目标格式、目标分辨率、裁剪区域、旋转角度（仅支持 0 / 90 / 180 / 270 度）。这些 setter 都只能在处理单元启动前调用，\ ``open`` 时一次性向硬件下发组合参数。

PPA 内部根据当前请求选择硬件路径：单纯的颜色格式转换可以由 2D-DMA 完成，吞吐更高；涉及缩放、旋转或 2D-DMA 不支持的格式组合时回退到 PPA 主路径。路径选择由框架内部完成，处理单元仅暴露统一接口。

.. code:: c

    esp_gmf_element_handle_t ppa = NULL;
    esp_gmf_video_ppa_init(NULL, &ppa);

    esp_gmf_video_resolution_t dst_res = { .width = 480, .height = 320 };
    esp_gmf_video_ppa_set_dst_resolution(ppa, &dst_res);
    esp_gmf_video_ppa_set_dst_format(ppa, ESP_FOURCC_RGB16);
    esp_gmf_video_ppa_set_rotation(ppa, 90);

    esp_gmf_video_rgn_t crop = { .x = 0, .y = 0, .width = 800, .height = 600 };
    esp_gmf_video_ppa_set_cropped_rgn(ppa, &crop);

非 ESP32-P4 芯片上 vid_ppa 不可用，需要等价功能时用下文 4 个软件效果处理单元串接（vid_color_cvt → vid_crop → vid_scale → vid_rotate），代价是 CPU 占用上升。

软件图像效果
^^^^^^^^^^^^^^^^^^^^^^^^^^

四个软件效果处理单元包装 ``esp_image_effects`` 库的对应算法，对外只暴露各自的 setter。所有效果都遵循同一条路径：\ ``acquire_in`` 取输入帧、按配置生成输出帧大小、\ ``acquire_out`` 取输出帧、调用 ``esp_imgfx_*_process`` 处理、双双 release。

**vid_color_cvt**\ ：像素格式转换，例如 RGB565 → YUV420P、YUYV → RGB888。颜色空间标准（BT601 / BT709）通过初始化时的 ``esp_imgfx_color_convert_cfg_t.color_space_std`` 字段选择。\ :cpp:func:`esp_gmf_video_color_convert_dst_format` 在运行中切换目标格式，置 ``need_recfg``\ 。

**vid_crop**\ ：裁剪元素。初始化用 ``DEFAULT_ESP_GMF_CROP_CONFIG`` 给出默认 320×240 输入、160×120 裁剪区。运行中用 :cpp:func:`esp_gmf_video_crop_rgn` 切换裁剪矩形（\ :cpp:type:`esp_gmf_video_rgn_t` 给出 x / y / width / height）。

**vid_scale**\ ：缩放元素。\ ``filter_type`` 字段决定算法（如 ``ESP_IMGFX_SCALE_FILTER_TYPE_DOWN_RESAMPLE``\ ）。运行中用 :cpp:func:`esp_gmf_video_scale_dst_resolution` 修改目标分辨率。

**vid_rotate**\ ：旋转元素。与 vid_ppa 只支持四个角度不同，软件旋转支持任意角度（单位为度），适合需要倾斜显示或图像校正的场景。\ :cpp:func:`esp_gmf_video_rotate_set_rotation` 在运行中切换角度。

.. code:: c

    esp_imgfx_scale_cfg_t cfg = DEFAULT_ESP_GMF_SCALE_CONFIG();
    cfg.in_res       = (esp_imgfx_res_t){ .width = 640, .height = 480 };
    cfg.in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE;
    cfg.scale_res    = (esp_imgfx_res_t){ .width = 320, .height = 240 };

    esp_gmf_element_handle_t scale = NULL;
    esp_gmf_video_scale_init(&cfg, &scale);

帧率与叠加
^^^^^^^^^^^^^^^^^^^^^^^^^^

**vid_fps_cvt**\ ：通过丢帧把输入帧率降低到指定输出帧率。处理单元维护起始 PTS 与累计已输出帧数，按 ``1 / fps`` 步长计算 ``expected_pts``\ 。每一帧到来时：当前帧 PTS 大于等于 ``expected_pts`` 才透传到下游并累计计数；否则直接 ``release_in`` 丢弃该帧。

由于纯丢帧不能升采样，\ ``set_fps`` 设定值必须低于上游帧率，否则等于无效。

.. code:: c

    esp_gmf_element_handle_t fps = NULL;
    esp_gmf_video_fps_cvt_init(NULL, &fps);
    esp_gmf_video_fps_cvt_set_fps(fps, 15);  /* 从 30 fps 降到 15 fps */

**vid_overlay**\ ：把额外画面叠加到主视频上。处理单元是多输入：主视频从默认 in 数据端口进入，叠加层从用户通过 :cpp:func:`esp_gmf_video_overlay_set_overlay_port` 注册的额外数据端口进入。合成支持 alpha 混合与透明色（colorkey）两种模式。alpha 混合公式：

::

    Output = Original × (255 − alpha) + Overlay × alpha

``set_rgn`` 指定叠加层在主画面上的位置与格式（\ :cpp:type:`esp_gmf_overlay_rgn_info_t` 含 ``format_id``\ 、\ ``dst_rgn``\ ，以及可选的 ``has_trans_color`` / ``trans_color`` 用于 colorkey 合成，与指定 RGB 接近的像素视为透明）；\ ``set_alpha`` 在运行中改透明度（0 全透明、255 全不透明）；\ :cpp:func:`esp_gmf_video_overlay_enable`\(false) 临时禁用叠加，处理单元不再 acquire 叠加数据端口，主画面原样透传。

.. code:: c

    esp_gmf_element_handle_t ovl = NULL;
    esp_gmf_video_overlay_init(NULL, &ovl);

    esp_gmf_video_overlay_set_overlay_port(ovl, overlay_port);
    esp_gmf_overlay_rgn_info_t rgn = {
        .format_id = ESP_FOURCC_RGB16,
        .dst_rgn   = { .x = 16, .y = 16, .width = 128, .height = 32 },
    };
    esp_gmf_video_overlay_set_rgn(ovl, &rgn);
    esp_gmf_video_overlay_set_alpha(ovl, 200);

性能
-------------------------------------

视频处理的吞吐主要由底层硬件 / 算法决定，处理单元调度与 acquire-release 的开销相对较小。按瓶颈分类：

.. list-table::
   :widths: 20 24 56
   :header-rows: 1

   * - 类别
     - 主要瓶颈
     - 调优方向
   * - vid_dec / vid_enc
     - 硬件 codec 或软件库的吞吐
     - ESP32-P4 支持 H.264 硬件编码；提前 ``preset`` 让框架预分配缓冲
   * - vid_ppa
     - PPA / 2D-DMA 时钟与缓冲带宽
     - 合并相邻像素操作到同一 vid_ppa 实例减少中间 buffer
   * - 软件效果
     - CPU 与内存带宽
     - 用 ESP32-P4 时优先 vid_ppa；其余 SoC 控制分辨率不要超过 SoC 处理能力
   * - vid_fps_cvt
     - 仅做 PTS 比较与丢帧，CPU 开销极小
     - 适合放在编码或显示前减少下游负载
   * - vid_overlay
     - alpha 混合或 colorkey 合成的 CPU 周期
     - 叠加区域越小、像素格式 byte 数越少越快

应用示例
-------------------------------------

- ``elements/test_apps/main/elements/gmf_video_el_test.c``\ ：典型 video 处理单元用例，包含 ``gen_pattern_color_bar`` 合成测试帧、open 阶段格式上报验证
- ``elements/test_apps/main/elements/gmf_image_effects_test.c``\ ：图像效果链测试，覆盖软件效果元素的串接

上层 ``esp_video_render`` 与 ``esp_capture`` 等应用组件也通过本组件连接视频处理链，可作进阶参考。

SoC 兼容性
-------------------------------------

各处理单元在乐鑫 SoC 上的支持矩阵：

.. list-table::
   :widths: 22 18 18 18 18
   :header-rows: 1

   * - element
     - ESP32
     - ESP32-S2
     - ESP32-S3
     - ESP32-P4
   * - vid_ppa
     - 不支持
     - 不支持
     - 不支持
     - 支持
   * - vid_fps_cvt
     - 支持
     - 支持
     - 支持
     - 支持
   * - vid_overlay
     - 支持
     - 支持
     - 支持
     - 支持
   * - vid_dec
     - MJPEG
     - MJPEG
     - SW H.264 / MJPEG
     - SW H.264 / HW MJPEG
   * - vid_enc
     - MJPEG
     - MJPEG
     - SW H.264 / MJPEG
     - HW H.264 / HW MJPEG
   * - vid_color_cvt / vid_crop / vid_scale / vid_rotate
     - 支持
     - 支持
     - 支持
     - 支持

ESP32 与 ESP32-S2 不支持 H.264。ESP32-S3 的 H.264 编解码均通过软件实现；ESP32-P4 支持 H.264 硬件编码与 MJPEG 硬件编解码，H.264 解码通过软件实现。若需 ESP32-P4 的 PPA 加速，可省略软件效果处理单元，改用 vid_ppa。

FAQ
-------------------------------------

**Q：** vid_dec 输出格式如何选择？

先用 :cpp:func:`esp_gmf_video_dec_get_dst_formats` 查询当前源编码可支持的输出格式列表，再选择符合下游处理单元输入要求的格式（例如 vid_ppa 需要 RGB565、显示需要 RGB888），并在处理单元启动前调用 :cpp:func:`esp_gmf_video_dec_set_dst_format` 固定输出格式。需要查询输出帧大小时，才要求先设置目标格式或在处理单元启动后调用对应查询接口。

**Q：** vid_enc 启动前查不到输出帧大小？

正常情况编码器要在获取上游 ``esp_gmf_info_video_t`` 后才能确定输出大小。若希望在启动前查询，先调 :cpp:func:`esp_gmf_video_enc_preset` 传入预期源信息与目标 codec，\ :cpp:func:`esp_gmf_video_enc_get_out_size` 才能返回值。

**Q：** vid_ppa 在 ESP32 上是否可用？

不支持。vid_ppa 依赖 ESP32-P4 的硬件 PPA / 2D-DMA。其他 SoC 上用 vid_color_cvt / vid_scale / vid_rotate / vid_crop 软件实现替代，分辨率与帧率需要按 CPU 实际能力收敛。

**Q：** vid_fps_cvt 设置的帧率高于上游会怎样？

处理单元不做插帧。若 ``set_fps`` 高于上游帧率，每一帧的 PTS 都大于等于 ``expected_pts``\ ，全部透传，输出帧率被上游限定。该配置仅在降低帧率时生效，设定值不得高于上游帧率。

**Q：** vid_overlay 的两个输入如何同步？

主输入是普通 GMF 输入数据端口，叠加输入是用户通过 ``set_overlay_port`` 注册的额外数据端口。处理单元每次 ``process`` 先 acquire 主输入再 acquire 叠加输入，应用方负责保证两路按帧对齐（例如同一个处理链内的两个上游处理单元同步产帧），否则可能出现叠加帧落后或抖动。

**Q：** 软件效果处理单元与 vid_ppa 在 ESP32-P4 上如何取舍？

需要做颜色转换 + 缩放 + 旋转 / 裁剪组合时优先用 vid_ppa：一次硬件处理完成全部转换，CPU 占用低、延迟稳。仅需单一操作或需要 PPA 不支持的角度 / 像素格式时用对应软件元素。

API 参考
-------------------------------------

本组件涉及的头文件：

- ``esp_gmf_video_dec.h`` / ``esp_gmf_video_enc.h``\ ：视频编解码处理单元
- ``esp_gmf_video_ppa.h``\ ：像素加速器
- ``esp_gmf_video_fps_cvt.h``\ ：帧率转换
- ``esp_gmf_video_overlay.h``\ ：叠加混合器
- ``esp_gmf_video_color_convert.h`` / ``esp_gmf_video_crop.h`` / ``esp_gmf_video_scale.h`` / ``esp_gmf_video_rotate.h``\ ：软件图像效果
- ``esp_gmf_video_types.h``\ ：视频分辨率 / 区域 / 叠加配置等共享类型
- ``esp_gmf_video_param.h``\ ：视频参数辅助接口

处理单元基类 ``esp_gmf_video_element_t`` 的接口位于 :doc:`/gmf-framework/gmf-core/gmf-core-element`。

.. include-build-file:: inc/esp_gmf_video_dec.inc

.. include-build-file:: inc/esp_gmf_video_enc.inc

.. include-build-file:: inc/esp_gmf_video_ppa.inc

.. include-build-file:: inc/esp_gmf_video_overlay.inc

.. include-build-file:: inc/esp_gmf_video_fps_cvt.inc

.. include-build-file:: inc/esp_gmf_video_color_convert.inc

.. include-build-file:: inc/esp_gmf_video_crop.inc

.. include-build-file:: inc/esp_gmf_video_scale.inc

.. include-build-file:: inc/esp_gmf_video_rotate.inc

.. include-build-file:: inc/esp_gmf_video_types.inc

.. include-build-file:: inc/esp_gmf_video_param.inc
