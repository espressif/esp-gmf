# 音频回环（边录边播）

- [English Version](./README.md)
- 例程难度：⭐⭐

## 例程简介

- 实时音频回环：从 ADC（麦克风）读取的采样数据立即经 DAC（扬声器）播放，持续时长由 `LOOPBACK_DURATION_SEC` 控制。
- 技术上演示在同一工程中同时使用 `audio_dac` 与 `audio_adc`，对两个 codec 分别以相同 `esp_codec_dev_sample_info_t` 执行 `esp_codec_dev_open`，并在循环中 read→write，无需 SD 存储。

### 典型场景

- 快速验证板级采集与播放链路是否正常。

### 运行机制

初始化 DAC → 初始化 ADC → 以相同采样格式分别 open → 设置音量/增益 → 循环读缓冲并写缓冲直至超时 → close codec 并释放设备。

### 文件结构

```
├── main
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   └── record_and_play.c
├── CMakeLists.txt               工程：bmgr_record_and_play
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

- 板级描述中同时包含 Audio ADC 与 Audio DAC（`audio_adc`、`audio_dac`），例如 ESP32-S3-Korvo-2 V3。
- 按硬件连接麦克风与扬声器。

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
cd $YOUR_GMF_PATH/packages/esp_board_manager/examples/record_and_play
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

- 板级由 `gen-bmgr-config` 管理。
- 可在 `record_and_play.c` 中调整 `DEFAULT_SAMPLE_RATE`、`DEFAULT_CHANNELS`、`DEFAULT_BITS_PER_SAMPLE`、`DEFAULT_PLAY_VOL`、`DEFAULT_REC_GAIN`、`LOOPBACK_DURATION_SEC`。
- 出现啸叫或回声时可降低播放音量或录音增益。
- 对于使用 ADC 麦克风、PDM 扬声器或是其他不依赖 Codec 芯片的扬声器的开发板，需要在 `menuconfig` 中打开相关的配置：

    - `Component config` -> `Audio Codec Device Data Interface Configuration` -> `Support ADC continuous data interface`

    - `Component config` -> `Audio Codec Device Configuration` -> `Support Dummy Codec Chip`

### 编译与烧录

```
idf.py build
idf.py -p PORT flash monitor
```

退出 monitor：`Ctrl-]`

## 如何使用例程

### 功能和用法

- 上电后自动运行设定时长的回环，麦克风拾取的声音应从扬声器听到。

### 日志输出

```text
I (xxx) BMGR_RECORD_AND_PLAY: Audio loopback: record from ADC and play through DAC for 30 seconds
I (xxx) BMGR_RECORD_AND_PLAY: Audio loopback started: 16000 Hz, 2 ch, 16 bit
I (xxx) BMGR_RECORD_AND_PLAY: Loopback running... 1/30s, total 131072 bytes
...
I (xxx) BMGR_RECORD_AND_PLAY: Audio loopback completed. Total bytes transferred: ...
```

（时间戳与总字节数以实际运行环境为准。）

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

### 啸叫或反馈

降低 `DEFAULT_PLAY_VOL` / `DEFAULT_REC_GAIN`，或加大麦克风与扬声器的物理隔离。

### 板子仅有 ADC 或仅有 DAC

本例程需板级同时定义两种设备；请选用全功能音频参考板或扩展自定义板 YAML。

### 自定义开发板

参阅 [esp_board_manager README](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md)。
