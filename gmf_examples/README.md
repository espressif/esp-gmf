# ESP-GMF-Examples

- [![Component Registry](https://components.espressif.com/components/espressif/gmf_examples/badge.svg)](https://components.espressif.com/components/espressif/gmf_examples)

- [中文版](./README_CN.md)

ESP GMF Examples is a component that collects GMF-related examples, mainly designed to help users quickly access GMF examples through [ESP Registry](https://components.espressif.com/). It is a virtual component that cannot be depended on by any component or project, which means you cannot use `idf.py add-dependency "espressif/gmf_examples"`. The examples are listed below:

| Example Name | Description | Main Components | Data Flow |
|---------|---------|---------|---------|
| [pipeline_play_embed_music](./basic_examples/pipeline_play_embed_music) | Play music embedded in Flash | - aud_dec<br>- aud_bit_cvt<br>- aud_rate_cvt<br>- aud_ch_cvt | Flash -> Decoder -> Audio Processing -> Output Device |
| [pipeline_play_sdcard_music](./basic_examples/pipeline_play_sdcard_music) | Play music from SD card | - aud_dec<br>- aud_rate_cvt<br>- aud_ch_cvt<br>- aud_bit_cvt | SD Card -> Decoder -> Audio Processing -> Output Device |
| [pipeline_record_sdcard](./basic_examples/pipeline_record_sdcard) | Record audio to SD card | - aud_enc | Input Device -> Encoder -> SD Card |

# Usage

Below is an example of how to get and compile the project using `pipeline_play_embed_music`. Before starting, make sure you have a working [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html) environment.

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
