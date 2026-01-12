# 播放 microSD card 音乐

- [English](./README.md)

## 例程简介

本例程介绍了使用 FatFs IO 读取 microSD 卡中的音乐，然后经过 decoder 元素解码，解码后数据进行音频效果处理后用 CODEC_DEV_TX IO 输出音乐。

本例支持 MP3、WAV、FLAC、AAC、M4A、TS、AMRNB、AMRWB 音频格式，默认使用 MP3 格式。

## 示例创建

### IDF 默认分支

本例程支持 IDF release/v5.3 及以后的分支。

### 配置

本例程需要准备一张 microSD 卡，同时将自备音源重命名为`test`存储到 microSD 卡中。例程中默认使用 MP3 格式，用户可通过调用函数 `esp_gmf_pipeline_set_in_uri` 来更改播放的音频文件。

### 编译和下载

编译本例程前需要先确保已配置 ESP-IDF 的环境，如果已配置可跳到下一项配置，如果未配置需要先在 ESP-IDF 根目录运行下面脚本设置编译环境，有关配置和使用 ESP-IDF 完整步骤，请参阅 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/index.html)：

```
./install.sh
. ./export.sh
```

下面是简略编译步骤：

- 进入播放 microSD card 音乐测试工程存放位置

```
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_play_sdcard_music
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

- 例程开始运行后，自动播放 microSD 卡中的音乐文件，播放完成后停止退出，打印如下：

```c
I (1021) PLAY_SDCARD_MUSIC: [ 1 ] Mount sdcard
I (1027) gpio: GPIO[15]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (1036) gpio: GPIO[7]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (1045) gpio: GPIO[4]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (1095) PLAY_SDCARD_MUSIC: [ 2 ] Register all the elements and set audio information to play codec device
I (1101) ES8311: Work in Slave mode
I (1105) gpio: GPIO[48]| InputEn: 0| OutputEn: 1| OpenDrain: 0| Pullup: 0| Pulldown: 0| Intr:0
E (1109) i2s_common: i2s_channel_disable(1107): the channel has not been enabled yet
I (1117) I2S_IF: channel mode 0 bits:16/16 channel:2 mask:3
I (1123) I2S_IF: STD Mode 1 bits:16/16 channel:2 sample_rate:48000 mask:3
I (1147) Adev_Codec: Open codec device OK
I (1147) PLAY_SDCARD_MUSIC: [ 3 ] Create audio pipeline, add all elements to pipeline
W (1149) AUD_SDEC_REG: Overwrote ES decoder 6
W (1154) AUD_SDEC_REG: Overwrote ES decoder 7
W (1159) AUD_SDEC_REG: Overwrote ES decoder 8
W (1164) ESP_GMF_CODEC_HELPER: The new dec type is 6
I (1169) PLAY_SDCARD_MUSIC: [ 3.1 ] Set audio url to play
I (1176) PLAY_SDCARD_MUSIC: [ 3.2 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (1187) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcba870-0x3fcba870, wk:0x0, run:0]
I (1196) ESP_GMF_THREAD: The TSK_0x3fcba870 created on internal memory
I (1203) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcba870-0x3fcba870, wk:0x3c1a909c, run:0]
I (1212) PLAY_SDCARD_MUSIC: [ 3.3 ] Create envent group and listening event from pipeline
I (1221) PLAY_SDCARD_MUSIC: [ 4 ] Start audio_pipeline
I (1227) FILE_IO: Open, dir:1, uri:/sdcard/test.wav
I (1235) FILE_IO: File size: 3539168 byte, file position: 0
I (1239) PLAY_SDCARD_MUSIC: CB: RECV Pipeline EVT: el: OBJ_GET_TAG(event->from)-0x3c1a88b0, type: 2000, sub: ESP_GMF_EVENT_STATE_OPENING, payload: 0x0, size: 0, 0x3fcbb780
I (1255) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a909c,ctx:0x3c1a88e8, label:decoder_open]
I (1265) ESP_GMF_PORT: ACQ IN, new self payload:0x3c1a909c, port:0x3c1a8f48, el:0x3c1a88e8-decoder
I (1275) PLAY_SDCARD_MUSIC: [ 5 ] Wait stop event to the pipeline and stop all the pipeline
I (1275) ESP_GMF_PORT: ACQ OUT SET, new self payload:0x3c1a9308, p:0x3c1a8ae8, el:0x3c1a88e8-decoder
I (1295) ESP_GMF_ADEC: NOTIFY Info, rate: 44100, bits: 16, ch: 2 --> rate: 44100, bits: 16, ch: 2
I (1383) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a9104,ctx:0x3c1a89dc, label:rate_cvt_open]
I (1383) ESP_GMF_PORT: ACQ OUT SET, new self payload:0x3c1a9104, p:0x3c1a8c90, el:0x3c1a89dc-aud_rate_cvt
I (1393) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a916c,ctx:0x3c1a8b70, label:ch_cvt_open]
I (1402) PLAY_SDCARD_MUSIC: CB: RECV Pipeline EVT: el: OBJ_GET_TAG(event->from)-0x3c1a8d18, type: 3000, sub: ESP_GMF_EVENT_STATE_INITIALIZED, payload: 0x3fcbb3e0, size: 12, 0x3fcbb780
I (1419) PLAY_SDCARD_MUSIC: CB: RECV Pipeline EVT: el: OBJ_GET_TAG(event->from)-0x3c1a8d18, type: 2000, sub: ESP_GMF_EVENT_STATE_RUNNING, payload: 0x0, size: 0, 0x3fcbb780
I (1435) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a91bc,ctx:0x3c1a8d18, label:bit_cvt_open]
I (21322) FILE_IO: No more data, ret: 0
I (21322) ESP_GMF_ADEC: Total: 3538944 bytes(194)
I (21322) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcba870-0x3fcba870, wk:0x3c1a90d0, job:0x3c1a88e8-decoder_proc]
I (21332) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcba870-0x3fcba870, wk:0x3c1a9138, job:0x3c1a89dc-rate_cvt_proc]
I (21342) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcba870-0x3fcba870, wk:0x3c1a918c, job:0x3c1a8b70-ch_cvt_proc]
I (21353) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcba870-0x3fcba870, wk:0x3c1a91f0, job:0x3c1a8d18-bit_cvt_proc]
I (21364) FILE_IO: CLose, 0x3c1a8ebc, pos = 3539168/3539168
I (21370) ESP_GMF_CODEC_IO: CLose, 0x3c1a8f8c, pos = 3851912/0
I (21377) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a90c0,ctx:0x3c1a88e8, label:decoder_close]
I (21387) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a9128,ctx:0x3c1a89dc, label:rate_cvt_close]
I (21397) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a915c,ctx:0x3c1a8b70, label:ch_cvt_close]
I (21408) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a9190,ctx:0x3c1a8d18, label:bit_cvt_close]
I (21418) PLAY_SDCARD_MUSIC: CB: RECV Pipeline EVT: el: OBJ_GET_TAG(event->from)-0x3c1a88b0, type: 2000, sub: ESP_GMF_EVENT_STATE_FINISHED, payload: 0x0, size: 0, 0x3fcbb780
I (21434) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcba870-0x3fcba870, wk:0x0, run:0]
I (21443) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcba870-0x3fcba870, wk:0x0, run:0]
W (21451) ESP_GMF_TASK: Already stopped, ESP_GMF_EVENT_STATE_FINISHED, [TSK_0x3fcba870,0x3fcba870]
I (21461) PLAY_SDCARD_MUSIC: [ 6 ] Destroy all the resources
```

## 故障排除

如果您的日志有如下的错误提示，这是因为在 microSD 卡 中没有找到需要播放的音频文件，请按照上面的 *配置* 一节操作重命名文件。

```c
I (1221) PLAY_SDCARD_MUSIC: [ 4 ] Start audio_pipeline
I (1227) FILE_IO: Open, dir:1, uri:/sdcard/test3.wav
I (1241) FILE_IO: File size: 0 byte, file position: 0
I (1241) PLAY_SDCARD_MUSIC: CB: RECV Pipeline EVT: el: OBJ_GET_TAG(event->from)-0x3c1a88b0, type: 2000, sub: ESP_GMF_EVENT_STATE_OPENING, payload: 0x0, size: 0, 0x3fcbb780
I (1254) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a909c,ctx:0x3c1a88e8, label:decoder_open]
I (1264) ESP_GMF_PORT: ACQ IN, new self payload:0x3c1a909c, port:0x3c1a8f48, el:0x3c1a88e8-decoder
E (1274) FILE_IO: The error is happened in reading data, error msg: Bad file number
E (1282) ESP_GMF_ADEC: Read data error, ret: -1, line: 106
E (1288) ESP_GMF_TASK: Job failed[tsk:TSK_0x3fcba870-0x3fcba870:0x3c1a90d0-0x3c1a88e8-decoder_proc], ret:-1, st:ESP_GMF_EVENT_STATE_RUNNING
```
