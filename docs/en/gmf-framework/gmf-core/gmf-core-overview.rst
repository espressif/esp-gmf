GMF-Core Overview
=================

:link_to_translation:`zh_CN:[中文]`

GMF-Core is the core framework of ESP-GMF, defining the element, payload, port, data bus, task, and lifecycle state machine. Multimedia algorithms such as decoders, encoders, resamplers, and mixers are integrated into the framework as elements.

This document covers the overall architecture and main objects. For details, see the dedicated chapters: :doc:`gmf-core-pipeline` covers pipeline and task collaboration and scheduling; :doc:`gmf-core-element` covers the element lifecycle, capabilities, and methods; :doc:`gmf-core-data-path` covers the data flow of payload, port, and IO; :doc:`gmf-core-databus` covers the data bus implementation and flow control; :doc:`gmf-core-fourcc` and :doc:`gmf-core-utils` can be consulted as needed.

Upper-layer applications can use GMF-Core indirectly through encapsulation components such as ``esp_audio_simple_player`` and ``esp_capture``. Direct GMF-Core programming is only required when developing custom elements.

Component Layers
----------------

GMF-Core divides the multimedia processing pipeline into four layers, each addressing a separate concern.

.. list-table::
   :widths: 12 24 64
   :header-rows: 1

   * - Layer
     - Module
     - Responsibilities
   * - Registry Layer
     - Pool
     - Centrally registers element and IO templates. At application startup, all available elements are registered once; when building a pipeline, instances are copied by name.
   * - Orchestration Layer
     - Pipeline, Task
     - A pipeline connects multiple elements in sequence; a task drives the elements on the pipeline according to defined rules.
   * - Processing Layer
     - Element
     - Each element implements three phases: ``open`` for one-time initialization, ``process`` for processing a block of data, and ``close`` for cleanup. Derives into audio, video, and picture subclasses.
   * - Data Layer
     - Payload, Port, Data Bus
     - Data flows between elements as payloads. Ports provide read/write interfaces for elements. The data bus handles queuing, buffering, and synchronization below the ports.

.. only:: html

   .. mermaid::

      flowchart TB
          subgraph registry ["Registry Layer"]
              Pool["Pool"]
          end

          subgraph orchestrate ["Orchestration Layer"]
              Pipeline["Pipeline"]
              Task["Task"]
              Pipeline -->|"Bind"| Task
          end

          subgraph process ["Processing Layer"]
              ElemA["Element A"] -->|"Payload"| ElemB["Element B"]
          end

          subgraph data ["Data Layer"]
              direction LR
              Port["Port"] --> DataBus["Data Bus"] --> Payload["Payload"]
          end

          Pool -->|"Instantiate"| Pipeline
          Pipeline -->|"Link"| ElemA
          Task -->|"Drive"| ElemA
          ElemA -->|"Read/Write"| Port

This layered design allows each part to be extended independently. Adding a new audio algorithm requires only implementing the element interface; building a new playback pipeline only requires selecting elements from the pool and composing them; adjusting data buffering strategy only requires replacing the corresponding data bus. The three types of work are decoupled and can be developed and adjusted independently.

Basic Object Model
------------------

All objects in the framework that can be registered, copied, or destroyed inherit from a common base class :cpp:type:`esp_gmf_obj_t`. The base class handles no business logic; it only defines how objects are created (``new_obj`` function pointer), how they are destroyed (``del_obj`` function pointer), and provides a string tag ``tag`` and a configuration data pointer ``cfg``.

There are three direct derived classes: ``esp_gmf_io_t`` represents an IO object (such as a file IO or I2S IO), ``esp_gmf_task_t`` represents a task, and ``esp_gmf_element_t`` represents an element. Elements are further derived into audio, video, and picture subclasses, each carrying the corresponding format information.

.. only:: html

   .. mermaid::

      classDiagram
          direction TB

          class esp_gmf_obj_t
          class esp_gmf_io_t
          class esp_gmf_task_t
          class esp_gmf_element_t
          class esp_gmf_audio_element_t
          class esp_gmf_video_element_t
          class esp_gmf_pic_element_t

          esp_gmf_obj_t <|-- esp_gmf_io_t
          esp_gmf_obj_t <|-- esp_gmf_task_t
          esp_gmf_obj_t <|-- esp_gmf_element_t
          esp_gmf_element_t <|-- esp_gmf_audio_element_t
          esp_gmf_element_t <|-- esp_gmf_video_element_t
          esp_gmf_element_t <|-- esp_gmf_pic_element_t

