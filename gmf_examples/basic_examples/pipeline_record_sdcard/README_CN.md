# 录制音频存储 microSD 卡

- [English](./README.md)

## 例程简介

本例程介绍了通过 CODEC_DEV_RX IO 获取 codec 录制的声音，经 encoder 元素编码后，通过 File IO 将编码数据保存 microSD 卡。

本例支持将录音编码为 AAC、G711A、G711U、AMRNB、AMRWB、OPUS、ADPCM 和 PCM 音频格式，默认使用 AAC 格式。

## 示例创建

### IDF 默认分支

本例程支持 IDF release/v5.3 及以后的分支。

### 配置

本例程需要准备一张 microSD 卡。录音编码后的音频会自动存入 microSD 卡，用户可通过 `esp_gmf_audio_helper_reconfig_enc_by_type ` 函数修改编码格式及音频参数配置。

### 编译和下载

编译本例程前需要先确保已配置 ESP-IDF 的环境，如果已配置可跳到下一项配置，如果未配置需要先在 ESP-IDF 根目录运行下面脚本设置编译环境，有关配置和使用 ESP-IDF 完整步骤，请参阅 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/index.html)：

```
./install.sh
. ./export.sh
```

下面是简略编译步骤：

- 进入录制音频存储 microSD 卡测试工程存放位置

```
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_record_sdcard
```

- 选择编译芯片，以 esp32s3 为例：

```
idf.py set-target esp32s3
```
- 选择编译板子，以 esp32 s3 Korvo V2 为例：

```
idf.py menuconfig
在 `menuconfig` 中选择 `GMF APP Configuration` -> `Audio Board` -> `ESP32-S3-Korvo V2`，然后保存退出
```

- 编译例子程序

```
idf.py build
```

- 烧录程序并运行 monitor 工具来查看串口输出 (替换 PORT 为端口名称)：

```
idf.py -p PORT flash monitor
```

- 退出调试界面使用 ``Ctrl-]``

## 如何使用例程

### 功能和用法

- 例程开始运行后，录音数据经过编码后将自动存入 microSD 卡，录制完成后停止退出，打印如下：

