# 播放 Flash 中的音乐

- [English](./README.md)

## 例程简介

本例程介绍了使用 Embed Flash IO 读取存储在嵌入 Flash 中的 MP3 二进制数据，然后经过 decoder 元素解码，解码后数据进行音频效果处理后用 CODEC_DEV_TX IO 输出音乐。

本例支持 MP3、WAV、FLAC、AAC、M4A、TS、AMRNB、AMRWB 音频格式，默认使用 MP3 格式。

## 示例创建

### IDF 默认分支

本例程支持 IDF release/v5.3 及以后的分支。

### 预备知识

例程中使用的音频文件是[嵌入式二进制](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/api-guides/build-system.html#cmake-embed-data)格式，它是随代码一起编译下载到 flash 中。

本例程`${PROJECT_DIR}/main/`文件夹中提供了两个测试文件`ff-16b-1c-44100hz.mp3` 和`alarm.mp3`。其中`esp_embed_tone.h` 和 `esp_embed_tone.cmake`文件是由`$YOUR_GMF_PATH/elements/gmf_io/mk_flash_embed_tone.py` 生成，如需要更换音频文件，需要运行脚本重新生成这两个文件，python 脚本命令如下：

```
python $YOUR_GMF_PATH/elements/gmf_io/mk_flash_embed_tone.py -p $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_play_embed_music/main
```

### 编译和下载

编译本例程前需要先确保已配置 ESP-IDF 的环境，如果已配置可跳到下一项配置，如果未配置需要先在 ESP-IDF 根目录运行下面脚本设置编译环境，有关配置和使用 ESP-IDF 完整步骤，请参阅 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/index.html)

```
./install.sh
. ./export.sh
```

下面是简略编译步骤：

- 进入播放 Flash 测试工程存放位置

```
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_play_embed_music
```

- 执行预编译脚本，根据提示选择编译芯片，自动设置 IDF Action 扩展，通过 `esp_board_manager` 选择支持的开发板或自定义开发板

在 Linux / macOS 中运行以下命令：
```bash/zsh
source prebuild.sh
```

在 Windows 中运行以下命令：
```powershell
.\prebuild.ps1
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

- 例程开始运行后，将获取 Flash 嵌入二进制数据中的 MP3 音频进行播放，播放完成后停止退出，打印如下：

```c
I (1100) PLAY_EMBED_MUSIC: [ 1 ] Mount sdcard
I (1106) gpio: GPIO[15]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (1115) gpio: GPIO[7]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (1124) gpio: GPIO[4]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (1175) PLAY_EMBED_MUSIC: [ 2 ] Register all the elements and set audio information to play codec device
I (1181) ES8311: Work in Slave mode
I (1184) gpio: GPIO[48]| InputEn: 0| OutputEn: 1| OpenDrain: 0| Pullup: 0| Pulldown: 0| Intr:0
E (1188) i2s_common: i2s_channel_disable(1107): the channel has not been enabled yet
I (1196) I2S_IF: channel mode 0 bits:16/16 channel:2 mask:3
I (1202) I2S_IF: STD Mode 1 bits:16/16 channel:2 sample_rate:48000 mask:3
I (1226) Adev_Codec: Open codec device OK
I (1226) ESP_GMF_POOL: Registered items on pool:0x3c1e7640, app_main-60
I (1227) ESP_GMF_POOL: IO, Item:0x3c1e7714, H:0x3c1e7654, TAG:io_http
I (1233) ESP_GMF_POOL: IO, Item:0x3c1e77e4, H:0x3c1e7724, TAG:io_http
I (1240) ESP_GMF_POOL: IO, Item:0x3c1e7880, H:0x3c1e77f4, TAG:io_file
I (1247) ESP_GMF_POOL: IO, Item:0x3c1e791c, H:0x3c1e7890, TAG:io_file
I (1254) ESP_GMF_POOL: IO, Item:0x3c1e7a4c, H:0x3c1e79b8, TAG:io_embed_flash
I (1262) ESP_GMF_POOL: IO, Item:0x3c1e88a0, H:0x3c1e8810, TAG:io_codec_dev
I (1269) ESP_GMF_POOL: EL, Item:0x3c1e79a8, H:0x3c1e792c, TAG:copier
I (1276) ESP_GMF_POOL: EL, Item:0x3c1e7b60, H:0x3c1e7a5c, TAG:aud_enc
I (1283) ESP_GMF_POOL: EL, Item:0x3c1e7c54, H:0x3c1e7b70, TAG:decoder
I (1290) ESP_GMF_POOL: EL, Item:0x3c1e7d30, H:0x3c1e7c64, TAG:aud_alc
I (1297) ESP_GMF_POOL: EL, Item:0x3c1e7df4, H:0x3c1e7d40, TAG:aud_eq
I (1304) ESP_GMF_POOL: EL, Item:0x3c1e7e28, H:0x3c1e7f5c, TAG:aud_ch_cvt
I (1311) ESP_GMF_POOL: EL, Item:0x3c1e7e58, H:0x3c1e8000, TAG:aud_bit_cvt
I (1318) ESP_GMF_POOL: EL, Item:0x3c1e8140, H:0x3c1e80a4, TAG:aud_rate_cvt
I (1325) ESP_GMF_POOL: EL, Item:0x3c1e821c, H:0x3c1e8150, TAG:aud_fade
I (1332) ESP_GMF_POOL: EL, Item:0x3c1e8318, H:0x3c1e822c, TAG:aud_sonic
I (1339) ESP_GMF_POOL: EL, Item:0x3c1e83f8, H:0x3c1e8328, TAG:aud_deintlv
I (1346) ESP_GMF_POOL: EL, Item:0x3c1e84d4, H:0x3c1e8408, TAG:aud_intlv
I (1354) ESP_GMF_POOL: EL, Item:0x3c1e85d4, H:0x3c1e84e4, TAG:aud_mixer
I (1361) PLAY_EMBED_MUSIC: [ 3 ] Create audio pipeline, add all elements to pipeline
W (1369) AUD_SDEC_REG: Overwrote ES decoder 6
W (1374) AUD_SDEC_REG: Overwrote ES decoder 7
W (1379) AUD_SDEC_REG: Overwrote ES decoder 8
I (1385) PLAY_EMBED_MUSIC: [ 3.1 ] Set audio url to play
I (1390) PLAY_EMBED_MUSIC: [ 3.2 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (1402) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcba980-0x3fcba980, wk:0x0, run:0]
I (1410) ESP_GMF_THREAD: The TSK_0x3fcba980 created on internal memory
I (1418) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcba980-0x3fcba980, wk:0x3c1e90cc, run:0]
I (1427) PLAY_EMBED_MUSIC: [ 3.3 ] Create envent group and listening event from pipeline
I (1436) PLAY_EMBED_MUSIC: [ 4 ] Start audio_pipeline
I (1441) EMBED_FLASH_IO: The read item is 1, embed://tone/1
I (1448) PLAY_EMBED_MUSIC: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c1e88b0, type:2000, sub:ESP_GMF_EVENT_STATE_OPENING, payload:0x0, size:0,0x3fcbbc90
I (1463) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1e90cc,ctx:0x3c1e88e8, label:decoder_open]
I (1473) ESP_GMF_PORT: ACQ IN, new self payload:0x3c1e90cc, port:0x3c1e8f50, el:0x3c1e88e8-decoder
I (1483) ESP_GMF_PORT: ACQ OUT SET, new self payload:0x3c1e9338, p:0x3c1e8ae8, el:0x3c1e88e8-decoder
W (1493) ESP_GMF_ADEC: Not enough memory for out, need:2304, old: 1024, new: 2304
I (1502) ESP_GMF_ADEC: NOTIFY Info, rate: 44100, bits: 16, ch: 1 --> rate: 44100, bits: 16, ch: 1
I (1510) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1e9134,ctx:0x3c1e89dc, label:bit_cvt_open]
I (1601) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1e919c,ctx:0x3c1e8b70, label:rate_cvt_open]
I (1601) ESP_GMF_PORT: ACQ OUT SET, new self payload:0x3c1e919c, p:0x3c1e8e34, el:0x3c1e8b70-aud_rate_cvt
I (1610) PLAY_EMBED_MUSIC: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c1e8d14, type:3000, sub:ESP_GMF_EVENT_STATE_INITIALIZED, payload:0x3fcbb8f0, size:12,0x3fcbbc90
I (1626) PLAY_EMBED_MUSIC: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c1e8d14, type:2000, sub:ESP_GMF_EVENT_STATE_RUNNING, payload:0x0, size:0,0x3fcbbc90
I (1641) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1e9134,ctx:0x3c1e8d14, label:ch_cvt_open]
I (1669) PLAY_EMBED_MUSIC: Func:app_main, Line:90, MEM Total:6577140 Bytes, Inter:278727 Bytes, Dram:278727 Bytes

