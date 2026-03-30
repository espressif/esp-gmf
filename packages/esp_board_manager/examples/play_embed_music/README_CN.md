# 播放嵌入式 WAV 音乐

- [English Version](./README.md)
- 例程难度：⭐

## 例程简介

- 本例程从固件内置 Flash 中的 WAV 数据播放音频，无需 microSD 卡或网络。
- 技术上演示通过 `esp_board_manager` 初始化音频 DAC、获取 `dev_audio_codec_handles_t`，并使用 `esp_codec_dev`（open、音量、write）播放；音频通过 CMake `EMBED_TXTFILES` 嵌入镜像。

### 典型场景

- 开机提示音、小体积演示音频等不依赖外置存储的场景。

### 预备知识

音频文件通过 ESP-IDF [嵌入式二进制](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/api-guides/build-system.html#cmake-embed-data)随固件编译。将 WAV 放在 `main/audio_files/`（默认 `test.wav`），并在 `main/CMakeLists.txt` 的 `EMBED_TXTFILES` 中注册。C 侧符号为 `_binary_<名称>_start` / `_binary_<名称>_end`（名称由路径规则生成）。

### 运行机制

`app_main` 经板级管理初始化 DAC，根据 Flash 中 WAV 头解析格式并打开 codec，将采样数据通过 `esp_codec_dev_write` 写入 ，最后释放设备。

### 文件结构

```
├── main
│   ├── audio_files              放置 test.wav 等
│   ├── CMakeLists.txt           EMBED_TXTFILES 嵌入 WAV
│   ├── idf_component.yml
│   └── play_embed_music.c
├── CMakeLists.txt               工程名：bmgr_play_embed_music
├── partitions.csv
├── sdkconfig.defaults
├── sdkconfig.defaults.esp32
├── sdkconfig.defaults.esp32s3
├── sdkconfig.defaults.esp32p4
├── README.md
└── README_CN.md
```

## 环境配置

### 硬件要求

- 连接扬声器

### 默认 IDF 分支

本例程支持 IDF release/v5.4 (>= v5.4.3) 与 release/v5.5 (>= v5.5.2) 分支。

## 编译和下载

### 编译准备

编译前需已配置 ESP-IDF 环境；若已配置可跳过安装，直接进入工程目录。若未配置，请在 ESP-IDF 根目录执行：

```
./install.sh
. ./export.sh
```

简略步骤：

- 进入本例程目录：

```
cd $YOUR_GMF_PATH/packages/esp_board_manager/examples/play_embed_music
```

- 配置 `esp_board_manager` 路径，激活环境（在当前终端下只需要执行一次）

```
# Linux / macOS:
export IDF_EXTRA_ACTIONS_PATH=$YOUR_GMF_PATH/packages/esp_board_manager

# Windows PowerShell:
$env:IDF_EXTRA_ACTIONS_PATH = "$YOUR_GMF_PATH/packages/esp_board_manager"

# Windows CMD:
set IDF_EXTRA_ACTIONS_PATH=$YOUR_GMF_PATH/packages/esp_board_manager
```

- 生成板级配置（示例：`esp32_s3_korvo2_v3`）：

```
idf.py gen-bmgr-config -b esp32_s3_korvo2_v3
```

自定义板：参考 [自定义板子](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md#custom-board)。

- 列出已支持开发板：

```
idf.py gen-bmgr-config -l
```

### 项目配置

- 板子与芯片默认由 `gen-bmgr-config` 及生成的 `components/gen_bmgr_codes/board_manager.defaults` 注入

### 编译与烧录

```
idf.py build
```

```
idf.py -p PORT flash monitor
```

退出 monitor：`Ctrl-]`

## 如何使用例程

### 功能和用法

- 烧录后设备自动播放嵌入的 `test.wav` 一遍，随后 `app_main` 结束。

### 日志输出

```text
I (918) BMGR_EMBED_MUSIC: Playing embedded music
I (923) PERIPH_I2S: I2S[0] TDM,  TX, ws: 45, bclk: 9, dout: 8, din: 10
I (929) PERIPH_I2S: I2S[0] initialize success: 0x3c1629b0
I (934) DEV_AUDIO_CODEC: DAC is ENABLED
I (938) DEV_AUDIO_CODEC: Init audio_dac, i2s_name: i2s_audio_out, i2s_rx_handle:0x0, i2s_tx_handle:0x3c1629b0, data_if: 0x3fcea7f4
I (950) PERIPH_I2C: I2C master bus initialized successfully
I (960) ES8311: Work in Slave mode
I (963) DEV_AUDIO_CODEC: Successfully initialized codec: audio_dac
I (964) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fceaa48, chip:es8311
I (971) BOARD_MANAGER: Device audio_dac initialized
I (976) BOARD_DEVICE: Device handle audio_dac found, Handle: 0x3fce9a7c TO: 0x3fce9a7c
I (983) BMGR_EMBED_MUSIC: Embedded WAV file size: 818920 bytes
I (989) BMGR_EMBED_MUSIC: WAV file info: 48000 Hz, 2 channels, 16 bits
I (996) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:3
I (1002) I2S_IF: TDM Mode 1 bits:16/16 channel:2 sample_rate:48000 mask:3
I (1023) Adev_Codec: Open codec device OK
I (5273) BMGR_EMBED_MUSIC: Embedded WAV file playback completed
I (5273) BOARD_DEVICE: Deinit device audio_dac ref_count: 0 device_handle:0x3fce9a7c
I (5286) BOARD_DEVICE: Device audio_dac config found: 0x3c12f0e4 (size: 92)
I (5286) BOARD_PERIPH: Deinit peripheral i2s_audio_out ref_count: 0
E (5288) i2s_common: i2s_channel_disable(1217): the channel has not been enabled yet
W (5296) PERIPH_I2S: Caution: Releasing TX (0x0).
W (5300) PERIPH_I2S: Caution: RX (0x3c162b64) forced to stop.
E (5306) i2s_common: i2s_channel_disable(1217): the channel has not been enabled yet
I (5313) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 0
I (5319) PERIPH_I2C: I2C master bus deinitialized successfully
I (5324) BOARD_MANAGER: Device audio_dac deinitialized
```

## 故障排除

### `idf.py gen-bmgr-config` 没有 `-b` 参数

请确认已设置 `IDF_EXTRA_ACTIONS_PATH` 指向 `esp_board_manager`：

```
# Linux / macOS:
echo $IDF_EXTRA_ACTIONS_PATH

# Windows PowerShell:
echo $env:IDF_EXTRA_ACTIONS_PATH

# Windows CMD:
echo %IDF_EXTRA_ACTIONS_PATH%
```

### 嵌入式 WAV 缺失或编译失败

确认 `main/audio_files/test.wav` 存在，且 `main/CMakeLists.txt` 中 `EMBED_TXTFILES` 已包含该文件。

### 自定义开发板

参阅 [esp_board_manager README](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md)。
