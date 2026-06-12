System Utilities
================

:link_to_translation:`zh_CN:[中文]`

GMF-Core provides two groups of general-purpose modules outside the main framework: the OS abstraction layer (OAL) and helpers. The former consolidates OS/IDF-related system calls into a unified interface for compatibility across different versions; the latter provides small utilities such as URI parsing and EIGHTCC capability identifiers, for reuse by elements and upper-layer applications.

This document covers the usage interfaces of both groups. For the main framework objects (pipeline, element, payload, port), see :doc:`gmf-core-pipeline`, :doc:`gmf-core-element`, and :doc:`gmf-core-data-path`.

OS Abstraction Layer
--------------------

Located in the ``oal/`` directory. Any GMF code involving memory, mutexes, threads, and other system calls accesses them through OAL rather than calling FreeRTOS or IDF APIs directly. OAL internally routes to the concrete implementation based on "target platform + compile switches," so the same framework code can run on ESP-IDF while leaving an abstraction layer for future migration to other RTOSes.

Memory Allocation
^^^^^^^^^^^^^^^^^

``esp_gmf_oal_malloc`` / ``esp_gmf_oal_calloc`` / ``esp_gmf_oal_realloc`` share the same names and semantics as the standard C interfaces, with one difference: when PSRAM is enabled, the framework selects internal RAM or PSRAM based on strategy without requiring application code to make the decision. :cpp:func:`esp_gmf_oal_calloc_inner` forces allocation in internal RAM, for latency-sensitive scenarios.

.. code:: c

    void *buf = esp_gmf_oal_calloc(1, size);
    if (!buf) {
        return ESP_GMF_ERR_MEMORY_LACK;
    }
    /* ... */
    esp_gmf_oal_free(buf);

For aligned allocation, use :cpp:func:`esp_gmf_oal_malloc_align`; ``align`` must be a power of 2 or 0.

PSRAM Status
^^^^^^^^^^^^

Three query interfaces allow code to sense PSRAM availability and alignment requirements:

- :cpp:func:`esp_gmf_oal_mem_spiram_is_enabled`: whether PSRAM is enabled in the current firmware
- :cpp:func:`esp_gmf_oal_mem_spiram_stack_is_enabled`: whether placing task stacks in PSRAM is allowed
- :cpp:func:`esp_gmf_oal_get_spiram_cache_align`: the alignment in bytes required by the PSRAM cache

When a custom element needs DMA across the PSRAM boundary, use :cpp:func:`esp_gmf_oal_get_spiram_cache_align` to retrieve the alignment value and fill it into the ``buf_addr_aligned`` / ``buf_size_aligned`` fields of ``ESP_GMF_ELEMENT_*_PORT_ATTR_SET``, to avoid cache coherency issues.

Mutex and Thread
^^^^^^^^^^^^^^^^

OAL wraps FreeRTOS mutex and task interfaces into a more concise API:

- ``esp_gmf_oal_mutex_create`` / ``lock`` / ``unlock`` / ``destroy``: recursive mutex
- ``esp_gmf_oal_thread_create`` / ``delete``: task creation (``delete`` accepts a NULL handle)

The thread interface takes an :cpp:type:`esp_gmf_oal_thread_t` configuration (stack size, priority, CPU core, whether to use external SPI RAM stack), consistent with the ``esp_gmf_task_config_t`` fields nested in ``esp_gmf_task_cfg_t``. When integrating tasks and pipelines, the OAL thread interface is generally not called directly; it is used indirectly via :cpp:func:`esp_gmf_task_init`.

Debug Helpers
^^^^^^^^^^^^^

``ESP_GMF_MEM_SHOW(tag)`` prints heap memory status at the current location. ``esp_gmf_oal_sys_get_real_time_stats`` outputs runtime task statistics, useful for diagnosing insufficient stack or high CPU usage. Both interfaces automatically embed the "call-site information" into the log via macros or functions, so line numbers do not need to be filled in manually during debugging.

URI Parsing
-----------

``esp_gmf_uri_parser`` splits a ``scheme://host[:port]/path?query`` format string into individual fields; IO elements use this to determine which specific implementation to select. Typical usage:

