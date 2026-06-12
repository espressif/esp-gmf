GMF-Video
=========

:link_to_translation:`zh_CN:[中文]`

gmf_video is the video processing component of ESP-GMF, providing 9 elements covering codec, pixel accelerator, software image effects, and frame rate / overlay tools. All elements inherit from ``esp_gmf_video_element_t``, exposing unified lifecycle functions, acquire-release data interfaces, and runtime method calls. The underlying algorithms are implemented by ESP32-P4's hardware PPA / 2D-DMA, the ``esp_video_codec`` library, and the ``esp_image_effects`` software image effects library. This document covers the purpose, configuration, and runtime control of each element by category. For the element base class and runtime method mechanism, see :doc:`/gmf-framework/gmf-core/gmf-core-element`; for the data path, see :doc:`/gmf-framework/gmf-core/gmf-core-data-path`.

Feature List
------------

- vid_dec: video decoding, supporting H.264 and MJPEG; output pixel format can be specified (e.g., YUV420P, RGB565LE)
- vid_enc: video encoding, supporting H.264 and MJPEG; bitrate, GOP and QP range(H.264 only) can be adjusted at runtime
- vid_ppa: ESP32-P4 pixel processing accelerator composite element, merging color conversion, scaling, cropping, and rotation into a single hardware pass
- vid_fps_cvt: frame rate conversion, dropping frames by PTS to reduce input frame rate to a specified output frame rate
- vid_overlay: overlay mixer, blending additional content (watermarks, UI, timestamps) onto the original video via alpha blending or transparent-color (colorkey) compositing
- vid_color_cvt: software color conversion, covering common format conversions such as YUYV, RGB565, RGB888, and YUV420P
- vid_crop: software cropping, extracting a specified rectangular region from the original frame
- vid_scale: software scaling, resampling video frames to a target resolution
- vid_rotate: software rotation, supporting arbitrary angles (in degrees)
- Common: bypass optimization, alignment auto-query, ``need_recfg``-triggered runtime reconfiguration, PTS pass-through and frame drop logic

Technical Details
-----------------

Element Hierarchy and Common Mechanisms
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Each specific element is constructed by "wrapping an underlying video algorithm / hardware handle": the derived struct uses ``esp_gmf_video_element_t`` as its first field, binding the codec / PPA / imgfx handle and this element's configuration; ``open`` creates the underlying handle and reports the output ``esp_gmf_info_video_t`` (width, height, frame rate, pixel format FourCC) to the pipeline; ``process`` fetches a frame from the input port at each scheduling interval, performs the conversion, and writes to the output port; ``close`` releases hardware resources.

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

Four common mechanisms:

- **Bypass optimization**: vid_dec enters zero-copy pass-through when source and destination formats are identical; software effect elements also set bypass when source and destination parameters are identical, directly passing the input payload to the output port.
- **need_recfg reconfiguration**: All setters (``set_dst_format`` / ``set_dst_resolution`` / ``set_angle``, etc.) set the ``need_recfg`` flag after a change; at the next ``process`` boundary, the old handle is destroyed and a new one is rebuilt, allowing resolution or pixel format changes at runtime without the application rebuilding the element.
- **Buffer alignment auto-query**: Underlying hardware (PPA, H.264 encoder) typically has alignment requirements for input/output buffers; the element queries alignment parameters from the hardware driver at ``open`` and writes them to the port attr so the framework allocates payloads with the correct alignment, preventing hardware from rejecting processing.
- **Format info reporting**: After the upstream element resolves the width/height / pixel format of the current frame, it reports via the ``REPORT_INFO`` event; the pipeline delivers the info to downstream elements, which open or trigger reconfiguration accordingly. For details see the REPORT_INFO description in :doc:`/gmf-framework/gmf-core/gmf-core-pipeline`.

Video Codec
^^^^^^^^^^^

vid_dec and vid_enc are the start or end elements of a video pipeline; a typical assembly is as follows.

.. only:: html

   .. mermaid::

      flowchart LR
          subgraph rec ["Recording Pipeline"]
              direction LR
              CapIn(("camera/raw")) --> CC1["vid_color_cvt"]
              CC1 --> Scale["vid_scale"]
              Scale --> Enc["vid_enc"]
              Enc --> FileOut(("file/http"))
          end
          subgraph play ["Playback Pipeline"]
              direction LR
              FileIn(("file/http")) --> Dec["vid_dec"]
              Dec --> PPA["vid_ppa"]
              PPA --> Disp(("display"))
          end

