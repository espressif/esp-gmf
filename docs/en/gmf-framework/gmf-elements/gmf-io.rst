GMF-IO
======

:link_to_translation:`zh_CN:[中文]`

gmf_io is the input/output component of ESP-GMF, providing concrete implementations of five types of IO: file, network, embedded flash, I2S PDM, and audio codec, all accessed through the ``esp_gmf_io_t`` base class. All subclasses share the same lifecycle, acquire-release protocol, URL scoring, asynchronous buffering, and done / abort / reload control; this document focuses on the differences, configuration options, and usage tips for each of the five subclasses.

The IO base class (``esp_gmf_io_t`` structure and ``esp_gmf_io_*`` API series) is defined in gmf-core; see :doc:`../gmf-core/gmf-core-data-path` for documentation.

Feature List
------------

- file io: reads and writes the file system based on POSIX ``fopen`` / ``fread`` / ``fwrite``, supporting ``/sdcard`` absolute paths and ``file://`` scheme, with optional DMA-compatible cache for accelerating large block reads/writes
- http io: reads and writes HTTP / HTTPS resources based on ``esp_http_client``, supporting Range requests, gzip decompression, event hooks, server certificates, and ``crt_bundle``
- embed_flash io: reads data from a read-only embedded flash resource table; URL format ``embed://group/<index>_name.ext``; suitable for firmware-embedded notification tones
- i2s_pdm io: wraps the ESP-IDF ``i2s_pdm`` driver channel for PDM microphone capture or PDM speaker playback
- codec_dev io: directly interfaces with ``esp_codec_dev`` device handles to read/write audio samples, typically for recording / playback via an on-board codec
- URL scoring: each subclass implements ``get_score``; the pool can score candidate IOs by URL and automatically select the best one
- Synchronous / asynchronous two operation modes: when a buffer and task are configured, the framework automatically spawns an io_process task to decouple production and consumption; otherwise all acquire / release calls execute synchronously in the caller's thread
- Speed monitoring: the base class can be enabled in both synchronous and asynchronous modes, providing instantaneous and average throughput in Kbps
- Reuse and switching: ``esp_gmf_io_reload`` switches URI without rebuilding the instance, commonly used for HLS segments and playlists
- End-of-stream and abort: unified ``done`` / ``abort`` semantics that handle the internal data bus and task accordingly

Technical Details
-----------------

IO Position in GMF
^^^^^^^^^^^^^^^^^^

Each gmf_io subclass is the leaf of a three-level inheritance chain: ``esp_gmf_obj_t`` provides common object fields (tag, cfg, new, del); ``esp_gmf_io_t`` defines the shared IO interface (open / seek / acquire / release / reset / reload / done / abort / speed stats) on top of it; the five subclasses in ``elements/gmf_io`` only implement callbacks and fill in subclass-specific configuration. The pipeline connects adjacent elements to an IO's internal data bus via ``esp_gmf_port``; neighbors read and write through the uniform port interface.

.. only:: html

   .. mermaid::

      flowchart TB
          Obj["esp_gmf_obj_t"]
          IoBase["IO Base Class (esp_gmf_io_t)"]
          IoImpl["Five IO Subclasses (file/http/embed_flash/i2s_pdm/codec_dev)"]
          Port["Data Port (esp_gmf_port)"]

          Obj --> IoBase
          IoBase --> IoImpl
          IoBase -. data bus .-> Port

In actual development, you only interact with the subclass-provided ``xxx_io_cfg_t`` and ``esp_gmf_io_xxx_init``; all runtime control uses the base class ``esp_gmf_io_*`` API series. To extend a custom IO, add a subclass inheriting from ``esp_gmf_io_t`` and fill in the function pointer table.

Synchronous and Asynchronous Operation Modes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The base class has two operating modes, selected by the subclass through ``esp_gmf_io_cfg_t.thread.stack`` and ``esp_gmf_io_cfg_t.buffer_cfg.buffer_size``.

**Synchronous mode**: ``thread.stack == 0`` with no data bus configured. ``acquire_read`` / ``release_read`` / ``acquire_write`` / ``release_write`` directly execute the subclass callback in the caller's thread. Low overhead; suitable for codec_dev, embed_flash, and i2s_pdm, where reading/writing is itself a blocking DMA or zero-copy memory access.

