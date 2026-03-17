# 播放 Flash 中的音乐

- [English Version](./README.md)
- 例程难度：⭐

## 例程简介

本示例展示了从嵌入 Flash 读取 MP3 等音频二进制数据，经 decoder 解码及音效处理后通过 CODEC_DEV_TX IO 输出播放。

- 示例使用单管道架构：`io_embed_flash` → decoder → 音效处理 → `io_codec_dev`。
- 支持 MP3、WAV、FLAC、AAC、M4A、TS、AMRNB、AMRWB 等格式，默认 MP3。

### 典型场景

- 开机提示音、内置铃音等无需外置存储的音频播放场景。

## 环境配置

### 硬件要求

- **开发板**：默认以 ESP32-S3-Korvo V3 为例，其他 ESP 音频板同样适用。
- **资源要求**：Audio DAC、扬声器。

### 默认 IDF 分支

本例程支持 IDF release/v5.4 (>= v5.4.3) 与 release/v5.5 (>= v5.5.2) 分支。

### 预备知识

例程中使用的音频文件是[嵌入式二进制](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/api-guides/build-system.html#cmake-embed-data)格式，它是随代码一起编译下载到 flash 中。

本例程`${PROJECT_DIR}/components/music_src/`文件夹中提供了两个测试文件`ff-16b-1c-44100hz.mp3` 和`alarm.mp3`。其中`esp_embed_tone.h` 和 `esp_embed_tone.cmake`文件是由`$YOUR_GMF_PATH/elements/gmf_io/mk_flash_embed_tone.py` 生成，如需要更换音频文件，需要运行脚本重新生成这两个文件，python 脚本命令如下：

```
python $YOUR_GMF_PATH/elements/gmf_io/mk_flash_embed_tone.py -p $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_play_embed_music/components/music_src
```

## 编译和下载

### 编译准备

编译本例程前需先确保已配置 ESP-IDF 环境；若已配置可跳过本段，直接进入工程目录并运行相关预编译脚本。若未配置，请在 ESP-IDF 根目录运行以下脚本完成环境设置，完整步骤请参阅 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/index.html)。

```
./install.sh
. ./export.sh
```

下面是简略步骤：

- 进入本例程工程目录：

```
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_play_embed_music
```