**vid_dec**: Decodes compressed streams to raw pixels. :cpp:func:`esp_gmf_video_dec_init` accepts :cpp:type:`esp_gmf_video_dec_cfg_t`; the ``codec_cc`` field uses FourCC to specify the decoder implementation; when zero, the framework automatically selects based on available hardware / software decoders.

Use :cpp:func:`esp_gmf_video_dec_get_dst_formats` to query the list of available output pixel formats for a given source codec (H.264 / MJPEG, etc.), then use :cpp:func:`esp_gmf_video_dec_set_dst_format` to fix the output format before starting the element. If the source and destination formats are identical (rare but useful, e.g., a remux-only pipeline), vid_dec automatically enters bypass mode and forwards payloads directly.

.. code:: c

    esp_gmf_video_dec_cfg_t cfg = { .codec_cc = 0 };  /* Auto select */
    esp_gmf_element_handle_t dec = NULL;
    esp_gmf_video_dec_init(&cfg, &dec);

    const uint32_t *fmts = NULL;
    uint8_t fmt_num = 0;
    esp_gmf_video_dec_get_dst_formats(dec, ESP_FOURCC_H264, &fmts, &fmt_num);
    esp_gmf_video_dec_set_dst_format(dec, fmts[0]);

**vid_enc**: Encodes raw pixels to a compressed stream. :cpp:func:`esp_gmf_video_enc_init` accepts :cpp:type:`esp_gmf_video_enc_cfg_t`; ``codec_cc`` specifies the encoder implementation.

The normal workflow is for the element to obtain source info (``esp_gmf_info_video_t``) from upstream after startup, then auto-select the specific implementation based on the target codec specified by ``set_dst_codec`` and open. To query encoder capabilities before startup (e.g., query supported source pixel formats), first call :cpp:func:`esp_gmf_video_enc_preset` with the source info and target codec, then call :cpp:func:`esp_gmf_video_enc_get_src_formats` and :cpp:func:`esp_gmf_video_enc_get_out_size`.

Three parameters can be adjusted at runtime (H.264 encoder additionally supports the last two):

.. code:: c

    esp_gmf_video_enc_set_bitrate(enc, 2 * 1000 * 1000);  /* 2 Mbps */
    esp_gmf_video_enc_set_gop(enc, 30);                    /* I-frame every 30 frames */
    esp_gmf_video_enc_set_qp(enc, 20, 40);                 /* QP range */

Output buffer size is estimated by :cpp:func:`esp_gmf_video_enc_get_out_size`, using a 10:1 compression ratio upper bound for MJPEG and 2:1 for H.264; the framework allocates output payloads at this size.

Hardware-Accelerated PPA
^^^^^^^^^^^^^^^^^^^^^^^^

**vid_ppa** is supported on ESP32-P4/ESP32S31 only, merging color conversion, scaling, cropping, and rotation into a single hardware pass. :cpp:func:`esp_gmf_video_ppa_init` requires no configuration; four setters configure the target format, target resolution, crop region, and rotation angle (only 0 / 90 / 180 / 270 degrees are supported). These setters can only be called before the element starts; ``open`` submits the combined parameters to hardware in one shot.

PPA internally selects the hardware path based on the current request: pure pixel format conversion can be completed by 2D-DMA with higher throughput; scaling, rotation, or format combinations not supported by 2D-DMA fall back to the main PPA path. Path selection is handled internally; the element only exposes a unified interface.

.. code:: c

    esp_gmf_element_handle_t ppa = NULL;
    esp_gmf_video_ppa_init(NULL, &ppa);

    esp_gmf_video_resolution_t dst_res = { .width = 480, .height = 320 };
    esp_gmf_video_ppa_set_dst_resolution(ppa, &dst_res);
    esp_gmf_video_ppa_set_dst_format(ppa, ESP_FOURCC_RGB16);
    esp_gmf_video_ppa_set_rotation(ppa, 90);

    esp_gmf_video_rgn_t crop = { .x = 0, .y = 0, .width = 800, .height = 600 };
    esp_gmf_video_ppa_set_cropped_rgn(ppa, &crop);

On non-ESP32-P4 chips, vid_ppa is unavailable; use the four software effect elements in series (vid_color_cvt → vid_crop → vid_scale → vid_rotate) for equivalent functionality, at the cost of increased CPU usage.

Software Image Effects
^^^^^^^^^^^^^^^^^^^^^^

The four software effect elements wrap the corresponding algorithms in the ``esp_image_effects`` library, exposing only their respective setters. All effects follow the same path: ``acquire_in`` to get the input frame, generate output frame size based on configuration, ``acquire_out`` to get the output frame, call ``esp_imgfx_*_process`` to process, then release both.

