Data Flow
=========

:link_to_translation:`zh_CN:[中文]`

This document describes how data flows between elements, covering payload fields and ownership, the port acquire-release protocol, how IO connects at the head and tail of a pipeline, and the byte cache.

For element lifecycle, port attributes, capability description, and runtime methods, see :doc:`gmf-core-element`. For pipeline orchestration, task scheduling, lifecycle, and control interfaces, see :doc:`gmf-core-pipeline`. For the four data bus implementations and their selection, see :doc:`gmf-core-databus`. For the overall architecture and object relationships, see :doc:`gmf-core-overview`.

Payload and Port
----------------

Each element exposes only two data interfaces externally: an input port and an output port. Data is passed between ports as :cpp:type:`esp_gmf_payload_t`; elements are not responsible for buffer allocation or cross-thread synchronization, and only need to read and write data following an acquire-release protocol.

The complete data flow in a single ``process`` call is shown below, using a ``Decoder -> Resample`` pipeline as an example.

.. only:: html

   .. mermaid::

      sequenceDiagram
          participant T as Task
          participant E as resample
          participant IP as in_port
          participant OP as out_port
          participant D as Downstream

          T->>+E: process()
          E->>+IP: acquire_in(size)
          IP-->>-E: input payload in
          E->>+OP: acquire_out(size)
          OP-->>-E: output payload out
          Note over E: Process input and generate output
          E->>OP: release_out(out)
          OP->>D: Submit output payload
          E->>IP: release_in(in)
          E-->>-T: ok

The sequence above contains two rules. First, the payload returned by ``acquire_in`` remains owned by the current element until ``release_in`` is called; the port maintains the validity of this data via reference counting or the underlying implementation, so the element does not need to handle cross-thread synchronization itself. Second, ``release_out`` submits the output payload. For block-type ports, the submission typically does not copy data, but hands the payload to the downstream input port or the underlying data queue, so one piece of data can traverse the entire pipeline without intermediate copies.

acquire-release is the fundamental protocol of the GMF-Core data path. The following sections detail the payload and port.

Payload
-------

Fields
^^^^^^

:cpp:type:`esp_gmf_payload_t` is defined in ``esp_gmf_payload.h`` with the following fields.

.. list-table::
   :widths: 22 18 60
   :header-rows: 1

   * - Field
     - Type
     - Meaning
   * - ``buf``
     - ``uint8_t*``
     - Pointer to the data buffer
   * - ``buf_length``
     - ``size_t``
     - Total buffer length
   * - ``valid_size``
     - ``size_t``
     - Length of valid data in the buffer
   * - ``is_done``
     - ``bool``
     - End-of-stream flag; set to 1 for the last data block, triggers the pipeline to enter ``FINISHED``
   * - ``pts``
     - ``uint64_t``
     - Presentation timestamp, used for audio/video synchronization
   * - ``needs_free``
     - ``uint8_t``
     - 1-bit field indicating whether ``buf`` should be automatically freed when the payload is destroyed
   * - ``meta_flag``
     - ``uint8_t``
     - 7-bit field; currently defines ``ESP_GMF_META_FLAG_AUD_RECOVERY_PLC`` to mark audio frames recovered via packet loss concealment

Lifecycle and Ownership
^^^^^^^^^^^^^^^^^^^^^^^

In element code, a payload appears as an :cpp:type:`esp_gmf_payload_t` \* pointer. Ownership belongs to the current element during acquire-release; after release, it is returned to port and data bus management. Three rules apply when using payloads:

- **Do not free manually**: Payloads acquired from a port are managed by the port; elements can only read and write ``buf`` content and must not call ``esp_gmf_payload_delete``.
- **valid_size is set by the writer**: After acquiring an output payload, the element writes data and fills ``valid_size`` with the actual number of bytes written; downstream uses this to determine how much valid data there is.
- **acquire and release must be paired**: Even if an error occurs during processing, release must still be called; otherwise the port enters a leaked state and subsequent calls will exhaust the buffers.

In the less common scenario where an element allocates its own payload (typically a source IO element), it can explicitly create one with :cpp:func:`esp_gmf_payload_new` or :cpp:func:`esp_gmf_payload_new_with_len` and release it with :cpp:func:`esp_gmf_payload_delete` when no longer referenced. For PSRAM cache alignment, use :cpp:func:`esp_gmf_payload_realloc_aligned_buf`.

is_done and pts
^^^^^^^^^^^^^^^

``is_done`` is the signal for the pipeline to naturally transition from ``RUNNING`` to ``FINISHED``. When an IO element reads to the end of the file, it sets ``is_done`` to 1 and sends it with the payload; downstream elements process the end-marked payload in turn and trigger close level by level.

