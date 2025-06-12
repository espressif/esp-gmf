# ESP-GMF-Examples

- [中文版](./README_CN.md)

ESP GMF Examples is a component that collects GMF-related examples, mainly designed to help users quickly access GMF examples through [ESP Registry](https://components.espressif.com/). It is a virtual component that cannot be depended on by any component or project, which means you cannot use `idf.py add-dependency "espressif/gmf_examples"`. The examples are listed below:

| Example Name | Description | Main Components | Data Flow |
|---------|---------|---------|---------|
| pipeline_play_embed_music | Play music embedded in Flash | - aud_dec<br>- aud_bit_cvt<br>- aud_rate_cvt<br>- aud_ch_cvt | Flash -> Decoder -> Audio Processing -> Output Device |
| pipeline_play_sdcard_music | Play music from SD card | - aud_dec<br>- aud_rate_cvt<br>- aud_ch_cvt<br>- aud_bit_cvt | SD Card -> Decoder -> Audio Processing -> Output Device |
| pipeline_record_sdcard | Record audio to SD card | - encoder | Input Device -> Encoder -> SD Card |
