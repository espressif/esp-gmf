GMF-Audio
=========

:link_to_translation:`zh_CN:[中文]`

gmf_audio is the audio processing component of ESP-GMF, providing 17 elements across five categories: codec, format conversion, audio effects, channel layout, and audio muxer. All elements inherit from ``esp_gmf_audio_element_t``, exposing unified lifecycle functions, acquire-release data interfaces, and runtime method calls. The underlying algorithms are implemented by ``esp_audio_codec``, ``esp_audio_effects``, and ``esp_muxer``. This document covers the purpose, configuration, and runtime control of each element by category. For the element base class and runtime method mechanism, see :doc:`/gmf-framework/gmf-core/gmf-core-element`; for the data path, see :doc:`/gmf-framework/gmf-core/gmf-core-data-path`.

Feature List
------------

- aud_dec: multi-format audio decoding, supporting MP3, AAC, AMRNB, AMRWB, FLAC, WAV, M4A, TS, OPUS, SBC, LC3, ADPCM, ALAC, G711, VORBIS, OGG, G722; format can be reconfigured without rebuilding the element
- aud_enc: multi-format audio encoding, supporting AAC, AMRNB, AMRWB, ADPCM, OPUS, PCM, ALAC, SBC, LC3, G711, G722; bitrate can be adjusted at runtime
- aud_rate_cvt: sample rate conversion; the target sample rate can be switched at runtime via ``set_dest_rate``
- aud_asrc: hardware-assisted sample rate conversion; ``perf_type`` supports AUTO / HW_ONLY / SW_MEMORY / SW_SPEED; ``complexity`` 0-3 controls precision; shares the same runtime method interface as ``aud_rate_cvt``
- aud_ch_cvt: channel count conversion, supporting mapping and downmixing between mono, stereo, and multi-channel
- aud_bit_cvt: bit depth conversion, covering mutual conversion among 8 / 16 / 24 / 32 bit PCM
- aud_eq: multi-band equalizer; each band supports filter types such as peak, low-shelf, high-shelf, low-pass, and high-pass; bands can be toggled individually
- aud_alc: automatic level control, adjusting gain independently per channel
- aud_fade: fade-in/fade-out, supporting linear and curve transitions; progress can be reset at runtime
- aud_sonic: time-stretch / pitch-shift; speed and pitch range [0.5, 2.0]
- aud_mixer: multi-track mixer; each input track has an independent transition mode; audio info can be reset at runtime
- aud_drc: single-band dynamic range control; parameterized attack / release / hold / makeup / knee and breakpoint curve
- aud_mbc: multi-band dynamic range compression; each band has independent solo / bypass / crossover frequency
- aud_howl: howling suppression; FFT spectrum analysis combined with PAPR / PHPR / PNPR / IMSD multi-criteria; dynamically inserts notch filters per frame
- aud_intlv / aud_deintlv: multi-channel interleaving and de-interleaving; packs N independent mono streams into one multi-channel stream or splits in reverse
- aud_muxer: audio muxer that packages encoded audio data into container formats (TS, MP4, FLV, WAV, CAF, OGG, AVI); supports both streaming output and segmented file writing modes
- Common: automatic format detection, dynamic reconfiguration (``need_reopen``), bypass optimization, effects buffer in-place, per-element mutex thread safety

Technical Details
-----------------

Element Hierarchy and Common Mechanisms
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Each specific element is constructed by "wrapping an ``esp_ae_*`` algorithm handle": the derived struct uses ``esp_gmf_audio_element_t`` as its first field, binds the algorithm handle, a configuration block, and an internal mutex; ``open`` creates the algorithm handle and fills back the element ``ops``; ``process`` acquires a pair of input/output payloads at each scheduling interval and calls the algorithm's ``process`` function; ``close`` destroys the handle.

.. only:: html

   .. mermaid::

      classDiagram
          direction TD

          class esp_gmf_audio_element_t
          class Codec {
              aud_dec
              aud_enc
          }
          class FormatConversion {
              aud_rate_cvt
              aud_asrc
              aud_ch_cvt
              aud_bit_cvt
          }
          class AudioEffects {
              aud_eq
              aud_alc
              aud_fade
              aud_sonic
              aud_mixer
              aud_drc
              aud_mbc
              aud_howl
          }
          class ChannelLayout {
              aud_intlv
              aud_deintlv
          }
          class Muxer {
              aud_muxer
          }

          esp_gmf_audio_element_t <|-- Codec
          esp_gmf_audio_element_t <|-- FormatConversion
          esp_gmf_audio_element_t <|-- AudioEffects
          esp_gmf_audio_element_t <|-- ChannelLayout
          esp_gmf_audio_element_t <|-- Muxer

