GMF Pipeline and Task Scheduling
================================

:link_to_translation:`zh_CN:[中文]`

This document follows a progressive structure of "Introduction - Principles - Basic Example - Advanced Topics - Miscellaneous," covering the collaboration, runtime scheduling, error handling, and composition of the pipeline, element, and task.

For element lifecycle, capability, runtime methods, and custom templates, see :doc:`gmf-core-element`. For data flow details between elements (payload, port, IO), see :doc:`gmf-core-data-path`. For the four data bus implementations, see :doc:`gmf-core-databus`. For the overall architecture and object relationships, see :doc:`gmf-core-overview`.

Introduction
------------

After reading this document, you will be able to:

- Understand how the task schedules the three callback functions of an element
- Know how events and states flow between the pipeline, task, and user code
- Use the pool to manage element templates in bulk and instantiate various pipelines on demand
- Choose the correct API for multi-pipeline composition, pause, stop, and error recovery
- Build a basic pipeline and start it running

This document only describes the orchestration and control layer. To write your own element, first read the element template in :doc:`gmf-core-element` and the acquire-release protocol in :doc:`gmf-core-data-path`.

Principles
----------

Responsibilities of Three Objects
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Multimedia processing is typically composed of multiple steps chained together, such as "read file - decode MP3 - resample - write I2S." Each step is an independent algorithm, and they must be started in the correct order, pass data step by step, and clean up together when an error or end-of-stream occurs. GMF-Core splits these concerns across three objects.

**An element is the concrete work executor**: It implements three callback functions: ``open``, ``process``, and ``close``. The framework guarantees these are called in the order open - process × N - close; element developers only need to focus on the processing logic of a single call.

**A pipeline is the orchestrator**: It connects multiple elements in sequence and exposes control interfaces such as ``run``, ``stop``, ``pause``, ``resume``, ``reset``, and ``seek``. Application code only interacts with the pipeline; event dispatching, IO open/close, and error cleanup are handled by the pipeline automatically. The pipeline itself does not execute element code.

**A task is the executor**: It is essentially a runtime unit encapsulated by the framework. A task maintains a job list and calls each element's callback functions in a defined order. The pipeline describes the "connection topology" and the task describes the "runtime configuration;" separating them allows the same pipeline to be bound to tasks with different configurations (different stack, priority, CPU core).

Overview of the three-object collaboration:

.. only:: html

   .. mermaid::

      flowchart TB
          User["User Code"]
          Pipeline["Pipeline"]
          Task["Task"]
          Element["Element Chain"]

          User -->|"run / stop / pause"| Pipeline
          Pipeline -->|"Control Commands"| Task
          Task -->|"State Events"| Pipeline
          Pipeline -->|"State Callback"| User
          Task -->|"Call Callbacks"| Element
          Element -->|"Format Info"| Pipeline

Runtime Sequence of Jobs and Events
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``open``, ``process``, and ``close`` of an element all exist as jobs inside the task. When jobs are scheduled and when events are called back follow a fixed sequence. The diagram below shows the complete flow of the control plane and event plane after a ``run`` call:

.. only:: html

   .. mermaid::

      sequenceDiagram
          participant U as User
          participant P as Pipeline
          participant T as Task
          participant E1 as Element 1
          participant E2 as Element 2

          U->>P: run()
          P->>T: Start task
          T-->>P: CHANGE_STATE: OPENING
          P-->>U: State callback

          T->>E1: open
          T->>E1: process (first round)
          E1-->>P: REPORT_INFO (format)
          P->>E2: Write format info + register open/process job
          T->>E2: open
          T->>E2: process (first round)

          T-->>P: CHANGE_STATE: RUNNING
          P-->>U: State callback

          loop running loop
              T->>E1: process
              T->>E2: process
          end

          Note over T: Trigger STOP / FINISH / ERROR
          T->>E1: close
          T->>E2: close
          T-->>P: CHANGE_STATE: STOPPED/FINISHED/ERROR
          P-->>U: State callback

