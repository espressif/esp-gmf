# 播放 microSD 卡音乐

- [English](./README.md)

## 例程简介

本例程介绍了如何使用 `esp_board_manager` 初始化 codec 和 sdcard，获取设备的句柄，并使用 `esp_codec_dev` 的 API 播放 microSD 卡中的音乐。

## 示例创建

### IDF 默认分支

本例程仅支持 IDF release/v5.5 (>=5.5.2) 及 IDF release/v5.4 (>=5.4.3) 分支。

### 准备工作

本例程需要一张 microSD 卡，并在根目录中存放一个名为 `test.wav` 的 WAV 文件。

### 编译和下载

编译本例程前需要先确保已配置 ESP-IDF ，如果已配置可跳到下一项配置，如果未配置需要先在 ESP-IDF 根目录运行下面脚本设置编译环境，有关配置和使用 ESP-IDF 完整步骤，请参阅 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/index.html)：

```shell
./install.sh
. ./export.sh
```

下面是简略编译步骤：

- 进入播放 microSD 卡音乐测试工程存放位置

```shell
cd $YOUR_GMF_PATH/packages/esp_board_manager/example/dev_audio_codec/play_sdcard_music
```

- 配置 `esp_board_manager` 路径，激活环境（在当前终端下只需要执行一次）

```shell
export IDF_EXTRA_ACTIONS_PATH=$YOUR_GMF_PATH/packages/esp_board_manager
```

- 选择使用的开发板

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

- 例程开始运行后，将自动播放 microSD 卡中的 `test.wav` 文件。输出如下：

```c
I (732) BOARD_MANAGER_PLAY_SDCARD_MUSIC: Playing music from /sdcard/test.wav
I (739) PERIPH_I2S: I2S[0] TDM,  TX, ws: 45, bclk: 9, dout: 8, din: 10
I (745) PERIPH_I2S: I2S[0] initialize success: 0x3c096c58
I (750) DEV_AUDIO_CODEC: DAC is ENABLED
I (754) DEV_AUDIO_CODEC: Init audio_dac, i2s_name: i2s_audio_out, i2s_rx_handle:0x0, i2s_tx_handle:0x3c096c58, data_if: 0x3fcea804
I (765) PERIPH_I2C: i2c_new_master_bus initialize success
I (775) ES8311: Work in Slave mode
I (778) DEV_AUDIO_CODEC: Successfully initialized codec: audio_dac
I (779) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fceaa68, chip:es8311
I (787) BOARD_MANAGER: Device audio_dac initialized
I (791) DEV_FATFS_SDCARD: slot_config: cd=-1, wp=-1, clk=15, cmd=7, d0=4, d1=-1, d2=-1, d3=-1, d4=-1, d5=-1, d6=-1, d7=-1, width=1, flags=0x0
I (873) DEV_FATFS_SDCARD: Filesystem mounted, base path: /sdcard
Name: SD64G
Type: SDHC
Speed: 40.00 MHz (limit: 40.00 MHz)
Size: 60906MB
CSD: ver=2, sector_size=512, capacity=124735488 read_bl_len=9
SSR: bus_width=1
I (881) BOARD_MANAGER: Device fs_sdcard initialized
I (886) BOARD_DEVICE: Device handle audio_dac found, Handle: 0x3fce9a7c TO: 0x3fce9a7c
I (900) WAV_HEADER: WAV file: 48000 Hz, 2 channels, 16 bits
I (900) BOARD_MANAGER_PLAY_SDCARD_MUSIC: Play WAV file info: 48000 Hz, 2 channels, 16 bits
I (907) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:3
I (912) I2S_IF: TDM Mode 1 bits:16/16 channel:2 sample_rate:48000 mask:3
I (933) Adev_Codec: Open codec device OK
I (5185) BOARD_MANAGER_PLAY_SDCARD_MUSIC: Play WAV file completed
I (5185) BOARD_DEVICE: Deinit device audio_dac ref_count: 0 device_handle:0x3fce9a7c
I (5197) BOARD_DEVICE: Device audio_dac config found: 0x3c064ebc (size: 92)
I (5198) BOARD_PERIPH: Deinit peripheral i2s_audio_out ref_count: 0
E (5200) i2s_common: i2s_channel_disable(1217): the channel has not been enabled yet
W (5208) PERIPH_I2S: Caution: Releasing TX (0x0).
W (5212) PERIPH_I2S: Caution: RX (0x3c096e0c) forced to stop.
E (5217) i2s_common: i2s_channel_disable(1217): the channel has not been enabled yet
I (5225) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 0
I (5231) PERIPH_I2C: i2c_del_master_bus deinitialize
I (5235) BOARD_MANAGER: Device audio_dac deinitialized
I (5240) BOARD_DEVICE: Deinit device fs_sdcard ref_count: 0 device_handle:0x3fceaa98
I (5248) BOARD_MANAGER: Device fs_sdcard deinitialized

```

## 问题解答

1. 如果出现以下错误信息，请执行 `echo $IDF_EXTRA_ACTIONS_PATH` 检查 `esp_board_manager` 路径配置是否正确：

```c
Usage: idf.py gen-bmgr-config [OPTIONS]
Try 'idf.py gen-bmgr-config --help' for help.

Error: No such option: -b
```

2. 如果需要使用自定义的开发板，请参考 [README_CN.md](../../../README_CN.md) 中关于 **自定义板级** 的说明。