All elements share three common mechanisms; understanding them avoids repeatedly consulting the source code:

- **Effects buffer in-place**: Effect elements (``eq`` / ``alc`` / ``fade``, etc.) check ``in_port->is_shared`` in ``process``; if set, they directly let ``out_load = in_load``, sharing the same payload buffer between input and output; the underlying ``esp_ae_*`` algorithm processes in-place, avoiding extra allocation.
- **Bypass optimization**: Conversion elements (``rate_cvt`` / ``ch_cvt`` / ``bit_cvt`` / ``asrc``) compare source and destination parameters at the ``open`` stage; if identical, they enter bypass mode. In bypass mode, if ``in_port->is_shared`` is set, ``out_load = in_load`` is used for zero-copy pass-through; otherwise a single memcpy is performed.
- **Fine-grained mutex**: All algorithm handle calls are wrapped in the element's own mutex, allowing setters to be called from other threads during runtime (e.g., adjusting volume while running).
- **Format notify-and-update**: The upstream element notifies the downstream element after resolving the current sample format; the downstream element rebuilds the underlying algorithm handle accordingly. Detailed in the next section.

Format Notify-and-Update Flow
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Audio processing pipelines often encounter situations where downstream algorithms can only be configured correctly after the upstream decoder resolves the actual sample rate. gmf_audio divides this flow into two actions: notify and update:

**Notify (upstream)**: After the upstream element (decoder, rate converter, channel converter, etc.) confirms the actual sample_rate / channel / bit_depth of the current frame in ``open`` or ``process``, it calls the ``GMF_AUDIO_UPDATE_SND_INFO(self, rate, bits, ch)`` macro, which does two things: writes the new ``esp_gmf_info_sound_t`` to its own audio info field, and reports a ``REPORT_INFO`` event via ``esp_gmf_element_notify_snd_info``; the pipeline receives this and delivers it to subsequent elements along the topology.

**Update (downstream)**: The downstream element writes the new info to its own audio info field in the event callback and sets ``need_reopen``. At the next ``process`` boundary, the element detects this flag, calls ``close`` to destroy the old handle, then ``open`` to rebuild with the new parameters. The framework completes the reopen automatically; application code only needs to ensure that downstream elements have an event subscription relationship with upstream elements (which pipelines created from a pool satisfy by default).

Setter calls (``set_dest_rate`` / ``set_para``, etc.) use the same update path: after a setter change, ``need_reopen`` is also set, and the algorithm handle is rebuilt at the next ``process`` boundary.

For format info events and dependency-type element startup flow, see the REPORT_INFO description in :doc:`/gmf-framework/gmf-core/gmf-core-pipeline`.

Runtime Method Call Pattern
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Each element exposes a set of macro-based method names via ``esp_gmf_audio_methods_def.h``; the application layer assembles a string with ``AMETHOD(MODULE, METHOD)`` and calls ``esp_gmf_element_exe_method``:

.. code:: c

    /* Adjust ALC channel 0 gain to -6 dB */
    uint8_t buf[2] = { 0 /* idx */, (uint8_t)(-6) };
    esp_gmf_element_exe_method(alc_el, AMETHOD(ALC, SET_GAIN), buf, sizeof(buf));

You can also call the element's own named setter directly (e.g., :cpp:func:`esp_gmf_alc_set_gain`), both paths use the same implementation; the only difference is the calling style. This component provides complete named APIs per element, and either can be chosen at runtime.

Another advantage of runtime methods is the decoupling of interface from implementation. Taking ``aud_rate_cvt`` as an example: when the application calls ``AMETHOD(RATE_CVT, SET_DEST_RATE)``, it only depends on the method name, not the specific element. If switching to hardware ASRC (``aud_asrc``) is needed, simply replace ``aud_rate_cvt`` with ``aud_asrc`` in the pool; the latter implements the same ``RATE_CVT::SET_DEST_RATE`` interface, so no application code changes are required. Similarly, a custom resampling implementation can be registered as a custom element under the same runtime method name and connected directly to an existing pipeline without modifying the upper-level call logic.

