# 播放 microSD 卡音乐

- [English Version](./README.md)
- 例程难度：⭐

## 例程简介

本示例展示了使用 FatFs IO 读取 microSD 卡中的音乐文件，经 decoder 解码及音效处理后通过 CODEC_DEV_TX IO 输出播放。

- 本示例使用单管道架构：`io_file` → decoder → 音效处理 → `io_codec_dev`。
- 支持 MP3、WAV、FLAC、AAC、M4A、TS、AMRNB、AMRWB 等格式，默认 MP3。

### 典型场景

- 本地音乐播放、语音提示等从 SD 卡读文件的场景。

## 环境配置

### 硬件要求

- **开发板**：默认以 ESP32-S3-Korvo V3 为例，其他 ESP 音频板同样适用。
- **资源要求**：microSD 卡、Audio DAC、扬声器。

### 默认 IDF 分支

本例程支持 IDF release/v5.4 (>= v5.4.3) 与 release/v5.5 (>= v5.5.2) 分支。

### 软件要求

准备一张 microSD 卡，将自备音源重命名为 `test`（扩展名与格式一致，如 `test.mp3`）并存入卡中。可通过 `DEFAULT_PLAY_URL` 更改播放文件路径。

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
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_play_sdcard_music
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

- 若需调整音频效果相关参数（采样率、声道、位深等），可在 menuconfig 中配置 GMF Audio 相关选项。

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

- 上电后例程会挂载 microSD 卡并初始化音频，自动播放卡中默认文件（如 `/sdcard/test.mp3` 或 `/sdcard/test.wav`），播放完成后停止并释放资源。

### 日志输出

- 正常流程会依次打印挂载 SD 卡、注册元素、创建 pipeline、设置 url、绑定任务、监听事件、启动 pipeline、打开文件并解码播放、等待结束并销毁资源。关键步骤以 `[ 1 ]`～`[ 6 ]` 标出。

