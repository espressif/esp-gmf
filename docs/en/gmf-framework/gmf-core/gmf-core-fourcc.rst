GMF FourCC
==========

:link_to_translation:`zh_CN:[中文]`

GMF FourCC (four-character code) is the unified media format descriptor for ESP-GMF, using 4 ASCII characters to identify audio encoding formats, video encoding formats, image encoding formats, pixel formats, and container formats. It spans peripheral drivers, capture, elements, codec libraries, and the rendering pipeline, serving as the common language for all modules to describe and negotiate media formats.

Readers can use this document to look up the FourCC for a specific format, understand how the macros are constructed, and learn how to correctly use FourCC in custom elements. For the capability description mechanism, see :doc:`gmf-core-element`; for the format info reporting flow, see :doc:`gmf-core-pipeline`.

In the tables below, ``␣`` in the 4-character column represents an ASCII space character (``0x20``). FourCC is always 4 characters; when a format name is shorter than 4 characters, the macro pads it with spaces, e.g., ``MP3␣`` represents ``M``, ``P``, ``3``, and one space.

Constructing and Converting FourCC
----------------------------------

A FourCC is a 32-bit unsigned integer in memory, with the type alias ``esp_fourcc_t``. The framework provides two macros for mutual conversion between values and strings.

.. code:: c

    /* Construct a FourCC from 4 characters */
    #define ESP_FOURCC_TO_INT(a, b, c, d) \
        ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

    /* Convert back to a string, producing a 5-byte buffer (including null terminator) */
    char str[5];
    gmf_fourcc_to_str(ESP_FOURCC_MP3, str);   /* str = "MP3 " */

    /* Use directly as a string for logging */
    ESP_LOGI(TAG, "format=%s", ESP_FOURCC_TO_STR(ESP_FOURCC_MP3));

The byte order convention is: the first character is stored in the lowest byte. In most cases, readers only need to use the predefined macros without assembling them manually.

Video Encoding
--------------

.. list-table::
   :widths: 26 22 52
   :header-rows: 1

   * - Macro
     - Characters
     - Description
   * - ``ESP_FOURCC_H263``
     - ``H263``
     - H.263
   * - ``ESP_FOURCC_H264``
     - ``H264``
     - H.264 (with start code)
   * - ``ESP_FOURCC_AVC1``
     - ``AVC1``
     - H.264 (without start code)
   * - ``ESP_FOURCC_H265``
     - ``H265``
     - HEVC
   * - ``ESP_FOURCC_VP8``
     - ``VP8␣``
     - VP8
   * - ``ESP_FOURCC_MJPG``
     - ``MJPG``
     - Motion-JPEG

Container Formats
-----------------

.. list-table::
   :widths: 26 22 52
   :header-rows: 1

   * - Macro
     - Characters
     - Description
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
     - MPEG-2 Transport Stream (Blu-ray)
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

Image Encoding
--------------

.. list-table::
   :widths: 26 22 52
   :header-rows: 1

   * - Macro
     - Characters
     - Description
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

Audio Encoding
--------------

.. list-table::
   :widths: 28 18 54
   :header-rows: 1

   * - Macro
     - Characters
     - Description
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
     - AMR Narrowband / Wideband
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
     - Raw PCM (commonly used in audio metadata)
   * - ``ESP_FOURCC_PCM_U08``
     - ``U08L``
     - 8-bit unsigned PCM
   * - ``ESP_FOURCC_PCM_S16`` / ``S24`` / ``S32``
     - ``S16L`` / ``S24L`` / ``S32L``
     - 16/24/32-bit signed PCM (little-endian)
   * - ``ESP_FOURCC_OPUS``
     - ``OPUS``
     - Raw Opus
   * - ``ESP_FOURCC_SPEEX``
     - ``SPEX``
     - Speex
   * - ``ESP_FOURCC_SBC``
     - ``SBC␣``
     - Sub-Band Codec (Bluetooth)
   * - ``ESP_FOURCC_LC3``
     - ``LC30``
     - Low Complexity Communication Codec
   * - ``ESP_FOURCC_LYRA``
     - ``LYRA``
     - Lyra
   * - ``ESP_FOURCC_G722``
     - ``G722``
     - G.722

RGB Pixel Formats
-----------------

.. list-table::
   :widths: 28 22 50
   :header-rows: 1

   * - Macro
     - Characters
     - Description
   * - ``ESP_FOURCC_RGB15`` / ``BGR15``
     - ``RG15`` / ``BG15``
     - 16 bpp RGB-5-5-5 / BGR-5-5-5
   * - ``ESP_FOURCC_RGB16`` / ``BGR16``
     - ``RGBL`` / ``BGRL``
     - 16 bpp RGB-5-6-5 little-endian / BGR little-endian
   * - ``ESP_FOURCC_RGB16_BE`` / ``BGR16_BE``
     - ``RGBB`` / ``BGRB``
     - 16 bpp RGB-5-6-5 big-endian / BGR big-endian
   * - ``ESP_FOURCC_RGB24`` / ``BGR24``
     - ``RGB3`` / ``BGR3``
     - 24 bpp RGB-8-8-8 / BGR-8-8-8
   * - ``ESP_FOURCC_RGBA32`` / ``ARGB32``
     - ``RA24`` / ``AB24``
     - 32 bpp with alpha
   * - ``ESP_FOURCC_RGBX32`` / ``XRGB32``
     - ``RX24`` / ``XB24``
     - 32 bpp with padding byte
   * - ``ESP_FOURCC_BGRA32`` / ``ABGR32``
     - ``BA24`` / ``AR24``
     - 32 bpp BGR layout with alpha
   * - ``ESP_FOURCC_BGRX32`` / ``XBGR32``
     - ``BX24`` / ``XR24``
     - 32 bpp BGR layout with padding byte