Audio Codec
^^^^^^^^^^^

aud_dec and aud_enc are the start or end elements of playback and recording pipelines; a typical assembly is as follows.

.. only:: html

   .. mermaid::

      flowchart LR
          subgraph play ["Playback Pipeline"]
              direction LR
              FileIn(("file/http")) --> Dec["aud_dec"]
              Dec --> Rate["aud_rate_cvt"]
              Rate --> EQ["aud_eq"]
              EQ --> Out(("codec_dev"))
          end
          subgraph rec ["Recording Pipeline"]
              direction LR
              CodecIn(("codec_dev")) --> Ch["aud_ch_cvt"]
              Ch --> AGC["aud_alc"]
              AGC --> Enc["aud_enc"]
              Enc --> FileOut(("file/http"))
          end

**aud_dec**: Decodes compressed streams to PCM. :cpp:func:`esp_gmf_audio_dec_init` accepts ``esp_audio_simple_dec_cfg_t``; ``dec_type`` specifies the format; setting ``use_frame_dec`` to true uses frame-level decoding directly, bypassing the simple decoder's container parsing layer. At runtime, reconfigure via :cpp:func:`esp_gmf_audio_dec_reconfig` or by ``esp_gmf_info_sound_t``, which applies when the codec is configured after the IO returns the stream header. aud_dec has a built-in ``audio_dec_detect_type`` that automatically detects common container headers (``#!AMR``, ``RIFF``, ``fLaC``, ``OggS``, ``ID3``); when ``dec_type`` is unspecified, it can identify MP3 / AAC / FLAC / OGG, and other formats.

.. code:: c

    esp_audio_simple_dec_cfg_t cfg = DEFAULT_ESP_GMF_AUDIO_DEC_CONFIG();
    cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;

    esp_gmf_element_handle_t dec = NULL;
    esp_gmf_audio_dec_init(&cfg, &dec);
    /* Switch to AAC after receiving stream header */
    esp_gmf_info_sound_t info = { .format_id = ESP_FOURCC_AAC, ... };
    esp_gmf_audio_dec_reconfig_by_sound_info(dec, &info);

**aud_enc**: Encodes PCM to a compressed format. :cpp:func:`esp_gmf_audio_enc_init` accepts ``esp_audio_enc_config_t``; ``type`` specifies the output format (AAC, OPUS, SBC, LC3, G722, etc.). At runtime, call :cpp:func:`esp_gmf_audio_enc_set_bitrate` to change the bitrate; :cpp:func:`esp_gmf_audio_enc_reconfig` / ``..._by_sound_info`` switches format or parameters when the element is in ``NONE`` or ``INITIALIZED`` state.

aud_enc internally uses a byte cache to handle the case where input data is not necessarily aligned to a full frame; it cooperates with job return codes and task scheduling:

- When input is not enough for one frame, it accumulates in the byte cache and returns ``ESP_GMF_JOB_ERR_CONTINUE``; the task feeds another block of data to the same element;
- When input is larger than one frame, only complete frames from the byte cache are consumed; the remaining tail stays in the byte cache and returns ``ESP_GMF_JOB_ERR_TRUNCATE``; the task skips to the downstream element to consume the encoded output, then returns to this element to process the remaining bytes;
- After accumulating a complete frame, calls the underlying ``esp_audio_enc_process`` to produce output.

PTS is also maintained by the element at the byte cache level: when the accumulated duration in the byte cache exceeds the ``pts`` of the current origin, it is automatically clamped to 0 to avoid rollback. aud_dec uses the same ``CONTINUE`` / ``TRUNCATE`` pattern; application code does not need to care. Custom elements connected after a codec must also follow the same convention.

Format Conversion
^^^^^^^^^^^^^^^^^

The format conversion section has four elements: ``rate_cvt`` / ``ch_cvt`` / ``bit_cvt`` wrap the corresponding algorithms in ``esp_audio_effects``, exposing only one ``set_dest_*`` setter each; ``asrc`` uses the independent ``esp_asrc`` library, providing hardware-assisted or software-fallback processing paths.

