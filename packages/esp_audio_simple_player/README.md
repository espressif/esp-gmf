# ESP Audio Simple Player

- [![Component Registry](https://components.espressif.com/components/espressif/esp_audio_simple_player/badge.svg)](https://components.espressif.com/components/espressif/esp_audio_simple_player)

[中文版](./README_CN.md)

The **ESP Audio Simple Player**, developed by Espressif and built on the ESP-GMF framework, is a lightweight and user-friendly audio player designed for simplicity and efficiency. With its modular, customizable, and resource-efficient architecture, it streamlines audio processing development, making it an ideal solution for audio applications in IoT devices. It offers developers a powerful yet flexible audio playback system.

## Key Features
- Supports popular audio formats, including AAC, MP3, AMR, FLAC, WAV, M4A, RAW_OPUS, and TS
- Configurable audio transformers with Bit Depth conversion, Channel conversion, and Sample Rate conversion
- Provides both synchronous and asynchronous playback interfaces
- Supports customizable IO streams and audio processing elements for tailored solutions

## How It Works
The ESP Audio Simple Player uses a modular audio processing pipeline that combines decoders and audio transformers. Audio data is fed into the system through a pre-configured IO Stream, processed by the decoder and transformers, and then delivered to the user layer via a callback function. This design allows developers to adapt the system to specific use cases, enabling flexible and efficient audio playback.

The player uses a URI to identify the audio resource. The URI's scheme determines the IO Stream type (e.g., HTTP stream, local file, or embedded flash), while the file extension specifies the audio format, ensuring the correct decoder is automatically selected (e.g., MP3, WAV, or AAC). The player's status and audio decoding details are communicated to the application layer through events, allowing for real-time monitoring and control.

Designed with resource efficiency in mind, the ESP Audio Simple Player supports audio transformers for Bit Depth conversion, Channel conversion, and Sample Rate conversion (enabled by default). It also supports three IO Stream types: HTTP, File, and embedded Flash. Using the Menuconfig interface, developers can fine-tune the system by enabling or disabling specific audio transformers and IO Streams based on their hardware capabilities and application needs. This ensures optimal resource utilization, minimizing memory and processing overhead.

Additionally, the player offers extensibility through custom IO and audio processing elements. Developers can easily integrate their own modules, enhancing the player's functionality to meet unique audio processing requirements.

## Configuration Guide
To configure the player, open the Menuconfig interface by running the following command in your terminal:
```
idf.py menuconfig
```

### Audio Transformers and IO Streams
In the Menuconfig interface, navigate to `Component config` -> `ESP Audio Simple Player` to enable or disable audio transformers and IO Streams. You can also configure the output parameters for the audio transformers, such as Bit Depth, Channel, and Sample Rate.

### Audio Formats
Under `Component config` -> `Audio Codec Configuration` -> `Audio Decoder Configuration` and `Audio Simple Decoder Configuration`, you can select the audio formats to support. This helps reduce the size of the compiled binary, saving Flash space and optimizing RAM usage.

## Example Usage
For a practical example of how to use the ESP Audio Simple Player, check out the sample code in [test_apps](./test_apps/main/aud_simp_player_test.c).