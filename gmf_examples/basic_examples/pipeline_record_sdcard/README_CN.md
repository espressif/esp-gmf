# 录制音频存储 microSD 卡

- [English Version](./README.md)
- 例程难度：⭐

## 例程简介

本示例展示了通过 CODEC_DEV_RX IO 采集 codec 录音，经 encoder 编码后通过 File IO 将数据保存到 microSD 卡。

- 示例使用单管道架构：`io_codec_dev` → `aud_enc` → `io_file`。
- 支持 AAC、G711A、G711U、AMRNB、AMRWB、OPUS、ADPCM、PCM 等编码格式，默认 AAC。
- 例程通过固定较低 CPU 频率从而降低整体功耗，适用于低功耗录音笔等场景。

> [!NOTE]
> 1. 可修改 `DEFAULT_CPU_FREQ_MHZ` 实现不同 CPU 频率，实际支持的频率参考对应芯片中 `rtc_clk_cpu_freq_mhz_to_config` 可选频率。
> 2. 不同频率下的功耗差异可参考对应芯片数据手册中「其他功耗模式下的功耗」，如 [ESP32-S3 数据手册](https://documentation.espressif.com/esp32-s3_datasheet_cn.pdf)。
> 3. 若不采用固定 CPU 频率，可参考 [ESP32 电源管理](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/api-reference/system/power_management.html#esp32) 实现默认电源管理策略。

### 典型场景

- 本地录音并保存为文件，如低功耗录音笔、会议记录等。

## 环境配置

### 硬件要求

- **开发板**：默认以 ESP32-S3-Korvo V3 为例，其他 ESP 音频板同样适用。
- **资源要求**：microSD 卡、麦克风、Audio ADC。

### 默认 IDF 分支

本例程支持 IDF release/v5.4 (>= v5.4.3) 与 release/v5.5 (>= v5.5.2) 分支。

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
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_record_sdcard
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

本例程可以修改源文件中以下定义完成相关配置：

- `DEFAULT_RECORD_OUTPUT_URL`：音频存储 URL
- `DEFAULT_RECORD_SAMPLE_RATE`：采样率（如 16000 Hz）
- `DEFAULT_RECORD_BITS`：采样位数（如 16 bits）
- `DEFAULT_RECORD_CHANNEL`：音频通道数
- `DEFAULT_MICROPHONE_GAIN`：录制麦克风增益
- `DEFAULT_RECORD_DURATION_MS`：录制时长（如 10000 ms）

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

- 上电后例程会挂载 microSD 卡并初始化录音 codec，开始录制。
- 录制一段时间后自动停止并将编码数据写入 microSD 卡（如 `/sdcard/esp_gmf_rec001.aac`），最后释放资源。

### 日志输出

- 正常流程会依次打印外设初始化、注册元素、创建 pipeline、设置录音 url、绑定任务、监听事件、启动 pipeline、等待结束并销毁资源。
- 可关注不同 CPU 频率下的 loading 情况，确认是否可以在该频率下正常编码。

```c
I (853) main_task: Calling app_main()
I (855) pm: Frequency switching config: CPU_MAX: 80, APB_MAX: 80, APB_MIN: 80, Light sleep: DISABLED
I (864) REC_SDCARD: [ 1 ] Setup peripheral
I (1089) REC_SDCARD: [ 2 ] Register all the elements and set audio information to record codec device
I (1118) REC_SDCARD: [ 3 ] Create audio pipeline
I (1123) REC_SDCARD: [ 3.1 ] Set audio url to record
I (1127) REC_SDCARD: [ 3.2 ] Reconfig audio encoder type by url and audio information and report information to the record pipeline
I (1149) REC_SDCARD: [ 3.3 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (1174) REC_SDCARD: [ 3.4 ] Create envent group and listening event from pipeline
I (1181) REC_SDCARD: [ 4 ] Start audio_pipeline
I (1185) ESP_GMF_FILE: Open, dir:2, uri:/sdcard/esp_gmf_rec001.aac
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

I (11283) ESP_GMF_FILE: CLose, 0x3c12b2c8, pos = 120509/0
I (11292) REC_SDCARD: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c12b0b0, type:8192, sub:ESP_GMF_EVENT_STATE_STOPPED, payload:0x0, size:0,0x0
I (11320) REC_SDCARD: [ 6 ] Destroy all the resources
I (11416) main_task: Returned from app_main()
```

## 故障排除

### microSD 卡未挂载或无法写入

- 确认 microSD 卡已正确插入且格式为 FAT32。
- 若日志提示打开文件失败，请检查卡容量与写保护状态。

### 无录音或编码失败

- 确认板级已正确配置录音 codec（如 ES7210）与 I2S。
- 若出现编码器初始化失败，请检查 menuconfig 中对应编码格式是否已启用。

### 相关参考

- 如需支持封装更多音频容器或获取视频支持，请参考 [esp_capture](../../../packages/esp_capture/README_CN.md)

## 技术支持

请按照下面的链接获取技术支持：

- 技术支持参见 [esp32.com](https://esp32.com/viewforum.php?f=20) 论坛
- 问题反馈与功能需求，请创建 [GitHub issue](https://github.com/espressif/esp-gmf/issues)

我们会尽快回复。