**aud_rate_cvt**: Resamples input PCM to the target sample rate. ``DEFAULT_ESP_GMF_RATE_CVT_CONFIG`` defaults to 44100 → 48000, stereo, 16-bit; ``complexity`` ranges from 0-5, where higher values give better quality at greater cost; ``perf_type`` selects between ``ESP_AE_RATE_CVT_PERF_TYPE_SPEED`` and ``QUALITY``. To switch target sample rate at runtime:

.. code:: c

    esp_gmf_rate_cvt_set_dest_rate(rate_el, 16000);

The setter triggers ``need_reopen``; the underlying handle is automatically rebuilt at the next ``process`` boundary.

**aud_ch_cvt**: Converts channel count, supporting arbitrary N → M mapping. If the source and destination channels are equal, bypass is set at open. Use :cpp:func:`esp_gmf_ch_cvt_set_dest_ch` to change the destination channel count at runtime.

**aud_bit_cvt**: Converts bit depth, covering mutual conversion among uint8 / int16 / int24 / int32 PCM. Use :cpp:func:`esp_gmf_bit_cvt_set_dest_bits` to change the destination bit depth at runtime.

**aud_asrc**: Uses the hardware ASRC peripheral (when supported by the SoC) or a software fallback for sample rate conversion; ``perf_type`` determines the backend selection:

- ``ESP_ASRC_PERF_TYPE_AUTO``: Prefers hardware; automatically falls back to software when unavailable
- ``ESP_ASRC_PERF_TYPE_HW_ONLY``: Hardware only; returns an error on unsupported SoCs
- ``ESP_ASRC_PERF_TYPE_SW_MEMORY`` / ``ESP_ASRC_PERF_TYPE_SW_SPEED``: Pure software; optimizes memory usage or processing speed respectively

``complexity`` ranges from 0-3 (only valid in software mode); higher values give better quality at greater cost. ``timeout_ms`` is the timeout threshold for waiting for hardware to complete one frame; ignored in software mode. ``DEFAULT_ESP_GMF_ASRC_CONFIG`` defaults to 44100 → 48000, stereo, 16-bit.

.. code:: c

    esp_asrc_cfg_t cfg = DEFAULT_ESP_GMF_ASRC_CONFIG();
    esp_gmf_element_handle_t asrc_el = NULL;
    esp_gmf_asrc_init(&cfg, &asrc_el);
    /* Switch target sample rate at runtime */
    esp_gmf_asrc_set_dest_rate(asrc_el, 16000);

The setter triggers ``need_reopen``, same behavior as ``aud_rate_cvt``. ``aud_asrc`` registers the ``RATE_CVT::SET_DEST_RATE`` method with the same interface as ``aud_rate_cvt``, and can be swapped in the pool without modifying application code.

All three conversion elements (``rate_cvt`` / ``ch_cvt`` / ``bit_cvt``) follow the same data flow: in ``process``, acquire one block of input, compute the required output bytes by src/dest ratio, acquire output, call ``esp_ae_*_process`` to process one frame, then release both. In bypass mode, if ``in_port->is_shared`` is set, the output payload directly points to the input payload for zero-copy pass-through.

Audio Effects
^^^^^^^^^^^^^

**aud_eq**: Multi-band equalizer. ``DEFAULT_ESP_GMF_EQ_CONFIG`` uses built-in 10-band default parameters (covering 31 Hz - 16 kHz) when ``filter_num = 0``. Each band is configured independently:

.. code:: c

    esp_ae_eq_filter_para_t para = {
        .filter_type = ESP_AE_EQ_FILTER_PEAK,
        .fc          = 1000,
        .q           = 1.0f,
        .gain        = 6.0f,
    };
    esp_gmf_eq_set_para(eq_el, 0, &para);
    esp_gmf_eq_enable_filter(eq_el, 0, true);

EQ decides which bands participate in processing based on the enable state at ``open``; ``process`` calls ``esp_ae_eq_process`` to apply the overall frequency response adjustment to the current PCM.

**aud_alc**: Automatic level control; gain is set independently per channel, range [-64, 63] dB; values below ``-64`` are treated as mute.

.. code:: c

    esp_gmf_alc_set_gain(alc_el, 0, -6);  /* Left -6 dB */
    esp_gmf_alc_set_gain(alc_el, 1, -6);  /* Right -6 dB */

