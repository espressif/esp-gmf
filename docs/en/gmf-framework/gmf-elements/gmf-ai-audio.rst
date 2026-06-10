GMF-AI-Audio
============

:link_to_translation:`zh_CN:[中文]`

gmf_ai_audio is the AI voice front-end component of ESP-GMF, wrapping the `esp-sr <https://github.com/espressif/esp-sr>`__ speech algorithm library (wake word, command word, AEC, NS, VAD, DOA) into elements that can be connected to a pipeline. The component provides six elements (ai_afe / ai_aec / ai_wn / ai_ns / ai_vad / ai_doa) and an internal manager (``esp_gmf_afe_manager``). Among them, ai_afe is the comprehensive interface encapsulating full voice front-end capabilities, suitable for direct use as a unified entry point; ai_aec / ai_wn / ai_ns / ai_vad / ai_doa are for individual capabilities. All six elements follow the unified element interface and can be chained and combined in any order in the same pipeline. This document covers the principles, configuration, and event system in the order of manager, comprehensive element, and standalone algorithm elements; for the element base class and runtime method mechanism, see :doc:`/gmf-framework/gmf-core/gmf-core-element`; for the data path, see :doc:`/gmf-framework/gmf-core/gmf-core-data-path`.

Feature List
------------

- ai_afe: full voice front-end element; after connecting to codec_dev IO, outputs 16-bit mono PCM and reports wakeup, VAD, and command word events via event callbacks
- ai_aec: standalone echo cancellation element; performs AEC on input PCM and outputs 16-bit mono PCM; suitable for scenarios requiring only echo-cancelled microphone signal without wakeup / VAD / command word
- ai_wn: standalone wake word detection element; does not create feed / fetch tasks; synchronously detects in the element ``process`` and transparently passes the input PCM to the output port
- ai_ns: standalone noise suppression element; input format is 16 kHz, 16-bit mono PCM; supports NSNet2 model or WebRTC NS backend
- ai_vad: standalone voice activity detection element; supports WebRTC VAD and VADNet backends; reports VAD state via callback and can pass through input PCM
- ai_doa: standalone direction of arrival estimation element; requires input format with two microphone channels; outputs angle estimation results via callback
- esp_gmf_afe_manager: internal manager that encapsulates feed / fetch two tasks, responsible for ``esp-sr`` AFE data input, result retrieval, feature toggling, and pause / resume
- NS / VAD / SE: ai_afe uses ``esp-sr``'s noise suppression (NS), voice activity detection (VAD), and speech enhancement (SE) capabilities via the AFE manager; whether enabled is controlled by ``afe_config_t`` and runtime feature switches
- Channel format convention: the input channel arrangement is described by a string; ``M`` for microphone, ``R`` for speaker reference, ``N`` for unused channel; e.g., ``MMNR`` means the first two channels are microphones, plus one unused and one reference channel
- Wakeup and VAD state machine: supports three combinations - "wakeup only", "VAD only", "wakeup + VAD"; automatically maintains IDLE / WAKEUP / SPEECHING / WAIT_FOR_SLEEP states
- Command word detection (VCMD): based on ``MultiNet``; independent of the wakeup state machine; started by the application calling ``esp_gmf_afe_vcmd_detection_begin``
- Manual wakeup control: ``esp_gmf_afe_keep_awake`` keeps awake state; ``esp_gmf_afe_trigger_wakeup`` / ``..._trigger_sleep`` switches manually; suitable for button wakeup and other non-voice triggers
- Event system: 6 event types covering wakeup start / end, VAD start / end, command word detection, and command word timeout

Technical Details
-----------------

Component Hierarchy
^^^^^^^^^^^^^^^^^^^

The relationship between the six elements and the manager is shown below. ai_afe is the upper-level wrapper of the manager, adapting manager callbacks to the GMF element interface; ai_aec, ai_wn, ai_ns, ai_vad, and ai_doa call the corresponding ``esp-sr`` algorithms directly, without going through the manager.