**vid_color_cvt**: Pixel format conversion, e.g., RGB565 → YUV420P, YUYV → RGB888. The color space standard (BT601 / BT709) is selected via the ``esp_imgfx_color_convert_cfg_t.color_space_std`` field at initialization. :cpp:func:`esp_gmf_video_color_convert_dst_format` switches the target format at runtime, setting ``need_recfg``.

**vid_crop**: Crop element. Initialized with ``DEFAULT_ESP_GMF_CROP_CONFIG`` for a default 320×240 input and 160×120 crop region. Use :cpp:func:`esp_gmf_video_crop_rgn` at runtime to switch the crop rectangle (:cpp:type:`esp_gmf_video_rgn_t` provides x / y / width / height).

**vid_scale**: Scale element. The ``filter_type`` field determines the algorithm (e.g., ``ESP_IMGFX_SCALE_FILTER_TYPE_DOWN_RESAMPLE``). Use :cpp:func:`esp_gmf_video_scale_dst_resolution` at runtime to change the target resolution.

**vid_rotate**: Rotate element. Unlike vid_ppa which only supports four angles, software rotation supports arbitrary angles (in degrees), suitable for tilted display or image correction scenarios. :cpp:func:`esp_gmf_video_rotate_set_rotation` switches the angle at runtime.

.. code:: c

    esp_imgfx_scale_cfg_t cfg = DEFAULT_ESP_GMF_SCALE_CONFIG();
    cfg.in_res       = (esp_imgfx_res_t){ .width = 640, .height = 480 };
    cfg.in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE;
    cfg.scale_res    = (esp_imgfx_res_t){ .width = 320, .height = 240 };

    esp_gmf_element_handle_t scale = NULL;
    esp_gmf_video_scale_init(&cfg, &scale);

Frame Rate and Overlay
^^^^^^^^^^^^^^^^^^^^^^

**vid_fps_cvt**: Reduces input frame rate to a specified output frame rate by dropping frames. The element maintains a start PTS and accumulated output frame count, computing ``expected_pts`` at ``1 / fps`` steps. For each arriving frame: the frame is passed to downstream and the count is incremented only if its PTS is >= ``expected_pts``; otherwise it is dropped via ``release_in``.

Since pure frame dropping cannot upsample, the value set by ``set_fps`` must be lower than the upstream frame rate; otherwise it has no effect.

.. code:: c

    esp_gmf_element_handle_t fps = NULL;
    esp_gmf_video_fps_cvt_init(NULL, &fps);
    esp_gmf_video_fps_cvt_set_fps(fps, 15);  /* Reduce from 30 fps to 15 fps */

**vid_overlay**: Overlays additional content onto the main video. The element has multiple inputs: the main video enters through the default in port, and the overlay layer enters through the extra port registered by the user via :cpp:func:`esp_gmf_video_overlay_set_overlay_port`. Compositing supports alpha blending and transparent-color (colorkey) modes. Alpha blending formula:

::

    Output = Original × (255 − alpha) + Overlay × alpha

``set_rgn`` specifies the position and format of the overlay layer on the main frame (:cpp:type:`esp_gmf_overlay_rgn_info_t` contains ``format_id``, ``dst_rgn``, and optional ``has_trans_color`` / ``trans_color`` for colorkey compositing—pixels near the specified RGB value are treated as transparent); ``set_alpha`` changes transparency at runtime (0 fully transparent, 255 fully opaque); :cpp:func:`esp_gmf_video_overlay_enable`\(false) temporarily disables the overlay, the element no longer acquires the overlay port, and the main frame passes through unchanged.

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

Performance
-----------

Video processing throughput is primarily determined by the underlying hardware / algorithm; the overhead of element scheduling and acquire-release is relatively small. By bottleneck category:

.. list-table::
   :widths: 20 24 56
   :header-rows: 1

   * - Category
     - Main Bottleneck
     - Optimization Direction
   * - vid_dec / vid_enc
     - Throughput of hardware codec or software library
     - ESP32-P4 supports H.264 hardware encoding; call ``preset`` early to let the framework pre-allocate buffers
   * - vid_ppa
     - PPA / 2D-DMA clock and buffer bandwidth
     - Merge adjacent pixel operations into the same vid_ppa instance to reduce intermediate buffers
   * - Software effects
     - CPU and memory bandwidth
     - Prefer vid_ppa on ESP32-P4; control resolution to within SoC processing capacity for other SoCs
   * - vid_fps_cvt
     - Only PTS comparison and frame dropping; very low CPU overhead
     - Suitable for placement before encoding or display to reduce downstream load
   * - vid_overlay
     - CPU cycles for alpha blending or colorkey compositing
     - Smaller overlay region and fewer bytes per pixel format means faster processing

