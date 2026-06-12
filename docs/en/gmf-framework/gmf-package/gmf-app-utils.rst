GMF App Utils
=============

:link_to_translation:`zh_CN:[中文]`

GMF App Utils provides common helper interfaces for ESP-GMF application development, consolidating peripheral initialization, system management, and debugging interfaces into a unified set of APIs and configuration options. It offers reusable interfaces for audio codecs, I2C, SD card, Wi-Fi connectivity, CLI, and unit test helpers, reducing repetitive low-level initialization code.

Key Features
------------

- Peripheral initialization: covers audio codecs (I2S standard / TDM / PDM, playback and recording configured independently), I2C bus, SD card, and Wi-Fi connectivity
- Board abstraction: obtain board device handles via ``esp_board_manager``, decoupling application code from specific hardware
- System management: performance monitoring interface for runtime statistics including task, memory, and CPU usage
- Serial CLI: built-in console interface for debugging pipelines and elements in a REPL
- Configuration-driven: Wi-Fi and unit test parameters managed via menuconfig without code changes
- Lightweight: all features can be independently included or excluded; unused sub-modules consume no runtime resources
- Unit test helpers: Unity test entry, memory-leak detection, and TCP/IP test resource pre-allocation APIs