.. only:: html

   .. mermaid::

      classDiagram
          direction TB

          class esp_gmf_audio_element_t
          class ai_afe {
              feed/fetch task
              wakeup state machine
              command word detection
              event callback
          }
          class ai_aec {
              standalone AEC
              reference + microphone channels
          }
          class ai_wn {
              standalone WakeNet
              wake word detection
          }
          class ai_ns {
              standalone NS
              single-channel noise suppression
          }
          class ai_vad {
              standalone VAD
              state callback
          }
          class ai_doa {
              standalone DOA
              sound source localization
          }
          class esp_gmf_afe_manager {
              feed_task
              fetch_task
              feature control
          }

          esp_gmf_audio_element_t <|-- ai_afe
          esp_gmf_audio_element_t <|-- ai_aec
          esp_gmf_audio_element_t <|-- ai_wn
          esp_gmf_audio_element_t <|-- ai_ns
          esp_gmf_audio_element_t <|-- ai_vad
          esp_gmf_audio_element_t <|-- ai_doa
          ai_afe ..> esp_gmf_afe_manager : uses

When element behavior is needed, choose ai_afe / ai_aec / ai_wn / ai_ns / ai_vad / ai_doa to connect to the pipeline based on the scenario; when bypassing the GMF framework to use ``esp-sr`` directly, ``esp_gmf_afe_manager`` can be used standalone.

input_format Channel String
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Elements using multi-channel input use the ``input_format`` string to describe the role of each channel: ``M`` for microphone capture, ``R`` for speaker reference (used as AEC reference), ``N`` for unused channel. For detailed rules of the channel string, see `esp-sr AFE Input Channel Description <https://docs.espressif.com/projects/esp-sr/zh_CN/latest/esp32/audio_front_end/README.html#id5>`__.

For example, ``"MMNR"`` means the input is 4-channel interleaved PCM, channels 1/2 are microphones, channel 3 is unused, and channel 4 is reference. The component automatically extracts the required channels from the input PCM and feeds them to the underlying algorithm. The ai_afe input sample rate is fixed at 16 kHz, 16-bit; ai_aec additionally supports 8 kHz when using ``AFE_TYPE_VC_8K``; ai_doa requires exactly two ``M`` channels in the input format.

AFE Manager
^^^^^^^^^^^

``esp_gmf_afe_manager`` wraps the ``esp-sr`` AFE interface into a callback model of "data input → algorithm processing → result output", automatically creating feed and fetch tasks.

.. only:: html

   .. mermaid::

      flowchart LR
          ReadCb[("read_cb<br/>(provided by app)")] --> Feed["feed_task"]
          Feed --> Core["AFE processing module<br/>esp-sr"]
          Core --> Fetch["fetch_task"]
          Fetch --> ResultCb[("result_cb<br/>(received by app)")]

The application provides ``read_cb`` and ``result_cb`` via :cpp:type:`esp_gmf_afe_manager_cfg_t`; feed_task periodically calls ``read_cb`` to get one frame of multi-channel PCM and feeds it to the ``esp-sr`` AFE; fetch_task retrieves the processed result (noise-suppressed / AEC-processed mono PCM + wakeup / VAD / command word events) and calls ``result_cb``. The two tasks default to different cores (core 0 / core 1), stack 3 KiB, priority 5; ``DEFAULT_GMF_AFE_MANAGER_CFG`` provides default values.

Individual features can be toggled at runtime:

.. code:: c

    esp_gmf_afe_manager_enable_features(mgr, ESP_AFE_FEATURE_AEC, true);
    esp_gmf_afe_manager_enable_features(mgr, ESP_AFE_FEATURE_VAD, false);

After calling :cpp:func:`esp_gmf_afe_manager_suspend` with the suspend flag set to ``true``, both feed and fetch tasks can be suspended simultaneously, suitable for low-power scenarios. After initialization, :cpp:func:`esp_gmf_afe_manager_get_chunk_size` and ``get_input_ch_num`` can be used to query the number of samples per processing chunk and the total number of input channels, helping the application adjust its IO buffer.

ai_afe: Full Voice Front-End
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

ai_afe wraps ``esp_gmf_afe_manager`` into an element that can be connected to a pipeline: feeds multi-channel PCM read from codec_dev IO into the manager, writes the mono PCM output by fetch to the output port, and converts wakeup / VAD / command word into event callbacks to send to the application.

The output of ai_afe is 16-bit mono PCM. Depending on the ``esp-sr`` AFE configuration, the output audio may incorporate AEC, NS, SE, AGC processing results; wakeup, VAD, and command word detection results are reported via event callbacks and are not placed in audio payloads.

