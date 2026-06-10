Data Bus
========

:link_to_translation:`zh_CN:[中文]`

A data bus is the queue implementation beneath a port that carries data. Ports provide a unified acquire-release protocol; the data bus is responsible for buffer management, cross-thread synchronization, and queuing payloads between producers and consumers. GMF-Core provides four implementations - ringbuffer, fifo, block, and pbuf - targeting different copy strategies, blocking semantics, and data granularity. Elements access data through ports and do not need to be aware of the specific data bus implementation.

For ports and the acquire-release protocol, see :doc:`gmf-core-data-path`. For the relationship between pipelines and tasks, see :doc:`gmf-core-pipeline`.

Design Dimensions
-----------------

When choosing a data bus implementation, the main considerations are how data enters the buffer, whether reads and writes need to block, and whether each transfer is a byte stream or an entire block. The following table compares the four implementations by copy strategy, blocking semantics, and data granularity; subsequent sections explain each one's buffer source and applicable scenarios.

.. list-table::
   :widths: 24 26 26 24
   :header-rows: 1

   * - Dimension
     - ringbuffer
     - fifo / block
     - pbuf
   * - Copy Strategy
     - memcpy
     - Zero-copy (address passing)
     - Zero-copy (pointer queue)
   * - Blocking Semantics
     - Both read and write block
     - Both read and write block
     - Non-blocking
   * - Data Granularity
     - Arbitrary bytes
     - Entire block
     - Entire block

When selecting, first determine three things: how large is the rate difference between upstream and downstream, whether data can be passed at block boundaries, and whether the caller can block when the buffer is unavailable.

Four Implementations
--------------------

ringbuffer
^^^^^^^^^^

A ringbuffer maintains a fixed-size circular buffer and reads/writes by bytes each time. ``acquire_read`` waits until data is available; ``acquire_write`` waits until space is available. Both writing in and reading out pass through the ringbuffer's internal buffer, so each read and write involves one memcpy.

.. code:: c

    esp_gmf_db_handle_t db;
    esp_gmf_db_new_ringbuffer(num, buf_size, &db);

Suitable for scenarios with large processing rate differences between upstream and downstream that require buffered decoupling. For example, when an MP3 decoder outputs PCM frame by frame and I2S consumes data at a fixed pace, the ringbuffer can absorb the burst nature of decoder output.

fifo
^^^^

A fifo is a first-in-first-out queue that passes by address. ``acquire_write`` returns a free payload container; the element writes into it and enqueues it via ``release_write``; ``acquire_read`` dequeues the filled payload from the front for downstream to read. Data is passed by address in the queue without internal memcpy.

.. code:: c

    esp_gmf_db_new_fifo(capacity, &db);

Suitable for scenarios where upstream and downstream rates are similar, access granularity is consistent, and blocking semantics are needed. A fifo does not support "reading an arbitrary number of bytes"; each acquire returns a complete data block. A fifo's payload memory can be provided externally; to specify write-side buffer alignment, call :cpp:func:`esp_gmf_fifo_set_align` before the first ``acquire_write``.

block
^^^^^

A block emphasizes "one entire block at a time" semantics. At creation, ``num`` buffers of ``buf_size`` bytes are pre-allocated; the write side obtains a free block via ``acquire_write``, fills it, and submits it to the read side via ``release_write``.

.. code:: c

    esp_gmf_db_new_block(num, buf_size, &db);

Suitable for whole-frame transfer (such as one video frame), fixed object sizes, and zero-copy requirements. The difference from fifo lies in buffer management: fifo's payload memory can be provided externally, while block's buffers are pre-allocated by the data bus at creation time.

pbuf
^^^^

A pbuf is a payload pointer queue. It does not allocate buffers; it only queues payload pointers. Both ``acquire`` and ``release`` are non-blocking calls.

.. code:: c

    esp_gmf_db_new_pbuf(depth, &db);

Suitable for scenarios where data is already in a fixed memory region (such as a DMA capture buffer). The producer submits a payload pointer carrying the buffer address to pbuf; downstream then obtains the same payload pointer. Since pbuf does not block, the producer must ensure the queue does not overflow on its own.

Both block and pbuf pass data as entire payload blocks. If a downstream element needs to read by an arbitrary number of bytes, it can use the byte cache inside the element: load the acquired block payload into ``esp_gmf_cache``, then read data at a fixed or arbitrary length via ``esp_gmf_cache_acquire``. This preserves the zero-copy transfer of block/pbuf while converting the element's internal access granularity to byte-level reading.

Selection Guide
---------------