The sequence contains three rules: ``CHANGE_STATE`` is reported by the task and forwarded by the pipeline to user callbacks; ``REPORT_INFO`` is reported by the element, and the pipeline uses it to write format info into downstream dependent elements; non-dependent elements register open/process jobs at ``loading_jobs``, while dependent elements register open/process jobs after receiving upstream format info.

Element Lifecycle and Three Derived Types
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Each element must implement three callback functions, corresponding to the initialization, data processing, and resource cleanup phases, which the framework calls in a fixed order.

**open**: One-time initialization. The element configures itself based on format information provided by upstream, for example, an MP3 decoder initializes its decoder context, allocates work buffers, and configures filter coefficients based on sample rate. If open succeeds, the element waits for process to be scheduled; if it returns failure, the pipeline immediately enters ``ERROR`` and subsequent elements' open calls are not executed.

**process**: The data processing main loop. This callback is called repeatedly, each time executing one round of acquire/process/release on the input and output ports. The return value of process determines what the task does next (continue, report completion, report error); specific return codes are listed in the next section.

**close**: Resource cleanup. Whether the pipeline exits due to normal end-of-stream, user stop, or error, the framework calls the close of each element that has been opened in sequence. The close implementation should correspond one-to-one with resources allocated in open; even if an error occurs, it is recommended to only log the error and let the framework continue executing subsequent elements' close calls, to avoid resource leaks.

Based on the type of data processed, elements derive into three subclasses, differing in the specialized format information structure each carries.

.. list-table::
   :widths: 36 30 34
   :header-rows: 1

   * - Derived Class
     - Info Structure
     - Fields
   * - ``esp_gmf_audio_element_t``
     - ``esp_gmf_info_sound_t``
     - Sample rate, channels, bit depth, FourCC
   * - ``esp_gmf_video_element_t``
     - ``esp_gmf_info_video_t``
     - Resolution, frame rate, FourCC
   * - ``esp_gmf_pic_element_t``
     - ``esp_gmf_info_pic_t``
     - Width, height

Formats are identified by FourCC. For all macro definitions and a table organized by category, see :doc:`gmf-core-fourcc`.

Task Three-Phase Scheduling
^^^^^^^^^^^^^^^^^^^^^^^^^^^

After the task starts, it executes the job list in three phases: opening - running - cleanup.

**opening**: The task iterates through the job list, calls open for each element in turn, and immediately calls process once on success. The first process call allows elements to report format info early, so downstream dependent elements can be awakened. If an element's open returns failure, the pipeline immediately enters ``ERROR``.

**running**: Calls process in the order elements are chained, looping continuously. Each round ends and immediately begins the next, until one of the following exit conditions is triggered:

- An element's process returns ``ESP_GMF_JOB_ERR_DONE``; the element is permanently complete and removed from the job list. When all elements are DONE, the task enters the FINISH flow.
- An element's process returns ``ESP_GMF_JOB_ERR_FAIL``; the task immediately enters the ERROR flow.
- An element's process returns ``ESP_GMF_JOB_ERR_ABORT``; the task enters the ABORT flow.
- The user calls :cpp:func:`esp_gmf_pipeline_stop`; the task detects the ``STOP`` flag at the next job boundary and enters the STOP flow.

**cleanup**: Regardless of how running is exited, the task appends and executes the close of each element that has been opened. Each open corresponds to one close. After cleanup, the task enters one of ``STOPPED``, ``FINISHED``, or ``ERROR`` and notifies the pipeline via an event callback.

Job Return Code Effects on Scheduling
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The open, process, and close of an element all exist as jobs inside the framework, and the return value determines how the task schedules next.

