GMF FourCC
====================

:link_to_translation:`en:[English]`

GMF FourCC（four-character code，四字符代码）是 ESP-GMF 的统一媒体格式描述符，用 4 个 ASCII 字符标识音频编码格式、视频编码格式、图像编码格式、像素格式与容器格式。它贯穿外设驱动、capture、处理单元（element）、编解码库与渲染链路，是各模块描述和协商媒体格式的共同语言。

读者可通过本篇查找特定格式对应的 FourCC，理解宏的构造方式，以及在自定义处理单元中如何正确使用 FourCC。能力描述（capability）机制见 :doc:`gmf-core-element`，格式信息上报流程见 :doc:`gmf-core-pipeline`。

下文表格中 4 字符列里的 ``␣`` 表示一个 ASCII 空格字符（\ ``0x20``\ ）。FourCC 固定占 4 个字符；当格式名称不足 4 个字符时，宏会用空格补齐，例如 ``MP3␣`` 表示 ``M``、\ ``P``\ 、\ ``3`` 和一个空格。

构造与转换宏
-------------------------------------

FourCC 在内存中是一个 32 位无符号整数，类型别名 ``esp_fourcc_t``\ 。框架提供两个宏完成数值与字符串的相互转换。

.. code:: c

    /* 由 4 个字符构造 FourCC */
    #define ESP_FOURCC_TO_INT(a, b, c, d) \
        ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

    /* 反向转字符串，得到 5 字节缓冲（含终止符） */
    char str[5];
    gmf_fourcc_to_str(ESP_FOURCC_MP3, str);   /* str = "MP3 " */

    /* 直接当作字符串用于日志 */
    ESP_LOGI(TAG, "format=%s", ESP_FOURCC_TO_STR(ESP_FOURCC_MP3));

字节序约定为：第一个字符存于最低字节。多数情况下读者只需直接使用预定义宏，无需自行拼接。

视频编码
-------------------------------------

.. list-table::
   :widths: 26 22 52
   :header-rows: 1

   * - 宏
     - 字符
     - 含义
   * - ``ESP_FOURCC_H263``
     - ``H263``
     - H.263
   * - ``ESP_FOURCC_H264``
     - ``H264``
     - H.264（带 start code）
   * - ``ESP_FOURCC_AVC1``
     - ``AVC1``
     - H.264（不带 start code）
   * - ``ESP_FOURCC_H265``
     - ``H265``
     - HEVC
   * - ``ESP_FOURCC_VP8``
     - ``VP8␣``
     - VP8
   * - ``ESP_FOURCC_MJPG``
     - ``MJPG``
     - Motion-JPEG

容器格式
-------------------------------------

.. list-table::
   :widths: 26 22 52
   :header-rows: 1

   * - 宏
     - 字符
     - 含义
   * - ``ESP_FOURCC_WAV``
     - ``WAV␣``
     - WAV
   * - ``ESP_FOURCC_MP4``
     - ``MP4␣``
     - MPEG-4
   * - ``ESP_FOURCC_TS``
     - ``TS␣␣``
     - MPEG-2 Transport Stream
   * - ``ESP_FOURCC_M2TS``
     - ``M2TS``
     - MPEG-2 Transport Stream（蓝光）
   * - ``ESP_FOURCC_FLV``
     - ``FLV1``
     - Flash Video
   * - ``ESP_FOURCC_AVI``
     - ``AVI␣``
     - AVI
   * - ``ESP_FOURCC_OGG``
     - ``OGG␣``
     - Ogg
   * - ``ESP_FOURCC_WEBM``
     - ``WEBM``
     - WebM
   * - ``ESP_FOURCC_CAF``
     - ``CAF␣``
     - Core Audio Format

图像编码
-------------------------------------

.. list-table::
   :widths: 26 22 52
   :header-rows: 1

   * - 宏
     - 字符
     - 含义
   * - ``ESP_FOURCC_PNG``
     - ``PNG␣``
     - PNG
   * - ``ESP_FOURCC_JPEG``
     - ``JPEG``
     - JPEG / JFIF
   * - ``ESP_FOURCC_GIF``
     - ``GIF␣``
     - GIF
   * - ``ESP_FOURCC_WEBP``
     - ``WEBP``
     - WebP
   * - ``ESP_FOURCC_BMP``
     - ``BMP␣``
     - Bitmap

音频编码
-------------------------------------

