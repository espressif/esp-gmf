GMF Elements
============

:link_to_translation:`zh_CN:[中文]`

An element is the object in GMF-Core that carries concrete processing logic. Steps such as decoding, encoding, resampling, mixing, and image scaling are all integrated into the pipeline as elements; the pipeline is responsible for orchestrating connections, the task is responsible for scheduling element callbacks, and the element only implements its own initialization, processing, and resource cleanup logic.

For payload, port, and the acquire-release protocol, see :doc:`gmf-core-data-path`. For pipeline orchestration, task scheduling, lifecycle, and control interfaces, see :doc:`gmf-core-pipeline`. For the overall object relationships, see :doc:`gmf-core-overview`.

Lifecycle and Object Structure
------------------------------

An element inherits from the object base class ``esp_gmf_obj_t``, which provides tag, configuration data, copy, and destroy interfaces; the element base class :cpp:type:`esp_gmf_element_t` adds lifecycle callback functions, input/output ports, capability description, runtime methods, and an event receiver interface.

An element must implement three main callback functions:

- ``open``: One-time resource initialization, such as creating algorithm handles, reading configuration, and computing internal parameters based on upstream format info.
- ``process``: Execute one data processing step, typically acquiring a payload from the input port, processing it, and writing to the output port.
- ``close``: Release resources allocated in the ``open`` phase. Regardless of whether the pipeline ends normally, is stopped, or encounters an error, the framework attempts to execute close for each opened element.

An element can also implement ``reset`` and ``event_receiver``. The former resets internal state; the latter receives upstream format info, pipeline events, or application-side events. Elements that require upstream format info to open should set ``dependency = true`` in their configuration, so the pipeline registers their open/process jobs after receiving the REPORT_INFO event; for the flow, see the dependent element section in :doc:`gmf-core-pipeline`.

Based on media type, GMF-Core provides three derived element classes.

.. list-table::
   :widths: 34 34 32
   :header-rows: 1

   * - Derived Class
     - Format Info
     - Typical Fields
   * - :cpp:type:`esp_gmf_audio_element_t`
     - ``esp_gmf_info_sound_t``
     - Sample rate, channels, bit depth, FourCC
   * - :cpp:type:`esp_gmf_video_element_t`
     - ``esp_gmf_info_video_t``
     - Resolution, frame rate, FourCC
   * - :cpp:type:`esp_gmf_pic_element_t`
     - ``esp_gmf_info_pic_t``
     - Width, height, FourCC

Port Attributes
---------------

At initialization, an element declares constraints on its input and output ports, including port count, buffer alignment, port type, and recommended data size per acquire. These constraints are described by :cpp:type:`esp_gmf_element_port_attr_t` and are typically set via macros.

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

``ESP_GMF_EL_PORT_CAP_SINGLE`` means the port can only connect to one peer; ``ESP_GMF_EL_PORT_CAP_MULTI`` means multiple peers can share the port. For port type, payload ownership, and acquire-release call order, see :doc:`gmf-core-data-path`.

Capability Description
----------------------

Capability description is the mechanism by which an element declares its capabilities externally. It is not a single format field, but a set of capability nodes that can be queried externally: each :cpp:type:`esp_gmf_cap_t` identifies the capability category with ``cap_eightcc``, and can carry performance info ``perf`` and an attribute iterator function ``attr_fun``. Attributes are described by :cpp:type:`esp_gmf_cap_attr_t`, supporting discrete values, step ranges, multiple ranges, and constant values. For EIGHTCC capability identifier definitions, see :doc:`gmf-core-utils`; for media format FourCC definitions, see :doc:`gmf-core-fourcc`.

An element lazily loads its capability list via ``ops.load_caps``. The first time external code calls :cpp:func:`esp_gmf_element_get_caps`, the framework calls this callback and caches the result in the element object. A typical implementation:

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

Capability queries can occur during the pool phase or after the pipeline is built. Applications or upper-layer wrappers first iterate element templates in the pool, then read each element's capability description, and select suitable elements by capability category or attribute range. For example, a video pipeline can find elements with scaling, cropping, and rotation capabilities via ``cap_eightcc``; an AI audio pipeline can check whether an ai_afe simultaneously declares AEC, NS, AGC, and VAD capabilities.

Common query steps:

1. Call :cpp:func:`esp_gmf_element_get_caps` to get the capability list.
2. Call :cpp:func:`esp_gmf_cap_fetch_node` to find a capability by ``cap_eightcc``.
3. Call :cpp:func:`esp_gmf_cap_iterate_attr` or :cpp:func:`esp_gmf_cap_find_attr` to get capability attributes.
4. Call :cpp:func:`esp_gmf_cap_attr_check_value` to determine whether a target value is supported.

Attribute types describe different negotiation methods. Discrete attributes are suitable for listing a set of supported values, such as sample rates or pixel format sets; step attributes describe minimum, maximum, and step values, such as scaled width/height ranges; multiple attributes describe values that change by a factor; constant attributes describe fixed capabilities. Upper-layer pipeline builders can use these for dynamic selection, reducing hardcoded element names and parameters.

Runtime Methods
---------------

Runtime methods are the mechanism by which an element exposes runtime control actions externally, used for parameter setting or querying outside of open/process/close, such as setting the target sample rate, adjusting volume, switching EQ presets, or modifying target resolution. Each :cpp:type:`esp_gmf_method_t` contains a method name, execution function, and a list of parameter descriptions.

An element lazily loads its method list via ``ops.load_methods``. The framework calls this callback when needed when external code calls :cpp:func:`esp_gmf_element_get_method` or :cpp:func:`esp_gmf_element_exe_method`.

.. code:: c

    static esp_gmf_err_t set_volume(void *handle,
                                    esp_gmf_args_desc_t *args,
                                    uint8_t *buf, int len)
    {
        uint8_t volume = *buf;
        /* Update the element's internal volume state */
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

The caller executes by method name:

.. code:: c

    uint8_t volume = 80;
    esp_gmf_element_exe_method(el, "set_volume", &volume, sizeof(volume));

Method names should remain stable; parameter descriptions tell callers the parameter name, type, size, and offset. When multiple elements implement the same runtime method name, upper-layer applications can depend only on the method name rather than the specific element type; for example, software resampling and hardware ASRC can expose the same target sample rate setting method, and when the pipeline replaces the element, the upper-layer call logic remains unchanged.

Custom Element Template
-----------------------

The following provides a minimal audio element skeleton that connects the element lifecycle, port attributes, and acquire-release calls. The element multiplies input PCM by a gain factor and outputs it.

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

Key points of the template:

- The custom element struct places the derived base class as the first member, enabling casts between the derived class and the element base class.
- The configuration struct is bound to the object via ``esp_gmf_obj_set_config`` and retrieved in the open callback via ``OBJ_GET_CFG``.
- The three lifecycle callback functions are defined inside the element and assigned to ``base->ops`` at construction time.
- ``dependency = true`` declares that this element requires upstream format info before it can start. If the processing logic does not depend on upstream format, keep the default value of false.
- In process, ``acquire_in`` / ``release_in`` and ``acquire_out`` / ``release_out`` must be strictly paired; each error branch must release any already-acquired payloads, otherwise the port will leak.
- The ``is_done`` of the input payload must be propagated or converted according to processing semantics; return ``DONE`` on end-of-stream to let the framework remove the corresponding job from the list.

A constructed element can be registered in the pool, which then copies instances by name and builds pipelines. For pipeline construction and control interfaces, see :doc:`gmf-core-pipeline`.

API Reference
-------------

Core header files covered in this document:

- ``esp_gmf_element.h``: Element base class, lifecycle, port attributes, capability description, and runtime method access interfaces
- ``esp_gmf_audio_element.h`` / ``esp_gmf_video_element.h`` / ``esp_gmf_pic_element.h``: Three derived element classes
- ``esp_gmf_cap.h``: Capability description list, attribute descriptions, and attribute matching
- ``esp_gmf_method.h`` / ``esp_gmf_args_desc.h`` / ``esp_gmf_method_helper.h``: Runtime method descriptions, parameter descriptions, and helper calls

.. include-build-file:: inc/esp_gmf_element.inc

.. include-build-file:: inc/esp_gmf_audio_element.inc

.. include-build-file:: inc/esp_gmf_video_element.inc

.. include-build-file:: inc/esp_gmf_pic_element.inc

.. include-build-file:: inc/esp_gmf_cap.inc

.. include-build-file:: inc/esp_gmf_method.inc

.. include-build-file:: inc/esp_gmf_args_desc.inc

.. include-build-file:: inc/esp_gmf_method_helper.inc