Application Examples
--------------------

- ``elements/test_apps/main/elements/gmf_video_el_test.c``: Typical video element test cases, including ``gen_pattern_color_bar`` for synthesizing test frames and format reporting validation at the open stage
- ``elements/test_apps/main/elements/gmf_image_effects_test.c``: Image effects pipeline test, covering chaining of software effect elements

The upper-level application components ``esp_video_render`` and ``esp_capture`` also connect video pipelines through this component and can serve as advanced references.

SoC Compatibility
-----------------

Support matrix of elements on Espressif SoCs:

.. list-table::
   :widths: 22 18 18 18 18
   :header-rows: 1

   * - element
     - ESP32
     - ESP32-S2
     - ESP32-S3
     - ESP32-P4
   * - vid_ppa
     - Not supported
     - Not supported
     - Not supported
     - Supported
   * - vid_fps_cvt
     - Supported
     - Supported
     - Supported
     - Supported
   * - vid_overlay
     - Supported
     - Supported
     - Supported
     - Supported
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
     - Supported
     - Supported
     - Supported
     - Supported

ESP32 and ESP32-S2 do not support H.264. On ESP32-S3, H.264 encode and decode are implemented in software. On ESP32-P4, H.264 hardware encoding and MJPEG hardware encode/decode are supported; H.264 decoding is implemented in software. For ESP32-P4 PPA acceleration, omit the software effect elements and use vid_ppa instead.

FAQ
---

**Q:** How do I select the output format for vid_dec?

First use :cpp:func:`esp_gmf_video_dec_get_dst_formats` to query the list of output formats supported by the current source codec, then select a format that meets the input requirements of the downstream element (e.g., vid_ppa needs RGB565, display needs RGB888), and call :cpp:func:`esp_gmf_video_dec_set_dst_format` to fix the output format before starting the element. Output frame size queries require the target format to be set first, or the corresponding query interface to be called after the element starts.

**Q:** Cannot query output frame size before vid_enc starts?

Normally the encoder can only determine the output size after obtaining ``esp_gmf_info_video_t`` from upstream. To query before startup, first call :cpp:func:`esp_gmf_video_enc_preset` with the expected source info and target codec; then :cpp:func:`esp_gmf_video_enc_get_out_size` can return a value.

**Q:** Is vid_ppa supported on ESP32?

No. vid_ppa depends on ESP32-P4 hardware PPA / 2D-DMA. On other SoCs, use the software implementations vid_color_cvt / vid_scale / vid_rotate / vid_crop instead; resolution and frame rate should be constrained according to actual CPU capability.

**Q:** What happens if the frame rate set in vid_fps_cvt is higher than upstream?

The element does not insert frames. If ``set_fps`` is higher than the upstream frame rate, every frame's PTS will be >= ``expected_pts``, all frames pass through, and the output frame rate is limited by upstream. The configuration takes effect only when reducing the frame rate; it must not exceed the upstream rate.

**Q:** How are the two inputs of vid_overlay synchronized?

The main input is a regular GMF input port; the overlay input is the extra port registered by the user via ``set_overlay_port``. The element acquires the main input first then the overlay input in each ``process``; the application is responsible for ensuring the two streams are frame-aligned (e.g., two upstream elements in the same pipeline producing frames synchronously); otherwise overlay frames may lag or jitter.

**Q:** How to choose between software effect elements and vid_ppa on ESP32-P4?

When combining color conversion + scaling + rotation / cropping, prefer vid_ppa: a single hardware pass completes all conversions with low CPU usage and stable latency. Use the corresponding software elements when only a single operation is needed, or when the angle / pixel format required is not supported by PPA.

API Reference
-------------

Header files for this component:

- ``esp_gmf_video_dec.h`` / ``esp_gmf_video_enc.h``: Video codec elements
- ``esp_gmf_video_ppa.h``: Pixel processing accelerator
- ``esp_gmf_video_fps_cvt.h``: Frame rate conversion
- ``esp_gmf_video_overlay.h``: Overlay mixer
- ``esp_gmf_video_color_convert.h`` / ``esp_gmf_video_crop.h`` / ``esp_gmf_video_scale.h`` / ``esp_gmf_video_rotate.h``: Software image effects
- ``esp_gmf_video_types.h``: Shared types for video resolution / region / overlay configuration
- ``esp_gmf_video_param.h``: Video parameter helper interface

The interface for the element base class ``esp_gmf_video_element_t`` is located in :doc:`/gmf-framework/gmf-core/gmf-core-element`.

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