``pts`` is the presentation timestamp, typically filled by the source element. Intermediate elements update it as needed or pass it through directly. Elements near the playback end use it for audio/video synchronization.

Port
----

Direction and Type
^^^^^^^^^^^^^^^^^^

Each port is defined by two attributes.

**Direction**: ``ESP_GMF_PORT_DIR_IN`` indicates an input port (reads payloads from upstream); ``ESP_GMF_PORT_DIR_OUT`` indicates an output port (writes payloads to downstream). Each element has one input port and one output port.

**Type**: Byte type and block type, corresponding to two data exchange strategies.

- ``ESP_GMF_PORT_TYPE_BYTE`` (byte type) allows the element to read and write any number of bytes; the framework handles internal copying and splicing as needed. Suitable for scenarios requiring precise fixed-length access, such as a decoder parsing protocol headers.
- ``ESP_GMF_PORT_TYPE_BLOCK`` (block type) accesses one entire block at a time, without copying; only buffer addresses are passed between ports. Suitable for processing an entire video frame or bulk PCM, with higher performance but lower configurability.

acquire-release Protocol
^^^^^^^^^^^^^^^^^^^^^^^^

A port exposes four APIs used in pairs to form the acquire-release access protocol. The complete signature of ``acquire_in`` as an example:

.. code:: c

    esp_gmf_err_io_t esp_gmf_port_acquire_in(
        esp_gmf_port_handle_t   handle,
        esp_gmf_payload_t     **load,
        uint32_t                wanted_size,
        int                     wait_ticks);

The four parameters:

- ``handle`` is the input port handle.
- ``load`` is an output parameter; after the call returns, ``*load`` points to a payload ready for reading.
- ``wanted_size`` is the data length the element wants to obtain. For source IOs or ports bound to a data bus, the underlying implementation prepares data according to this value; for payloads already submitted to the input port by an upstream element, the port returns the current payload directly, and the actual valid length is based on ``(*load)->valid_size``.
- ``wait_ticks`` is the wait timeout in system ticks. ``ESP_GMF_MAX_DELAY`` means wait indefinitely; ``0`` means return immediately.

The return type is :cpp:type:`esp_gmf_err_io_t`:

.. list-table::
   :widths: 28 14 58
   :header-rows: 1

   * - Return Code
     - Value
     - Meaning
   * - ``ESP_GMF_IO_OK``
     - ``>= 0``
     - Success; the value indicates the actual number of bytes read
   * - ``ESP_GMF_IO_FAIL``
     - -1
     - Operation failed
   * - ``ESP_GMF_IO_TIMEOUT``
     - -2
     - Wait timed out
   * - ``ESP_GMF_IO_ABORT``
     - -3
     - Actively aborted by ``esp_gmf_db_abort`` or a task stop

Template for element code handling return values:

.. code:: c

    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_err_io_t ret = esp_gmf_port_acquire_in(in_port, &in_load, 1024, ESP_GMF_MAX_DELAY);
    if (ret == ESP_GMF_IO_ABORT) {
        return ESP_GMF_JOB_ERR_ABORT;
    } else if (ret < 0) {
        return ESP_GMF_JOB_ERR_FAIL;
    }
    /* Use in_load->buf, in_load->valid_size */

The four APIs work in pairs:

- Read side: ``acquire_in`` gets a readable payload; the element consumes it and calls ``release_in`` to return it.
- Write side: ``acquire_out`` gets a writable payload; the element fills it and calls ``release_out`` to submit it.

The typical call order in process is: ``acquire_in`` first to get the input, then ``acquire_out`` to prepare the output buffer, after processing call ``release_out`` to submit to downstream, and finally ``release_in`` to return the input. Acquiring the output before processing allows waiting in advance if downstream is blocked, avoiding discovering an unavailable output port only after processing is complete.

acquire and release must be paired, even if an error occurs during processing. The framework provides several helper macros to simplify error paths:

.. code:: c

    ESP_GMF_PORT_ACQUIRE_IN_CHECK(TAG, ret, err, goto ACQ_FAIL);
    ESP_GMF_PORT_RELEASE_IN_CHECK(TAG, ret, err, goto REL_FAIL);

These macros consolidate the return value check and error branch into a single line.

Shared Buffer Management
^^^^^^^^^^^^^^^^^^^^^^^^

GMF provides two mechanisms to reduce memory usage and avoid unnecessary data copies.

**Reference Counting**

