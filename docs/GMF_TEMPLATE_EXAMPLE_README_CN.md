_这是 ESP-GMF 例程的 README 模板文件，其中斜体内容（两个下划线之间的内容，如 `_XXX_`）为提示文字，请根据例程的实际情况进行替换。其中标为「推荐项」的条目建议尽可能填写，标为「可选项」的条目可根据例程实际需要酌情添加。_


# _例程标题_

_英文版本链接_

- [English Version](./README.md)

_对例程的难易程度进行标记，三档任选其一，选中后该行文字无需再改：_

- 例程难度：⭐
- 例程难度：⭐⭐
- 例程难度：⭐⭐⭐

_含义：一星表示入门级别，如仅运行单个 pipeline；二星表示中等级别，如多个 pipeline 相互协调；三星表示复杂级别，如使用多种协议并运行多条 pipeline。_

## 例程简介

- _从功能的角度介绍例程的实现。如：此例程可选择不同的解码器来播放 microSD 卡中对应格式的音乐_
- _从技术的角度演示 GMF 框架中的部分技术功能。如：演示管道和元素的使用，元素直接从 callback 获取数据_

### 典型场景

_描述例程典型使用场景，如低功耗录音笔，音视频通话等_

### 资源列表（可选项）

_如 RAM、CPU 负载_


### 预备知识（可选项）

- _引导新用户先使用 get started 例程_
- _引导客户学习背景知识_
- _以下为典型示例；若当前例程不涉及嵌入式音频，可删除或改写本段。请按当前例程实际使用的资源替换：_

例程中使用的音频文件是[嵌入式二进制](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/api-guides/build-system.html#cmake-embed-data)格式，它是随代码一起编译下载到 flash 中。

本例程`${PROJECT_DIR}/components/music_src/`文件夹中提供了两个测试文件`manloud_48000_2_16_10.wav` 和`tone.mp3`。其中`esp_embed_tone.h` 和 `esp_embed_tone.cmake`文件是由`$YOUR_GMF_PATH/elements/gmf_io/mk_flash_embed_tone.py` 生成，如需要更换音频文件，需要运行脚本重新生成这两个文件，python 脚本命令如下：

```
python $YOUR_GMF_PATH/elements/gmf_io/mk_flash_embed_tone.py -p $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_audio_effects/components/music_src
```


### 文件结构（可选项）

- _介绍示例的文件夹结构和主要文件，如 `pipeline_play_multi_source_music` 文件夹_

```
├── components
│   ├── gen_bmgr_codes
│   │   ├── board_manager.defaults
│   │   ├── CMakeLists.txt
│   │   ├── gen_board_device_config.c
│   │   ├── gen_board_device_handles.c
│   │   ├── gen_board_info.c
│   │   ├── gen_board_periph_config.c
│   │   ├── gen_board_periph_handles.c
│   │   └── idf_component.yml
│   └── tone
│       ├── alarm.mp3
│       ├── CMakeLists.txt
│       ├── dingdong.mp3
│       ├── esp_embed_tone.cmake
│       ├── esp_embed_tone.h
│       ├── haode.mp3
│       └── new_message.mp3
├── main
│   ├── audio_player
│   │   ├── audio_multi_source_player.c
│   │   └── audio_multi_source_player.h
│   ├── command_interface
│   │   ├── audio_commands.c
│   │   └── audio_commands.h
│   ├── common
│   │   ├── audio_config.h
│   │   └── audio_types.h
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   └── play_multi_source_music.c
├── CMakeLists.txt
├── partitions.csv
├── prebuild.sh                         预编译脚本（生成板级代码等）
├── pytest_play_multi_source_music.py   自动化测试脚本
├── README.md
└── README_CN.md
```


## 环境配置


### 硬件要求

_列举本例程需要的硬件资源，包括默认使用的开发板，实际需要的设备和资源列表：Wi-Fi、麦克风、Audio ADC、Audio DAC、扬声器、microSD 卡、JPEG Encoder、H264 Encoder、LCD 模块、Camera（DVP，MIPI）、蓝牙音响等_

### 默认 IDF 分支

_如果不依赖于 esp_board_manager，则使用如下方式_

本例程支持 IDF release/v[x.y] 及以后的分支，例程默认使用 IDF release/v[x.y] 分支。

_如果依赖 esp_board_manager，则统一使用如下方式（整段照抄即可，无需修改）_

本例程支持 IDF release/v5.4(>= v5.4.3) and release/v5.5(>= v5.5.2) 分支。

### 软件要求（可选项）

- _与本例程配合的软件或资源，如：测试音频文件（路径或 URL）、录制上传例程所需的 HTTP 服务（如 server.py）等_


## 编译和下载


### 编译准备

_说明编译前的准备工作。下述文字中仅需将“本例程工程目录”和路径改为当前例程，其余可保持不变_

编译本例程前需先确保已配置 ESP-IDF 环境；若已配置可跳过本段，直接进入工程目录并运行相关预编译脚本。若未配置，请在 ESP-IDF 根目录运行以下脚本完成环境设置，完整步骤请参阅 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/index.html)。

```
./install.sh
. ./export.sh
```

下面是简略步骤：

- 进入本例程工程目录（以下为示例路径，请改为实际例程路径）：