**Asynchronous mode**: ``thread.stack > 0`` and ``buffer_size > 0``. The base class creates an ``io_process`` task and a block-type data bus at the ``esp_gmf_io_open`` stage; the task loops internally: in the read direction, it reads data from the device via ``acquire_read`` and submits it to the data bus via ``db_release_write``; in the write direction, it retrieves data from the data bus via ``db_acquire_read`` and delivers it to the device via ``release_write``. The application's ``esp_gmf_io_acquire_*`` actually operates on the data bus, decoupled from device IO. Suitable for HTTP and file IO with variable latency that requires prefetching.

The diagram below compares the two modes in the read direction.

.. only:: html

   .. mermaid::

      flowchart TD
          subgraph sync ["Synchronous Mode"]
              User_A["Caller Thread"]
              DevA["Device acquire_read"]
              User_A -->|"direct call"| DevA
          end
          subgraph async ["Asynchronous Mode"]
              User_B["Caller Thread"]
              DbB["Block Data Bus"]
              TaskB["io_process Task"]
              DevB["Device acquire_read"]
              User_B -->|"db_acquire_read"| DbB
              TaskB -->|"read device"| DevB
              TaskB -->|"db_release_write"| DbB
          end

io_http defaults to asynchronous; other subclasses default to synchronous; the application can override as needed.

URL Scoring and Automatic Selection
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The pool supports registering multiple IOs simultaneously and uses ``esp_gmf_io_get_score`` to determine "which IO can handle this URL". Scores have three levels:

- ``ESP_GMF_IO_SCORE_NONE`` (0): Not supported
- ``ESP_GMF_IO_SCORE_STANDARD`` (50): Matches scheme (``http://``, ``file://``, ``embed://``)
- ``ESP_GMF_IO_SCORE_PERFECT`` (100): Exact match (scheme + extension / dedicated IO)

Built-in scoring rules for the five subclasses:

.. list-table::
   :widths: 24 76
   :header-rows: 1

   * - Subclass
     - Matching Condition
   * - io_file
     - URL starts with ``/`` or ``file://`` (recommended: ``file:///sdcard/xxx``) → ``STANDARD``
   * - io_http
     - URL starts with ``http://`` or ``https://`` → ``STANDARD``
   * - io_embed_flash
     - URL starts with ``embed://`` → ``STANDARD``
   * - io_i2s_pdm / io_codec_dev
     - No scheme-based matching; selected directly by pool tag (``io_codec_dev``, ``io_i2s_pdm``)

Custom IOs that want to take priority over the default implementation (e.g., HLS over HTTP) can return ``PERFECT``.

io_file
^^^^^^^

Based on POSIX stdio. :cpp:func:`esp_gmf_io_file_init` allocates an instance according to :cpp:type:`file_io_cfg_t`; at open, the URI is obtained via ``esp_gmf_io_get_uri``, supporting ``/sdcard/xxx`` absolute paths and ``file:///sdcard/xxx`` file URIs. The read direction uses ``"rb"``; the write direction uses ``"wb"``; the read direction writes the file size obtained from ``stat`` back to the base class, used for progress reporting and seek validation.

**Resume from position**. If the base class has a non-zero ``info.pos`` at the open stage, the subclass calls ``fseek(SEEK_SET)`` to jump to that position. Combined with the pipeline's ``seek`` / application's ``esp_gmf_io_set_pos``, playback can start from a specified byte offset.

**Cache configuration**. ``cache_size`` controls the user buffer allocated by ``setvbuf``. Values ``<= 512`` are treated as disabled; above this threshold, allocation is aligned up to 512 bytes. ``cache_caps`` selects the heap tag for the cache; zero defaults to ``MALLOC_CAP_DMA``. On chips with PSRAM DMA support (e.g., esp32p4), it is recommended to use ``MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA`` to save internal SRAM.

**Seek and reset**. ``seek`` directly calls ``fseek(SEEK_SET)``; ``reset`` sets the cursor to 0. Both take effect immediately in synchronous mode; in asynchronous mode, the base class converts them to deferred processing by the task.

.. code:: c

    file_io_cfg_t cfg = FILE_IO_CFG_DEFAULT();
    cfg.dir        = ESP_GMF_IO_DIR_READER;
    cfg.cache_size = 8 * 1024;
    cfg.cache_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA;

    esp_gmf_io_handle_t io = NULL;
    esp_gmf_io_file_init(&cfg, &io);
    esp_gmf_io_set_uri(io, "/sdcard/test.mp3");

io_http
^^^^^^^

Based on ``esp_http_client``. Default asynchronous: stack 6 KiB, priority 10, core 0, data bus 20 KiB, IO block 3 KiB; override via ``http_io_cfg_t.io_cfg``.

**Request construction**. At open, the client is established with ``timeout_ms = 30000``; optionally ``cert_pem`` or ``crt_bundle_attach``. If the base class has a non-zero ``info.pos`` (indicating resume), open automatically adds a ``Range: bytes=<pos>-`` header. 301 / 302 responses are automatically redirected with a reconnect.

**Automatic gzip decompression**. If the response header contains ``Content-Encoding: gzip``, io_http internally decompresses using ``gzip_miniz`` while reading; the application receives the decompressed byte stream. Other ``Content-Encoding`` values are treated as unsupported and return failure.

**Event hook**. The application registers a callback via ``http_io_cfg_t.event_handle`` or :cpp:func:`esp_gmf_io_http_set_event_callback`; it is called at various points: before connection, during request, during response, at request end, and on track switch. A positive return value causes io_http to skip the default read/write action, suitable for implementing custom authentication, data modification, and multipart upload. Event enums are in ``http_stream_event_id_t``.

**reset and reload**. :cpp:func:`esp_gmf_io_http_reset` resets HTTP state, commonly used in event callbacks to "finish the current resource and switch to the next segment". The base class's ``esp_gmf_io_reload`` reuses the existing connection after setting a new URI to reduce handshake overhead, suitable for sequential switching within the same host.

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
^^^^^^^^^^^^^^

Reads read-only resources linked into the firmware; supports read direction only. Typical use cases include firmware-embedded notification tones, button feedback, and startup sounds.

**Resource table**. The context is an ``embed_item_info_t`` array, where each entry contains ``address`` and ``size`` fields. Set via :cpp:func:`esp_gmf_io_embed_flash_set_context`; ``max_files`` provides an upper bound, which can be larger than the actual number of entries.

**URL format**. ``embed://<group>/<index>_<name>.<ext>``. At open, the URL is parsed to extract the last numeric segment before ``_`` as the index, which is used to look up ``address`` and ``size`` in the resource table. The read data is raw bytes; if encoded as mp3 / wav, a decoding element is needed.

.. code:: c

    extern const embed_item_info_t g_my_tones[];

    embed_flash_io_cfg_t cfg = EMBED_FLASH_CFG_DEFAULT();
    cfg.max_files = ESP_MY_TONE_URL_MAX;

    esp_gmf_io_handle_t io = NULL;
    esp_gmf_io_embed_flash_init(&cfg, &io);
    esp_gmf_io_embed_flash_set_context(io, g_my_tones, ESP_MY_TONE_URL_MAX);
    esp_gmf_io_set_uri(io, "embed://tone/0_startup.mp3");

The repository provides the ``mk_flash_embed_tone.py`` helper script to generate the resource table and corresponding URI enum from audio files in a specified directory.

io_i2s_pdm
^^^^^^^^^^

Wraps the ESP-IDF ``i2s_pdm`` driver channel as an IO. The read/write direction is determined by ``i2s_pdm_io_cfg_t.dir``; the channel handle ``pdm_chan`` is created by the application in advance following the ESP-IDF procedure.

At open, ``i2s_channel_enable`` is called to enable the channel; in the write direction, an ``on_send_q_ovf`` event callback is additionally registered to detect send queue overflow, and the ``PDM_TX_DONE_BIT`` event group bit is used to wait for the last transmission to complete at close, ensuring tail data has been sent. In the read direction, ``i2s_channel_read`` blocks until a payload is filled; in the write direction, ``i2s_channel_write`` blocks until the entire payload is written.

.. code:: c

    i2s_chan_handle_t tx_handle = NULL;
    /* Create I2S PDM TX channel following ESP-IDF procedure */

    i2s_pdm_io_cfg_t cfg = ESP_GMF_IO_I2S_PDM_CFG_DEFAULT();
    cfg.dir      = ESP_GMF_IO_DIR_WRITER;
    cfg.pdm_chan = tx_handle;

    esp_gmf_io_handle_t io = NULL;
    esp_gmf_io_i2s_pdm_init(&cfg, &io);

io_codec_dev
^^^^^^^^^^^^

Directly uses an ``esp_codec_dev`` device handle as an IO. The device is bound at construction via ``codec_dev_io_cfg_t.dev``; at runtime, it can be hot-swapped via :cpp:func:`esp_gmf_io_codec_dev_set_dev` (e.g., switching from Bluetooth back to local codec).

``acquire_read`` calls ``esp_codec_dev_read`` to synchronously read the specified number of bytes; ``release_write`` calls ``esp_codec_dev_write`` to write the valid bytes of the payload. open / close only update state flags; hardware start/stop is handled by the application via ``esp_codec_dev_open`` / ``esp_codec_dev_close``. codec_dev io does not perform data format conversion; input samples must be aligned to the device's current sample rate, bit width, and channels by upstream elements (``aud_rate_cvt``, ``aud_bit_cvt``, ``aud_ch_cvt``).

.. code:: c

    codec_dev_io_cfg_t cfg = ESP_GMF_IO_CODEC_DEV_CFG_DEFAULT();
    cfg.dir = ESP_GMF_IO_DIR_WRITER;
    cfg.dev = playback_handle;

    esp_gmf_io_handle_t io = NULL;
    esp_gmf_io_codec_dev_init(&cfg, &io);
    /* Switch device at runtime */
    esp_gmf_io_codec_dev_set_dev(io, new_playback_handle);

Runtime Control
^^^^^^^^^^^^^^^

All subclasses share the same set of control interfaces, grouped by purpose into four categories.

**Position and size**. ``esp_gmf_io_set_uri`` / ``get_uri`` maintains the URL; ``set_size`` / ``get_size`` and ``set_pos`` / ``update_pos`` / ``get_pos`` maintain bytes read and total bytes. Subclasses write the total size to the base class at open (file from ``stat``, http from ``Content-Length``); used by the pipeline for progress reporting and seek validation.

**End-of-stream and abort**. ``esp_gmf_io_done`` marks the current data source as fully read; downstream ``acquire_*`` returns payloads with ``is_done = true``; in asynchronous mode, it also pauses the IO task, which resumes after the application calls ``esp_gmf_io_clear_done``. ``esp_gmf_io_abort`` is stronger: subsequent ``acquire_*`` / ``release_*`` return ``ESP_GMF_IO_ABORT`` directly; ``esp_gmf_io_clear_abort`` is needed to recover. These correspond to the two distinct semantics of "skip track" and "user stop".

**Reset and reload**. ``esp_gmf_io_reset`` clears pos, calls the subclass reset, and resets the async task and job; the pipeline calls this automatically during ``reset``. ``esp_gmf_io_reload`` is for seamless switching: it replaces the URI and reuses the connection and resources, saving time compared to ``close + set_uri + open``.

**Speed statistics**. Enable via ``esp_gmf_io_cfg_t.enable_speed_monitor`` or ``esp_gmf_io_enable_speed_monitor``; the base class accumulates transferred bytes and elapsed time during I/O and maintains instantaneous and average Kbps. Available in both synchronous and asynchronous modes; read via ``esp_gmf_io_get_speed_stats`` as ``esp_gmf_io_speed_stats_t``, for observing network bandwidth, file read/write throughput, and similar metrics.

Performance
-----------

The table below gives typical throughput levels and optimization directions for common configurations. Measured values are strongly dependent on file system, network, PSRAM, and codec sample rate; provided for relative comparison only.

.. list-table::
   :widths: 18 22 28 32
   :header-rows: 1

   * - IO
     - Typical Mode
     - Main Bottleneck
     - Optimization Direction
   * - io_file
     - Synchronous
     - SD card controller / FATFS block size
     - Increase ``cache_size`` (recommended 4-16 KiB); use SPIRAM cache on chips with PSRAM DMA support
   * - io_http
     - Asynchronous
     - TLS handshake, link bandwidth
     - Increase ``buffer_size`` (default 20 KiB) and ``io_size`` (default 3 KiB); set task core affinity appropriately
   * - io_embed_flash
     - Synchronous
     - Flash read bandwidth / memcpy
     - Avoid too-small ``wanted_size``; align with the downstream element's ``io_size``
   * - io_i2s_pdm
     - Synchronous
     - I2S driver DMA descriptor size
     - Keep payload size as an integer multiple of the DMA descriptor size
   * - io_codec_dev
     - Synchronous
     - Codec sample rate determines the hard ceiling
     - Align downstream elements to the codec's sample rate / bit width / channels; avoid software conversion

Application Examples
--------------------

- ``gmf_examples/basic_examples/pipeline_play_embed_music``: Reads mp3 from embed_flash, processes through ``aud_dec`` and effects chain, then outputs via codec_dev; covers three stages of pool registration, pipeline construction, and IO binding

The upper-level application components such as ``esp_audio_simple_player`` also connect file and HTTP sources through this component internally and can serve as advanced references.

SoC Compatibility
-----------------

- The behavior of ``cache_caps`` in io_file differs across chips:

  - esp32: use ``MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`` or ``MALLOC_CAP_DMA``
  - esp32sx / esp32cx: can additionally use ``MALLOC_CAP_INTERNAL``
  - esp32p4 and other chips with ``SOC_SDMMC_PSRAM_DMA_CAPABLE``: can use ``MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA`` to save SRAM

- io_i2s_pdm depends on ``driver/i2s_pdm.h`` and is only available on SoCs with PDM support; use the corresponding I2S Std wrapper for chips without PDM support
- Enabling ``crt_bundle_attach`` for io_http requires ``CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`` to be enabled

FAQ
---

**Q:** When should asynchronous mode be enabled for an IO?

When the IO latency has noticeable jitter, or when downstream element consumption rate is sensitive to IO jitter. A typical example is HTTP audio-on-demand, where network jitter needs to be absorbed by a prefetch buffer. For stable devices like codec_dev and embed_flash, synchronous mode is sufficient; asynchronous mode would only add context switching overhead.

**Q:** ``acquire_read`` returns success but the payload's ``valid_size`` is smaller than ``wanted_size``?

This is normal. The end of a file, network chunking, and I2S DMA descriptor switching can all cause a single read to return less than the requested length. The caller should use ``valid_size`` as the actual data length and stop reading when ``is_done == true``.

**Q:** io_http has ``event_handle`` configured but the callback is not triggered?

Confirm the event id is one of the five ``HTTP_STREAM_*`` types; also the callback must be passed in via :cpp:type:`http_io_cfg_t` at configuration, or by calling :cpp:func:`esp_gmf_io_http_set_event_callback` after init. Directly modifying the configuration struct members after init does not take effect.

**Q:** embed_flash io returns error "No _ in file name" halfway through reading?

The URL format requires ``embed://<group>/<index>_<name>.<ext>``; the index and filename are separated by an underscore. Fix the URI or resource table naming to resolve this.

**Q:** Can codec_dev io drive I2S directly?

No. codec_dev io only interacts with the ``esp_codec_dev`` handle, which internally connects to the I2S or I2S_PDM driver. To operate I2S directly, use io_i2s_pdm or another I2S subclass wrapper.

**Q:** seek takes a long time in asynchronous mode?

In asynchronous mode, seek aborts the current data bus, waits for the IO task to execute ``fseek`` / Range request at the next job boundary, then refreshes the data. The wait time depends on the IO operation the task is currently performing; if necessary, the wait limit can be adjusted via ``esp_gmf_io_set_task_timeout``.

API Reference
-------------

Header files for this component:

- ``esp_gmf_io_file.h``: file io configuration and initialization
- ``esp_gmf_io_http.h``: http io configuration, events, certificates, reset
- ``esp_gmf_io_embed_flash.h``: embed_flash io configuration and resource table setup
- ``esp_gmf_io_i2s_pdm.h``: i2s_pdm io configuration and initialization
- ``esp_gmf_io_codec_dev.h``: codec_dev io configuration and device hot-swap

The base class API documentation for ``esp_gmf_io.h`` is in :doc:`../gmf-core/gmf-core-data-path`.

.. include-build-file:: inc/esp_gmf_io_file.inc

.. include-build-file:: inc/esp_gmf_io_http.inc

.. include-build-file:: inc/esp_gmf_io_embed_flash.inc

.. include-build-file:: inc/esp_gmf_io_i2s_pdm.inc

.. include-build-file:: inc/esp_gmf_io_codec_dev.inc