**aud_fade**: Fade-in/fade-out; ``mode`` selects ``FADE_IN`` or ``FADE_OUT``; ``curve`` selects ``LINE`` or ``CURVE``; ``transit_time`` in ms. :cpp:func:`esp_gmf_fade_reset` resets the current fade progress to the initial weight, useful for "reusing the same fade element when switching tracks".

**aud_sonic**: Time-stretch / pitch-shift. speed and pitch range [0.5, 2.0], where 1.0 means no change. Time-stretch changes the number of output samples; the element internally maintains a variable-length output buffer to handle this scaling relationship.

.. code:: c

    esp_gmf_sonic_set_speed(sonic_el, 1.25f);  /* 1.25x speed */
    esp_gmf_sonic_set_pitch(sonic_el, 0.9f);   /* Slightly lower pitch */

**aud_mixer**: Multi-track mixer; the number of input ports is determined by ``src_num``. Each track sets its transition mode independently via :cpp:func:`esp_gmf_mixer_set_mode` (``FADE_BY_SAMPLES`` / ``MUTE`` / ``NONE``, etc.); the shared sample rate / bit width / channel can be reset at runtime via :cpp:func:`esp_gmf_mixer_set_audio_info`. The first input blocks for 0 time, others block for the maximum delay: this ensures sound output immediately when the main track has data, and secondary tracks only mix in when they have data.

**aud_drc**: Single-band dynamic range control. In addition to the five time and curve parameters attack / release / hold / makeup / knee, you can also set input/output breakpoint curves via :cpp:func:`esp_gmf_drc_set_points` (supporting up to several ``esp_ae_drc_point_t`` points). Combined with ``aud_eq``, this creates a typical master bus compression chain.

**aud_mbc**: Multi-band dynamic compression. Each band independently adjusts threshold / ratio / attack / release / hold / knee / makeup; set crossover frequencies between bands via ``set_fc``; use ``set_solo`` and ``set_bypass`` for debugging to listen to or bypass a specific band.

**aud_howl**: Howling suppression; detects and attenuates feedback howling produced by the microphone, amplifier, and speaker loop. The algorithm evaluates three criteria simultaneously for each FFT frequency bin: PAPR (peak-to-average power ratio), PHPR (peak-to-harmonic power ratio), and PNPR (peak-to-noise power ratio). Frequency bins meeting any threshold are marked as howling candidates; the algorithm dynamically inserts biquad notch filters at those frequencies and reduces overall gain to suppress new howling peaks.

The optional fourth criterion IMSD (inter-frame spectral magnitude deviation) is enabled via ``enable_imsd``, which is more sensitive to sustained stable frequencies and is suitable for mixed speech and music content; disabling it in pure speech scenarios saves memory and CPU.

The processing frame length is determined by ``esp_ae_howl_get_frame_size`` at open based on sample rate (512 samples/channel when sample_rate < 32000 Hz, otherwise 1024 samples/channel). The algorithm supports in-place processing; input and output can point to the same buffer.

.. note::

   The GMF element wrapper for ``aud_howl`` (``esp_gmf_howl.h``) is still under development; the specific initialization API is subject to the final merged implementation.

Channel Layout
^^^^^^^^^^^^^^

**aud_intlv**: Merges N mono inputs into one N-channel interleaved PCM stream; ``src_num`` determines the number of input ports. Commonly used to merge AEC reference and microphone channels into stereo for file writing.

**aud_deintlv**: The reverse operation; splits one interleaved PCM stream into N output ports by channel. Commonly used to split stereo into left and right channels for separate post-processing.

Both only perform memory reorganization without modifying sample values; ``process`` loops memcpy at sample stride.

Audio Muxer
^^^^^^^^^^^

**aud_muxer**: Packages encoded audio data into container format output. Driven by the ``esp_muxer`` library, supported container formats include TS, MP4, FLV, WAV, CAF, OGG, and AVI. aud_muxer is typically placed after aud_enc, responsible for completing format encapsulation at the end of recording or transcoding pipelines.

Main fields of ``esp_gmf_audio_muxer_cfg_t``:

.. list-table::
   :widths: 28 72
   :header-rows: 1

   * - Field
     - Description
   * - ``muxer_type``
     - Container type: ``ESP_MUXER_TYPE_TS / MP4 / FLV / WAV / CAF / OGG / AVI``
   * - ``codec``
     - Codec type of the input audio (corresponding to the output format of aud_enc)
   * - ``output_type``
     - Output mode: ``STREAMING`` (streaming output via databus) or ``FILE`` (written to files)
   * - ``slice_duration``
     - Valid in file mode only; duration of each file segment in milliseconds, default 60000
   * - ``url_pattern`` / ``url_ctx``
     - Valid in file mode only; segmented file path callback that determines which file each segment is written to
   * - ``get_codec_spec_info_cb``
     - Optional. Callback to provide additional metadata such as SPS for formats like AAC that require codec-specific information

**Streaming output mode**: When ``output_type = ESP_GMF_AUDIO_MUXER_OUTPUT_STREAMING``, the element creates an internal block databus at ``open``; the muxer writes encapsulated data to the databus via callback, then forwards it to downstream via out_port (e.g., an IO element for file writing or network transmission).

**File segment mode**: When ``output_type = ESP_GMF_AUDIO_MUXER_OUTPUT_FILE``, the element does not occupy out_port; it directly splits and writes a series of files at ``slice_duration`` intervals via the ``url_pattern`` callback, suitable for continuous recording scenarios.

.. code:: c

    esp_gmf_audio_muxer_cfg_t cfg = {
        .muxer_type  = ESP_MUXER_TYPE_MP4,
        .codec       = ESP_MUXER_AUDIO_CODEC_AAC,
        .output_type = ESP_GMF_AUDIO_MUXER_OUTPUT_STREAMING,
    };
    esp_gmf_element_handle_t muxer_el = NULL;
    esp_gmf_audio_muxer_init(&cfg, &muxer_el);

aud_muxer relies on the ``esp_gmf_info_sound_t`` (sample rate, bit depth, channel) reported by the upstream aud_enc to initialize the muxer audio stream descriptor; ensure aud_enc completes the ``REPORT_INFO`` event delivery before aud_muxer by maintaining the standard pipeline connection order enc → muxer.

Performance
-----------

Overall, the timing of each element is primarily determined by the underlying ``esp_ae_*`` algorithm; the relative overhead of the framework's acquire-release and runtime method scheduling per ``process`` call is small (on the order of hundreds of microseconds). When balancing CPU and audio quality:

- The ``complexity`` field of ``aud_rate_cvt`` directly trades audio quality for CPU; ``perf_type = SPEED`` is generally sufficient for embedded player scenarios
- More enabled bands in ``aud_eq`` / ``aud_drc`` / ``aud_mbc`` mean higher cost; use ``enable_filter`` / ``set_bypass`` to disable them on demand at runtime
- The throughput of ``aud_dec`` and ``aud_enc`` depends on the specific codec format (OPUS, AAC, and FLAC differ significantly); refer to the benchmarks provided by ``esp_audio_codec``
- Conversion elements automatically bypass when source and destination parameters are identical; no need to split elements for "possibly unnecessary conversions" in pipeline design

Application Examples
--------------------

- ``elements/test_apps/main/elements/gmf_audio_el_test.c``: Individual test cases for each element, covering default parameters, runtime setters, reset, etc.
- ``elements/test_apps/main/elements/gmf_audio_play_el_test.c``: Comprehensive use case assembling a playback pipeline
- ``elements/test_apps/main/elements/gmf_audio_effects_test.c``: Effects pipeline test
- ``elements/test_apps/main/elements/gmf_audio_rec_el_test.c``: Recording pipeline test, covering the encoding side
- ``gmf_examples/basic_examples/pipeline_play_embed_music``: Complete application project

The upper-level wrappers ``esp_audio_simple_player`` / ``esp_audio_render`` also connect audio pipelines through this component internally and can serve as advanced references.

FAQ
---

**Q:** Can ``aud_dec`` still decode when ``dec_type`` is not specified?

Yes. This works only when ``use_frame_dec = false`` (default). In this case, the decoder enables its internal parser to automatically identify the container header and frame sync bytes from the input stream to determine the format and initialize the decoder. If ``use_frame_dec = true``, the input is treated as complete encoded frames without going through the parser, so automatic format detection will not work. When the format is known, it is recommended to explicitly set ``dec_type`` for faster startup and to avoid misidentification.

