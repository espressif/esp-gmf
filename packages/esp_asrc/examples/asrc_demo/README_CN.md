# ASRC 音频格式转换演示

- [English Version](./README.md)
- 例程难度：⭐

## 例程简介

本示例演示如何使用 ASRC（音频采样率转换）模块进行音频格式转换，包括采样率转换、声道转换和位深转换。

- 示例读取嵌入式 PCM 文件，通过 ASRC 完成格式转换后统计输出字节数。
- 支持软件 ASRC（全系列芯片）和硬件 ASRC。

### 典型场景

- 将录音设备输出的低采样率单声道 PCM 数据上采样为标准播放格式（如 48 kHz 立体声）。
- 需要在不同采样率音频系统之间进行实时格式对齐的场景。

## 环境配置

### 硬件要求

- **开发板**：默认以 ESP32-S3 为例，其他 ESP 系列芯片同样适用。
- **资源要求**：无特殊外设需求，仅使用片内 SRAM 和 PSRAM（可选）。

### 默认 IDF 分支

本例程支持 IDF release/v6.1 及以上版本。

## 编译和下载

### 编译准备

编译本例程前需先确保已配置 ESP-IDF 环境；若已配置可跳过本段，直接进入工程目录。若未配置，请在 ESP-IDF 根目录运行以下脚本完成环境设置，完整步骤请参阅 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/index.html)。

```bash
./install.sh
. ./export.sh
```

### 进入工程目录

```bash
cd $YOUR_ESP_ASRC_PATH/example/asrc_demo
```

### 设置目标芯片

```bash
idf.py set-target esp32s3
```

### 项目配置

本示例无需额外 menuconfig 配置，默认演示将 **16 kHz 单声道**转换为 **48 kHz 立体声**。如需调整转换参数，直接修改 `main/asrc_example.c` 中的 `asrc_cfg` 结构体：

| 参数 | 源格式 | 目标格式 |
|------|--------|----------|
| 采样率 | 16000 Hz | 48000 Hz |
| 声道数 | 1（单声道） | 2（立体声） |
| 位深度 | 16 bit | 16 bit |

### 编译与烧录

- 编译示例程序：

```bash
idf.py build
```

- 烧录程序并运行 monitor 工具来查看串口输出（替换 `PORT` 为端口名称）：

```bash
idf.py -p PORT flash monitor
```

- 退出调试界面使用 `Ctrl-]`。

## 如何使用例程

### 功能和用法

- 上电后例程自动运行：初始化 ASRC、读取嵌入式 PCM 文件（16 kHz, 1ch, 16bit）、循环调用 `esp_asrc_process` 完成格式转换，处理完成后打印统计信息并释放资源。
- 无需用户交互，观察串口日志即可确认运行结果。

### 日志输出

正常流程会依次打印打开 ASRC、分配缓冲区、逐帧处理及完成信息，如下所示：

```
I (836) main_task: Calling app_main()
I (926) ASRC_EXAMPLE: ASRC process finished
I (926) main_task: Returned from app_main()
```

## 性能模式

| 模式 | 说明 |
|------|------|
| `ESP_ASRC_PERF_TYPE_AUTO` | 自动选择最优模式（推荐） |
| `ESP_ASRC_PERF_TYPE_HW_ONLY` | 仅使用硬件 ASRC |
| `ESP_ASRC_PERF_TYPE_SW_MEMORY` | 软件 ASRC，内存优先 |
| `ESP_ASRC_PERF_TYPE_SW_SPEED` | 软件 ASRC，速度优先 |

## 质量等级

`complexity` 参数控制软件 ASRC 的转换质量：

| 等级 | 说明 |
|------|------|
| 1 | 最低质量，最快速度 |
| 2 | 中等质量，平衡性能 |
| 3 | 最高质量，最慢速度 |

> **注意**：使用硬件 ASRC 时，`complexity` 参数被忽略。

## 支持的芯片

| 芯片型号 | 软件 ASRC | 硬件 ASRC |
|----------|-----------|-----------|
| ESP32 | ✅ | ❌ |
| ESP32-S2 | ✅ | ❌ |
| ESP32-S3 | ✅ | ❌ |
| ESP32-C2 | ✅ | ❌ |
| ESP32-C3 | ✅ | ❌ |
| ESP32-C5 | ✅ | ❌ |
| ESP32-C6 | ✅ | ❌ |
| ESP32-P4 | ✅ | ❌ |
| ESP32-H4 | ✅ | ✅ |
| ESP32-S31 | ✅ | ✅ |

## 故障排除

- **内存分配失败**：检查可用堆内存，减小 `in_samples`（每次处理帧数）或降低 `complexity` 等级。
- **硬件 ASRC 不可用**：确认使用芯片支持硬解采样率转换，且 `perf_type` 设为 `ESP_ASRC_PERF_TYPE_AUTO` 或 `ESP_ASRC_PERF_TYPE_HW_ONLY`。
- **输出字节数异常**：确认 `asrc_cfg` 中源格式与嵌入式 PCM 文件（16 kHz, 1ch, 16bit）一致。
- **超时错误**：增大 `timeout_ms` 参数或检查系统负载。

## 技术支持

请按照下面的链接获取技术支持：

- 技术支持参见 [esp32.com](https://esp32.com/viewforum.php?f=20) 论坛
- 问题反馈与功能需求，请创建 [GitHub issue](https://github.com/espressif/esp-gmf/issues)

我们会尽快回复。