**Configuration**. :cpp:type:`esp_gmf_afe_cfg_t` requires at least ``afe_manager``, ``models`` (loaded ``esp-sr`` models), and ``event_cb``. The underlying AFE type is determined by the ``type`` parameter of ``afe_config_init``; common types in the latest ``esp-sr`` include ``AFE_TYPE_SR`` (speech recognition), ``AFE_TYPE_VC`` / ``AFE_TYPE_VC_8K`` (voice communication), and ``AFE_TYPE_FD`` (full duplex, suitable for voice interaction scenarios with simultaneous playback and capture):

.. code:: c

    afe_config_t *afe_cfg = afe_config_init("MMNR", models, AFE_TYPE_FD, AFE_MODE_LOW_COST);
    afe_cfg->wakenet_init = true;
    afe_cfg->vad_init     = true;
    afe_cfg->aec_init     = true;

    esp_gmf_afe_manager_cfg_t mgr_cfg = DEFAULT_GMF_AFE_MANAGER_CFG(afe_cfg,
        my_read_cb, &io_ctx, NULL, NULL);
    esp_gmf_afe_manager_handle_t mgr = NULL;
    esp_gmf_afe_manager_create(&mgr_cfg, &mgr);

    esp_gmf_afe_cfg_t afe_el_cfg = DEFAULT_GMF_AFE_CFG(mgr, my_event_cb, &app_ctx, models);
    afe_el_cfg.vcmd_detect_en = true;
    esp_gmf_element_handle_t ai_afe = NULL;
    esp_gmf_afe_init(&afe_el_cfg, &ai_afe);

Four timing parameters (default values are given in ``esp_gmf_afe.h``) control state machine behavior:

.. list-table::
   :widths: 24 18 58
   :header-rows: 1

   * - Parameter
     - Default
     - Description
   * - ``wakeup_time``
     - 10000 ms
     - How long after wakeup without any VAD event before ``WAKEUP_END`` is triggered
   * - ``wakeup_end``
     - 2000 ms
     - How long after VAD ends with silence before ``WAKEUP_END`` is triggered
   * - ``vcmd_timeout``
     - 5760 ms
     - Command word detection timeout; begin must be called again after timeout
   * - ``delay_samples``
     - 2048 samples
     - Output PCM delay to compensate for VAD detection lag; converted to time, should be greater than ``afe_config_t.vad_min_speech_ms``

**Wakeup and VAD State Machine**. Three combinations switch automatically. Wakeup only:

.. only:: html

   .. mermaid::

      stateDiagram-v2
          [*] --> IDLE
          IDLE --> WAKEUP : wake word / WAKEUP_START
          WAKEUP --> IDLE : wakeup_time timeout / WAKEUP_END

VAD only:

.. only:: html

   .. mermaid::

      stateDiagram-v2
          [*] --> IDLE
          IDLE --> SPEECHING : voice detected / VAD_START
          SPEECHING --> IDLE : silence / VAD_END

Wakeup + VAD combined:

.. only:: html

   .. mermaid::

      stateDiagram-v2
          [*] --> IDLE
          IDLE --> WAKEUP : wake word / WAKEUP_START
          WAKEUP --> SPEECHING : voice / VAD_START
          WAKEUP --> IDLE : wakeup_time timeout / WAKEUP_END
          SPEECHING --> WAIT_FOR_SLEEP : silence / VAD_END
          WAIT_FOR_SLEEP --> SPEECHING : voice / VAD_START
          WAIT_FOR_SLEEP --> IDLE : wakeup_end timeout / WAKEUP_END

The combined mode is suitable for the interaction flow of "speak wake word → voice input → return to standby after silence", avoiding frequent VAD events being triggered outside the wakeup interval.

**Manual wakeup control**. In addition to the automatic state machine, three APIs provide non-voice triggering:

- :cpp:func:`esp_gmf_afe_trigger_wakeup`: simulates a wake word hit, immediately entering WAKEUP state and broadcasting ``WAKEUP_START``; used for button wakeup and external sensor triggers
- :cpp:func:`esp_gmf_afe_trigger_sleep`: manually switches to IDLE
- :cpp:func:`esp_gmf_afe_keep_awake`: once enabled, disables the automatic sleep timer (``wakeup_time`` and ``wakeup_end``). After setting, the element will not automatically exit WAKEUP due to timeout; :cpp:func:`esp_gmf_afe_trigger_sleep` must be called to return to IDLE

