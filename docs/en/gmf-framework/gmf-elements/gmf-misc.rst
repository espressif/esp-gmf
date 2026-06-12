GMF-Misc
========

:link_to_translation:`zh_CN:[中文]`

gmf_misc collects utility-type elements in ESP-GMF that have a single function and do not belong to the three main categories of codec / IO / effects. Currently it contains only ``copier``: a single-input multiple-output element that broadcasts one frame of payload from upstream to N downstream paths at once, implementing a fan-out topology in conjunction with the pipeline cascading mechanism. For the element base class, see :doc:`/gmf-framework/gmf-core/gmf-core-element`; for the data path, see :doc:`/gmf-framework/gmf-core/gmf-core-data-path`; for pipeline composition, see :doc:`/gmf-framework/gmf-core/gmf-core-pipeline`.

Feature List
------------

- copier: broadcasts the payload from a single input port to multiple output ports; the first output shares the same data as the input (zero-copy), while other outputs each copy the data via ``esp_gmf_payload_copy_data``
- Port type: both input and output ports support ``ESP_GMF_PORT_TYPE_BYTE`` and ``ESP_GMF_PORT_TYPE_BLOCK``, automatically adapting to upstream and downstream
- Pass-through ``is_done`` flag: the end-of-stream marker in the input payload is passed to the first output, allowing downstream pipelines to terminate naturally

Technical Details
-----------------

How copier Works
^^^^^^^^^^^^^^^^

copier is a tool for implementing "fan-out" topology in a pipeline. On the ``esp_gmf_element_t`` base class, the input port attribute is set to ``ESP_GMF_EL_PORT_CAP_SINGLE`` (only 1 input), and the output port attribute is set to ``ESP_GMF_EL_PORT_CAP_MULTI`` (allows multiple connections). After registering to the pool, the application connects multiple pipelines / ports to copier's output via ``esp_gmf_pipeline_connect_pipe`` or ``esp_gmf_pipeline_reg_el_port``; at runtime, copier performs the following in each ``process``:

1. ``acquire_in`` retrieves one frame of input payload; the length is determined by ``data_length``.
2. Iterate over the output port list, for each output:

   - The first output (list head) is handled specially: ``out_load`` points to the same input payload, syncing only ``valid_size`` and ``is_done``; on ``release_out``, it is passed to downstream by reference count, avoiding memory copying;
   - Other outputs each ``acquire_out`` an output payload of capacity >= valid input bytes, call ``esp_gmf_payload_copy_data`` to copy the input data, then ``release_out``.

3. After all outputs are released, ``release_in`` returns the input payload.

Each output's ``release_out`` uses its respective port's ``wait_ticks``; the application can set different blocking times for different downstream elements when adding ports, avoiding the entire copier from blocking when one downstream is slow. Regarding return values: when the input payload's ``is_done`` is triggered, ``ESP_GMF_JOB_ERR_DONE`` is returned directly, causing the task to remove copier from the job list; under normal conditions, it returns the current frame's ``valid_size``.

Configuration
^^^^^^^^^^^^^

:cpp:type:`esp_gmf_copier_cfg_t` has only one field:

.. code:: c

    typedef struct {
        uint8_t copy_num;  /* number of copies */
    } esp_gmf_copier_cfg_t;

``copy_num`` declares the number of planned output connections at initialization. The actual number of connections to copier is determined by the number of output ports bound when the pipeline is created; ``copy_num`` is primarily used for resource estimation and logging.

Code Example
^^^^^^^^^^^^

After registering copier to the pool, it can appear in the pipeline string by name ``"copier"``:

.. code:: c

    esp_gmf_copier_cfg_t cfg = { .copy_num = 2 };
    esp_gmf_obj_handle_t copier = NULL;
    esp_gmf_copier_init(&cfg, &copier);
    esp_gmf_pool_register_element(pool, copier, NULL);

    /* Main pipeline: File → Decoder → Copier → Resample → I2S */
    const char *p1_els[] = {"aud_dec", "copier", "aud_rate_cvt"};
    esp_gmf_pipeline_handle_t p1 = NULL;
    esp_gmf_pool_new_pipeline(pool, "io_file", p1_els, 3, "io_codec_dev", &p1);

    /* Side pipeline: Second output of Copier → Resample → File */
    const char *p2_els[] = {"aud_rate_cvt"};
    esp_gmf_pipeline_handle_t p2 = NULL;
    esp_gmf_pool_new_pipeline(pool, NULL, p2_els, 1, "io_file", &p2);

    /* Connect the output port of copier in p1 to the resample input port of p2 */
    esp_gmf_pipeline_connect_pipe(p1, "copier", NULL, p2, "aud_rate_cvt", NULL);

For the complete "fan-out" topology combination (event cascading, prev_run coordination), see :doc:`/gmf-framework/gmf-core/gmf-core-pipeline`.

Application Examples
--------------------

- ``elements/test_apps/main/elements/gmf_audio_play_el_test.c``: Comprehensive use case demonstrating copier combined with audio elements
- :doc:`/gmf-framework/gmf-core/gmf-core-overview` "fan-out splitting" typical topology: simultaneously sends the same audio to the AEC reference path while playing music

FAQ
---

**Q:** What is the difference between copier's first output and the others?

The first output shares the same payload as the input (zero-copy); the other outputs each allocate and copy a separate payload via ``esp_gmf_payload_copy_data`` in ``process``. Connect the downstream with the highest latency and throughput requirements to the first output to avoid one memcpy.

**Q:** Is there a limit on how many outputs copier can have?

There is no fixed upper limit at the source code level; ``copy_num`` is only used for resource estimation. The actual limit depends on memory: each additional output adds one payload buffer. Plan according to downstream element throughput and buffer together.

**Q:** Will one slow downstream affect the others?

Yes. copier serially calls ``release_out`` in list order during ``process``; when one output blocks, the others also wait. Place latency-sensitive downstream elements first (first output), and set shorter ``wait_ticks`` for slower ports; when the timeout is reached, copier skips that release and logs a warning.

**Q:** Can copier be placed at the end of a pipeline?

Yes, but when placed at the end it cannot split data to subsequent elements. The common usage is to place copier in the middle, with some output ports connected to subsequent elements in the current pipeline and others connected to a branch pipeline via ``esp_gmf_pipeline_connect_pipe``.

API Reference
-------------

Header files for this component:

- ``esp_gmf_copier.h``: copier configuration and initialization

.. include-build-file:: inc/esp_gmf_copier.inc