.. list-table::
   :widths: 28 18 54
   :header-rows: 1

   * - 宏
     - 字符
     - 含义
   * - ``ESP_FOURCC_MP2`` / ``ESP_FOURCC_MP3``
     - ``MP2␣`` / ``MP3␣``
     - MPEG Layer II / III
   * - ``ESP_FOURCC_AAC``
     - ``AAC␣``
     - Advanced Audio Codec
   * - ``ESP_FOURCC_FLAC``
     - ``FLAC``
     - Free Lossless Audio Codec
   * - ``ESP_FOURCC_M4A``
     - ``M4AA``
     - MPEG-4 Audio
   * - ``ESP_FOURCC_VORBIS``
     - ``VOBS``
     - Vorbis
   * - ``ESP_FOURCC_AMRNB`` / ``ESP_FOURCC_AMRWB``
     - ``AMRN`` / ``AMRW``
     - AMR 窄带 / 宽带
   * - ``ESP_FOURCC_ALAC``
     - ``ALAC``
     - Apple Lossless
   * - ``ESP_FOURCC_ALAW`` / ``ESP_FOURCC_ULAW``
     - ``ALAW`` / ``ULAW``
     - G.711 a-law / u-law
   * - ``ESP_FOURCC_ADPCM``
     - ``ADPC``
     - IMA-ADPCM
   * - ``ESP_FOURCC_PCM``
     - ``PCM␣``
     - 原始 PCM（音频元数据中常用）
   * - ``ESP_FOURCC_PCM_U08``
     - ``U08L``
     - 8 bit 无符号 PCM
   * - ``ESP_FOURCC_PCM_S16`` / ``S24`` / ``S32``
     - ``S16L`` / ``S24L`` / ``S32L``
     - 16/24/32 bit 有符号 PCM（小端）
   * - ``ESP_FOURCC_OPUS``
     - ``OPUS``
     - Raw Opus
   * - ``ESP_FOURCC_SPEEX``
     - ``SPEX``
     - Speex
   * - ``ESP_FOURCC_SBC``
     - ``SBC␣``
     - Sub-Band Codec（蓝牙）
   * - ``ESP_FOURCC_LC3``
     - ``LC30``
     - Low Complexity Communication Codec
   * - ``ESP_FOURCC_LYRA``
     - ``LYRA``
     - Lyra
   * - ``ESP_FOURCC_G722``
     - ``G722``
     - G.722

RGB 像素格式
-------------------------------------

.. list-table::
   :widths: 28 22 50
   :header-rows: 1

   * - 宏
     - 字符
     - 含义
   * - ``ESP_FOURCC_RGB15`` / ``BGR15``
     - ``RG15`` / ``BG15``
     - 16 bpp RGB-5-5-5 / BGR-5-5-5
   * - ``ESP_FOURCC_RGB16`` / ``BGR16``
     - ``RGBL`` / ``BGRL``
     - 16 bpp RGB-5-6-5 小端 / BGR 小端
   * - ``ESP_FOURCC_RGB16_BE`` / ``BGR16_BE``
     - ``RGBB`` / ``BGRB``
     - 16 bpp RGB-5-6-5 大端 / BGR 大端
   * - ``ESP_FOURCC_RGB24`` / ``BGR24``
     - ``RGB3`` / ``BGR3``
     - 24 bpp RGB-8-8-8 / BGR-8-8-8
   * - ``ESP_FOURCC_RGBA32`` / ``ARGB32``
     - ``RA24`` / ``AB24``
     - 32 bpp 含 alpha
   * - ``ESP_FOURCC_RGBX32`` / ``XRGB32``
     - ``RX24`` / ``XB24``
     - 32 bpp 含填充字节
   * - ``ESP_FOURCC_BGRA32`` / ``ABGR32``
     - ``BA24`` / ``AR24``
     - 32 bpp BGR 排列含 alpha
   * - ``ESP_FOURCC_BGRX32`` / ``XBGR32``
     - ``BX24`` / ``XR24``
     - 32 bpp BGR 排列含填充字节

每种格式的位排列在 ``esp_fourcc.h`` 中以注释给出，跨模块共享同一像素布局时务必对齐这些注释。

灰度与 YUV 像素格式
-------------------------------------

灰度（一类）：\ ``ESP_FOURCC_GREY``\ （8 bpp）、\ ``ESP_FOURCC_Y16`` / ``Y16_BE``\ （16 bpp 小端 / 大端）。

YUV 打包格式（4:2:2 / 4:4:4 等）：\ ``YUYV`` / ``YVYU`` / ``YYUV`` / ``UYVY`` / ``VYUY``\ 、\ ``YUV``\ （V308）、\ ``UYV``\ （IYU2）、\ ``VUY``\ （VUY4）。

Espressif 自定义打包格式：\ ``ESP_FOURCC_OUYY_EVYY``\ 、\ ``YUY2YVY2``\ 、\ ``YUYYVY``\ ，对应硬件 LCD/CAM 输出的特殊排列。