For bypass or in-place processing elements, the same payload can be passed directly to downstream elements or user applications. Each additional consumer increments the port's ``ref_count``; the corresponding ``release_in`` / ``release_out`` decrements it. When the count reaches zero, the port invokes the underlying ``release`` callback to reclaim the buffer. Sharing is controlled by the port's ``is_shared`` flag, which can be enabled or disabled via :cpp:func:`esp_gmf_port_enable_payload_share` (shared by default).

.. only:: html

   .. mermaid::

      flowchart LR
          Source --> Bypass
          Bypass --> User
          Bypass --> Downstream["Downstream Element"]

The same data is shared throughout this flow without extra copies.

**Buffer Reuse (AB Buffer)**

For elements that produce new output data, the framework reuses payload buffers across elements via ``is_shared``. For a pipeline ``A → B → C``:

.. code:: text

    A → B → C

- A outputs Buffer A.
- B uses Buffer A as input and writes to Buffer B.
- After B completes, Buffer A can be reclaimed and reallocated.
- C uses Buffer B as input; its output can reuse the freed Buffer A.

As data flows through the pipeline, Buffer A and Buffer B alternate in reuse. Even with many elements in the chain, two working buffers are typically sufficient, significantly reducing memory consumption.

At initialization, elements declare constraints on their input and output ports, such as port type, alignment requirements, and recommended data size per acquire. Port attributes are part of the element configuration; for the complete description, see :doc:`gmf-core-element`.

The data queue corresponding to a port is managed by the data bus. GMF-Core provides four data buses (ringbuffer / fifo / block / pbuf); a port binds to one of them at creation time, and the interface seen by element code is completely identical. For the trade-offs between the four implementations and flow control interfaces, see :doc:`gmf-core-databus`.

External IO
-----------

An external IO (:cpp:type:`esp_gmf_io_t`) is a special element at the head and tail of a pipeline, handling reading and writing of external data sources such as files, networks, and codecs. In addition to the basic ``open`` / ``acquire`` / ``release`` / ``close``, asynchronous mode is supported: an ``io_process`` task writes data to an internal data bus while the caller reads from the buffer, smoothing rate fluctuations such as HTTP downloads; transfer rates are available via :cpp:type:`esp_gmf_io_speed_stats_t` as instantaneous and average Kbps. Advanced control interfaces for switching data sources, interruption recovery, and similar scenarios are also provided.

Synchronous and Asynchronous Modes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

An IO is configured at creation via :cpp:type:`esp_gmf_io_cfg_t` to select its operating mode.

**Synchronous mode**: ``thread.stack = 0`` and no ``buffer_cfg`` configured. ``esp_gmf_io_acquire_read`` executes the underlying IO operation (such as ``fread``) directly in the calling thread context. Advantages are low latency and small memory footprint; the disadvantage is that the caller must tolerate potentially long blocking from IO operations (such as network reads).

**Asynchronous mode**: ``thread.stack > 0`` and ``buffer_cfg.buffer_size`` configured. The framework internally creates an ``io_process`` task that independently performs the underlying IO operation and caches data to the data bus. When the user calls ``acquire_read``, it actually reads from the data bus, decoupled from the underlying IO. This mode is suitable for "reading a fixed number of bytes" or "large production/consumption rate differences," such as when a decoder needs a stable byte stream but network download is bursty.

.. code:: c

    esp_gmf_io_cfg_t cfg = {
        .thread = {
            .stack = 4 * 1024,
            .prio  = 5,
            .core  = 0,
        },
        .buffer_cfg = {
            .io_size     = 4096,   /* Size of each underlying IO read */
            .buffer_size = 16 * 1024,
            .read_filter = NULL,
        },
        .enable_speed_monitor = true,
    };

URL Scoring Strategy
^^^^^^^^^^^^^^^^^^^^

The ``get_score`` of :cpp:type:`esp_gmf_io_t` is used to obtain the match score, enabling the pool to find the best-matching IO among multiple registered IOs. The pool function ``esp_gmf_pool_get_io_tag_by_url`` iterates all registered IOs' ``get_score`` and returns the one with the highest score. Three scoring levels:

.. list-table::
   :widths: 36 14 50
   :header-rows: 1

   * - Score
     - Value
     - Meaning
   * - ``ESP_GMF_IO_SCORE_NONE``
     - 0
     - IO does not support this URL
   * - ``ESP_GMF_IO_SCORE_STANDARD``
     - 50
     - Protocol prefix matches, e.g., ``http://`` is handled by the HTTP IO
   * - ``ESP_GMF_IO_SCORE_PERFECT``
     - 100
     - Both protocol and file extension match, or a specialized IO with high-priority matching

Custom IOs implementing ``get_score`` can return any intermediate value to distinguish priority among multiple candidate IOs.

