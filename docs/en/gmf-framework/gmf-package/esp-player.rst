ESP Player
=======================

:link_to_translation:`zh_CN:[中文]`

``esp_player`` is an embedded multimedia playback component from Espressif. A single instance chains demuxing, decoding, and rendering, supporting local files, HTTP(S) streams, HLS, and containerless external frame input. It targets resource-constrained IoT and multimedia applications.

Key Features
------------

- Multiple input sources: local files (``file:///``), HTTP/HTTPS streams, HLS (``.m3u8`` auto-detection), and external frame modes (``fill:///``, ``block:///``)
- Common container formats: WAV, MP4, M4A, TS, OGG, AVI, FLV, CAF; raw ES stream files (``.mp3``, ``.aac``, ``.flac``, ``.amr`` without container headers)
- Audio decode formats: AAC, MP3, Vorbis, Opus, FLAC, AMR-NB/WB, G.711 A-law/μ-law, ALAC, ADPCM, SBC, LC3
- Video decode formats: H.264, MJPEG
- A/V synchronization: four modes—system clock, audio master clock, video master clock, and freerun (no sync)
- Playback control: play, pause, resume, stop, seek (milliseconds), and playback speed
- Audio/video track selection: independent audio/video enable, with track enumeration and selection for multi-track containers
- Network buffering: startup pre-buffering and runtime re-buffering based on queue watermarks
- Event notification: synchronous callbacks or asynchronous event queue, covering playback state, buffering, errors, and track information
- Custom decoders: register proprietary GMF audio/video decode elements via factory callbacks, coexisting with built-in decoders