The bit arrangement for each format is given as comments in ``esp_fourcc.h``; these must be aligned when sharing pixel layouts across modules.

Grayscale and YUV Pixel Formats
-------------------------------

Grayscale (one type): ``ESP_FOURCC_GREY`` (8 bpp), ``ESP_FOURCC_Y16`` / ``Y16_BE`` (16 bpp little-endian / big-endian).

YUV packed formats (4:2:2 / 4:4:4, etc.): ``YUYV`` / ``YVYU`` / ``YYUV`` / ``UYVY`` / ``VYUY``, ``YUV`` (V308), ``UYV`` (IYU2), ``VUY`` (VUY4).

Espressif custom packed formats: ``ESP_FOURCC_OUYY_EVYY``, ``YUY2YVY2``, ``YUYYVY``, corresponding to special arrangements output by hardware LCD/CAM.

Semi-planar (NV series): ``NV12`` / ``NV21`` (4:2:0), ``NV16`` / ``NV61`` (4:2:2), ``NV24`` / ``NV42`` (4:4:4). Variants ending in ``M`` (such as ``NV12M``) indicate multi-planar, spanning multiple memory blocks.

Planar: ``YUV410P`` / ``YVU410P``, ``YUV411P``, ``YUV420P`` / ``YVU420P``, ``YUV422P``, ``YUV444P``. There are also multi-planar variants ending in ``M``.

The specific byte layout for each format is given in bitmap form as comments in ``esp_fourcc.h``; these must be checked field by field when sharing data across chips.

CMYK and RAW Formats
--------------------

CMYK: ``ESP_FOURCC_CMYK`` (32 bpp).

Camera raw data: ``ESP_FOURCC_RAW8`` / ``RAW10`` / ``RAW12`` / ``RAW16``, corresponding to Bayer raw formats of different bit depths.

Auxiliary Formats
-----------------

Luminance single channel: ``ESP_FOURCC_LUM4`` / ``LUM8``.

Alpha single channel: ``ESP_FOURCC_ALPHA4`` / ``ALPHA8``.

Application Scenarios of FourCC
-------------------------------

FourCC in GMF is not an exclusive field of a single struct, but a format description language used across modules. Typical scenarios include:

- **Element format reporting**: After an element parses the specific format in ``open`` or ``process``, it fills FourCC into ``format_id`` and reports via the notify interface. Downstream dependent elements initialize after receiving the format info.
- **Capability description attributes**: Capability description uses EIGHTCC to describe capability categories, such as audio decoding, video scaling, and color conversion; specific input/output formats can be described via capability attributes or the ``format_id`` in format info. For the complete capability description mechanism, see :doc:`gmf-core-element`.
- **Codec library and hardware driver adaptation**: Video codecs, JPEG/MJPEG, H.264, and other drivers and codec libraries use FourCC to distinguish input/output formats; elements use it to select the target codec, pixel format, or container format.
- **Capture and rendering pipeline**: The sink configuration in capture, video rendering, and image processing pipelines pass the target audio/video format via ``format_id``, ensuring that capture, processing, and output modules use the same set of format identifiers.

Audio format info reporting example:

.. code:: c

    esp_gmf_info_sound_t snd = {
        .format_id    = ESP_FOURCC_PCM,
        .sample_rates = 48000,
        .channels     = 2,
        .bits         = 16,
    };
    esp_gmf_element_notify_snd_info(handle, &snd);

Video format info also uses the same fields:

.. code:: c

    esp_gmf_info_video_t vid = {
        .format_id = ESP_FOURCC_H264,
        .width     = 1280,
        .height    = 720,
        .fps       = 30,
    };

Guidelines for Adding New GMF FourCC
------------------------------------

When adding a new FourCC, follow these conventions:

- Prioritize readability and semantic clarity so that log and debug output directly reflects the format meaning.
- Use 4 visible ASCII characters; if the name is shorter than 4 characters, pad with spaces ``' '`` or numeric characters.
- Align naming with industry-standard conventions as much as possible (e.g., ``MP3 ``, ``H264``, ``OPUS``).
- Avoid conflicts with existing definitions; search for the same macro name in ``esp_fourcc.h`` before adding.
- If the implementation needs to distinguish variants (such as with/without start code), do not reuse the same FourCC; define a new one instead (refer to ``H264`` and ``AVC1``).

API Reference
-------------

**Header File**

- `gmf_core/helpers/include/esp_fourcc.h <https://github.com/espressif/esp-gmf/blob/main/gmf_core/helpers/include/esp_fourcc.h>`_

``esp_fourcc.h`` provides the following interfaces:

- ``ESP_FOURCC_TO_INT(a, b, c, d)``: A compile-time macro that packs four character constants into a 32-bit integer, for defining or comparing FourCC values.
- ``ESP_FOURCC_TO_STR(fourcc)``: A compile-time macro that converts a 32-bit FourCC integer back to a readable four-character string, commonly used for log output.
- ``gmf_fourcc_to_str(fourcc, buf)``: A runtime function that writes the FourCC integer into a caller-provided buffer and returns a string pointer, suitable for formatted output in dynamic scenarios.

For FourCC macro definitions and descriptions of each audio/video and codec format, see the ``esp_fourcc.h`` file comments.
