# ESP-ASRC

- [![组件注册表](https://components.espressif.com/components/espressif/esp_asrc/badge.svg)](https://components.espressif.com/components/espressif/esp_asrc)

- [English](./README.md)

ASRC (Audio Sample Rate Converter) 模块是一个高性能音频数据格式转换单元，支持采样率、位深和通道数转换，可在不同音频系统之间实现格式对齐与适配。该模块采用软硬件协同架构：在具备 ASRC 外设的芯片上，模块可根据配置自动调度硬件资源，实现低延迟、高效率的实时转换；在无硬件支持时，则使用优化的软件路径保障功能。模块会根据当前芯片能力和配置策略，自动计算最优资源占用的处理路径。接口设计清晰，配置灵活，易于集成，适合用于嵌入式音频框架、音频中间件、或高层应用程序中的音频格式适配场景。

## 功能特性

- 采样率：`src_info.sample_rate` / `dest_info.sample_rate` 为 **4000 Hz** 或 **11025 Hz** 的整数倍，且各自 **≤ 192000 Hz**（以驱动校验为准）。
- 位深：**8**（无符号 PCM）、**16 / 24 / 32**（有符号，交错）。
- 声道：`esp_asrc_aud_info_t` 中任意 `channel`；声道布局变化时可选 `weight`，且 `weight_len == src_ch × dest_ch`。
- 模式：`ESP_ASRC_PERF_TYPE_AUTO`、`ESP_ASRC_PERF_TYPE_HW_ONLY`、`ESP_ASRC_PERF_TYPE_SW_SPEED`、`ESP_ASRC_PERF_TYPE_SW_MEMORY`（见 `esp_asrc_types.h`）。
- 软件路径：非 `ESP_ASRC_PERF_TYPE_HW_ONLY` 时 `complexity` 取 **1–3**。
- 硬件路径：`timeout_ms` 须 **> 0** 或 **−1**（长等待）；**`0` 非法**，`esp_asrc_open()` 返回 `ESP_ASRC_ERR_INVALID_PARAMETER`。
- 缓冲区：先调用 `esp_asrc_get_buffer_alignment()` 获取对齐参数，再调用 `esp_asrc_align_alloc()` 分配输入/输出缓冲区。

## 目录结构

| 路径 | 说明 |
|------|------|
| `include/` | 对外 API：`esp_asrc.h`、`esp_asrc_types.h` |
| `src/`、`inc/` | 核心调度与内部工具 |
| `hw/` | 外设驱动、能力检查、HW 操作表 |
| `sw/` | 软件转换操作表 |
| `examples/asrc_demo/` | 示例工程（编译/烧录见该目录下 README） |
| `test_apps/` | Unity 测试与 pytest 入口 |

## 快速开始

- **ESP-IDF**：使用当前 esp-gmf 分支所支持的 IDF/工具链（下表性能数据在 ESP32-H4 上使用 **IDF v6.1**）。
- **依赖**：根目录 `idf_component.yml` 中的 `espressif/esp_audio_effects`（单仓内可能通过 `override_path` 指定）。
- **示例**：打开 `examples/asrc_demo/README.md`，选定目标芯片后编译/烧录；参考实现：[`examples/asrc_demo/main/asrc_example.c`](./examples/asrc_demo/main/asrc_example.c)。

## 注意事项

- **PSRAM + 硬件加速**：位于 PSRAM 的输入/输出缓冲区须满足 cache line 对齐与长度规则；建议先调用 `esp_asrc_get_buffer_alignment()` 获取 `inbuf_*`/`outbuf_*`，再传给 `esp_asrc_align_alloc()`。
- **不可热更新参数**：变更采样率、位深或声道须 `esp_asrc_close()` → 更新 `esp_asrc_cfg_t` → `esp_asrc_open()`。
- **非整数倍率**：速率比非整数时，`esp_asrc_process()` 单次输出样点数可能略有波动（内部残余缓冲）。
- **立体声**：相对下表单声道数据，CPU 负载约 **2×**。

## 性能

测试环境：

| 芯片     | IDF   | CPU    | SPI RAM | Flash |
|----------|-------|--------|---------|-------|
| ESP32-H4 | v6.1  | 96 MHz | 64 MHz  | QIO   |

单声道 **16-bit** PCM，硬件与软件 ASRC 对比（代表性扫频）：

| 输入            | 输出            | 模式    | 内存 (KB) | CPU (%) |
|-----------------|-----------------|---------|-----------|---------|
| 8kHz/1ch/16bit  | 16kHz/1ch/16bit | Use HW  | 0.6       | 0.8     |
|                 |                 | Only SW | 0.4       | 2.0     |
| 8kHz/1ch/16bit  | 24kHz/1ch/16bit | Use HW  | 0.5       | 0.9     |
|                 |                 | Only SW | 0.5       | 2.9     |
| 8kHz/1ch/16bit  | 32kHz/1ch/16bit | Use HW  | 0.5       | 1.1     |
|                 |                 | Only SW | 0.5       | 3.8     |
| 8kHz/1ch/16bit  | 44kHz/1ch/16bit | Use HW  | 0.5       | 1.3     |
|                 |                 | Only SW | 15.9      | 11.6    |
| 8kHz/1ch/16bit  | 48kHz/1ch/16bit | Use HW  | 0.5       | 1.4     |
|                 |                 | Only SW | 0.6       | 5.6     |
| 16kHz/1ch/16bit | 8kHz/1ch/16bit  | Use HW  | 0.5       | 1.0     |
|                 |                 | Only SW | 0.4       | 1.7     |
| 16kHz/1ch/16bit | 24kHz/1ch/16bit | Use HW  | 0.5       | 1.4     |
|                 |                 | Only SW | 0.5       | 6.3     |
| 16kHz/1ch/16bit | 32kHz/1ch/16bit | Use HW  | 0.5       | 1.5     |
|                 |                 | Only SW | 0.4       | 4.0     |
| 16kHz/1ch/16bit | 44kHz/1ch/16bit | Use HW  | 0.5       | 1.8     |
|                 |                 | Only SW | 15.9      | 11.6    |
| 16kHz/1ch/16bit | 48kHz/1ch/16bit | Use HW  | 0.5       | 1.8     |
|                 |                 | Only SW | 0.5       | 5.8     |
| 24kHz/1ch/16bit | 8kHz/1ch/16bit  | Use HW  | 0.5       | 1.4     |
|                 |                 | Only SW | 0.5       | 2.5     |
| 24kHz/1ch/16bit | 16kHz/1ch/16bit | Use HW  | 0.5       | 1.6     |
|                 |                 | Only SW | 0.5       | 3.7     |
| 24kHz/1ch/16bit | 32kHz/1ch/16bit | Use HW  | 0.5       | 2.0     |
|                 |                 | Only SW | 0.5       | 8.4     |
| 24kHz/1ch/16bit | 44kHz/1ch/16bit | Use HW  | 0.5       | 2.2     |
|                 |                 | Only SW | 5.6       | 11.6    |
| 24kHz/1ch/16bit | 48kHz/1ch/16bit | Use HW  | 0.5       | 2.3     |
|                 |                 | Only SW | 0.4       | 5.9     |
| 32kHz/1ch/16bit | 8kHz/1ch/16bit  | Use HW  | 0.5       | 1.8     |
|                 |                 | Only SW | 0.5       | 3.3     |
| 32kHz/1ch/16bit | 16kHz/1ch/16bit | Use HW  | 0.5       | 2.0     |
|                 |                 | Only SW | 0.4       | 3.5     |
| 32kHz/1ch/16bit | 24kHz/1ch/16bit | Use HW  | 0.5       | 2.2     |
|                 |                 | Only SW | 0.5       | 5.0     |
| 32kHz/1ch/16bit | 44kHz/1ch/16bit | Use HW  | 0.5       | 2.6     |
|                 |                 | Only SW | 15.9      | 11.7    |
| 32kHz/1ch/16bit | 48kHz/1ch/16bit | Use HW  | 0.5       | 2.7     |
|                 |                 | Only SW | 0.5       | 12.6    |
| 44kHz/1ch/16bit | 8kHz/1ch/16bit  | Use HW  | 0.5       | 2.4     |
|                 |                 | Only SW | 8.2       | 5.8     |
| 44kHz/1ch/16bit | 16kHz/1ch/16bit | Use HW  | 0.5       | 2.6     |
|                 |                 | Only SW | 11.0      | 6.5     |
| 44kHz/1ch/16bit | 24kHz/1ch/16bit | Use HW  | 0.5       | 2.8     |
|                 |                 | Only SW | 5.7       | 7.1     |
| 44kHz/1ch/16bit | 32kHz/1ch/16bit | Use HW  | 0.5       | 3.0     |
|                 |                 | Only SW | 11.6      | 7.5     |
| 44kHz/1ch/16bit | 48kHz/1ch/16bit | Use HW  | 0.5       | 3.4     |
|                 |                 | Only SW | 6.0       | 12.8    |
| 48kHz/1ch/16bit | 8kHz/1ch/16bit  | Use HW  | 0.5       | 2.6     |
|                 |                 | Only SW | 0.5       | 4.9     |
| 48kHz/1ch/16bit | 16kHz/1ch/16bit | Use HW  | 0.5       | 2.8     |
|                 |                 | Only SW | 0.5       | 5.0     |
| 48kHz/1ch/16bit | 24kHz/1ch/16bit | Use HW  | 0.5       | 3.0     |
|                 |                 | Only SW | 0.4       | 5.2     |
| 48kHz/1ch/16bit | 32kHz/1ch/16bit | Use HW  | 0.5       | 3.2     |
|                 |                 | Only SW | 0.5       | 7.5     |
| 48kHz/1ch/16bit | 44kHz/1ch/16bit | Use HW  | 0.5       | 3.5     |
|                 |                 | Only SW | 5.6       | 11.6    |

- **Use HW**：片内 ASRC 承担适用场景的速率转换；同行对比 CPU 低于 **Only SW**。
- **Only SW**：纯软件路径；部分速率比下 CPU 更高。
- **CPU (%)**：连续处理 **10 s** 的平均 CPU 时间占比（单声道）。
- **立体声**：CPU 约为上表单声道数值的 **2×**。

## SoC 兼容性

| 芯片      | v1.0.0 | 备注       |
|-----------|--------|------------|
| ESP32     | ✅     | 仅软件     |
| ESP32-S2  | ✅     | 仅软件     |
| ESP32-S3  | ✅     | 仅软件     |
| ESP32-C2  | ✅     | 仅软件     |
| ESP32-C3  | ✅     | 仅软件     |
| ESP32-C5  | ✅     | 仅软件     |
| ESP32-C6  | ✅     | 仅软件     |
| ESP32-P4  | ✅     | 仅软件     |
| ESP32-H4  | ✅     | 硬件 + 软件 |
| ESP32-S31 | ✅     | 硬件 + 软件 |

## FAQ

1）**ASRC 对缓冲区与对齐是否有要求？**

- **硬件加速**且缓冲区在 **PSRAM**：长度与地址须满足 DMA 的 **cache line** 对齐规则。
- **软件**路径：一般可使用 `malloc()` 类分配；吞吐与硬件路径可能不同。
- 优先使用 **`esp_asrc_get_buffer_alignment()` + `esp_asrc_align_alloc()`** 组合，使分配与当前模式一致。

2）**能否在句柄打开时修改采样率、位深或声道数？**

- **不能。**须 `esp_asrc_close()`，更新 `esp_asrc_cfg_t`，再 `esp_asrc_open()`。

3）**`ESP_ASRC_PERF_TYPE_*` 各表示什么？**

| 符号 | 行为 |
|------|------|
| `ESP_ASRC_PERF_TYPE_AUTO` | 按能力与负载在 HW/SW 间选择。 |
| `ESP_ASRC_PERF_TYPE_SW_SPEED` | 仅软件；内部缓冲优先 **IRAM**。 |
| `ESP_ASRC_PERF_TYPE_SW_MEMORY` | 仅软件；内部缓冲优先 **PSRAM**。 |
| `ESP_ASRC_PERF_TYPE_HW_ONLY` | 仅硬件；不支持 ASRC 时 `esp_asrc_open()` 失败。 |

多目标场景默认建议使用 **`ESP_ASRC_PERF_TYPE_AUTO`**。