.. list-table::
   :widths: 28 16 24 12 20
   :header-rows: 1

   * - Requirement Characteristics
     - Recommended
     - Buffer Source
     - Copy
     - Blocking
   * - Byte access, large rate difference
     - ringbuffer
     - Internal circular buffer of the data bus
     - Yes
     - Yes
   * - Arbitrary-size writes, zero-copy
     - fifo
     - Externally provided payload / buffer
     - No
     - Yes
   * - Whole-frame data, zero-copy
     - block
     - Pre-allocated fixed blocks in the data bus
     - No
     - Yes
   * - Direct DMA buffer passing
     - pbuf
     - Payload pointer provided by producer
     - No
     - No

Recommended selection order: first determine whether memcpy is acceptable, then determine whether reads and writes need to block, and finally determine whether data is passed in fixed blocks. When "reading by an arbitrary number of bytes" is needed, ringbuffer is typically chosen; if upstream uses block or pbuf to pass entire blocks, the element can also use the byte cache internally to achieve byte-level reading.

Common Interface
----------------

Read, write, reset, and status query operations for the data bus are provided by a unified interface in ``esp_gmf_data_bus.h``. A port binds to one data bus at creation time and subsequently accesses it via the same acquire-release interface.

.. list-table::
   :widths: 32 68
   :header-rows: 1

   * - API
     - Function
   * - ``esp_gmf_db_acquire_write``
     - Acquire a writable payload block
   * - ``esp_gmf_db_release_write``
     - Submit the written payload to the read side
   * - ``esp_gmf_db_acquire_read``
     - Acquire a readable payload block
   * - ``esp_gmf_db_release_read``
     - Return the read payload to the write side
   * - ``esp_gmf_db_reset``
     - Clear the buffer and reset internal state
   * - ``esp_gmf_db_get_total_size`` / ``esp_gmf_db_get_filled_size``
     - Query capacity and filled bytes
   * - ``esp_gmf_db_get_available``
     - Query current writable space

Normally elements do not call these APIs directly; they access the data bus indirectly through the port's ``esp_gmf_port_acquire_*``. For ports and the acquire-release protocol, see :doc:`gmf-core-data-path`.

Some implementations also provide dedicated configuration interfaces. For example, fifo allows calling :cpp:func:`esp_gmf_fifo_set_align` before the first write-side acquire to set the buffer address alignment, useful for DMA or PSRAM cache alignment scenarios.

EOF and Abort Control
---------------------

In addition to the normal read/write loop, the data bus provides four flow control interfaces to express the EOF and abort semantics.

.. list-table::
   :widths: 26 30 44
   :header-rows: 1

   * - API
     - Effect
     - Typical Scenario
   * - :cpp:func:`esp_gmf_db_done_write`
     - Sets EOF; causes ``acquire_read`` to return a payload with ``is_done`` set
     - Data source has reached the end
   * - :cpp:func:`esp_gmf_db_reset_done_write`
     - Clears EOF
     - Switching to a new data source, reusing the data bus
   * - :cpp:func:`esp_gmf_db_abort`
     - Wakes all blocked calls, returns ``ESP_GMF_IO_ABORT``
     - Emergency stop, error recovery
   * - :cpp:func:`esp_gmf_db_clear_abort`
     - Only clears the abort flag; buffer data is preserved
     - Continue reading after error recovery

The semantic difference between ``done`` and ``abort``: ``done`` means data has ended naturally; downstream processes remaining data and then enters the end flow. ``abort`` means immediate termination; subsequent read/write calls return ``ESP_GMF_IO_ABORT``. Both cause the task to exit the running phase, entering ``FINISHED`` and ``STOPPED`` paths respectively (see the task scheduling section in :doc:`gmf-core-pipeline`).

The difference between ``clear_abort`` and ``reset``: :cpp:func:`esp_gmf_db_reset` clears buffered data and resets internal state; ``clear_abort`` only clears the abort status flag. To continue processing data retained in the buffer after recovery, use ``clear_abort``; to discard old data and start fresh, use ``reset``.

API Reference
-------------

Unified interface and common types for the four implementations:

- ``esp_gmf_data_bus.h``: Data bus common interface (acquire / release / done / abort / reset)
- ``esp_gmf_new_databus.h``: Creation interfaces for the four implementations

For implementation-specific header files, consult ``esp_gmf_ringbuffer.h`` / ``esp_gmf_fifo.h`` / ``esp_gmf_block.h`` / ``esp_gmf_pbuf.h`` as needed. Normal reads and writes are completed via the data bus common interface; implementation-specific configuration interfaces are only needed when using implementation-specific features.

.. include-build-file:: inc/esp_gmf_data_bus.inc

.. include-build-file:: inc/esp_gmf_new_databus.inc

.. include-build-file:: inc/esp_gmf_ringbuffer.inc

.. include-build-file:: inc/esp_gmf_fifo.inc

.. include-build-file:: inc/esp_gmf_block.inc

.. include-build-file:: inc/esp_gmf_pbuf.inc