.. list-table::
   :widths: 32 12 56
   :header-rows: 1

   * - Return Code
     - Value
     - Scheduling Behavior
   * - ``ESP_GMF_JOB_ERR_OK``
     - 0
     - Completed normally. One-shot jobs are removed from the list; infinite jobs are retained for the next round.
   * - ``ESP_GMF_JOB_ERR_CONTINUE``
     - 1
     - Immediately jumps to the first element's job and begins execution; jobs after the current element are skipped this round. Commonly used when an element finds insufficient data and needs to fetch more.
   * - ``ESP_GMF_JOB_ERR_DONE``
     - 2
     - Job permanently complete; immediately removed from the list. Even infinite jobs are no longer scheduled.
   * - ``ESP_GMF_JOB_ERR_TRUNCATE``
     - 3
     - The current job is saved to the job stack; the next round continues from the element that returned ``ESP_GMF_JOB_ERR_TRUNCATE``. Generally used when the output buffer is full and downstream must consume data before writing can continue.
   * - ``ESP_GMF_JOB_ERR_FAIL``
     - -1
     - Execution failed; triggers the task's ERROR flow.
   * - ``ESP_GMF_JOB_ERR_ABORT``
     - -3
     - Actively aborted; subsequent flow (STOP or RESET) is determined by the abort strategy.

``TRUNCATE`` combined with the job stack allows an element to pause itself in scenarios where "processing is not complete, but downstream needs to execute first," waiting for downstream to consume data before resuming. For example, an upstream element with a block-type port returns TRUNCATE when its output buffer is full, letting downstream consume first before resuming the current write flow.

Basic Example
-------------

The following uses the pool to manage element templates in bulk and builds a minimal "file - decode - resample - I2S" pipeline. The pool is suitable for registering all supported elements once at application startup, then composing various pipelines by name.

First, at application startup, register the elements and IOs to be used in the pool:

.. code:: c

    esp_gmf_pool_handle_t pool;
    esp_gmf_pool_init(&pool);

    esp_gmf_pool_register_element(pool, audio_decoder_handle, NULL);
    esp_gmf_pool_register_element(pool, audio_rate_cvt_handle, NULL);
    esp_gmf_pool_register_io(pool, file_io_handle, NULL);
    esp_gmf_pool_register_io(pool, i2s_io_handle, NULL);

Second, build the pipeline by name, bind the task, load jobs, then run:

.. code:: c

    const char *el_names[] = {"aud_dec", "aud_rate_cvt"};
    esp_gmf_pipeline_handle_t pipe = NULL;
    esp_gmf_pool_new_pipeline(pool, "io_file", el_names, 2, "io_i2s", &pipe);

    esp_gmf_task_handle_t task = NULL;
    esp_gmf_task_cfg_t task_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    task_cfg.thread.stack = 8 * 1024;
    task_cfg.name = "player_task";
    esp_gmf_task_init(&task_cfg, &task);

    esp_gmf_pipeline_bind_task(pipe, task);
    esp_gmf_pipeline_loading_jobs(pipe);
    esp_gmf_pipeline_set_event(pipe, my_event_cb, my_ctx);

    esp_gmf_pipeline_set_in_uri(pipe, "/sdcard/song.mp3");
    esp_gmf_pipeline_run(pipe);

Meaning of each step:

- :cpp:func:`esp_gmf_pool_new_pipeline` looks up templates by tag string internally, calls ``esp_gmf_obj_dupl`` to copy fresh instances, chains them in order, and binds head/tail IOs.
- :cpp:func:`esp_gmf_task_init` creates a task. :cpp:type:`esp_gmf_task_cfg_t` allows configuring stack size, priority, CPU core, and whether to place the stack in external PSRAM. ``DEFAULT_ESP_GMF_TASK_CONFIG()`` provides defaults of 4 KB stack, priority 5, core 0.
- :cpp:func:`esp_gmf_pipeline_bind_task` establishes the association between the pipeline and the task, and sets the task's event callback to the pipeline's internal event handler. After this step, any state change during task execution is sensed by the pipeline.
- :cpp:func:`esp_gmf_pipeline_loading_jobs` registers the open and process of elements into the task's job list. Only elements in ``INITIALIZED`` state are registered; dependent elements are not registered until the pipeline receives REPORT_INFO from upstream, at which point the pipeline registers their open/process jobs.
- :cpp:func:`esp_gmf_pipeline_run` starts the task and begins executing the job list in sequence.

To adjust parameters of a specific element, retrieve the handle via :cpp:func:`esp_gmf_pipeline_get_el_by_name` and modify:

.. code:: c

    esp_gmf_element_handle_t dec = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec);
    mp3_decoder_cfg_t *cfg = (mp3_decoder_cfg_t *)OBJ_GET_CFG(dec);
    cfg->out_rb_size = 8 * 1024;

Without the pool, you can also build manually: call ``esp_gmf_pipeline_create`` - ``register_el`` - ``set_io`` - ``bind_task`` - ``loading_jobs`` - ``run`` in sequence. The pool performs the same steps internally, saving you from writing repetitive registration code.

Advanced Topics
---------------

Deferred Startup of Dependent Elements
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A common problem in multimedia processing: some elements' initialization parameters can only be obtained after upstream elements have started processing. A typical example is a resampler, which can only be configured when the input sample rate is known, but the sample rate is only available after the MP3 decoder has parsed the file header. If all elements are opened together when the pipeline starts, the resampler either initializes with incorrect default values or fails to open.

GMF-Core solves this kind of forward dependency using the ``dependency`` flag and the REPORT_INFO event:

1. The developer sets ``dependency = true`` in the element's configuration, declaring that the element requires upstream information to initialize.
2. After the pipeline starts, the task calls open and process on non-dependent elements first, and does not schedule dependent elements yet.
3. After the upstream element parses format info in its own open or process, it reports via the REPORT_INFO event.
4. The pipeline receives the event, writes the info into the downstream dependent element, and triggers that element to enter ``INITIALIZED`` state.
5. The pipeline registers the open and process jobs for that element, and the task subsequently schedules it according to the job list.

Dependencies can be cascaded: a pipeline can have multiple dependent elements chained together, each registering open/process jobs only after its upstream reports format info. Application code does not need to write coordination logic; the pipeline handles registration and scheduling according to the rules above.

Format Information and REPORT_INFO
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Format information is used to pass audio/video parameters between elements; the relevant structures are defined in ``esp_gmf_info.h``. Audio elements commonly use ``esp_gmf_info_sound_t`` to describe sample rate, channels, bit depth, and FourCC; video elements use ``esp_gmf_info_video_t`` to describe width/height, frame rate, and pixel format. After an element parses format info in open or process, it reports it to the pipeline via the REPORT_INFO event.

.. code:: c

    esp_gmf_info_sound_t snd = {
        .format_id    = ESP_FOURCC_PCM,
        .sample_rates = 44100,
        .bitrate      = 0,
        .channels     = 2,
        .bits         = 16,
    };
    esp_gmf_audio_el_set_snd_info(handle, &snd);
    esp_gmf_element_notify_snd_info(handle, &snd);

After the pipeline receives REPORT_INFO, it writes the format info into downstream dependent elements and registers open/process jobs for elements whose dependencies are satisfied. Therefore, REPORT_INFO is not only a notification event to the user, but also the trigger for deferred startup of dependent elements. Applications can also actively report format info via :cpp:func:`esp_gmf_pipeline_report_info` for scenarios where no upstream element reports automatically.

Advanced Pool API
^^^^^^^^^^^^^^^^^

In addition to basic registration and build interfaces, the pool also provides three advanced usages.

**Insert high-priority templates**: :cpp:func:`esp_gmf_pool_register_element_at_head` behaves the same as :cpp:func:`esp_gmf_pool_register_element`, except it inserts the element at the head of the linked list. The pool searches templates in linked list order, with earlier registrations matching first; therefore, a custom element registered with ``_at_head`` can override a default implementation with the same name.

**Auto-select IO by URL**: :cpp:func:`esp_gmf_pool_get_io_tag_by_url` iterates all IO templates in the pool, calls each IO's ``get_score`` callback to score the URL, and returns the IO tag with the highest score.

.. code:: c

    char *io_tag = NULL;
    esp_gmf_pool_get_io_tag_by_url(pool, "http://server/song.mp3",
                                   ESP_GMF_IO_DIR_READER, &io_tag);
    /* io_tag is typically "io_http", used as the in_name for esp_gmf_pool_new_pipeline */

For detailed scoring level definitions, see the "URL Scoring Strategy" section in :doc:`gmf-core-data-path`.

