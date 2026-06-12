ESP Audio Simple Player
=======================

:link_to_translation:`zh_CN:[中文]`

ESP Audio Simple Player is a simple audio player based on ESP-GMF. It assembles decoders and audio transformers into an audio pipeline and exposes synchronous and asynchronous playback interfaces. URI-driven: the scheme determines the IO type and the extension determines the decoder; for streams without a reliable suffix, the player combines URL-suffix hints with input content probing to select the decoder; playback state and decode information are delivered to the application via event callbacks; transformers and IO components can be selectively included via menuconfig.

Key Features
------------

- Supported audio formats: AAC, MP3, AMR, M4A, PCM, WAV, ADPCM, G711, OGG, VORBIS, OPUS, ALAC, FLAC, SBC, LC3, TS
- Configurable audio transformers: bit depth conversion, channel conversion, sample rate conversion (sample rate conversion is enabled by default)
- Supports both synchronous and asynchronous playback interfaces
- Three built-in IO streams: HTTP, File, and embedded flash, automatically selected by URI scheme
- Decoder automatically selected by URI extension
- Event callback mechanism: playback state changes and audio decode information reported to the application layer
- Register custom IO and elements: extend functionality without modifying component source code
- menuconfig pruning: select transformers and IO according to hardware resources and application requirements, reducing memory and processor load
- On-demand decoder formats: enable only the formats actually used via menuconfig to reduce firmware size
- Smart format detection: suffix-based hints plus content probing for streams with missing or unreliable extensions
- Configurable bit-depth conversion output target via menuconfig