```
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_audio_effects
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

- _介绍和设置重要的 menuconfig 项目，如：FatFs 长文件名、开发板、芯片类型、PSRAM 时钟、Wi-Fi/LWIP 参数等_
  
- _其他需要的软件配置，如：指定的 patch_

- _以播放在线音乐例程的 menuconfig 为例，其他例程请替换为各自的 menuconfig 选项_

```bash
idf.py menuconfig
```

在 menuconfig 中进行以下配置（示例）：

- `GMF APP Configuration` → `Example Connection Configuration` → `WiFi SSID`
- `GMF APP Configuration` → `Example Connection Configuration` → `WiFi Password`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `Channel Convert Destination Channel`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `Bit Convert Destination Bits`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `Rate Convert Destination Rate`

> 配置完成后按 `s` 保存，然后按 `Esc` 退出。


### 编译与烧录

_直接使用如下文字，无需修改_

- 编译示例程序

```
idf.py build
```

- 烧录程序并运行 monitor 工具来查看串口输出 (替换 PORT 为端口名称)：

```
idf.py -p PORT flash monitor
```

## 如何使用例程


### 功能和用法

- _说明例程如何使用、支持的功能及预期反馈，例如：支持的按键、语音命令（若适用）或交互方式。若为语音类例程，可保留类似下面的示例；否则请替换或删除：_
  - _（示例）语音命令：\"小度小度\"\"在呢\"\"讲个笑话\"、\"播放一首歌\" 等_
- _若需其他软件配合（如 HTTP 服务器、蓝牙设备、手机 APP、第二块芯片等），请在此列举并说明设置方法，附链接更佳。例如：_
  - _使用前需在 PC 上运行 `python server.py` 启动 HTTP 服务器以接收数据（如录制上传类例程）。_


### 日志输出

_日志无需完整，仅展示关键信息即可，如展示例程功能；如需确认启动 log，可自行添加。_
_尽量减少无关 log，代码中可通过 esp_log_level_set() 降低无关输出。_

- _简要说明例程运行过程，实际效果，如有必要则指出关键 log_

```c
W (952) PERIPH_I2S: I2S[0] STD already enabled, tx:0x3c117df8, rx:0x3c117fb4
I (982) PLAY_MUSIC_NO_GAP: [ 2 ] Register all the elements and set audio information to play codec device
I (984) PLAY_MUSIC_NO_GAP: [ 3 ] Create audio pipeline
I (986) PLAY_MUSIC_NO_GAP: [ 3.1 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (997) PLAY_MUSIC_NO_GAP: [ 3.2 ] Create event group and listen events from pipeline
I (1004) PLAY_MUSIC_NO_GAP: [ 4 ] Start audio_pipeline
I (1011) PLAY_MUSIC_NO_GAP: CB: RECV Pipeline EVT: el: NULL-0x3c118c68, type: 2000, sub: ESP_GMF_EVENT_STATE_OPENING, payload: 0x0, size: 0, 0x3fcec32c
I (1022) PLAY_MUSIC_NO_GAP: [ 4.1 ] Playing 60000ms before change strategy
W (1025) ESP_GMF_ASMP_DEC: Not enough memory for out, need:2304, old: 1024, new: 2304
I (1179) PLAY_MUSIC_NO_GAP: CB: RECV Pipeline EVT: el: aud_rate_cvt-0x3c119068, type: 3000, sub: ESP_GMF_EVENT_STATE_INITIALIZED, payload: 0x3c11a160, size: 16, 0x3fcec32c
I (1183) PLAY_MUSIC_NO_GAP: CB: RECV Pipeline EVT: el: aud_rate_cvt-0x3c119068, type: 2000, sub: ESP_GMF_EVENT_STATE_RUNNING, payload: 0x0, size: 0, 0x3fcec32c
I (8861) PLAY_MUSIC_NO_GAP: Play file: /sdcard/test_short.mp3
I (16573) PLAY_MUSIC_NO_GAP: Play file: /sdcard/test.mp3
I (24296) PLAY_MUSIC_NO_GAP: Play file: /sdcard/test_short.mp3
I (32005) PLAY_MUSIC_NO_GAP: Play file: /sdcard/test.mp3
I (39720) PLAY_MUSIC_NO_GAP: Play file: /sdcard/test_short.mp3
I (47440) PLAY_MUSIC_NO_GAP: Play file: /sdcard/test.mp3
I (55155) PLAY_MUSIC_NO_GAP: Play file: /sdcard/test_short.mp3
I (61212) PLAY_MUSIC_NO_GAP: [ 5 ] Wait stop event to the pipeline and stop all the pipeline
I (62873) PLAY_MUSIC_NO_GAP: CB: RECV Pipeline EVT: el: NULL-0x3c118c68, type: 2000, sub: ESP_GMF_EVENT_STATE_FINISHED, payload: 0x0, size: 0, 0x3fcec32c
I (62876) PLAY_MUSIC_NO_GAP: [ 6 ] Destroy all the resources
```


### 参考文献（推荐项）

- _若有运行过程、结果或视频链接，建议在本节补充_
- _也可简要说明运行机制或内部流程_


## 故障排除（推荐项）

_主要用来描述在使用过程中可能存在的问题，以及解决方法_
_典型示例如下：_

### 音频文件未找到

如果您的日志有如下的错误提示，这是因为在 microSD 卡中没有找到需要播放的音频文件，请确保音频文件已正确命名并存储在卡中：

```c
E (1133) ESP_GMF_FILE: Failed to open on read, path: /sdcard/test.mp3, err: No such file or directory
E (1140) ESP_GMF_IO: esp_gmf_io_open(71): esp_gmf_io_open failed
```

## 技术支持

请按照下面的链接获取技术支持：

- 技术支持参见 [esp32.com](https://esp32.com/viewforum.php?f=20) 论坛
- 问题反馈与功能需求，请创建 [GitHub issue](https://github.com/espressif/esp-gmf/issues)

我们会尽快回复。