```c
I (888) main_task: Calling app_main()
I (888) PLAY_SDCARD_MUSIC: [ 1 ] Mount sdcard
I (892) DEV_FS_FAT_SUB_SDMMC: slot_config: cd=-1, wp=-1, clk=15, cmd=7, d0=4, d1=-1, d2=-1, d3=-1, d4=-1, d5=-1, d6=-1, d7=-1, width=1, flags=0x1
Name: SA32G
Type: SDHC
Speed: 40.00 MHz (limit: 40.00 MHz)
Size: 29544MB
CSD: ver=2, sector_size=512, capacity=60506112 read_bl_len=9
SSR: bus_width=1
I (951) DEV_FS_FAT: Filesystem mounted, base path: /sdcard
I (956) BOARD_MANAGER: Device fs_sdcard initialized
I (961) PERIPH_I2S: I2S[0] TDM,  TX, ws: 45, bclk: 9, dout: 8, din: 10
I (967) PERIPH_I2S: I2S[0] initialize success: 0x3c14fae4
I (972) DEV_AUDIO_CODEC: DAC is ENABLED
I (976) DEV_AUDIO_CODEC: Init audio_dac, i2s_name: i2s_audio_out, i2s_rx_handle:0x0, i2s_tx_handle:0x3c14fae4, data_if: 0x3fcead9c
I (988) PERIPH_I2C: I2C master bus initialized successfully
I (998) ES8311: Work in Slave mode
I (1000) DEV_AUDIO_CODEC: Successfully initialized codec: audio_dac
I (1002) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fceaff0, chip:es8311
I (1009) BOARD_MANAGER: Device audio_dac initialized
I (1014) BOARD_DEVICE: Device handle audio_dac found, Handle: 0x3fcea024 TO: 0x3fcea024
I (1022) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:3
I (1027) I2S_IF: TDM Mode 1 bits:16/16 channel:2 sample_rate:44100 mask:3
I (1048) Adev_Codec: Open codec device OK
I (1048) PLAY_SDCARD_MUSIC: [ 2 ] Register all the elements and set audio information to play codec device
I (1051) PLAY_SDCARD_MUSIC: [ 3 ] Create audio pipeline
I (1055) PLAY_SDCARD_MUSIC: [ 3.1 ] Set audio url to play
I (1060) PLAY_SDCARD_MUSIC: [ 3.2 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (1071) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcec084-0x3fcec084, wk:0x0, run:0]
I (1078) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcec084-0x3fcec084, wk:0x3c151b34, run:0]
I (1087) PLAY_SDCARD_MUSIC: [ 3.3 ] Create envent group and listening event from pipeline
I (1095) PLAY_SDCARD_MUSIC: [ 4 ] Start audio_pipeline
I (1100) ESP_GMF_FILE: Open, dir:1, uri:/sdcard/test.mp3
I (1105) PLAY_SDCARD_MUSIC: [ 5 ] Wait stop event to the pipeline and stop all the pipeline
I (1107) ESP_GMF_FILE: File size: 231023 byte, file position: 0
I (1118) PLAY_SDCARD_MUSIC: CB: RECV Pipeline EVT: el: NULL-0x3c15131c, type: 2000, sub: ESP_GMF_EVENT_STATE_OPENING, payload: 0x0, size: 0, 0x3fced37c
I (1132) ESP_GMF_TASK: One times job is complete, del[wk:0x3c151b34, ctx:0x3c151360, label:aud_dec_open]
I (1141) ESP_GMF_PORT: ACQ IN, new self payload:0x3c151b34, port:0x3c151990, el:0x3c151360-aud_dec
I (1150) ESP_ES_PARSER: The verion of es_parser is v1.0.0
W (1155) ESP_GMF_ASMP_DEC: Not enough memory for out, need:4608, old: 1024, new: 4608
I (1165) ESP_GMF_TASK: One times job is complete, del[wk:0x3c151df8, ctx:0x3c151480, label:aud_ch_cvt_open]
I (1172) ESP_GMF_TASK: One times job is complete, del[wk:0x3c151e48, ctx:0x3c1515d8, label:aud_bit_cvt_open]
I (1181) PLAY_SDCARD_MUSIC: CB: RECV Pipeline EVT: el: aud_rate_cvt-0x3c15172c, type: 3000, sub: ESP_GMF_EVENT_STATE_INITIALIZED, payload: 0x3fcecfd0, size: 16, 0x3fced37c
I (1196) PLAY_SDCARD_MUSIC: CB: RECV Pipeline EVT: el: aud_rate_cvt-0x3c15172c, type: 2000, sub: ESP_GMF_EVENT_STATE_RUNNING, payload: 0x0, size: 0, 0x3fced37c
I (1210) ESP_GMF_TASK: One times job is complete, del[wk:0x3c151df8, ctx:0x3c15172c, label:aud_rate_cvt_open]
I (21180) ESP_GMF_FILE: No more data, ret: 0
I (21198) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcec084-0x3fcec084, wk:0x3c151b70, job:0x3c151360-aud_dec_proc]
I (21198) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcec084-0x3fcec084, wk:0x3c151e20, job:0x3c151480-aud_ch_cvt_proc]
I (21207) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcec084-0x3fcec084, wk:0x3c151e88, job:0x3c1515d8-aud_bit_cvt_proc]
I (21217) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcec084-0x3fcec084, wk:0x3c151ee0, job:0x3c15172c-aud_rate_cvt_proc]
I (21228) ESP_GMF_TASK: Finish, strategy action: 0, [tsk:0x3fcec084-TSK_0x3fcec084]
I (21235) ESP_GMF_FILE: CLose, 0x3c151888, pos = 231023/231023
I (21241) ESP_GMF_CODEC_DEV: CLose, 0x3c1519d0, pos = 3528000/0
I (21246) ESP_GMF_TASK: One times job is complete, del[wk:0x3c151b58, ctx:0x3c151360, label:aud_dec_close]
I (21256) ESP_GMF_TASK: One times job is complete, del[wk:0x3c151b80, ctx:0x3c151480, label:aud_ch_cvt_close]
I (21265) ESP_GMF_TASK: One times job is complete, del[wk:0x3c151df8, ctx:0x3c1515d8, label:aud_bit_cvt_close]
I (21275) ESP_GMF_TASK: One times job is complete, del[wk:0x3c151e38, ctx:0x3c15172c, label:aud_rate_cvt_close]
I (21285) PLAY_SDCARD_MUSIC: CB: RECV Pipeline EVT: el: NULL-0x3c15131c, type: 2000, sub: ESP_GMF_EVENT_STATE_FINISHED, payload: 0x0, size: 0, 0x3fced37c
I (21298) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcec084-0x3fcec084, wk:0x0, run:0]
I (21306) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcec084-0x3fcec084, wk:0x0, run:0]
I (21314) PLAY_SDCARD_MUSIC: [ 6 ] Destroy all the resources
W (21319) GMF_SETUP_AUD_CODEC: Unregistering default decoder
I (21330) BOARD_DEVICE: Deinit device audio_dac ref_count: 0 device_handle:0x3fcea024
I (21337) BOARD_DEVICE: Device audio_dac config found: 0x3c0de0d4 (size: 92)
I (21339) BOARD_PERIPH: Deinit peripheral i2s_audio_out ref_count: 0
E (21345) i2s_common: i2s_channel_disable(1262): the channel has not been enabled yet
W (21353) PERIPH_I2S: Caution: Releasing TX (0x0).
W (21357) PERIPH_I2S: Caution: RX (0x3c14fca0) forced to stop.
E (21363) i2s_common: i2s_channel_disable(1262): the channel has not been enabled yet
I (21370) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 0
I (21376) PERIPH_I2C: I2C master bus deinitialized successfully
I (21382) BOARD_MANAGER: Device audio_dac deinitialized
I (21387) BOARD_DEVICE: Deinit device fs_sdcard ref_count: 0 device_handle:0x3fce9a7c
I (21394) BOARD_DEVICE: Device fs_sdcard config found: 0x3c0de024 (size: 84)
I (21401) DEV_FS_FAT: Sub device 'sdmmc' deinitialized successfully
I (21407) BOARD_MANAGER: Device fs_sdcard deinitialized
I (21412) main_task: Returned from app_main()
```

## 故障排除

### 音频文件未找到或无法播放

若日志出现 `FILE_IO: The error is happened in reading data` 或 `Bad file number`，多为 microSD 卡中未找到对应文件。请确认已将音源重命名为 `test` 并带正确扩展名（如 `test.mp3`、`test.wav`）存入卡根目录，且卡已正确挂载。

## 技术支持

请按照下面的链接获取技术支持：

- 技术支持参见 [esp32.com](https://esp32.com/viewforum.php?f=20) 论坛
- 问题反馈与功能需求，请创建 [GitHub issue](https://github.com/espressif/esp-gmf/issues)

我们会尽快回复。
