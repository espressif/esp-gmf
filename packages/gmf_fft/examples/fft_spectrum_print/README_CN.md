# FFT Spectrum Print 示例

- [English Version](./README.md)
- 例程难度：⭐

## 例程简介

本示例演示 `gmf_fft` 组件的基本用法：生成多频余弦信号，执行 512 点 Q15 实数 FFT，在串口终端打印 dB 刻度的频谱柱状图，然后执行 IFFT 并打印往返峰值误差。

- 频谱打印：合成 3 组余弦波，执行实数 FFT，并在串口中打印频谱柱状图。
- 往返验证：对 FFT 输出执行 IFFT，并打印缩放后的峰值误差。

### 典型场景

- 快速验证 `gmf_fft` 的初始化、正变换、逆变换和资源释放流程。
- 观察指定频率 bin 的谱峰，确认半边频谱输出布局。
- 作为 Q15 实数 FFT 接入用户工程的参考示例。

### 运行机制

- 示例先生成 3 组余弦波，频率 bin 分别为 8、24、40，幅度分别为 1000、5000、3000。
- 调用 `esp_gmf_fft_forward` 完成 Q15 实数 FFT，输出 `N/2 + 1` 个频点的半边频谱。
- 根据频谱中各 bin 的幅度换算 dB，并打印竖直柱状图。
- 调用 `esp_gmf_fft_inverse` 还原时域信号，并打印缩放后的峰值误差。

## 环境配置

### 硬件要求

- **开发板**：ESP32-S3 或 ESP32-P4 开发板。
- **资源要求**：可查看串口输出的下载和调试环境。

### 默认 IDF 分支

本例程面向 ESP-IDF v5.x 分支，实际支持范围以组件管理器和工程构建配置为准。

### 预备知识

使用本例程前，建议了解 ESP-IDF 示例工程构建流程、Q15 定点数据格式和实数 FFT 半边频谱布局。

## 编译和下载

### 编译准备

编译本例程前需先确保已配置 ESP-IDF 环境。若未配置，请在 ESP-IDF 根目录运行以下脚本完成环境设置，完整步骤请参阅 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/index.html)。

```
./install.sh
. ./export.sh
```

下面是简略步骤：

- 进入本例程工程目录：

```
cd examples/fft_spectrum_print
```

- 根据目标芯片设置 target：

```
idf.py set-target esp32p4
```

如需在 ESP32-S3 上运行，请将 target 设置为 `esp32s3`。

### 项目配置

本例程默认使用 512 点 FFT，并显示前 56 个频率 bin。可在 `examples/fft_spectrum_print/main/fft_spectrum_print.c` 中修改以下宏：

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `N_FFT` | 512 | FFT 点数，需为 2 的幂，范围 [32, 8192] |
| `N_TONES` | 3 | 合成余弦波数量 |
| `TONE_BIN[]` | {8, 24, 40} | 各余弦波对应的频率 bin |
| `TONE_AMP[]` | {1000, 5000, 3000} | 各余弦波 Q15 幅度 |
| `PLOT_BINS` | 56 | 图表 X 轴显示的 bin 数 |
| `PLOT_HEIGHT` | 20 | 图表 Y 轴行数 |
| `DB_FLOOR` | -60 | dB 下限 |

### 编译与烧录

- 编译示例程序：

```
idf.py build
```

- 烧录程序并运行 monitor 工具来查看串口输出（替换 PORT 为端口名称）：

```
idf.py -p PORT flash monitor
```

- 退出调试界面使用 `Ctrl-]`

## 如何使用例程

### 功能和用法

示例运行后会依次执行以下流程：

1. 分配 `ESP_GMF_FFT_BUFFER_SIZE(N_FFT)` 个 `int16_t` 的对齐缓冲区。
2. 生成 3 组余弦波并保存原始输入。
3. 初始化 `esp_gmf_fft_handle_t` 并执行 `esp_gmf_fft_forward`。
4. 打印频谱图后执行 `esp_gmf_fft_inverse`。
5. 打印往返峰值误差并释放资源。

### 日志输出

以下为关键输出示例：

```text
  esp_gmf_fft  N=512  |  signal: cos(bin=8,A=1000) + cos(bin=24,A=5000) + cos(bin=40,A=3000)
  Peak: bin=24  mag=...

  -3 |        #               #              #              |
  -6 |        #               #              #              |
     ...
     +--------------------------------------------------------+
      0       8      16      24      32      40      48
      Bin

  Round-trip peak error after scaling by N/4: ...
```

三个频率 bin 处会出现明显峰值，其余 bin 接近噪底。

## 故障排除

### 初始化失败

若 `esp_gmf_fft_init` 返回失败，请确认 `N_FFT` 为 2 的幂，且范围在 [32, 8192] 内。

### 频谱峰值不符合预期

请确认 `TONE_BIN[]` 中的频率 bin 小于 `N_FFT / 2`，并避免多个 tone 的幅度叠加后超过 Q15 `int16_t` 范围。

## 技术支持

请按照下面的链接获取技术支持：

- 技术支持参见 [esp32.com](https://esp32.com/viewforum.php?f=20) 论坛
- 问题反馈与功能需求，请创建 [GitLab issue](https://gitlab.espressif.cn/adf/gmf_fft/-/issues)

我们会尽快回复。
