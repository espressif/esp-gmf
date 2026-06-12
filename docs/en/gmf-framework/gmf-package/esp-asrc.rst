ESP ASRC
========

:link_to_translation:`zh_CN:[中文]`

ESP ASRC (``esp_asrc``) is a sample rate, bit depth, and channel conversion component for audio pipelines, aligning audio formats between modules such as playback, recording, Bluetooth audio, and speech front-end. The component uses a software-hardware co-design architecture: on SoCs with ASRC hardware, the hardware path offers low-latency, low-CPU real-time conversion; otherwise an optimized software path is used, ensuring the same API is usable across different SoCs.

Key Features
------------

- Sample rate conversion: supports conversion between common speech, music, and communication sample rates, up to 192 kHz (subject to driver verification)
- Bit depth conversion: supports 8-bit unsigned PCM and 16/24/32-bit signed interleaved PCM
- Channel conversion: supports mapping and mixing between mono, stereo, and multi-channel, for compatibility with codecs, Bluetooth, and algorithm modules
- Software-hardware co-design: supports AUTO strategy to automatically select hardware or software paths
- Low resource usage: hardware path reduces CPU usage; software path trades off quality, speed, and memory via a complexity parameter
- Alignment helper: provides buffer alignment query and allocation interfaces for safe use in PSRAM, cache, and hardware DMA scenarios