**Iterate registered templates**: :cpp:func:`esp_gmf_pool_iterate_element` and :cpp:func:`esp_gmf_pipeline_iterate_element` provide an iterator-based traversal, commonly used for log output, capability probing, or debugging. Initialize ``*iterator`` to ``NULL`` and call repeatedly until ``ESP_GMF_ERR_NOT_FOUND`` is returned.

.. code:: c

    const void *it = NULL;
    esp_gmf_element_handle_t el = NULL;
    while (esp_gmf_pool_iterate_element(pool, &it, &el) == ESP_GMF_ERR_OK) {
        const char *tag = NULL;
        esp_gmf_obj_get_tag((esp_gmf_obj_handle_t)el, &tag);
        ESP_LOGI("APP", "registered element: %s", tag);
    }

Task Strategy and Abort Flow
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The task's default behavior is: when all jobs are DONE, enter ``FINISHED``; when a job FAILs, enter ``ERROR``; when a job ABORTs, enter ``STOPPED``. For scenarios requiring special behavior, register a strategy function via :cpp:func:`esp_gmf_task_set_strategy_func` to override the default.

The strategy function is called at two trigger points:

- ``GMF_TASK_STRATEGY_TYPE_FINISH``: when all jobs return DONE
- ``GMF_TASK_STRATEGY_TYPE_ABORT``: when a job returns ABORT

The return value of the strategy function determines which path the task takes next:

- ``GMF_TASK_STRATEGY_ACTION_DEFAULT``: follow the default flow to enter the terminal state
- ``GMF_TASK_STRATEGY_ACTION_RESET``: load the reset job for each element, restore to ``INITIALIZED``. Suitable for "auto-advance to next track after playback ends" scenarios, avoiding rebuilding the entire pipeline.
- ``GMF_TASK_STRATEGY_ACTION_STOP``: load close jobs to enter the STOP flow

Strategy functions must not call any task or pipeline control APIs internally, as this would trigger timeout errors. If waiting for an external event is needed, use a blocking mechanism such as a semaphore.

The semantic difference between stop and abort: ``stop`` is initiated by the user calling :cpp:func:`esp_gmf_pipeline_stop` / :cpp:func:`esp_gmf_task_stop`; the task detects the ``STOP`` flag at the next job boundary and enters cleanup - a soft stop where the currently executing job completes its current call before exiting. ``abort`` is triggered by an element returning ``ESP_GMF_JOB_ERR_ABORT`` in ``process``, or by the data layer calling ``esp_gmf_db_abort`` / ``esp_gmf_io_abort``; when a network disconnection occurs, the HTTP IO reports ``ESP_GMF_IO_ABORT``, and the element converts this IO error into an ABORT return code for the job. After abort, the task enters STOPPED or RESET based on the strategy function; combined with the RESET action, this enables a recovery flow without destroying the pipeline, such as "resume playing the same audio after reconnecting" or "switch to the next track."

Error Handling Path
^^^^^^^^^^^^^^^^^^^

When any element's job returns ``FAIL``, the task cleans up and notifies in the following order:

1. The task removes all unexecuted process jobs from the job list, preventing new data processing actions.
2. The task appends the close of each element that has been opened to the end of the job list.
3. The task executes each close in turn, allowing elements to release their allocated resources.
4. The pipeline's event handler receives ``CHANGE_STATE = ERROR`` and calls the close of the input and output IOs to close the peripherals.
5. The pipeline forwards the ERROR event to the user-registered callback.

This path is idempotent: even if a close itself fails, the task continues processing remaining closes until the list is empty, preventing a single element's failure from blocking the entire cleanup flow. After entering ERROR, the pipeline stays in that state; :cpp:func:`esp_gmf_pipeline_reset` must be called to clear the state before the pipeline can be restarted.

A failed open of the input or output IO also triggers the same path. For example, when an input file cannot be opened, the pipeline's event handler detects that the IO open returned non-OK, directly switches the state to ERROR, and initiates cleanup.

Combining Multiple Pipelines
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