I (1670) PLAY_EMBED_MUSIC: [ 5 ] Wait stop event to the pipeline and stop all the pipeline
W (23606) EMBED_FLASH_IO: No more data, ret:0, pos: 231725/231725
I (23606) ESP_GMF_ADEC: Total: 1951682 bytes(194)
I (23607) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcba980-0x3fcba980, wk:0x3c1e9100, job:0x3c1e88e8-decoder_proc]
I (23618) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcba980-0x3fcba980, wk:0x3c1e9168, job:0x3c1e89dc-bit_cvt_proc]
I (23629) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcba980-0x3fcba980, wk:0x3c1e91d0, job:0x3c1e8b70-rate_cvt_proc]
I (23640) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcba980-0x3fcba980, wk:0x3c1e9204, job:0x3c1e8d14-ch_cvt_proc]
I (23651) EMBED_FLASH_IO: Closed, pos: 231725/231725
I (23656) ESP_GMF_CODEC_IO: CLose, 0x3c1e8f94, pos = 4248560/0
I (23663) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1e90f0,ctx:0x3c1e88e8, label:decoder_close]
I (23673) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1e9110,ctx:0x3c1e89dc, label:bit_cvt_close]
I (23684) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1e9144,ctx:0x3c1e8b70, label:rate_cvt_close]
I (23694) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1e9178,ctx:0x3c1e8d14, label:ch_cvt_close]
I (23704) PLAY_EMBED_MUSIC: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c1e88b0, type:2000, sub:ESP_GMF_EVENT_STATE_FINISHED, payload:0x0, size:0,0x3fcbbc90
I (23719) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcba980-0x3fcba980, wk:0x0, run:0]
I (23728) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcba980-0x3fcba980, wk:0x0, run:0]
W (23737) ESP_GMF_TASK: Already stopped, ESP_GMF_EVENT_STATE_FINISHED, [TSK_0x3fcba980,0x3fcba980]
I (23747) PLAY_EMBED_MUSIC: [ 6 ] Destroy all the resources
E (23755) I2C_If: Fail to write to dev 30
E (23767) i2s_common: i2s_channel_disable(1107): the channel has not been enabled yet
I (23768) PLAY_EMBED_MUSIC: Func:app_main, Line:104, MEM Total:6716288 Bytes, Inter:328699 Bytes, Dram:328699 Bytes
```
