# 录制音频到 microSD 卡

- [English](./README.md)

## 例程简介

本例程介绍了如何使用 `esp_board_manager` 初始化 codec 和 sdcard，获取设备的句柄，并使用 `esp_codec_dev` 的 API 录制音频数据，然后将其以 WAV 文件格式保存到 microSD 卡中。

## 示例创建

### IDF 默认分支

本例程仅支持 IDF release/v5.5 (>=5.5.2) 及 IDF release/v5.4 (>=5.4.3) 分支。

### 准备工作

本例程需要准备一张 microSD 卡。录制的音频文件将命名为 `record.wav` 并存储在卡上。

### 编译和下载

编译本例程前需要先确保已配置 ESP-IDF 的环境，如果已配置可跳到下一项配置，如果未配置需要先在 ESP-IDF 根目录运行下面脚本设置编译环境，有关配置和使用 ESP-IDF 完整步骤，请参阅 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/index.html)：

```shell
./install.sh
. ./export.sh
```

下面是简略编译步骤：

- 进入播放 microSD 卡音乐测试工程存放位置

```shell
cd $YOUR_GMF_PATH/packages/esp_board_manager/example/dev_audio_codec/record_to_sdcard
```

- 配置 `esp_board_manager` 路径，激活环境（在当前终端下只需要执行一次）

```shell
export IDF_EXTRA_ACTIONS_PATH=$YOUR_GMF_PATH/packages/esp_board_manager
```

- 选择使用的开发板，以 esp32_s3_korvo2_v3 为例：

```shell
idf.py gen-bmgr-config -b esp32_s3_korvo2_v3
```

- 也可以执行以下命令查看支持的开发板列表

```shell
idf.py gen-bmgr-config -l
```

- 编译例程代码

```shell
idf.py build
```

- 烧录程序并运行 monitor 工具来查看串口输出 (替换 PORT 为端口名称)：

```shell
idf.py -p PORT flash monitor
```

- 退出调试界面使用 ``Ctrl-]``

## 如何使用例程

### 功能和用法

- 例程开始运行后，将自动开始录制 10 秒钟的音频，并将音频保存到 microSD 卡的 `record.wav` 文件中。输出如下：

```c
I (732) BOARD_MANAGER_RECORD_TO_SDCARD: Record to /sdcard/record.wav
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
I (991) BOARD_MANAGER_RECORD_TO_SDCARD: Record WAV file info: 48000 Hz, 1 channels, 32 bits
I (995) BOARD_MANAGER_RECORD_TO_SDCARD: Starting I2S recording...
I (11012) BOARD_MANAGER_RECORD_TO_SDCARD: I2S recording completed. Total bytes recorded: 1925120
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

## 问题解答

1. 如果出现以下错误信息，请执行 `echo $IDF_EXTRA_ACTIONS_PATH` 检查 `esp_board_manager` 路径配置是否正确：

```c
Usage: idf.py gen-bmgr-config [OPTIONS]
Try 'idf.py gen-bmgr-config --help' for help.

Error: No such option: -b
```

2. 如果需要使用自定义的开发板，请参考 [README_CN.md](../../../README_CN.md) 中关于 **自定义板级** 的说明。