An application is often composed of multiple pipelines (for example, a decode pipeline and a render pipeline bridged by a ringbuffer). GMF-Core provides the following interfaces to support pipeline composition.

**Event cascading**: :cpp:func:`esp_gmf_pipeline_reg_event_recipient` adds connectee to connector's ``evt_conveyor`` chain. After this, any REPORT_INFO event received by connector is also forwarded to connectee, so dependent elements in the downstream pipeline can be awakened when the upstream pipeline obtains format info.

:cpp:func:`esp_gmf_pipeline_connect_pipe` additionally, on top of event cascading, directly connects the out port of an element in connector to the in port of an element in connectee. This is used when fine-grained connection of specific elements across two pipelines is needed.

**Pre/post callbacks**: :cpp:func:`esp_gmf_pipeline_set_prev_run_cb` and :cpp:func:`esp_gmf_pipeline_set_prev_stop_cb` allow inserting custom actions before run and stop. A common use case is dependency management: notifying downstream to stop first before upstream stops, preventing downstream from continuing to wait for data after upstream has stopped. These callbacks can also be triggered manually via :cpp:func:`esp_gmf_pipeline_prev_run` / :cpp:func:`esp_gmf_pipeline_prev_stop`; once triggered manually, they are not called again by run / stop.

**pause-on-start**: :cpp:func:`esp_gmf_pipeline_set_pause_on_start` makes the pipeline immediately enter ``PAUSED`` after run, without executing process. A typical use case is when multiple pipelines need to start simultaneously: each enters PAUSED first, then a unified resume ensures a synchronized start.

**IO replacement**: :cpp:func:`esp_gmf_pipeline_replace_in` / :cpp:func:`esp_gmf_pipeline_replace_out` allow replacing the head/tail IOs when the pipeline is not in ``RUNNING`` state, for scenarios such as "switching to a different song." Ownership of the replaced old IO is returned to the user, who must destroy it manually.

Miscellaneous
-------------

Valid States for Control APIs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The pipeline exposes a unified set of control APIs that are all forwarded to the bound task internally. The table below lists the valid call states for each API; calling in an invalid state returns ``ESP_GMF_ERR_NOT_SUPPORT`` or ``ESP_GMF_ERR_INVALID_STATE``. For the complete state enumeration, see :doc:`gmf-core-overview`.

.. list-table::
   :widths: 26 30 44
   :header-rows: 1

   * - API
     - Valid States
     - Effect
   * - :cpp:func:`esp_gmf_pipeline_run`
     - ``INITIALIZED`` / ``STOPPED`` / ``FINISHED``
     - Triggers ``prev_run`` callback (if configured), starts the task, enters ``OPENING``
   * - :cpp:func:`esp_gmf_pipeline_stop`
     - ``RUNNING`` / ``PAUSED``
     - Triggers ``prev_stop`` callback (if configured), requests the task to stop, enters ``STOPPED``
   * - :cpp:func:`esp_gmf_pipeline_pause`
     - ``RUNNING``
     - Suspends process scheduling, preserves context, enters ``PAUSED``
   * - :cpp:func:`esp_gmf_pipeline_resume`
     - ``PAUSED``
     - Releases the suspend semaphore, restores to ``RUNNING``
   * - :cpp:func:`esp_gmf_pipeline_reset`
     - Non-``RUNNING``
     - Clears job list, resets element and port state, returns to ``INITIALIZED``
   * - :cpp:func:`esp_gmf_pipeline_seek`
     - ``PAUSED`` / ``STOPPED`` / ``FINISHED``
     - Calls the input IO's seek interface to jump to a specified position

A few details worth noting:

- ``pause`` sets the ``PAUSE`` flag; the task blocks on a semaphore at the next job boundary. ``resume`` releases the semaphore to let the task continue. Pause occurs at job boundaries, not inside jobs, to avoid interrupting an element in the middle of data processing.
- ``reset`` clears the task's job list and calls each element's ``reset_state`` and ``reset_port``. After reset, before running again, :cpp:func:`esp_gmf_pipeline_loading_jobs` must be called to reload jobs.
- ``seek`` is only meaningful for streaming formats that support independent frame decoding (such as MP3, AAC, TS). The pipeline must be in a non-running state before calling, to avoid the IO being read concurrently during seek.

