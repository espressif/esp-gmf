Glossary
========

:link_to_translation:`zh_CN:[中文]`

Terms, domain nouns, and abbreviations used in ESP-GMF code and documentation, listed in alphabetical order.

A2DP
    Advanced Audio Distribution Profile

    Classic Bluetooth audio transport protocol for bidirectional audio streams.

Acquire-Release
    Data access protocol for data ports

    The read side calls ``acquire_in`` to obtain a payload, then ``release_in`` to return it. The write side calls ``acquire_out`` to obtain an empty payload, then ``release_out`` to submit it downstream.

AEC
    Acoustic Echo Cancellation

    Removes the loudspeaker output component from the microphone signal.

AFE
    Audio Front-End

    A set of algorithms that process raw microphone data, including AEC, NS, VAD, and WakeNet modules.

ALC
    Automatic Level Control

    Adjusts gain independently per channel.

AVRCP
    Audio/Video Remote Control Profile

    Used for playback control, metadata, and notifications.

BMGR / Board Manager
    Board Manager

    A YAML-based board hardware configuration system (``esp_board_manager``); a code generator translates the YAML into C initialization code.

Cache
    Byte cache

    ``esp_gmf_cache`` provides a byte-level sliding buffer that accumulates bytes from multiple payloads into a contiguous cache, then reads them back in arbitrary lengths. Commonly used by decoders to parse protocol or frame headers.

Capability
    Capability descriptor

    The mechanism by which an element declares its capabilities externally. Each capability descriptor node uses an EIGHTCC to identify the capability category and can carry performance information and attribute ranges, which the pool, pipeline, or application queries to select an element.

Data Bus
    Data bus

    The data queue underlying a port, providing four implementations: ringbuffer, fifo, block, and pbuf, responsible for cross-thread synchronization and buffering.

DRC
    Dynamic Range Control

    Compresses the signal according to a knee curve.

EIGHTCC
    Eight Character Code

    An 8-character algorithm category identifier defined in ``esp_gmf_caps_def.h``, describing the algorithm type of an element (e.g., ``AUDDEC`` / ``AUDEQ``).

Element
    Processing unit

    The processing unit in ESP-GMF, corresponding to one specific algorithm. Each element implements three fixed lifecycle functions: ``open`` / ``process`` / ``close``.

EQ
    Equalizer

    Adjusts frequency response by band.

FourCC
    Four Character Code

    A 32-bit identifier using 4 ASCII characters to represent an encoding or container format, defined in ``esp_fourcc.h``.

GMF
    General Multimedia Framework

    A lightweight general-purpose software framework built by Espressif for IoT multimedia applications.

GOP
    Group of Pictures

    The number of frames between two I-frames in video encoding.

HFP
    Hands-Free Profile

    Classic Bluetooth hands-free calling protocol.

IO
    External interface

    Special elements at the head or tail of a pipeline that connect to external data sources or sinks, implemented based on ``esp_gmf_io_t`` for file, HTTP, flash, codec, and I2S read/write capabilities.

is_done
    End-of-stream flag (``esp_gmf_payload_t`` field)

    Set to 1 when the source IO reaches the end of its data; downstream elements process it in turn, triggering close at each stage.

Job
    Work unit

    The specific work unit scheduled by a task, registered in the task's job list according to the element's open / process / close phases. Can be marked for single or unlimited execution.

Job Stack
    Job stack

    The structure inside a task that stores jobs temporarily suspended by ``TRUNCATE``. After downstream consumers process data, the task can return to the suspended job and continue execution.

MBC
    Multi-Band Compressor

    Compresses each frequency band independently.

Method
    Runtime method

    A runtime control action exposed by an element outside of ``open`` / ``process`` / ``close``, described by ``esp_gmf_method_t`` with method name, execution function, and parameter description. Applications can call methods by name to adjust parameters such as volume, target sample rate, or target resolution.

NS
    Noise Suppression

    A sub-module within AFE.

OAL
    Operating System Abstraction Layer

    Encapsulates memory, mutex, thread, and system statistics interfaces related to the operating system and ESP-IDF.

Payload
    Payload

    The data carrier (``esp_gmf_payload_t``) flowing between elements, containing a data buffer, valid length, end-of-stream flag, timestamp, and other fields.

Pipeline
    Pipeline

    The orchestrator of a processing chain, connecting multiple elements in sequence and exposing ``run`` / ``stop`` / ``pause`` / ``resume`` / ``reset`` / ``seek`` control interfaces.

Pool
    Pool

    A template registry for elements and IO components (``esp_gmf_pool_t``), used to instantiate pipelines by name.

Port
    Port

    The data read/write interface exposed by an element, using the acquire-release protocol to access payloads. The underlying data queue is provided by a data bus.

PPA
    Pixel Processing Accelerator

    A hardware acceleration unit on ESP32-P4/ESP32-S3, supporting color conversion, scaling, rotation, and cropping.

PTS
    Presentation Time Stamp

    Used for audio/video synchronization.

QP
    Quantization Parameter

    The quantization parameter in video encoding, affecting compression ratio and image quality.

SE
    Speech Enhancement

    A sub-module within AFE.

Task
    Task

    A FreeRTOS thread encapsulated by the framework, running in ``esp_gmf_task.c``, executing jobs one by one.

URL Score
    URL scoring mechanism

    The IO selection mechanism. Each IO implements a ``get_score`` callback to evaluate URL match quality; the pool selects the IO with the highest score.

VAD
    Voice Activity Detection

    Determines whether the current frame contains speech.

VCMD
    Voice Command Detection

    Recognizes preset command phrases based on the ``MultiNet`` model.

WWE / WakeNet
    Wake Word Engine

    Detects a specific wake word to trigger device activation.
