GMF Loader
==========

:link_to_translation:`zh_CN:[中文]`

GMF Loader is an initialization and configuration helper component for the GMF pool. It allows batch selection of elements and IO components via menuconfig, setting default parameters before registering them into the pool. It covers common combinations including IO (codec_dev, file, http, flash read), audio codecs, audio effects, video codecs, and image effects, eliminating repetitive registration logic in application code.

Key Features
------------

- One-step initialization: after selecting in menuconfig, ``gmf_loader_setup_*`` batch-registers the corresponding elements into the user pool
- IO selection: reader side covers codec_dev RX, file, http, flash; writer side covers codec_dev TX, file, http
- Audio codecs: decoder supports MP3, AAC, AMR-NB/WB, FLAC, WAV, M4A, TS, OGG, OPUS, G711, PCM, ADPCM, LC3, SBC, ALAC, VORBIS, G722; encoder supports AAC, AMR-NB/WB, G711, OPUS, ADPCM, PCM, ALAC, LC3, SBC, G722
- Audio effects: ALC, EQ, Mixer, Sonic, Fade, DRC, MBC, channel/bit depth/sample rate conversion, each independently enabled, plus ASRC, howling suppression (HOWL), and channel interleave/deinterleave
- Video codecs and image effects: H.264 / MJPEG SW/HW encoding and decoding, PPA acceleration, scaling, rotation, cropping, overlay, frame rate conversion, and color conversion, selectable as needed
- AI audio: wake word detection (WakeNet), AEC, and the full AFE front-end are automatically registered into the pool via menuconfig; standalone VAD, NS, and DOA elements can also be registered via menuconfig
- Configuration over code: all default parameters are exposed via Kconfig; binary size can be reduced without modifying C source code
- Audio muxer: TS, MP4, FLV, WAV, CAF, OGG containers, with streaming or file output modes
- Miscellaneous: Copier copies data between elements