Seamless Reload and Reset
^^^^^^^^^^^^^^^^^^^^^^^^^

:cpp:func:`esp_gmf_io_reload` reopens an existing IO with a new URI without destroying the underlying connection. Primarily for scenarios like HLS segments where "multiple segments are downloaded continuously from the same host," reusing the HTTP connection to avoid repeated handshake overhead. Internally, the reload clears the ``data_bus`` EOF flag, clears the abort flag, and calls the IO's ``reload`` callback to reopen the new URI. ``reload`` must be called after the previous read has completed.

:cpp:func:`esp_gmf_io_reset` performs a more comprehensive cleanup: clears position and file size to zero, calls the IO's ``reset`` callback, resets the task, and reloads the IO process job. Commonly used for corresponding IO cleanup after a pipeline reset.

Done / Abort Control
^^^^^^^^^^^^^^^^^^^^

The IO layer's done and abort control interfaces correspond one-to-one with the data bus, but in asynchronous mode they also additionally manage the IO task's hold state.

- :cpp:func:`esp_gmf_io_done` marks EOF: aborts the asynchronous IO's data bus and holds the task; downstream ``acquire`` will get a payload with ``is_done`` set. The caller **must** call :cpp:func:`esp_gmf_io_clear_done` to release the held task before switching to the next data source, otherwise the subsequent IO will not advance.
- :cpp:func:`esp_gmf_io_abort` immediately aborts the current operation: data bus abort, task hold; all ``acquire`` / ``release`` return ``ESP_GMF_IO_ABORT``. The paired :cpp:func:`esp_gmf_io_clear_abort` restores normal operation.

Typical flow for switching tracks in a playlist:

.. code:: c

    /* Current track finished */
    esp_gmf_io_done(io_handle);

    /* Wait for the pipeline to finish processing remaining payloads */
    ...

    /* Switch to the next track */
    esp_gmf_io_set_uri(io_handle, next_uri);
    esp_gmf_io_clear_done(io_handle);
    /* Pipeline continues running without rebuilding */

Custom Payload Processing
^^^^^^^^^^^^^^^^^^^^^^^^^

The ``read_filter`` callback in :cpp:type:`esp_gmf_io_buffer_cfg_t` allows inserting custom processing after the IO reads raw data and before it is written to the data bus. Common uses include decryption, byte-order conversion, and protocol de-encapsulation. Callback signature:

.. code:: c

    esp_gmf_err_t (*read_filter)(esp_gmf_io_handle_t obj,
                                  void *payload,
                                  uint32_t wanted_size,
                                  int block_ticks);

The payload's ``buf`` and ``valid_size`` can be modified in-place within the callback.

Speed Monitoring
^^^^^^^^^^^^^^^^

After setting ``enable_speed_monitor`` to true in :cpp:type:`esp_gmf_io_cfg_t`, the framework maintains current and average transfer speeds (Kbps), which can be retrieved via :cpp:func:`esp_gmf_io_get_speed_stats` as :cpp:type:`esp_gmf_io_speed_stats_t`. The monitor can also be dynamically toggled at runtime with :cpp:func:`esp_gmf_io_enable_speed_monitor`. Commonly used for network condition monitoring and adaptive transfer rate.

Byte Cache
----------

A block-type port returns an entire block per acquire, but elements such as decoders often need to "read exactly N bytes from the data stream." The ``esp_gmf_cache`` module provides byte-level caching:

.. code:: c

    esp_gmf_cache_handle_t cache;
    esp_gmf_cache_new(1024, &cache);

    /* After each block payload is acquired, load it into cache */
    esp_gmf_cache_load(cache, in_load->buf, in_load->valid_size);

    /* Then read by any number of bytes */
    int read;
    esp_gmf_cache_acquire(cache, 7, out_buf, &read);
    esp_gmf_cache_release(cache, 7);

The byte cache internally maintains an expandable sliding buffer. An element can continuously load bytes from multiple payloads into the cache, then read data of any desired length; even if a single read spans the boundary of two payloads, the cache returns the data continuously from its internal buffer. A typical use case is audio decoders parsing protocol or frame headers.

API Reference
-------------

Core header files covered in this document:

- ``esp_gmf_payload.h``: Payload structure and lifecycle
- ``esp_gmf_port.h``: Port configuration and access APIs
- ``esp_gmf_cache.h``: Byte cache
- ``esp_gmf_io.h``: Public interfaces for IO objects

.. include-build-file:: inc/esp_gmf_payload.inc

.. include-build-file:: inc/esp_gmf_port.inc

.. include-build-file:: inc/esp_gmf_cache.inc

.. include-build-file:: inc/esp_gmf_io.inc
