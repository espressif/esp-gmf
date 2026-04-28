# GMF FFT

- [![组件注册表](https://components.espressif.com/components/espressif/gmf_fft/badge.svg)](https://components.espressif.com/components/espressif/gmf_fft)
- [English](./README.md)

`gmf_fft` 是面向 ESP-IDF 的定点 Q15 实数 FFT / IFFT 组件，支持 ESP 系列芯片的 FFT 与 IFFT 处理。实现包含通用 C 版本及针对 Xtensa / RISC-V 的 PIE 向量汇编优化，在典型长度下较纯软件 DIT 延迟更低；同时利用实信号的 Hermitian 共轭对称特性，在正变换时仅保留 `N/2 + 1` 个频点，从而降低频域存储与计算开销，适用于无 FPU 或对时延敏感的嵌入式场景。

## 功能特性

- 支持 32 到 8192 点、长度为 2 的幂的实数 FFT / IFFT。
- 使用 Q15 `int16_t` 数据格式，减少运算和存储开销。
- 同一 `handle` 可被多个线程共享，前提是不同线程使用各自的 `data` 缓冲区。

## 支持目标

v0.1.0 支持 ESP32-S3 和 ESP32-P4。

## API

```c
#include "esp_gmf_fft.h"

int16_t data[ESP_GMF_FFT_BUFFER_SIZE(256)] = {0};
esp_gmf_fft_handle_t handle = NULL;
const esp_gmf_fft_cfg_t cfg = {
    .n_fft = 256,
    .fft_type = ESP_GMF_FFT_TYPE_REAL_Q15,
};

esp_gmf_fft_init(&cfg, &handle);
esp_gmf_fft_forward(handle, data);
esp_gmf_fft_inverse(handle, data);
esp_gmf_fft_deinit(&handle);
```

正变换输入为 `N` 个实数采样，`data` 缓冲区需要预留 `ESP_GMF_FFT_BUFFER_SIZE(N)` 个 `int16_t` 元素。输出布局为：`data[0]` 存放 DC 实部，`data[1]` 存放 Nyquist 实部，`data[2k]` 和 `data[2k + 1]` 分别存放第 `k` 个频点的实部和虚部。

`forward -> inverse` 往返后的幅度约为原始输入的 `4/N`，库内不做补偿；如果需要恢复原始幅度，可在逆变换后乘以 `N/4`。

## 内存开销

- 用户数据缓冲区：`ESP_GMF_FFT_BUFFER_SIZE(N)` 个 `int16_t`，约 `2 * (N + 2)` 字节。
- `handle` 内部旋转因子表：约 `3 * N` 字节，外加少量 plan 结构体开销。

## 精度与性能

测试条件：ESP32-S3 / ESP32-P4，PIE 汇编实现，`-O2`，Unity 测试框架。正变换参考值来自浮点 DFT，并使用与定点实现一致的半边频谱缩放。

| N_real | ESP32-S3 forward | ESP32-S3 inverse | ESP32-P4 forward | ESP32-P4 inverse |
|-------:|-----------------:|-----------------:|-----------------:|-----------------:|
|     32 |             9 us |            22 us |             3 us |             7 us |
|    512 |            40 us |            23 us |            21 us |            22 us |
|   1024 |            51 us |            36 us |            43 us |            44 us |

往返测试会将逆变换输出乘以 `N/4` 后与原始输入比较。在输入峰值为 10000 时，`N=32`、`N=512`、`N=1024` 的最大缩放误差分别为 10、181、528。

## 编译与测试

```bash
cd test_apps

idf.py set-target esp32p4
idf.py build flash monitor

idf.py set-target esp32s3
idf.py build flash monitor
```

## 与 ESP-DSP FFT 的区别

| 维度 | gmf_fft | ESP-DSP FFT |
|------|---------|-------------|
| 数据格式 | Q15 `int16_t` | `int16_t` 和 `float32` |
| 变换类型 | 实数 FFT / IFFT，输出半边频谱 | 通用 FFT 接口 |
| 核心实现 | 针对 ESP32-S3 / ESP32-P4 的 PIE 向量汇编 | 通用 DSP 实现和平台优化实现 |
| 输出布局 | `N/2 + 1` 个频点的紧凑半谱 | 取决于所用 ESP-DSP API |
| 目标场景 | 无 FPU、低延迟、确定时序的实时处理 | 通用信号处理 |