**Command word detection (VCMD)**: independent of the wakeup state machine. The typical flow is to call :cpp:func:`esp_gmf_afe_vcmd_detection_begin` after receiving ``WAKEUP_START``; upon detection, the event callback provides :cpp:type:`esp_gmf_afe_vcmd_info_t` (containing ``phrase_id``, ``prob``, and the command string); call begin again after detection or timeout to continue. ``vcmd_detection_cancel`` clears the current detection state while preserving the feature enable flag, so begin can be called again later.

Event System
^^^^^^^^^^^^

ai_afe reports six event types via :cpp:type:`esp_gmf_afe_event_cb_t`; event enum values can be positive or negative, allowing command word IDs to be placed directly in the enum:

.. list-table::
   :widths: 36 16 48
   :header-rows: 1

   * - Event
     - Value
     - Payload
   * - :c:macro:`ESP_GMF_AFE_EVT_WAKEUP_START`
     - -100
     - :cpp:type:`esp_gmf_afe_wakeup_info_t` (volume, wake word index, model index)
   * - :c:macro:`ESP_GMF_AFE_EVT_WAKEUP_END`
     - -99
     - NULL
   * - :c:macro:`ESP_GMF_AFE_EVT_VAD_START`
     - -98
     - NULL
   * - :c:macro:`ESP_GMF_AFE_EVT_VAD_END`
     - -97
     - NULL
   * - :c:macro:`ESP_GMF_AFE_EVT_VCMD_DECT_TIMEOUT`
     - -96
     - NULL
   * - :c:macro:`ESP_GMF_AFE_EVT_VCMD_DECTECTED`
     - ``>= 0``
     - :cpp:type:`esp_gmf_afe_vcmd_info_t`, enumeration value equals phrase ID

The callback executes in the fetch_task context; the application is recommended to only do lightweight dispatching (update state, enqueue message); time-consuming logic should run in the main thread or an independent task.

ai_aec: Standalone Echo Cancellation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

ai_aec only performs echo cancellation: extracts microphone + reference channels from multi-channel input PCM according to ``input_format``, processes them through the AEC algorithm, and outputs mono PCM. No model partition is required; it consumes fewer resources than ai_afe and is suitable for recording pipelines that only need echo cancellation without wakeup / VAD / command word.

Three tuning fields in :cpp:type:`esp_gmf_aec_cfg_t`:

- ``filter_len``: filter length; recommended 4 for ESP32-S3 / P4, 2 for ESP32-C5; higher values consume more CPU
- ``type``: ``AFE_TYPE_VC`` (voice communication) or ``AFE_TYPE_SR`` (speech recognition)
- ``mode``: ``AFE_MODE_LOW_POWER`` or ``AFE_MODE_HIGH_PERF``

.. code:: c

    esp_gmf_aec_cfg_t cfg = {
        .filter_len   = 4,
        .type         = AFE_TYPE_SR,
        .mode         = AFE_MODE_HIGH_PERF,
        .input_format = "MMNR",
    };
    esp_gmf_obj_handle_t aec = NULL;
    esp_gmf_aec_init(&cfg, &aec);

ai_aec internally maintains a synchronization buffer for reference and microphone signals: each ``process`` accumulates one frame of aligned data before calling the underlying AEC, outputting 16-bit mono PCM. The input sample rate is typically 16 kHz; when configured with ``AFE_TYPE_VC_8K``, the input sample rate is 8 kHz. The bit depth must be 16-bit PCM; any mismatch is rejected at the ``open`` stage.

ai_wn: Standalone Wake Word Detection
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

ai_wn is a lightweight wrapper of WakeNet: ``process`` synchronously runs detection on the input PCM, calls the user's ``detect_cb`` on a hit and passes the current frame through to the output port; on a miss, it also passes through, leaving the downstream to decide how to handle it.

Differences from ai_afe:

- Does not create feed / fetch tasks; processing occurs directly in the GMF task context
- Does not depend on the AFE manager or the full model set; only loads the WakeNet model
- Lower resource usage; suitable for memory-constrained scenarios or those requiring only wake word detection

