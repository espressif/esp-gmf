# ESP-GMF-Audio

- [![Component Registry](https://components.espressif.com/components/espressif/gmf_audio/badge.svg)](https://components.espressif.com/components/espressif/gmf_audio)

- [中文版](./README_CN.md)

ESP GMF Audio is a collection of GMF elements related to audio processing, including audio encoding, decoding, and audio effect processing algorithms. The currently supported audio modules are listed in the table below.

|Name|TAG|Function|Method|Input Port|Output Port|Input blocking time|Output blocking time|Dependent on Audio Information|
|:----:|:----:|:-----:|:----:|:----:|:----:|:----:|:----:|:----|
|  AUDIO_DEC |aud_dec |Audio decoder: MP3,AAC,AMRNB,<br>AMRWB,FLAC,WAV,M4A,TS|Nil|Single|Single|User configurable, default value is maximum delay|User configurable, default value is maximum delay|No|
|  AUDIO_ENC |aud_enc |Audio encoder: AAC,AMRNB,AMRWB,<br>ADPCM,OPUS,PCM|Nil|Single|Single|User configurable, default value is maximum delay|User configurable, default value is maximum delay|Yes|
|  RATE_CVT|aud_rate_cvt |Audio sampling rate adjustment|`set_dest_rate`|Single|Single|Maximum delay|Maximum delay|Yes|
|  BIT_CVT |aud_bit_cvt |Audio bit-depth conversion|`set_dest_bits`|Single|Single|Maximum delay|Maximum delay|Yes|
|  CH_CVT  |aud_ch_cvt |Audio channel conversion|`set_dest_ch`|Single|Single|Maximum delay|Maximum delay|Yes|
|  ALC     |aud_alc |Audio volume adjustment|`set_gain`<br>`get_gain`|Single|Single|Maximum delay|Maximum delay|Yes|
|  EQ      |aud_eq |Audio equalizer adjustment|`set_para`<br>`get_para`<br>`enable_filter`<br>`disable_filter`|Single|Single|Maximum delay|Maximum delay|Yes|
|  FADE    |aud_fade |Audio fade-in and fade-out effects|`set_mode`<br>`get_mode`<br>`reset_weight`|Single|Single|Maximum delay|Maximum delay|Yes|
|  SONIC   |aud_sonic |Audio pitch and speed shifting effects|`set_speed`<br>`get_speed`<br>`set_pitch`<br>`get_pitch`|Single|Single|Maximum delay|Maximum delay|Yes|
|  MIXER   |aud_mixer |Audio mixing effects|`set_info`<br>`set_mode`|Multiple|Single|The blocking time for the first channel is 0, while the blocking time for other channels is maximum delay|Maximum delay|No|
|INTERLEAVE|aud_intlv|Data interleaving|Nil|Multiple|Single|User configurable, default value is maximum delay|Maximum delay|Yes|
|DEINTERLEAVE|aud_deintlv|Data de-interleaving|Nil|Single|Multiple|Maximum delay|User configurable, default value is maximum delay|Yes|

## Usage
The ESP GMF Audio is often used in combination to form a pipeline. For example code, please refer to [test_app](../test_apps/main/elements/gmf_audio_play_el_test.c)。

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