.. code:: c

    esp_gmf_uri_t info = { 0 };
    esp_gmf_uri_parse("http://server:8080/song.mp3?token=abc", &info);
    /* info.scheme = "http", info.host = "server", info.path = "/song.mp3" */
    esp_gmf_uri_free(&info);

The strings in the parsed result are copies made from the source string; :cpp:func:`esp_gmf_uri_free` must be called after use to release them. The pool's :cpp:func:`esp_gmf_pool_get_io_tag_by_url` uses the same parser internally; when a custom IO implements the ``get_score`` callback to evaluate a URL, it is also recommended to reuse this module.

EIGHTCC Capability Identifiers
------------------------------

EIGHTCC (Eight Character Code) uses 8 ASCII characters to identify the capability category of an element. Capability descriptions declare via ``cap_eightcc`` which category of capability an element belongs to, such as audio decoding, audio mixing, video scaling, or video overlay. EIGHTCC describes "what can be done" while FourCC describes "what media format is processed"; for FourCC definitions, see :doc:`gmf-core-fourcc`.

``esp_gmf_caps_def.h`` defines common EIGHTCCs organized into audio and video categories.

**Audio Capabilities**

- ``ESP_GMF_CAPS_AUDIO_DECODER`` / ``ESP_GMF_CAPS_AUDIO_ENCODER``: Audio decoding / encoding
- ``ESP_GMF_CAPS_AUDIO_RATE_CONVERT`` / ``ESP_GMF_CAPS_AUDIO_CHANNEL_CONVERT`` / ``ESP_GMF_CAPS_AUDIO_BIT_CONVERT``: Sample rate, channel count, and bit depth conversion
- ``ESP_GMF_CAPS_AUDIO_EQUALIZER`` / ``ESP_GMF_CAPS_AUDIO_MIXER`` / ``ESP_GMF_CAPS_AUDIO_SONIC`` / ``ESP_GMF_CAPS_AUDIO_FADE``: Common audio effects
- ``ESP_GMF_CAPS_AUDIO_AEC`` / ``ESP_GMF_CAPS_AUDIO_NS`` / ``ESP_GMF_CAPS_AUDIO_AGC`` / ``ESP_GMF_CAPS_AUDIO_VAD`` / ``ESP_GMF_CAPS_AUDIO_WWE`` / ``ESP_GMF_CAPS_AUDIO_VCMD``: AI speech front-end capabilities
- ``ESP_GMF_CAPS_AUDIO_DRC`` / ``ESP_GMF_CAPS_AUDIO_MBC``: Dynamic range processing

**Video Capabilities**

- ``ESP_GMF_CAPS_VIDEO_DECODER`` / ``ESP_GMF_CAPS_VIDEO_ENCODER``: Video decoding / encoding
- ``ESP_GMF_CAPS_VIDEO_COLOR_CONVERT``: Color format conversion
- ``ESP_GMF_CAPS_VIDEO_CROP`` / ``ESP_GMF_CAPS_VIDEO_ROTATE`` / ``ESP_GMF_CAPS_VIDEO_SCALE``: Cropping, rotation, and scaling
- ``ESP_GMF_CAPS_VIDEO_OVERLAY`` / ``ESP_GMF_CAPS_VIDEO_FPS_CVT``: Overlay and frame rate conversion

Mutual conversion between string and numeric forms is done via the ``STR_2_EIGHTCC`` / ``EIGHTCC_2_STR`` macros; when adding custom capability identifiers, reuse these two macros rather than concatenating bytes directly.

API Reference
-------------

Header files covered in this document:

- ``esp_gmf_oal_mem.h``: Memory allocation and PSRAM status queries
- ``esp_gmf_oal_mutex.h``: Mutex
- ``esp_gmf_oal_thread.h``: Thread wrapper
- ``esp_gmf_oal_sys.h``: Runtime statistics and system helpers
- ``esp_gmf_uri_parser.h``: URI parsing
- ``esp_gmf_caps_def.h``: EIGHTCC capability identifiers

.. include-build-file:: inc/esp_gmf_oal_mem.inc

.. include-build-file:: inc/esp_gmf_oal_mutex.inc

.. include-build-file:: inc/esp_gmf_oal_thread.inc

.. include-build-file:: inc/esp_gmf_oal_sys.inc

.. include-build-file:: inc/esp_gmf_uri_parser.inc

.. include-build-file:: inc/esp_gmf_caps_def.inc
