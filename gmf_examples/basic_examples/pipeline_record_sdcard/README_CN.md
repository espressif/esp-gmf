# 录制音频存储 microSD 卡

- [English](./README.md)
- 例程难度 ⭐

## 例程简介

本例程介绍了通过 CODEC_DEV_RX IO 获取 codec 录制的声音，经 encoder 元素编码后，通过 File IO 将编码数据保存 microSD 卡。

本例支持将录音编码为 AAC、G711A、G711U、AMRNB、AMRWB、OPUS、ADPCM 和 PCM 音频格式，默认使用 AAC 格式。

本例程通过固定较低 CPU 频率实现降低系统整体运行功耗的目标，在低功耗录音笔等项目中有良好的应用。

> [!NOTE]
> 1. 可修改 `DEFAULT_CPU_FREQ_MHZ` 实现不同 CPU 频率，实际支持的频率参考对应芯片中 `rtc_clk_cpu_freq_mhz_to_config` 可选频率
> 2. 不同频率下的功耗差异可参考对应芯片数据手册中 `其他功耗模式下的功耗`，如 [ESP32S3 数据手册](https://documentation.espressif.com/esp32-s3_datasheet_cn.pdf)
> 3. 如果不采用直接固定 CPU 频率的方式，则可参考 [ESP32 电源管理算法](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/api-reference/system/power_management.html#esp32) 实现默认的电源管理策略

## 示例创建

### IDF 默认分支

本例程支持 IDF release/v5.4(>= v5.4.3) and release/v5.5(>= v5.5.2) 分支。

### 配置

本例程需要准备一张 microSD 卡。录音编码后的音频会自动存入 microSD 卡，用户可通过 `esp_gmf_audio_helper_reconfig_enc_by_type ` 函数修改编码格式及音频参数配置。

### 编译和下载

编译本例程前需要先确保已配置 ESP-IDF 的环境，如果已配置可跳到下一项配置，如果未配置需要先在 ESP-IDF 根目录运行下面脚本设置编译环境，有关配置和使用 ESP-IDF 完整步骤，请参阅 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/index.html)：

```
./install.sh
. ./export.sh
```

下面是简略编译步骤：

- 进入录制音频存储 microSD 卡测试工程存放位置

```
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_record_sdcard
```

- 执行预编译脚本，根据提示选择编译芯片，自动设置 IDF Action 扩展

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

- 例程开始运行后，录音数据经过编码后将自动存入 microSD 卡，录制完成后停止退出，打印如下：

```c
I (853) main_task: Calling app_main()
I (855) pm: Frequency switching config: CPU_MAX: 80, APB_MAX: 80, APB_MIN: 80, Light sleep: DISABLED
I (864) REC_SDCARD: [ 1 ] Setup peripheral
I (868) DEV_FATFS_SDCARD: slot_config: cd=-1, wp=-1, clk=15, cmd=7, d0=4, d1=-1, d2=-1, d3=-1, d4=-1, d5=-1, d6=-1, d7=-1, width=1, flags=0x0
I (881) gpio: GPIO[15]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (889) gpio: GPIO[7]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (897) gpio: GPIO[4]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (953) DEV_FATFS_SDCARD: Filesystem mounted, base path: /sdcard
I (954) BOARD_MANAGER: Device fs_sdcard initialized
I (954) DEV_AUDIO_CODEC: ADC is ENABLED
W (957) i2s_tdm: the current mclk multiple cannot perform integer division (slot_num: 3, slot_bits: 16)
HINT: Please adjust the mclk multiple to get the accurate sample rate.
For example, if you're using 24-bit slot width or enabled 3 slots, then the mclk multiple should be a multiple of 3, otherwise the sample rate will be inaccurate.
W (966) i2s_tdm: the current mclk multiple cannot perform integer division (slot_num: 3, slot_bits: 16)
HINT: Please adjust the mclk multiple to get the accurate sample rate.
For example, if you're using 24-bit slot width or enabled 3 slots, then the mclk multiple should be a multiple of 3, otherwise the sample rate will be inaccurate.
I (975) PERIPH_I2S: I2S[0] TDM, RX, ws: 45, bclk: 9, dout: 8, din: 10
I (981) PERIPH_I2S: I2S[0] initialize success: 0x3c12a734
I (986) DEV_AUDIO_CODEC: Init audio_adc, i2s_name: i2s_audio_in, i2s_rx_handle:0x3c12a734, i2s_tx_handle:0x0, data_if: 0x3fcb1fec
I (998) PERIPH_I2C: i2c_new_master_bus initialize success
I (1007) ES7210: Work in Slave mode
I (1015) ES7210: Enable ES7210_INPUT_MIC1
I (1019) ES7210: Enable ES7210_INPUT_MIC2
I (1023) ES7210: Enable ES7210_INPUT_MIC3
I (1028) ES7210: Enable TDM mode
I (1032) DEV_AUDIO_CODEC: Successfully initialized codec: audio_adc
I (1032) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fcb2254, chip:es7210
I (1034) BOARD_MANAGER: Device audio_adc initialized
I (1039) BOARD_DEVICE: Device handle audio_adc found, Handle: 0x3fcad8cc TO: 0x3fcad8cc
I (1050) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:1
I (1052) I2S_IF: TDM Mode 0 bits:16/16 channel:2 sample_rate:48000 mask:1
I (1060) ES7210: Bits 8
I (1070) ES7210: Enable ES7210_INPUT_MIC1
I (1074) ES7210: Enable ES7210_INPUT_MIC2
I (1078) ES7210: Enable ES7210_INPUT_MIC3
I (1082) ES7210: Enable TDM mode
I (1089) ES7210: Unmuted
I (1089) Adev_Codec: Open codec device OK
I (1089) REC_SDCARD: [ 2 ] Register all the elements and set audio information to record codec device
I (1094) ESP_GMF_POOL: Registered items on pool:0x3c12ae28, app_main-127
I (1100) ESP_GMF_POOL: IO, Item:0x3c12aedc, H:0x3c12ae3c, TAG:io_codec_dev
I (1106) ESP_GMF_POOL: IO, Item:0x3c12af9c, H:0x3c12aeec, TAG:io_file
I (1112) ESP_GMF_POOL: EL, Item:0x3c12b0a0, H:0x3c12afac, TAG:aud_enc
I (1118) REC_SDCARD: [ 3 ] Create audio pipeline
I (1123) REC_SDCARD: [ 3.1 ] Set audio url to record
I (1127) REC_SDCARD: [ 3.2 ] Reconfig audio encoder type by url and audio information and report information to the record pipeline
W (1139) ESP_GMF_PIPELINE: There is no thread for add jobs, pipe:0x3c12b0b0, tsk:0x0, [el:aud_enc-0x3c12b0f4]
I (1149) REC_SDCARD: [ 3.3 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (1160) ESP_GMF_TASK: Waiting to run... [tsk:gmf_rec-0x3fcb0aac, wk:0x0, run:0]
I (1166) ESP_GMF_TASK: Waiting to run... [tsk:gmf_rec-0x3fcb0aac, wk:0x3c12b3f8, run:0]
I (1174) REC_SDCARD: [ 3.4 ] Create envent group and listening event from pipeline
I (1181) REC_SDCARD: [ 4 ] Start audio_pipeline
I (1185) ESP_GMF_FILE: Open, dir:2, uri:/sdcard/esp_gmf_rec001.aac
I (1192) REC_SDCARD: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c12b0b0, type:8192, sub:ESP_GMF_EVENT_STATE_OPENING, payload:0x0, size:0,0x0
I (1207) REC_SDCARD: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c12b0f4, type:12288, sub:ESP_GMF_EVENT_STATE_INITIALIZED, payload:0x3c13a6b4, size:16,0x0
I (1219) ESP_GMF_AENC: Open, type:AAC, acquire in frame: 2048, out frame: 968
I (1225) REC_SDCARD: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c12b0f4, type:8192, sub:ESP_GMF_EVENT_STATE_RUNNING, payload:0x0, size:0,0x0
I (1239) ESP_GMF_TASK: One times job is complete, del[wk:0x3c12b3f8, ctx:0x3c12b0f4, label:aud_enc_open]
I (1248) ESP_GMF_PORT: ACQ IN, new self payload:0x3c12b3f8, port:0x3c12b288, el:0x3c12b0f4-aud_enc
I (1263) REC_SDCARD: [ 5 ] Wait for a while to stop record pipeline
I (6872) : ┌───────────────────┬──────────┬─────────────┬─────────┬──────────┬───────────┬────────────┬───────┐
I (6888) : │ Task              │ Core ID  │ Run Time    │ CPU     │ Priority │ Stack HWM │ State      │ Stack │
I (6905) : ├───────────────────┼──────────┼─────────────┼─────────┼──────────┼───────────┼────────────┼───────┤
I (6927) : │ IDLE0             │ 0        │ 506963      │  25.26% │ 0        │ 720       │ Ready      │ Intr  │
I (6940) : │ gmf_rec           │ 0        │ 492176      │  24.52% │ 5        │ 37072     │ Blocked    │ Extr  │
I (6950) : │ sys_monitor       │ 0        │ 4469        │   0.22% │ 1        │ 3180      │ Running    │ Extr  │
I (6963) : │ main              │ 0        │ 0           │   0.00% │ 1        │ 1368      │ Blocked    │ Intr  │
I (6972) : │ ipc0              │ 0        │ 0           │   0.00% │ 24       │ 528       │ Suspended  │ Intr  │
I (6984) : ├───────────────────┼──────────┼─────────────┼─────────┼──────────┼───────────┼────────────┼───────┤
I (7011) : │ IDLE1             │ 1        │ 1004007     │  50.02% │ 0        │ 792       │ Ready      │ Intr  │
I (7024) : │ ipc1              │ 1        │ 0           │   0.00% │ 24       │ 536       │ Suspended  │ Intr  │
I (7034) : ├───────────────────┼──────────┼─────────────┼─────────┼──────────┼───────────┼────────────┼───────┤
I (7062) : │ Tmr Svc           │ 7fffffff │ 0           │   0.00% │ 1        │ 1360      │ Blocked    │ Intr  │
I (7072) : └───────────────────┴──────────┴─────────────┴─────────┴──────────┴───────────┴────────────┴───────┘
I (7105) MONITOR: Func:sys_monitor_task, Line:25, MEM Total:7349652 Bytes, Inter:318963 Bytes, Dram:318963 Bytes

I (11283) ESP_GMF_CODEC_DEV: CLose, 0x3c12b1e8, pos = 1028096/0
I (11283) ESP_GMF_FILE: CLose, 0x3c12b2c8, pos = 120509/0
I (11292) ESP_GMF_TASK: One times job is complete, del[wk:0x3c12b41c, ctx:0x3c12b0f4, label:aud_enc_close]
I (11292) REC_SDCARD: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c12b0b0, type:8192, sub:ESP_GMF_EVENT_STATE_STOPPED, payload:0x0, size:0,0x0
I (11305) ESP_GMF_TASK: Waiting to run... [tsk:gmf_rec-0x3fcb0aac, wk:0x0, run:0]
I (11313) ESP_GMF_TASK: Waiting to run... [tsk:gmf_rec-0x3fcb0aac, wk:0x0, run:0]
I (11320) REC_SDCARD: [ 6 ] Destroy all the resources
W (11325) GMF_SETUP_AUD_CODEC: Unregistering default encoder
I (11330) BOARD_DEVICE: Deinit device audio_adc ref_count: 0 device_handle:0x3fcad8cc
I (11342) BOARD_DEVICE: Device audio_adc config found: 0x3c0ca4a4 (size: 88)
I (11344) BOARD_PERIPH: Deinit peripheral i2s_audio_in ref_count: 0
E (11350) i2s_common: i2s_channel_disable(1200): the channel has not been enabled yet
W (11358) PERIPH_I2S: Caution: Releasing RX (0x0).
I (11362) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 0
I (11368) PERIPH_I2C: i2c_del_master_bus deinitialize
I (11373) BOARD_MANAGER: Device audio_adc deinitialized
I (11378) BOARD_DEVICE: Deinit device fs_sdcard ref_count: 0 device_handle:0x3fcad354
I (11386) gpio: GPIO[15]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (11394) gpio: GPIO[7]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (11402) gpio: GPIO[4]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (11411) BOARD_MANAGER: Device fs_sdcard deinitialized
I (11416) main_task: Returned from app_main()
```