The unified inheritance allows the pool to maintain a single :cpp:type:`esp_gmf_obj_t` \* linked list that stores templates of any type. When needed, :cpp:func:`esp_gmf_obj_dupl` copies a fresh instance from a template. The "register template first, instantiate on demand" pattern in the framework relies on this unified object interface.

Main Objects
------------

Pipeline
^^^^^^^^

A pipeline is the task orchestrator. It connects several elements in sequence and exposes control interfaces such as ``run``, ``stop``, ``pause``, ``resume``, ``reset``, and ``seek`` as a whole. From the application's perspective, operating on a single pipeline object is enough to start, pause, or stop the entire processing flow.

A pipeline also handles three types of internal coordination: opening and closing the head and tail IOs (file, I2S, etc.), forwarding format information reported by elements to downstream elements, and coordinating cleanup for all elements when an error or end-of-stream occurs. The pipeline itself does not execute element code; actual execution is done by the bound task. For construction and control details, see :doc:`gmf-core-pipeline`.

Task
^^^^

A task is the executor of the processing flow, essentially a runtime unit encapsulated by the framework. A task maintains a job list and calls each element's ``open``, ``process``, and ``close`` in the defined order. Each pipeline binds one task at runtime in a one-to-one relationship.

The pipeline and task are divided into two objects with different responsibilities. The pipeline describes the "connection topology," while the task describes the "runtime resources" (stack size, priority, CPU core). The same pipeline can be bound to tasks with different configurations, and the same task template can be reused repeatedly. For the scheduling model, see :doc:`gmf-core-pipeline`.

Element
^^^^^^^

An element is the concrete work executor; each element corresponds to one specific task (such as MP3 decoding, resampling, or mixing). Each element implements three fixed-phase callback functions: ``open`` for one-time initialization, ``process`` for processing a block of data, and ``close`` for releasing resources. The task first calls open, then repeatedly calls process during operation according to the job configuration (commonly ``ESP_GMF_JOB_TIMES_INFINITE``), until the element returns done, an error occurs, or a stop request is received, and finally calls close.

Based on the type of data processed, elements derive into three subclasses: audio (``esp_gmf_audio_element_t``), video (``esp_gmf_video_element_t``), and picture (``esp_gmf_pic_element_t``), each carrying format information such as sample rate/channels, resolution/frame rate, or width/height. For lifecycle details, interface implementation, and custom element templates, see :doc:`gmf-core-element`.

Payload
^^^^^^^

A payload is the unified container for data flowing between elements. Whether it is a decoded PCM frame, a compressed H.264 segment, or an HTTP chunk, it is all carried as ``esp_gmf_payload_t`` inside the framework. This structure contains a data buffer pointer, total buffer length, valid data length, end-of-stream flag, timestamp, and other fields.

A unified payload allows the framework to perform data transfer, buffering, and thread synchronization without depending on specific media types, avoiding each element from defining its own data structures. For payload fields, lifecycle, and ownership rules, see :doc:`gmf-core-data-path`.

Port
^^^^

A port is the data read/write interface exposed by an element. Each element has one input port and one output port, connected to upstream and downstream respectively. A port does not store data itself; it only provides the interface to "acquire a payload to read or write." The actual data resides in the underlying data bus.

Ports use an acquire-release access protocol: when an element needs to read data, it first acquires a payload, then releases it when done; when writing data, it first acquires an empty payload, fills it, then releases it to submit to downstream. This pattern defines data ownership boundaries, allowing elements to safely read and write payloads between calls without worrying about thread synchronization. For the detailed protocol, see :doc:`gmf-core-data-path`.

Data Bus
^^^^^^^^

A data bus is the actual data queue beneath a port, responsible for buffer allocation, cross-thread synchronization, and queuing payloads between producers and consumers. GMF-Core provides four implementations, targeting different copy strategies, blocking semantics, and data granularity. A port binds to one of them at creation time; the framework encapsulates the underlying differences, and element code uses only the unified port interface.

For the trade-offs, selection guide, and flow control interfaces of the four implementations, see :doc:`gmf-core-databus`.

Pool
^^^^

The pool is a template library for elements and IOs. At application startup, all supported elements (MP3 decoder, resampler, file IO, I2S IO, etc.) are registered to the pool at once; when building a pipeline, the application only provides a list of names, and the pool copies instances from templates and chains them in order.