**Q:** Under what conditions does ``aud_enc`` ``set_bitrate`` take effect?

When the encoder handle has not been created, :cpp:func:`esp_gmf_audio_enc_set_bitrate` validates the parameter and writes it into the ``esp_audio_enc_config_t`` configuration; when the handle exists, it applies directly to the running encoder. :cpp:func:`esp_gmf_audio_enc_reconfig` and ``..._by_sound_info`` can replace the full configuration only when state is less than ``OPENING``. Use :cpp:func:`esp_gmf_audio_enc_get_bitrate` to read the configured or active bitrate.

**Q:** Why is there a brief pause after switching the target sample rate in ``aud_rate_cvt``?

``set_dest_rate`` triggers ``need_reopen``; the element destroys the old handle and rebuilds a new one at the next process boundary. The rebuild process causes a brief pause. If a smooth transition is needed, perform the switch after ``pause``.

**Q:** If the main track of ``aud_mixer`` has no data, will the other tracks block?

The mixer design is "first track blocks 0, others block at maximum delay"; when the main track has no data, it returns immediately, and the remaining tracks mix with zero data. If you want secondary tracks to take over when the main track is missing, swap the connection order and connect the current secondary track to input port 0 as the main track.

**Q:** Why is the PCM length not half the input when ``aud_sonic`` has speed=2?

Time-stretch is a non-integer division operation; the element calculates output size by the current speed ratio while retaining cumulative error, so each frame's output length is not fixed. Downstream elements (such as codec_dev IO) should write to hardware based on ``valid_size`` and must not assume a fixed length.

**Q:** How is synchronization guaranteed between multiple inputs of ``aud_intlv``?

The interleave element does not perform synchronization; it relies on upstream elements providing a consistent number of samples from each track. The common approach for multiple AEC references is to connect all tracks to the same pipeline using the same task to serially acquire, thereby maintaining alignment.

API Reference
-------------

Header files for this component:

- ``esp_gmf_audio_dec.h`` / ``esp_gmf_audio_enc.h``: Codec elements
- ``esp_gmf_audio_helper.h`` / ``esp_gmf_audio_param.h``: Helper interfaces and parameter definitions
- ``esp_gmf_audio_methods_def.h``: Runtime method name and parameter name macros (pure macro definitions; see source header files for details)
- ``esp_gmf_rate_cvt.h`` / ``esp_gmf_ch_cvt.h`` / ``esp_gmf_bit_cvt.h``: Format conversion elements
- ``esp_gmf_asrc.h``: Hardware-assisted sample rate conversion element
- ``esp_gmf_eq.h`` / ``esp_gmf_alc.h`` / ``esp_gmf_fade.h`` / ``esp_gmf_sonic.h`` / ``esp_gmf_mixer.h``: Common audio effect elements
- ``esp_gmf_drc.h`` / ``esp_gmf_mbc.h``: Dynamic range control elements
- ``esp_gmf_interleave.h`` / ``esp_gmf_deinterleave.h``: Channel layout elements
- ``esp_gmf_audio_muxer.h``: Audio muxer element

The interface for the element base class ``esp_gmf_audio_element_t`` is located in :doc:`/gmf-framework/gmf-core/gmf-core-element`.

.. include-build-file:: inc/esp_gmf_audio_dec.inc

.. include-build-file:: inc/esp_gmf_audio_enc.inc

.. include-build-file:: inc/esp_gmf_audio_helper.inc

.. include-build-file:: inc/esp_gmf_audio_param.inc

.. include-build-file:: inc/esp_gmf_rate_cvt.inc

.. include-build-file:: inc/esp_gmf_ch_cvt.inc

.. include-build-file:: inc/esp_gmf_bit_cvt.inc

.. include-build-file:: inc/esp_gmf_eq.inc

.. include-build-file:: inc/esp_gmf_alc.inc

.. include-build-file:: inc/esp_gmf_fade.inc

.. include-build-file:: inc/esp_gmf_sonic.inc

.. include-build-file:: inc/esp_gmf_mixer.inc

.. include-build-file:: inc/esp_gmf_drc.inc

.. include-build-file:: inc/esp_gmf_mbc.inc

.. include-build-file:: inc/esp_gmf_interleave.inc

.. include-build-file:: inc/esp_gmf_deinterleave.inc
