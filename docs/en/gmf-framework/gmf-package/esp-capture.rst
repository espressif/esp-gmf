ESP Capture
===========

:link_to_translation:`zh_CN:[中文]`

ESP Capture is a lightweight multimedia capture component based on ESP-GMF. It follows the "capture source → capture path → capture sink" model to process data captured from input devices into the target output format. It integrates audio/video encoding, image rotation/scaling, acoustic echo cancellation, and layer compositing, with support for multiple parallel sinks, each having a built-in muxer and local storage capability. The processing chain is automatically negotiated based on source and target formats to simplify configuration. Common applications include audio/video recording, AI model input, WebRTC, RTMP streaming, local storage, and remote monitoring.

Key Features
------------

- Low memory overhead with a modular pipeline structure
- Deep integration with ESP-GMF, reusing the framework's advanced audio and video processing capabilities
- Multiple input devices: V4L2 camera, DVP camera, audio codec
- Parallel streaming and local storage: simultaneously stream and record from a single capture
- Automatic source/target negotiation: constructs the processing chain automatically based on input format and output requirements
- Customizable pipeline: supports custom source, path, sink, and negotiation strategies
- Multiple overlay regions: each sink can attach multiple overlay regions via a linked list