```c
I (1020) REC_SDCARD: [ 1 ] Mount sdcard
I (1025) gpio: GPIO[15]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (1034) gpio: GPIO[7]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (1044) gpio: GPIO[4]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (1093) REC_SDCARD: [ 2 ] Register all the elements and set audio information to record codec device
I (1097) ES7210: Work in Slave mode
I (1104) ES7210: Enable ES7210_INPUT_MIC1
I (1107) ES7210: Enable ES7210_INPUT_MIC2
I (1111) ES7210: Enable ES7210_INPUT_MIC3
I (1114) ES7210: Enable TDM mode
E (1120) i2s_common: i2s_channel_disable(1107): the channel has not been enabled yet
I (1123) I2S_IF: channel mode 0 bits:16/16 channel:2 mask:1
I (1129) I2S_IF: STD Mode 0 bits:16/16 channel:2 sample_rate:16000 mask:1
I (1137) ES7210: Bits 8
I (1147) ES7210: Enable ES7210_INPUT_MIC1
I (1150) ES7210: Enable ES7210_INPUT_MIC2
I (1153) ES7210: Enable ES7210_INPUT_MIC3
I (1157) ES7210: Enable TDM mode
I (1163) ES7210: Unmuted
I (1163) Adev_Codec: Open codec device OK
I (1166) ESP_GMF_POOL: Registered items on pool:0x3c1a7640, app_main-51
I (1173) ESP_GMF_POOL: IO, Item:0x3c1a78e0, H:0x3c1a7850, TAG:io_codec_dev
I (1180) ESP_GMF_POOL: IO, Item:0x3c1a79b0, H:0x3c1a78f0, TAG:io_http
I (1187) ESP_GMF_POOL: IO, Item:0x3c1a7a80, H:0x3c1a79c0, TAG:io_http
I (1194) ESP_GMF_POOL: IO, Item:0x3c1a7b1c, H:0x3c1a7a90, TAG:io_file
I (1201) ESP_GMF_POOL: IO, Item:0x3c1a7bb8, H:0x3c1a7b2c, TAG:io_file
I (1208) ESP_GMF_POOL: IO, Item:0x3c1a7ce8, H:0x3c1a7c54, TAG:io_embed_flash
I (1215) ESP_GMF_POOL: EL, Item:0x3c1a7c44, H:0x3c1a7bc8, TAG:copier
I (1222) ESP_GMF_POOL: EL, Item:0x3c1a7dfc, H:0x3c1a7cf8, TAG:aud_enc
I (1229) ESP_GMF_POOL: EL, Item:0x3c1a7ef0, H:0x3c1a7e0c, TAG:decoder
I (1237) ESP_GMF_POOL: EL, Item:0x3c1a7fcc, H:0x3c1a7f00, TAG:aud_alc
I (1243) ESP_GMF_POOL: EL, Item:0x3c1a8090, H:0x3c1a7fdc, TAG:aud_eqd_eqd_eqd_eq
I (1250) ESP_GMF_POOL: EL, Item:0x3c1a80c4, H:0x3c1a81f8, TAG:aud_ch_cvt
I (1257) ESP_GMF_POOL: EL, Item:0x3c1a80f4, H:0x3c1a829c, TAG:aud_bit_cvt
I (1264) ESP_GMF_POOL: EL, Item:0x3c1a83dc, H:0x3c1a8340, TAG:aud_rate_cvt
I (1271) ESP_GMF_POOL: EL, Item:0x3c1a84b8, H:0x3c1a83ec, TAG:aud_fade
I (1278) ESP_GMF_POOL: EL, Item:0x3c1a85b4, H:0x3c1a84c8, TAG:aud_sonic
I (1285) ESP_GMF_POOL: EL, Item:0x3c1a8694, H:0x3c1a85c4, TAG:aud_deintlv
I (1293) ESP_GMF_POOL: EL, Item:0x3c1a8770, H:0x3c1a86a4, TAG:aud_intlv
I (1300) ESP_GMF_POOL: EL, Item:0x3c1a8870, H:0x3c1a8780, TAG:aud_mixer
I (1307) REC_SDCARD: [ 3 ] Create audio pipeline, add all elements to pipeline
I (1315) REC_SDCARD: [ 3.1 ] Set audio url to record
I (1321) REC_SDCARD: [ 3.2 ] Reconfig audio encoder type by url and audio information and report information to the record pipeline
W (1333) ESP_GMF_PIPELINE: There is no thread for add jobs, pipe:0x3c1a8880, tsk:0x0, [el:aud_enc-0x3c1a88b8]
I (1344) REC_SDCARD: [ 3.3 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (1355) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcb1260-0x3fcb1260, wk:0x0, run:0]
I (1363) ESP_GMF_THREAD: The TSK_0x3fcb1260 created on internal memory
I (1370) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcb1260-0x3fcb1260, wk:0x3c1a8ba8, run:0]
I (1380) REC_SDCARD: [ 3.4 ] Create envent group and listening event from pipeline
I (1388) REC_SDCARD: [ 4 ] Start audio_pipeline
I (1393) FILE_IO: Open, dir:2, uri:/sdcard/esp_gmf_rec001.aac
I (1409) REC_SDCARD: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c1a8880, type:8192, sub:ESP_GMF_EVENT_STATE_OPENING, payload:0x0, size:0,0x0
I (1416) REC_SDCARD: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c1a88b8, type:8192, sub:ESP_GMF_EVENT_STATE_RUNNING, payload:0x0, size:0,0x0
I (1428) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a8ba8,ctx:0x3c1a88b8, label:encoder_open]
I (1438) ESP_GMF_PORT: ACQ IN, new self payload:0x3c1a8ba8, port:0x3c1a8a50, el:0x3c1a88b8-aud_enc
I (1448) ESP_GMF_PORT: ACQ OUT, new self payload:0x3c1b6220, port:0x3c1a8b20, el:0x3c1a88b8-aud_enc
I (1461) REC_SDCARD: [ 5 ] Wait for a while to stop record pipeline
I (11494) ESP_GMF_CODEC_IO: CLose, 0x3c1a89e4, pos = 323584/0
I (11494) FILE_IO: CLose, 0x3c1a8a94, pos = 80896/0
I (11495) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a8bcc,ctx:0x3c1a88b8, label:encoder_close]
I (11505) REC_SDCARD: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c1a8880, type:8192, sub:ESP_GMF_EVENT_STATE_STOPPED, payload:0x0, size:0,0x0
I (11519) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcb1260-0x3fcb1260, wk:0x0, run:0]
I (11528) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcb1260-0x3fcb1260, wk:0x0, run:0]
I (11537) REC_SDCARD: [ 6 ] Destroy all the resources
E (11546) i2s_common: i2s_channel_disable(1107): the channel has not been enabled yet
```