- 执行预编译脚本，根据提示选择编译芯片，自动设置 IDF Action 扩展，通过 `esp_board_manager` 选择支持的开发板，如需选择自定义开发板，详情参考：[自定义板子](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md#custom-board)

在 Linux / macOS 中运行以下命令：
```bash/zsh
source prebuild.sh
```

在 Windows 中运行以下命令：
```powershell
.\prebuild.ps1
```

### 项目配置

- 若需调整音频效果参数，可在 menuconfig 中配置 GMF Audio 相关选项；默认可直接编译。

```bash
idf.py menuconfig
```

在 menuconfig 中进行以下配置：

- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `Channel Convert Destination Channel`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `Bit Convert Destination Bits`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `Rate Convert Destination Rate`

> 配置完成后按 `s` 保存，然后按 `Esc` 退出。

### 编译与烧录

- 编译示例程序

```
idf.py build
```

- 烧录程序并运行 monitor 工具来查看串口输出 (替换 PORT 为端口名称)：

```
idf.py -p PORT flash monitor
```

- 退出调试界面使用 `Ctrl-]`

## 如何使用例程

### 功能和用法

- 上电后例程会挂载 SD 卡并初始化音频，从 Flash 嵌入二进制中读取 MP3 进行播放，播放完成后停止并释放资源。

### 日志输出

- 正常流程会依次打印挂载、注册元素、创建 pipeline、设置 url、绑定任务、监听事件、启动 pipeline、等待结束并销毁资源。关键步骤以 `[ 1 ]`～`[ 6 ]` 标出。

```c
I (912) main_task: Calling app_main()
I (912) PLAY_EMBED_MUSIC: Func:app_main, Line:85, MEM Total:7253704 Bytes, Inter:334847 Bytes, Dram:334847 Bytes

I (922) PLAY_EMBED_MUSIC: [ 1 ] Mount peripheral
I (927) PERIPH_I2S: I2S[0] TDM,  TX, ws: 45, bclk: 9, dout: 8, din: 10
I (933) PERIPH_I2S: I2S[0] initialize success: 0x3c15e744
I (938) DEV_AUDIO_CODEC: DAC is ENABLED
I (942) DEV_AUDIO_CODEC: Init audio_dac, i2s_name: i2s_audio_out, i2s_rx_handle:0x0, i2s_tx_handle:0x3c15e744, data_if: 0x3fcea7f4
I (953) PERIPH_I2C: I2C master bus initialized successfully
I (963) ES8311: Work in Slave mode
I (966) DEV_AUDIO_CODEC: Successfully initialized codec: audio_dac
I (967) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fceaa48, chip:es8311
I (975) BOARD_MANAGER: Device audio_dac initialized
I (979) BOARD_DEVICE: Device handle audio_dac found, Handle: 0x3fce9a7c TO: 0x3fce9a7c
I (987) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:3
I (992) I2S_IF: TDM Mode 1 bits:16/16 channel:2 sample_rate:44100 mask:3
I (1013) Adev_Codec: Open codec device OK
I (1013) PLAY_EMBED_MUSIC: [ 2 ] Register all the elements and set audio information to play codec device
I (1015) ESP_GMF_POOL: Registered items on pool:0x3c15efa0, app_main-100
I (1021) ESP_GMF_POOL: IO, Item:0x3c15f0b4, H:0x3c15efb4, TAG:io_codec_dev
I (1028) ESP_GMF_POOL: IO, Item:0x3c15f1c0, H:0x3c15f0c4, TAG:io_embed_flash
I (1035) ESP_GMF_POOL: EL, Item:0x3c15f2f8, H:0x3c15f1d0, TAG:aud_dec
I (1041) ESP_GMF_POOL: EL, Item:0x3c15f3fc, H:0x3c15f308, TAG:aud_alc
I (1047) ESP_GMF_POOL: EL, Item:0x3c15f4e4, H:0x3c15f40c, TAG:aud_ch_cvt
I (1054) ESP_GMF_POOL: EL, Item:0x3c15f5c8, H:0x3c15f4f4, TAG:aud_bit_cvt
I (1060) ESP_GMF_POOL: EL, Item:0x3c15f6b4, H:0x3c15f5d8, TAG:aud_rate_cvt
I (1067) PLAY_EMBED_MUSIC: [ 3 ] Create audio pipeline
I (1072) PLAY_EMBED_MUSIC: [ 3.1 ] Set audio url to play
I (1077) PLAY_EMBED_MUSIC: [ 3.2 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (1087) ESP_GMF_TASK: Waiting to run... [tsk:pipe_embed-0x3fcebadc, wk:0x0, run:0]
I (1095) ESP_GMF_TASK: Waiting to run... [tsk:pipe_embed-0x3fcebadc, wk:0x3c15ff04, run:0]
I (1102) PLAY_EMBED_MUSIC: [ 3.3 ] Create envent group and listening event from pipeline
I (1110) PLAY_EMBED_MUSIC: [ 4 ] Start audio_pipeline
I (1115) ESP_GMF_EMBED_FLASH: The read item is 1, embed://tone/1
I (1121) PLAY_EMBED_MUSIC: CB: RECV Pipeline EVT: el: NULL-0x3c15f6c4, type: 2000, sub: ESP_GMF_EVENT_STATE_OPENING, payload: 0x0, size: 0, 0x3fcecdd4
I (1134) ESP_GMF_TASK: One times job is complete, del[wk:0x3c15ff04, ctx:0x3c15f708, label:aud_dec_open]
I (1143) ESP_GMF_PORT: ACQ IN, new self payload:0x3c15ff04, port:0x3c15fd34, el:0x3c15f708-aud_dec
I (1152) ESP_ES_PARSER: The verion of es_parser is v1.0.0
W (1158) ESP_GMF_ASMP_DEC: Not enough memory for out, need:2304, old: 1024, new: 2304
I (1166) ESP_GMF_TASK: One times job is complete, del[wk:0x3c15ffb0, ctx:0x3c15f830, label:aud_bit_cvt_open]
I (1174) ESP_GMF_TASK: One times job is complete, del[wk:0x3c160b14, ctx:0x3c15f984, label:aud_rate_cvt_open]
I (1184) PLAY_EMBED_MUSIC: CB: RECV Pipeline EVT: el: aud_ch_cvt-0x3c15fae0, type: 3000, sub: ESP_GMF_EVENT_STATE_INITIALIZED, payload: 0x3fceca20, size: 16, 0x3fcecdd4
I (1198) PLAY_EMBED_MUSIC: CB: RECV Pipeline EVT: el: aud_ch_cvt-0x3c15fae0, type: 2000, sub: ESP_GMF_EVENT_STATE_RUNNING, payload: 0x0, size: 0, 0x3fcecdd4
I (1212) ESP_GMF_TASK: One times job is complete, del[wk:0x3c15ffb0, ctx:0x3c15fae0, label:aud_ch_cvt_open]
I (1223) PLAY_EMBED_MUSIC: [ 5 ] Wait stop event to the pipeline and stop all the pipeline
W (23329) ESP_GMF_EMBED_FLASH: No more data, ret:0, pos: 231725/231725
I (23329) ESP_GMF_TASK: Job is done, [tsk:pipe_embed-0x3fcebadc, wk:0x3c15ff40, job:0x3c15f708-aud_dec_proc]
I (23333) ESP_GMF_TASK: Job is done, [tsk:pipe_embed-0x3fcebadc, wk:0x3c15ffd8, job:0x3c15f830-aud_bit_cvt_proc]
I (23343) ESP_GMF_TASK: Job is done, [tsk:pipe_embed-0x3fcebadc, wk:0x3c160b54, job:0x3c15f984-aud_rate_cvt_proc]
I (23353) ESP_GMF_TASK: Job is done, [tsk:pipe_embed-0x3fcebadc, wk:0x3c160b94, job:0x3c15fae0-aud_ch_cvt_proc]
I (23363) ESP_GMF_TASK: Finish, strategy action: 0, [tsk:0x3fcebadc-pipe_embed]
I (23370) ESP_GMF_EMBED_FLASH: Closed, pos: 231725/231725
I (23375) ESP_GMF_CODEC_DEV: CLose, 0x3c15fd74, pos = 3903364/0
I (23381) ESP_GMF_TASK: One times job is complete, del[wk:0x3c15ffd8, ctx:0x3c15f708, label:aud_dec_close]
I (23390) ESP_GMF_TASK: One times job is complete, del[wk:0x3c15ff3c, ctx:0x3c15f830, label:aud_bit_cvt_close]
I (23400) ESP_GMF_TASK: One times job is complete, del[wk:0x3c160b04, ctx:0x3c15f984, label:aud_rate_cvt_close]
I (23410) ESP_GMF_TASK: One times job is complete, del[wk:0x3c160b2c, ctx:0x3c15fae0, label:aud_ch_cvt_close]
I (23419) PLAY_EMBED_MUSIC: CB: RECV Pipeline EVT: el: NULL-0x3c15f6c4, type: 2000, sub: ESP_GMF_EVENT_STATE_FINISHED, payload: 0x0, size: 0, 0x3fcecdd4
I (23433) ESP_GMF_TASK: Waiting to run... [tsk:pipe_embed-0x3fcebadc, wk:0x0, run:0]
I (23440) ESP_GMF_TASK: Waiting to run... [tsk:pipe_embed-0x3fcebadc, wk:0x0, run:0]
I (23448) PLAY_EMBED_MUSIC: [ 6 ] Destroy all the resources
W (23453) GMF_SETUP_AUD_CODEC: Unregistering default decoder
I (23464) BOARD_DEVICE: Deinit device audio_dac ref_count: 0 device_handle:0x3fce9a7c
I (23471) BOARD_DEVICE: Device audio_dac config found: 0x3c11f6b4 (size: 92)
I (23473) BOARD_PERIPH: Deinit peripheral i2s_audio_out ref_count: 0
E (23479) i2s_common: i2s_channel_disable(1262): the channel has not been enabled yet
W (23486) PERIPH_I2S: Caution: Releasing TX (0x0).
W (23491) PERIPH_I2S: Caution: RX (0x3c15e914) forced to stop.
E (23496) i2s_common: i2s_channel_disable(1262): the channel has not been enabled yet
I (23504) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 0
I (23510) PERIPH_I2C: I2C master bus deinitialized successfully
I (23515) BOARD_MANAGER: Device audio_dac deinitialized
I (23520) PLAY_EMBED_MUSIC: Func:app_main, Line:151, MEM Total:7253236 Bytes, Inter:334379 Bytes, Dram:334379 Bytes

I (23531) main_task: Returned from app_main()
```

## 故障排除

### 音频文件未找到或无法播放

- 确认已运行 `mk_flash_embed_tone.py` 生成 `esp_embed_tone.h` 与 `esp_embed_tone.cmake`，且 `main` 目录下有所需音频文件（如 `ff-16b-1c-44100hz.mp3`、`alarm.mp3`）。

## 技术支持

请按照下面的链接获取技术支持：

- 技术支持参见 [esp32.com](https://esp32.com/viewforum.php?f=20) 论坛
- 问题反馈与功能需求，请创建 [GitHub issue](https://github.com/espressif/esp-gmf/issues)

我们会尽快回复。