Timeout and Synchronization Semantics
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

All control APIs block by default, with a maximum wait time controlled by ``DEFAULT_TASK_OPT_MAX_TIME_MS`` (2000 ms). This timeout can be adjusted via :cpp:func:`esp_gmf_task_set_timeout`.

``stop`` behaves slightly differently: even if it times out and returns ``ESP_GMF_ERR_TIMEOUT``, the stop action continues in the background. The retry logic inside the task continues to wait until all jobs exit, ensuring resources are in a stable state. ``pause`` similarly takes effect even after timeout; the caller can choose to wait again or proceed with subsequent flow.

Stop and Timeout
^^^^^^^^^^^^^^^^

:cpp:func:`esp_gmf_pipeline_stop` returning ``ESP_GMF_ERR_TIMEOUT`` does not mean the stop failed; it means the synchronous wait did not complete within the time limit, while the stop action continues in the background. The common cause is an element performing a long blocking operation in process (such as waiting for network data or a mutex). Two ways to handle this: increase the value set by :cpp:func:`esp_gmf_task_set_timeout`, or check the task's abort flag in the element implementation to exit proactively.

Dependency Not Started
^^^^^^^^^^^^^^^^^^^^^^

If an element still has not opened after ``loading_jobs``, it is usually because it is marked as ``dependency = true`` but upstream has never reported format info. Confirm that the upstream element calls ``esp_gmf_element_notify_snd_info`` (or the corresponding derived class's notify interface) in its open or process, or that the application layer actively reports info via :cpp:func:`esp_gmf_pipeline_report_info`.

Recovery from ERROR
^^^^^^^^^^^^^^^^^^^

Calling run in ERROR state returns ``ESP_GMF_ERR_NOT_SUPPORT``. The recovery steps are: call :cpp:func:`esp_gmf_pipeline_reset` to return the state to ``INITIALIZED``, then call :cpp:func:`esp_gmf_pipeline_loading_jobs` to reload jobs, and only then can you run.

Pipeline Blocked after Abort
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

After an element returns ``ABORT``, the task enters the strategy function. If the strategy function returns ``GMF_TASK_STRATEGY_ACTION_DEFAULT``, the pipeline exits via the STOPPED path and must be reset before it can run again. If you want automatic recovery after abort, register a strategy function returning ``GMF_TASK_STRATEGY_ACTION_RESET``. Strategy functions must not call any pipeline control APIs.

State Still RUNNING after Pause
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This should not occur under normal circumstances. ``pause`` waits for the task to process the pause request; only after the task enters ``PAUSED`` does it return ``ESP_GMF_ERR_OK``. Therefore, querying the state after ``pause`` returns OK should read ``PAUSED``, not ``RUNNING``.

If the task does not reach the next job boundary promptly, ``pause`` returns ``ESP_GMF_ERR_TIMEOUT``. This return value only means the synchronous wait timed out this time, not that the pause request was cancelled; the pause request will still take effect at the next job boundary. Therefore, querying the state immediately after a timeout return may still show ``RUNNING``.

API Reference
-------------

Core header files covered in this document:

- ``esp_gmf_pipeline.h``: Pipeline creation, control, and cascading
- ``esp_gmf_task.h``: Task configuration, control, and strategy
- ``esp_gmf_job.h``: Job structures, return codes, and job stack
- ``esp_gmf_pool.h``: Pool registration and instantiation
- ``esp_gmf_info.h``: Audio/video format information structures

For element base class and three derived element interfaces, see the "API Reference" in :doc:`gmf-core-element`; for IO interfaces, see :doc:`gmf-core-data-path`.

.. include-build-file:: inc/esp_gmf_pipeline.inc

.. include-build-file:: inc/esp_gmf_task.inc

.. include-build-file:: inc/esp_gmf_job.inc

.. include-build-file:: inc/esp_gmf_pool.inc

.. include-build-file:: inc/esp_gmf_info.inc
