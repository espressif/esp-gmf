# 录制音频到 microSD 卡

- [English Version](./README.md)
- 例程难度：⭐⭐

## 例程简介

- 从麦克风（Audio ADC）采集音频，按 WAV 格式写入 `/sdcard/test.wav`，录制时长由源码中 `DEFAULT_DURATION_SECONDS` 等宏配置。
- 技术上演示 `esp_board_manager` 初始化 Audio ADC 与 `fs_sdcard`，使用 `esp_codec_dev` 读音频数据，并借助 `common` 中 `write_wav_header` 写文件头。

### 典型场景

- 语音备忘、环境声采集存盘，或验证板级麦克风与 SD 链路。

### 运行机制

初始化 ADC → 挂载 SD → 写 WAV 头 → 循环读 ADC 并写入文件 → 关闭并释放设备。

### 文件结构

```
├── common
│   ├── CMakeLists.txt
│   ├── wav_header.c
│   └── include/wav_header.h
├── main
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   └── record_to_sdcard.c
├── CMakeLists.txt               工程：bmgr_record_to_sdcard
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

- 具备 Audio ADC 与 SD 卡的开发板

### 默认 IDF 分支

本例程支持 IDF release/v5.4 (>= v5.4.3) 与 release/v5.5 (>= v5.5.2) 分支。

### 软件要求

- SD 卡有足够空间保存 WAV 片段。

## 编译和下载

### 编译准备

```
./install.sh
. ./export.sh
```

```
cd $YOUR_GMF_PATH/packages/esp_board_manager/examples/record_to_sdcard
```

```
# Linux / macOS:
export IDF_EXTRA_ACTIONS_PATH=$YOUR_GMF_PATH/packages/esp_board_manager

# Windows PowerShell:
$env:IDF_EXTRA_ACTIONS_PATH = "$YOUR_GMF_PATH/packages/esp_board_manager"

# Windows CMD:
set IDF_EXTRA_ACTIONS_PATH=$YOUR_GMF_PATH/packages/esp_board_manager
```

```
idf.py gen-bmgr-config -b esp32_s3_korvo2_v3
idf.py gen-bmgr-config -l
```

自定义板：[自定义板子](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md#custom-board)。

### 项目配置

- 录制时长、采样率、路径、增益等可在 `record_to_sdcard.c` 中修改宏。

### 编译与烧录

```
idf.py build
idf.py -p PORT flash monitor
```

退出 monitor：`Ctrl-]`

## 如何使用例程

### 功能和用法

- 插入 SD 卡并烧录运行；上电后自动录制设定时长并生成 `/sdcard/test.wav`。

### 日志输出

```text
I (732) BMGR_RECORD_TO_SDCARD: Record to /sdcard/record.wav
I (738) DEV_AUDIO_CODEC: ADC is ENABLED
I (760) PERIPH_I2S: I2S[0] TDM, RX, ws: 45, bclk: 9, dout: 8, din: 10
I (766) PERIPH_I2S: I2S[0] initialize success: 0x3c096ebc
I (788) DEV_AUDIO_CODEC: Init audio_adc, i2s_name: i2s_audio_in, i2s_rx_handle:0x3c096ebc, i2s_tx_handle:0x3c096d08, data_if: 0x3fcee2dc
I (800) PERIPH_I2C: i2c_new_master_bus initialize success
I (808) ES7210: Work in Slave mode
I (814) ES7210: Enable ES7210_INPUT_MIC1
I (817) ES7210: Enable ES7210_INPUT_MIC2
I (820) ES7210: Enable ES7210_INPUT_MIC3
I (823) ES7210: Enable TDM mode
I (826) DEV_AUDIO_CODEC: Successfully initialized codec: audio_adc
I (828) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fcee514, chip:es7210
I (835) BOARD_MANAGER: Device audio_adc initialized
I (840) DEV_FS_FAT: slot_config: cd=-1, wp=-1, clk=15, cmd=7, d0=4, d1=-1, d2=-1, d3=-1, d4=-1, d5=-1, d6=-1, d7=-1, width=1, flags=0x0
I (922) DEV_FS_FAT: Filesystem mounted, base path: /sdcard
Name: SD64G
Type: SDHC
Speed: 40.00 MHz (limit: 40.00 MHz)
Size: 60906MB
CSD: ver=2, sector_size=512, capacity=124735488 read_bl_len=9
SSR: bus_width=1
I (930) BOARD_MANAGER: Device fs_sdcard initialized
I (935) BOARD_DEVICE: Device handle audio_adc found, Handle: 0x3fce9a7c TO: 0x3fce9a7c
I (949) I2S_IF: channel mode 2 bits:32/32 channel:2 mask:1
I (949) I2S_IF: TDM Mode 0 bits:32/32 channel:2 sample_rate:48000 mask:1
I (954) I2S_IF: channel mode 2 bits:32/32 channel:2 mask:1
I (959) I2S_IF: TDM Mode 1 bits:32/32 channel:2 sample_rate:48000 mask:1
I (966) ES7210: Bits 16
I (975) ES7210: Enable ES7210_INPUT_MIC1
I (977) ES7210: Enable ES7210_INPUT_MIC2
I (980) ES7210: Enable ES7210_INPUT_MIC3
I (983) ES7210: Enable TDM mode
I (988) ES7210: Unmuted
I (988) Adev_Codec: Open codec device OK
I (991) BMGR_RECORD_TO_SDCARD: Record WAV file info: 48000 Hz, 1 channels, 32 bits
I (995) BMGR_RECORD_TO_SDCARD: Starting I2S recording...
I (11012) BMGR_RECORD_TO_SDCARD: I2S recording completed. Total bytes recorded: 1925120
I (11018) BOARD_DEVICE: Deinit device audio_adc ref_count: 0 device_handle:0x3fce9a7c
I (11021) BOARD_DEVICE: Device audio_adc config found: 0x3c064f04 (size: 92)
I (11024) BOARD_PERIPH: Deinit peripheral i2s_audio_in ref_count: 0
E (11030) i2s_common: i2s_channel_disable(1217): the channel has not been enabled yet
W (11038) PERIPH_I2S: Caution: Releasing RX (0x0).
I (11042) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 0
I (11048) PERIPH_I2C: i2c_del_master_bus deinitialize
I (11053) BOARD_MANAGER: Device audio_adc deinitialized
I (11058) BOARD_DEVICE: Deinit device fs_sdcard ref_count: 0 device_handle:0x3fcee544
I (11065) BOARD_MANAGER: Device fs_sdcard deinitialized
```

（实际采样率、声道数可能随板级 codec 配置变化。）

## 故障排除

### `gen-bmgr-config` 无 `-b` 选项

检查 `IDF_EXTRA_ACTIONS_PATH` 是否指向 `esp_board_manager` 包目录。

```
# Linux / macOS:
echo $IDF_EXTRA_ACTIONS_PATH

# Windows PowerShell:
echo $env:IDF_EXTRA_ACTIONS_PATH

# Windows CMD:
echo %IDF_EXTRA_ACTIONS_PATH%
```

### SD 或录制失败

检查卡为 FAT、挂载为 `/sdcard`、未写保护；确认开发板支持 `audio_adc` 与 `fs_sdcard`。

### 自定义开发板

参阅 [esp_board_manager README](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md)。