Separating "registered templates" from "used instances" allows the same set of elements to be composed into many different pipelines. The system can have dozens of pipeline combinations simultaneously; using the pool requires registering a template only once, rather than writing a separate build process for each combination. In addition to looking up by name, the pool can automatically select a matching IO by scoring URLs (for example, ``http://`` matches the HTTP IO and ``file://`` matches the file IO), making it easy for upper-layer applications to switch data sources as needed. For usage, see :doc:`gmf-core-pipeline`.

Lifecycle States
----------------

The pipeline and task share a common lifecycle state enumeration :cpp:type:`esp_gmf_event_state_t`. Applications register event callbacks to be notified of every state transition.

.. list-table::
   :widths: 32 10 58
   :header-rows: 1

   * - State
     - Value
     - Meaning
   * - ``ESP_GMF_EVENT_STATE_NONE``
     - 0
     - Object just created, not yet initialized
   * - ``ESP_GMF_EVENT_STATE_INITIALIZED``
     - 1
     - Init complete, ready to start
   * - ``ESP_GMF_EVENT_STATE_OPENING``
     - 2
     - Element open phase in progress
   * - ``ESP_GMF_EVENT_STATE_RUNNING``
     - 3
     - Cyclic process phase being scheduled
   * - ``ESP_GMF_EVENT_STATE_PAUSED``
     - 4
     - Paused, context preserved
   * - ``ESP_GMF_EVENT_STATE_STOPPED``
     - 5
     - User-initiated stop, close in progress
   * - ``ESP_GMF_EVENT_STATE_FINISHED``
     - 6
     - Data ended naturally (triggered by ``is_done`` in payload)
   * - ``ESP_GMF_EVENT_STATE_ERROR``
     - 7
     - A job returned failure, cleanup in progress

.. only:: html

   .. mermaid::

      stateDiagram-v2
          direction TB

          [*] --> NONE
          NONE --> INITIALIZED : init
          INITIALIZED --> OPENING : run
          OPENING --> RUNNING : opens done
          OPENING --> ERROR : open fail
          RUNNING --> PAUSED : pause
          PAUSED --> RUNNING : resume
          RUNNING --> STOPPED : stop
          RUNNING --> FINISHED : is_done
          RUNNING --> ERROR : proc fail
          STOPPED --> INITIALIZED : reset
          FINISHED --> INITIALIZED : reset
          ERROR --> INITIALIZED : reset

The three terminal states ``STOPPED``, ``FINISHED``, and ``ERROR`` can all return to ``INITIALIZED`` via ``reset``, meaning the same pipeline can be run and stopped repeatedly without being rebuilt.

There is no independent "abort state" in the enumeration. When an element's ``process`` returns ``ESP_GMF_JOB_ERR_ABORT``, the task by default follows the ``STOPPED`` path to complete cleanup. If the application registers a strategy function via ``esp_gmf_task_set_strategy_func``, it can redirect abort to the ``RESET`` path, allowing the pipeline to automatically return to ``INITIALIZED``. For the detailed behavior of the abort flow, see :doc:`gmf-core-pipeline`.

Event Types
-----------

Events are carried by :cpp:type:`esp_gmf_event_pkt_t` and are divided into three categories:

- **State Change Events (CHANGE_STATE)**: Reported by the task to inform the application of the current lifecycle phase of the pipeline. User code can use this event to update the UI or trigger the next operation.
- **Format Info Events (REPORT_INFO)**: Reported by an element to inform the framework of the format it has parsed (for example, an MP3 decoder reports sample rate and channel count after reading the file header). When the pipeline receives this event, it writes the format information into downstream dependent elements and registers the open/process jobs for those elements.
- **Load Job Events (LOADING_JOB)**: Emitted when an element needs to re-register a job. When the pipeline receives this, it updates the task's job list. User code does not handle this event directly.

Events form a bidirectional flow within the pipeline: the task reports state changes to the pipeline, and the pipeline forwards them to user callbacks; elements report format information to the pipeline, and the pipeline writes it into downstream dependent elements and registers the corresponding jobs.

Typical Application Topologies
------------------------------

The following topologies demonstrate the composition capabilities of pipelines. Simple scenarios require only one pipeline; complex scenarios use multiple cascaded pipelines to share the workload.

Single-Channel Decode and Playback
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A file IO serves as the pipeline's IN, an I2S IO serves as OUT, with a decoder and resampler chained in between, all driven by one task. This structure is suitable for local music playback applications.

.. only:: html

   .. mermaid::

      flowchart LR
          File(("File")) --> Dec["Decoder"]
          Dec --> Rs["Resample"]
          Rs --> I2S(("I2S"))

Callback-Driven without IO
^^^^^^^^^^^^^^^^^^^^^^^^^^

IN/OUT are not connected to IO objects; instead, user code registers callbacks via ``esp_gmf_port_set_cb`` to provide or receive data. This is suitable for pipelines connected to Bluetooth audio, HTTP streams, or custom protocol stacks, where the external layer only needs to handle data input and output within the callbacks.

.. only:: html

   .. mermaid::

      flowchart LR
          InCb(("Input<br/>Callback")) --> Dec["Decoder"]
          Dec --> Rs["Resample"]
          Rs --> OutCb(("Output<br/>Callback"))

Bridging Two Pipelines with a Ringbuffer
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

When the processing rates of upstream and downstream differ significantly, divide the flow into two independent pipelines with a ringbuffer as the intermediate buffer. Each segment has its own task, which can be configured with different stack sizes and priorities: the first segment uses a larger stack for MP3 decoding, while the second uses a smaller stack for output scheduling.

.. only:: html

   .. mermaid::

      flowchart LR
          subgraph p1 ["Pipeline 1"]
              File(("File"))
              Dec["MP3<br/>Decoder"]
          end
          subgraph p2 ["Pipeline 2"]
              Rs["Resampler"]
              Out(("Output"))
          end
          File --> Dec
          Dec -->|"ringbuffer"| Rs
          Rs --> Out

Multi-Input Mixing
^^^^^^^^^^^^^^^^^^

Two upstream pipelines decode different audio sources and feed them into a mixing pipeline that combines and outputs the result. A typical application is overlaying navigation voice on background music: the main pipeline continuously plays music, and when navigation is triggered, a prompt audio pipeline is started; both PCM streams are sent to a mixer and written to I2S. Stopping the prompt audio only requires stopping the corresponding pipeline, leaving the main playback unaffected.

.. only:: html

   .. mermaid::

      flowchart LR
          subgraph p1 ["Background Music"]
              F1(("File"))
              D1["MP3"]
          end
          subgraph p2 ["Prompt Audio"]
              H1(("HTTP"))
              D2["MP3"]
          end
          subgraph pm ["Mix Output"]
              Mix["Mixer"]
              Out(("I2S"))
          end
          F1 --> D1
          D1 --> Mix
          H1 --> D2
          D2 --> Mix
          Mix --> Out

One-to-Many Split
^^^^^^^^^^^^^^^^^

A copier element copies one input stream into multiple outputs. For example, while playing music, the same audio is sent to an AEC algorithm as the echo reference signal: the music played by the speaker is picked up by the microphone, and AEC needs the original music as a reference to cancel the echo from the microphone signal.

.. only:: html

   .. mermaid::

      flowchart LR
          subgraph p1 ["Decode"]
              F(("File"))
              Dec["Decoder"]
              Cp{"Copier"}
          end
          subgraph p2 ["Playback"]
              R1["Resample"]
              I2S(("I2S"))
          end
          subgraph p3 ["AEC Reference"]
              R2["Resample"]
              Rec(("Recorder"))
          end
          F --> Dec
          Dec --> Cp
          Cp --> R1
          R1 --> I2S
          Cp --> R2
          R2 --> Rec

Component Directory
-------------------

.. code:: text

    gmf_core/
    ├── include/          Public header files
    ├── src/              Base types and framework implementation
    ├── data_bus/         Four data bus implementations
    ├── oal/              OS abstraction layer
    ├── helpers/          URI parsing and FourCC validation utilities
    ├── docs/             Doxygen configuration
    └── test_apps/        Unit tests and integration test cases

GMF-Core is built on top of ESP-IDF and depends on event groups, atomic operations, ``esp_common``, ``log``, and other system capabilities. It has no dependencies on any audio or video algorithm library: all decoders, encoders, and filters are provided as elements by upper-layer components (such as ``gmf_audio`` and ``gmf_video``).

API Reference
-------------

Base types and object hierarchy covered in this document:

- ``esp_gmf_obj.h``: Object base class
- ``esp_gmf_event.h``: State enumeration and event packets
- ``esp_gmf_err.h``: Error code definitions
- ``esp_gmf_info_file.h``: File information structure

FourCC macro definitions are detailed in :doc:`gmf-core-fourcc` and are not repeated here.

.. include-build-file:: inc/esp_gmf_obj.inc

.. include-build-file:: inc/esp_gmf_event.inc

.. include-build-file:: inc/esp_gmf_err.inc

.. include-build-file:: inc/esp_gmf_info_file.inc