.. code:: c

    esp_gmf_wn_cfg_t cfg = {
        .models       = models,
        .det_mode     = DET_MODE_2CH_90,
        .input_format = "MMNR",
        .detect_cb    = my_wakeup_cb,
        .user_ctx     = &ctx,
    };
    esp_gmf_element_handle_t wn = NULL;
    esp_gmf_wn_init(&cfg, &wn);

Supports sample rates of 8 kHz or 16 kHz, 16-bit PCM. The number of channels is determined by ``det_mode`` when WakeNet is initialized (e.g., ``DET_MODE_90``); the number of ``M`` channels in ``input_format`` must match; otherwise the model refuses to run.

ai_ns: Standalone Noise Suppression
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

ai_ns performs noise suppression on single-channel PCM and outputs PCM in the same format. It is suitable for recording or voice pre-processing pipelines that only need noise suppression without the full AFE state machine.

Main fields of :cpp:type:`esp_gmf_ns_cfg_t`:

- ``sample_rate``: sample rate; currently supports 16 kHz
- ``channel``: channel count; currently only mono is supported
- ``frame_ms``: WebRTC NS frame duration; supports 10 / 20 / 30 ms
- ``model_name`` and ``partition_label``: NSNet2 model name and model partition label

.. code:: c

    esp_gmf_ns_cfg_t cfg = ESP_GMF_NS_CFG_DEFAULT();
    esp_gmf_obj_handle_t ns = NULL;
    esp_gmf_ns_init(&cfg, &ns);

When ``CONFIG_SR_NSN_NSNET2`` is enabled, ai_ns loads the NSNet2 model from the partition specified by ``partition_label``; when the WebRTC NS backend is enabled, model-related fields are not used.

ai_vad: Standalone Voice Activity Detection
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

ai_vad performs voice activity detection on single-channel PCM and reports via callback when the VAD state changes. The element can copy the input PCM to the output port for subsequent pipeline consumption of the original audio.

Main fields of :cpp:type:`esp_gmf_vad_cfg_t`:

- ``sample_rate``: sample rate; WebRTC VAD supports 8 kHz / 16 kHz / 32 kHz
- ``frame_ms``: WebRTC VAD frame duration; supports 10 / 20 / 30 ms
- ``vad_mode``: VAD sensitivity mode
- ``result_callback``: state change callback, returns the underlying ``vad_state_t``
- ``model_name`` and ``partition_label``: VADNet model name and model partition label

.. code:: c

    static void vad_cb(vad_state_t state, void *ctx)
    {
        /* Update application logic based on VAD state */
    }

    esp_gmf_vad_cfg_t cfg = ESP_GMF_VAD_CFG_DEFAULT();
    cfg.result_callback = vad_cb;
    esp_gmf_obj_handle_t vad = NULL;
    esp_gmf_vad_init(&cfg, &vad);

When the VADNet backend is selected, the element loads the VADNet model from the model partition and uses the frame length required by the model; when the WebRTC backend is selected, ``frame_ms`` controls the processing duration per call.

ai_doa: Standalone Direction of Arrival Estimation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

ai_doa estimates the direction of the sound source based on two microphone signals; the processing result is returned as an angle value via callback without outputting new PCM data. It is suitable for applications where a microphone array needs to sense the direction of the sound source.

Main fields of :cpp:type:`esp_gmf_doa_cfg_t`:

- ``sample_rate``: sample rate; default 16 kHz
- ``resolution``: direction estimation resolution
- ``d_mics``: physical distance between the two microphones in meters
- ``frame_ms``: audio duration required to produce one DOA result; default 64 ms
- ``input_format``: input channel arrangement; must contain exactly two ``M`` channels
- ``result_callback``: direction estimation result callback

.. code:: c

    static void doa_cb(float angle, void *ctx)
    {
        /* angle is the direction of arrival estimation result */
    }

    esp_gmf_doa_cfg_t cfg = ESP_GMF_DOA_CFG_DEFAULT();
    cfg.result_callback = doa_cb;
    esp_gmf_obj_handle_t doa = NULL;
    esp_gmf_doa_init(&cfg, &doa);

Performance
-----------

