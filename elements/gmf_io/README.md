# ESP-GMF-IO

- [![Component Registry](https://components.espressif.com/components/espressif/gmf_io/badge.svg)](https://components.espressif.com/components/espressif/gmf_io)

- [中文版](./README_CN.md)

ESP GMF IO is a collection of GMF input and output streams. IO streams have two data flow directions: reading and writing. The input type is readable, and the output type is writable. IO streams classify data transmission types into byte access and block access types. In general, byte access involves copying the source data before transferring, while block access only transfers the source data address without copy overhead. Additionally, IO streams provide optional thread configuration, allowing some IO streams to establish their own independent threads. The currently supported IO streams are listed in the table below:

| Name | TAG | Data flow direction | Thread | Data Type| Dependent Components  | Notes |
| :----: | :----: | :----: | :----: | :----: | :----: |:----: |
|  File | io_file | RW  |  NO |  Byte  |NA  | NA |
|  HTTP | io_http |  RW | YES | Block | NA  | Not support HTTP Live Stream |
|  Codec Dev IO | io_codec_dev |  RW | NO | Byte | [ESP codec dev](https://components.espressif.com/components/espressif/esp_codec_dev)  | NA |
|  Embed Flash | io_embed_flash |  R | NO | Byte | NA  | NA |
|  I2S PDM | io_i2s_pdm |  RW | NO | Byte | NA  | NA |

## Usage
ESP GMF IO is typically used in combination with GMF Elements to form a pipeline, but it can also be used independently. In a pipeline, the IO stream is connected to the GMF Element's Port, and when used independently, the IO stream is accessed through the ESP_GMF_IO interface. For example code, please refer to [test_app](../test_apps/main/elements/gmf_audio_play_el_test.c)。

You can also create and compile a project using the following commands, taking the `pipeline_play_embed_music` project as an example. Before starting, make sure you have a working [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html) environment.

### 1. Create the Example Project

Create the `pipeline_play_embed_music` example project based on the `gmf_examples` component (using version v0.7.0 as an example; update the version as needed):

```shell
idf.py create-project-from-example "espressif/gmf_examples=0.7.0:pipeline_play_embed_music"
```

### 2. Build and Flash the Project Using an ESP32-S3 Board

```shell
cd pipeline_play_embed_music
idf.py set-target esp32s3
idf.py -p YOUR_PORT flash monitor
```
