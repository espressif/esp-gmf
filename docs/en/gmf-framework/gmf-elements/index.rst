GMF Elements
============

:link_to_translation:`zh_CN:[中文]`

Elements are the mid-layer processing units in ESP-GMF, responsible for specific media data operations: encoding and decoding, format conversion, effects processing, and IO read/write. Each element inherits from ``esp_gmf_element_t`` and exposes a uniform lifecycle interface (open / process / close) along with an acquire-release data interface based on ports. For the element base class, lifecycle, and capability descriptors, see :doc:`/gmf-framework/gmf-core/gmf-core-element`; for data path details, see :doc:`/gmf-framework/gmf-core/gmf-core-data-path`.

.. list-table::
   :widths: 20 30 50
   :header-rows: 1

   * - Category
     - Directory
     - Role
   * - I/O Elements
     - ``elements/gmf_io``
     - Interface to external data sources/sinks: file, http, embed_flash, i2s_pdm, codec_dev
   * - Audio Elements
     - ``elements/gmf_audio``
     - Audio encoding/decoding, sample rate/channel/bit depth conversion, EQ, Mixer, Fade, ALC, DRC
   * - Video Elements
     - ``elements/gmf_video``
     - Video encoding/decoding, PPA acceleration, scaling, rotation, overlay, frame rate conversion
   * - AI Audio Elements
     - ``elements/gmf_ai_audio``
     - Speech algorithms: AEC, NS, AGC, VAD, WWE, VCMD
   * - Miscellaneous
     - ``elements/gmf_misc``
     - Tool-type elements such as Copier, and dynamic selection capability for gmf_loader

.. toctree::
    :maxdepth: 1

    gmf-io
    gmf-audio
    gmf-video
    gmf-ai-audio
    gmf-misc