The bottleneck of AI Audio elements is concentrated in the underlying ``esp-sr`` algorithm; the GMF layer overhead is mainly acquire-release and callback dispatch. Optimization recommendations:

.. list-table::
   :widths: 18 22 60
   :header-rows: 1

   * - Module
     - Main Bottleneck
     - Optimization Direction
   * - ai_afe
     - CPU when wake model + AEC + NS run simultaneously
     - Assign feed / fetch to different cores (default 0 / 1); use ``afe_config_init`` to disable temporarily unneeded features
   * - ai_aec
     - Filter length
     - Use ``filter_len = 2`` on CPU-constrained SoCs like ESP32-C5; AEC can be disabled when only recording speech in quiet environments
   * - ai_wn
     - WakeNet model inference
     - Choose 1-channel version (``DET_MODE_90``) for ``det_mode`` to halve computation
   * - ai_ns
     - NS model or WebRTC NS computation
     - Use mono input; choose NSNet2 or WebRTC backend based on actual noise conditions
   * - ai_vad
     - VAD model or WebRTC VAD computation
     - Use shorter frame length for WebRTC backend to reduce latency; ensure correct model partition for VADNet backend
   * - ai_doa
     - DOA algorithm and dual-microphone channel extraction
     - Reduce ``frame_ms`` to lower callback interval; set ``d_mics`` according to actual microphone spacing
   * - AFE Manager
     - feed / fetch queue length and ringbuffer size
     - Watch ``afe_config_t.feed_buffer_size``; read_cb should not block too long to avoid slowing the algorithm

Application Examples
--------------------

- ``elements/gmf_ai_audio/examples/wwe``: Complete wake word detection project, covering ai_afe + manager creation, event callback handling, and command word triggering
- ``elements/gmf_ai_audio/examples/aec_rec``: AEC recording project, demonstrating ai_aec connected to a pipeline and outputting echo-cancelled PCM
- ``elements/gmf_ai_audio/examples/wwe/README_CN.md`` and ``elements/gmf_ai_audio/examples/aec_rec/README_CN.md``: Board wiring, Kconfig options, and run instructions for each project

Use ``idf.py create-project-from-example "espressif/gmf_ai_audio=<version>:wwe"`` to generate a compilable project directly based on this component.

Debugging Tools
---------------

`ESP Audio Analyzer <https://audio-tools.espressif.com.cn/>`_ is Espressif's audio testing solution, combining a device-side test project with a web-based analysis interface. Over a WebSocket connection, it runs standardized tests on microphones, speakers, AEC, and related capabilities, and outputs metrics such as THD and SNR along with structured test reports. After the device joins the network, connect from the web page to start testing.

The test project is built on gmf_ai_audio: the recording pipeline uses ai_afe with AEC enabled in the AFE by default. When tuning AEC performance, you can verify echo cancellation in full-duplex play-and-record scenarios without manually capturing PCM or writing playback scripts. The web UI adjusts MIC gain, playback volume, and channel format (``M`` / ``R`` / ``N`` layout, e.g. ``MMNR``) in real time to match hardware reference wiring and observe AEC changes. Exported raw recordings and before/after comparisons in reports help troubleshoot echo residual and similar issues.

- Covers 11 standardized audio tests across microphone, speaker, and AEC modules
- Test project enables AEC inside ai_afe by default, consistent with the element configuration in this document
- Web UI supports MIC gain, playback volume, and channel format adjustment for AEC comparison
- Supports raw recording export and structured test reports
- Companion test project: `esp_audio_analyzer_app <https://github.com/espressif/esp-adf/tree/master/adf_examples/checks/esp_audio_analyzer_app>`_

SoC Compatibility
-----------------

Different elements depend on different ``esp-sr`` models and hardware acceleration capabilities; the support matrix is as follows:

.. list-table::
   :widths: 18 16 18 18 18 18 18
   :header-rows: 1

   * - Element
     - ESP32
     - ESP32-S3
     - ESP32-S31
     - ESP32-C3
     - ESP32-C5
     - ESP32-P4
   * - ai_afe
     - Supported
     - Supported
     - Supported
     - Not supported
     - Not supported
     - Supported
   * - ai_aec
     - Supported
     - Supported
     - Supported
     - Not supported
     - Supported
     - Supported
   * - ai_wn
     - Supported
     - Supported
     - Supported
     - Supported
     - Supported
     - Supported
   * - ai_ns
     - Supported
     - Supported
     - Supported
     - Not supported
     - Supported
     - Supported
   * - ai_vad
     - Supported
     - Supported
     - Supported
     - Supported
     - Supported
     - Supported
   * - ai_doa
     - Not supported
     - Supported
     - Supported
     - Not supported
     - Not supported
     - Not supported