半平面（NV 系列）：\ ``NV12`` / ``NV21``\ （4:2:0）、\ ``NV16`` / ``NV61``\ （4:2:2）、\ ``NV24`` / ``NV42``\ （4:4:4）。以 ``M`` 结尾的变体（\ ``NV12M`` 等）表示多平面、跨多个内存块。

平面（Planar）：\ ``YUV410P`` / ``YVU410P``\ 、\ ``YUV411P``\ 、\ ``YUV420P`` / ``YVU420P``\ 、\ ``YUV422P``\ 、\ ``YUV444P``\ 。同样有 ``M`` 结尾的多平面变体。

每种格式具体的字节布局在 ``esp_fourcc.h`` 的注释中以位图形式给出，跨芯片共享数据时需逐字段对照。

CMYK 与 RAW 格式
-------------------------------------

CMYK：\ ``ESP_FOURCC_CMYK``\ （32 bpp）。

相机原始数据：\ ``ESP_FOURCC_RAW8`` / ``RAW10`` / ``RAW12`` / ``RAW16``\ ，对应不同位深的 Bayer 原始格式。

辅助格式
-------------------------------------

亮度单通道：\ ``ESP_FOURCC_LUM4`` / ``LUM8``\ 。

透明度单通道：\ ``ESP_FOURCC_ALPHA4`` / ``ALPHA8``\ 。

FourCC 的应用场景
-------------------------------------

FourCC 在 GMF 中不是某一个结构体的专属字段，而是跨模块使用的格式描述语言。典型场景包括：

- **处理单元格式上报**\ ：处理单元在 ``open`` 或 ``process`` 中解析到具体格式后，把 FourCC 填入 ``format_id`` 并通过 notify 接口上报。下游依赖型处理单元收到格式信息后再初始化。
- **能力描述属性**\ ：能力描述（capability）使用 EIGHTCC 描述能力类别，例如音频解码、视频缩放、颜色转换等；具体输入/输出格式可通过能力描述属性或格式信息中的 ``format_id`` 描述。能力描述的完整机制见 :doc:`gmf-core-element`。
- **编解码库与硬件驱动适配**\ ：视频编解码、JPEG/MJPEG、H.264 等驱动和编解码库使用 FourCC 区分输入输出格式，处理单元据此选择目标 codec、像素格式或封装格式。
- **capture 与渲染链路**\ ：capture 的 sink 配置、视频渲染和图像处理链路通过 ``format_id`` 传递目标音视频格式，使采集、处理和输出模块使用同一套格式标识。

音频格式信息上报示例：

.. code:: c

    esp_gmf_info_sound_t snd = {
        .format_id    = ESP_FOURCC_PCM,
        .sample_rates = 48000,
        .channels     = 2,
        .bits         = 16,
    };
    esp_gmf_element_notify_snd_info(handle, &snd);

视频格式信息也使用同样的字段：

.. code:: c

    esp_gmf_info_video_t vid = {
        .format_id = ESP_FOURCC_H264,
        .width     = 1280,
        .height    = 720,
        .fps       = 30,
    };

新增 GMF FourCC 标准
-------------------------------------

新增 FourCC 时遵循以下约定：

- 优先保证字符可读、语义可理解，使日志和调试输出能直接反映格式含义
- 使用 4 个 ASCII 可见字符，不足 4 个用空格 ``' '`` 或数字字符补齐
- 命名上尽量与业内通行写法保持一致（如 ``MP3 ``\ 、\ ``H264``\ 、\ ``OPUS``\ ）
- 避免与已有定义冲突，新增前在 ``esp_fourcc.h`` 中搜索同名宏
- 如果实现需要区分变体（如带/不带 start code），不要复用同一 FourCC，而是新定义一个（参考 ``H264`` 与 ``AVC1``\ ）

API 参考
-------------------------------------

**Header File**

- `gmf_core/helpers/include/esp_fourcc.h <https://github.com/espressif/esp-gmf/blob/main/gmf_core/helpers/include/esp_fourcc.h>`_

``esp_fourcc.h`` 提供以下接口：

- ``ESP_FOURCC_TO_INT(a, b, c, d)``\ ：编译期宏，将四个字符常量打包为一个 32 位整数，用于定义或比较 FourCC 值。
- ``ESP_FOURCC_TO_STR(fourcc)``\ ：编译期宏，将 32 位 FourCC 整数还原为可读的四字符字符串，常用于日志打印。
- ``gmf_fourcc_to_str(fourcc, buf)``\ ：运行期函数，将 FourCC 整数写入调用方提供的缓冲区并返回字符串指针，适合在动态场景下格式化输出。

各音视频与编解码格式对应的 FourCC 宏定义及说明，详见 ``esp_fourcc.h`` 文件注释。