Both ai_afe and ai_wn depend on the ``esp-sr`` model data partition; the application must reserve a ``model`` partition in the partition table and flash the corresponding model. For model preparation and flashing steps, refer to the ``esp-sr`` documentation and the model configuration instructions in ``elements/gmf_ai_audio/examples/wwe/README_CN.md``.

FAQ
---

**Q:** Wake word detection sensitivity is insufficient or events are not reported. How to troubleshoot?

Check in order: whether ``afe_config_t.wakenet_init`` is true, whether the model partition is correctly flashed, whether the number of ``M`` channels in ``input_format`` matches the hardware microphone wiring, and whether the microphone sampling level is too low (use an oscilloscope or ``esp_gmf_afe_wakeup_info_t.data_volume`` to back-calculate). The ``wwe`` example's ``README.md`` provides a complete hardware checklist.

**Q:** feed_task triggered a task watchdog timeout?

AFE inference has high CPU usage; ``feed_task`` and ``fetch_task`` should be assigned to different cores. On single-core ESP32 chips, ``feed_task`` easily times out when competing with other high-load application tasks; it is recommended to increase ``fetch_task_setting.prio`` or use a dedicated timer task to write input data to the AFE.

**Q:** ai_aec output has noticeable echo residue?

Confirm four things: whether the reference signal (``R`` channel) is connected to the speaker output reference, whether the sample rate is 16 kHz, whether there is a timing offset between the microphone and reference, and whether ``filter_len`` is too small (recommended 4 for ESP32-S3 / P4). For specific debugging methods, see the header comments in ``esp_gmf_aec.c`` and the ``esp-sr`` AEC documentation.

**Q:** No event after command word detection begin?

Check whether ``vcmd_detect_en`` is set to true in :cpp:type:`esp_gmf_afe_cfg_t`, whether ``mn_language`` matches the model language (``cn`` / ``en``), and whether a command word was input within ``vcmd_timeout``. After timeout, :c:macro:`ESP_GMF_AFE_EVT_VCMD_DECT_TIMEOUT` is returned; begin must be called again.

**Q:** How to choose between ai_wn and ai_afe?

Use ai_wn for lightweight wake-word-only scenarios (Bluetooth speakers, sensor nodes); use ai_afe when full voice interaction is needed (wakeup + VAD + command word / AEC / NS). Both process raw multi-channel PCM and cannot be chained in the same pipeline.

**Q:** How to use esp_gmf_afe_manager standalone without connecting to a GMF pipeline?

:cpp:func:`esp_gmf_afe_manager_create` does not require the caller to be a GMF element; both ``read_cb`` and ``result_cb`` are ordinary callbacks. It can be used standalone in non-GMF scenarios with self-managed input/output loops; it no longer provides the acquire-release protocol and pipeline control capability.

API Reference
-------------

Header files for this component:

- ``esp_gmf_afe_manager.h``: AFE manager configuration, feature toggling, pause / resume
- ``esp_gmf_afe.h``: ai_afe element initialization, command word control, manual wakeup, event callbacks
- ``esp_gmf_aec.h``: ai_aec element configuration
- ``esp_gmf_wn.h``: ai_wn element configuration and detection callbacks
- ``esp_gmf_ns.h``: ai_ns element configuration
- ``esp_gmf_vad.h``: ai_vad element configuration and result callbacks
- ``esp_gmf_doa.h``: ai_doa element configuration and direction estimation callbacks
- ``esp_gmf_ai_audio_methods.h``: runtime method name macros

.. include-build-file:: inc/esp_gmf_afe_manager.inc

.. include-build-file:: inc/esp_gmf_afe.inc

.. include-build-file:: inc/esp_gmf_aec.inc

.. include-build-file:: inc/esp_gmf_wn.inc

.. include-build-file:: inc/esp_gmf_ai_audio_methods.inc
