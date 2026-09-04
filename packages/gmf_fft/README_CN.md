# GMF FFT

- [![组件注册表](https://components.espressif.com/components/espressif/gmf_fft/badge.svg)](https://components.espressif.com/components/espressif/gmf_fft)
- [English](./README.md)

`gmf_fft` 是面向 ESP-IDF 的定点 Q15 实数 FFT / IFFT 组件，支持 ESP 系列芯片的 FFT 与 IFFT 处理。实现包含通用 C 版本及针对 Xtensa / RISC-V 的 PIE 向量汇编优化，在典型长度下较纯软件 DIT 延迟更低；同时利用实信号的 Hermitian 共轭对称特性，在正变换时仅保留 `N/2 + 1` 个频点，从而降低频域存储与计算开销，适用于无 FPU 或对时延敏感的嵌入式场景。

## 功能特性

- 支持 32 到 8192 点、长度为 2 的幂的实数 FFT / IFFT。PIE 目标上硬件 bit-reverse 最多 10 bit（实数 `N` 不超过 2048，对应 `ESP.FFT.BITREV` / `EE.BITREV`）；`N=4096` 和 `N=8192` 回退到软件 bit-reverse，蝴蝶结仍用 PIE。
- 使用 Q15 `int16_t` 数据格式，减少运算和存储开销。
- 同一 `handle` 可被多个线程共享，前提是不同线程使用各自的 `data` 缓冲区。
- 可选 HP 接口（`esp_gmf_fft_forward_hp` / `inverse_hp`）：块浮点在库内完成。逆变换输出为 Q15，不必再乘 `N/4`。

## 支持目标

- v0.1.0 支持 ESP32-S3 和 ESP32-P4。
- v1.0.0 支持 所有ESP32系列芯片。

## 自 v1.1.0 起的不兼容变更

- ESP32-S31 新增 `CONFIG_GMF_FFT_S31_USE_ASM`（默认 `n`），可在 PIE 汇编与纯 C 实现之间切换。ESP32-S31 的 PIE 仅在核 1 上可用，因此提供该开关，便于在其他核上调用 FFT/IFFT。
  - 开启：相关 FFT/IFFT API 必须在 **核 1** 上调用。
  - 关闭：可在任意核上运行，性能低于 PIE 汇编，详见下方性能表。

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

正变换输入为 `N` 个实数采样，`data` 缓冲区需要预留 `ESP_GMF_FFT_BUFFER_SIZE(N)` 个 `int16_t` 元素。两套接口的输出布局相同：`data[0]` 存放 DC 实部，`data[1]` 存放 Nyquist 实部，`data[2k]` 和 `data[2k + 1]` 分别存放第 `k` 个频点的实部和虚部。

同一 `handle` 可用于下面两套接口。不要交叉调用：`inverse` 只接 `forward` 的半谱，`inverse_hp` 只接同一 `handle` 上 `forward_hp` 的半谱。

| 用法 | `esp_gmf_fft_forward` / `inverse` | `esp_gmf_fft_forward_hp` / `inverse_hp` |
|------|-----------------------------------|-----------------------------------------|
| 调用参数 | `handle`、`data` | 相同 |
| 每级缩放 | 固定 `>>1` | 仅当 `max_abs >= 8192` 时 `>>1` |
| 往返幅度 | 约为 `4/N`，需自行乘 `N/4` | 输出为 Q15，近似原始输入 |
| 适用 | 更快，适合看稀疏谱峰 | 更高 SNR，适合还原时域 |

HP 示例（不必再乘 `N/4`）：

```c
esp_gmf_fft_forward_hp(handle, data);
esp_gmf_fft_inverse_hp(handle, data);
/* data 为 Q15，近似原始输入 */
```

## 内存开销

- 用户数据缓冲区：`ESP_GMF_FFT_BUFFER_SIZE(N)` 个 `int16_t`，约 `2 * (N + 2)` 字节。
- `handle` 内部旋转因子表：约 `3 * N` 字节，外加少量 plan 结构体开销。

## 精度与性能

测试条件：`-O2`，Unity 测试框架。正变换参考值来自浮点 DFT，并使用与定点实现一致的半边频谱缩放。ESP32-S3 / ESP32-P4 为 PIE 汇编实现；ESP32-S31 分别测试纯 C（`GMF_FFT_S31_USE_ASM=n`）与 PIE 汇编（`GMF_FFT_S31_USE_ASM=y`）。HP 列为 `esp_gmf_fft_forward_hp` / `inverse_hp` 单次调用耗时。HP 逆变换耗时包含把剩余 BFP 位移回折 Q15。

| N_real | ESP32-S3 forward | ESP32-S3 inverse | ESP32-S3 HP forward | ESP32-S3 HP inverse | ESP32-P4 forward | ESP32-P4 inverse | ESP32-P4 HP forward | ESP32-P4 HP inverse |
|-------:|-----------------:|-----------------:|--------------------:|--------------------:|-----------------:|-----------------:|--------------------:|--------------------:|
|     32 |            17 us |            21 us |              77 us |              33 us |             6 us |             2 us |               4 us |               8 us |
|    512 |            22 us |            23 us |              95 us |              76 us |             8 us |             8 us |              27 us |              24 us |
|   1024 |            28 us |            36 us |             148 us |             124 us |            16 us |            16 us |              53 us |              45 us |

| N_real | ESP32-S31 C forward | ESP32-S31 C inverse | ESP32-S31 C HP forward | ESP32-S31 C HP inverse | ESP32-S31 PIE forward | ESP32-S31 PIE inverse | ESP32-S31 PIE HP forward | ESP32-S31 PIE HP inverse |
|-------:|--------------------:|--------------------:|-----------------------:|-----------------------:|----------------------:|----------------------:|-------------------------:|-------------------------:|
|     32 |              21 us |              12 us |                 20 us |                 36 us |                  7 us |                  2 us |                   16 us |                    9 us |
|    512 |             277 us |             271 us |                491 us |                507 us |                 10 us |                 10 us |                   44 us |                   32 us |
|   1024 |             601 us |             595 us |               1085 us |               1124 us |                 21 us |                 21 us |                   78 us |                   60 us |

普通接口往返会将逆变换输出乘以 `N/4` 再比较。输入峰值 10000 时，`N=32` / `512` / `1024` 的最大缩放误差：ESP32-P4 与 ESP32-S31 PIE 为 6 / 115 / 200，ESP32-S3 与 ESP32-S31 C 为 11 / 183 / 528。HP 接口按 Q15 比较、不必乘 `N/4`；最大误差：P4 与 S31 PIE 为 2 / 4 / 4，S3 为 4 / 39 / 78，S31 C 为 4 / 58 / 96。

## 与 dl_fft / ESP-DSP 的性能对比（ESP32-P4）

测试板：ESP32-P4 rev v3.2，400 MHz，ESP-IDF v6.1，`-O2`。金标准为 `x/32768` 的未归一化双精度实 DFT。正变换 SNR 做最小二乘增益匹配。往返 SNR：HP 接口按 Q15 比较，`gmf_fft` 乘 `N/4`。激励：cosine（bin 8，幅度 10000）、two_tone（8000+4000）、random（约 0.25 FS）。耗时为含 memcpy 的 20 次平均。`dsps_fft2r_sc16` 为 ESP-DSP 的 N 点复数 Q15（虚部填 0）。

```bash
cd examples/fft_p4_compare_dl
idf.py set-target esp32p4
idf.py -p PORT flash
# 等到 FFT_COMPARE_DONE
```

完整表格见 [`examples/fft_p4_compare_dl/TEST_REPORT_CN.md`](examples/fft_p4_compare_dl/TEST_REPORT_CN.md)。

**N=1024 精度（正变换 SNR / 往返 SNR，dB）**

| 信号 | `gmf_fft_hp` | `dl_rfft_s16_hp` | `dsps_fft2r_sc16` |
|------|----------------|------------------|-------------------|
| cosine | 82.28 / **76.60** | **84.66** / 73.18 | 70.78 / 17.20 |
| two_tone | **78.80 / 74.64** | 77.91 / 73.44 | 68.30 / 14.65 |
| random | **69.46 / 67.64** | 67.10 / 65.59 | 19.55 / 14.57 |

**耗时（正 / 逆，µs，cosine）**

| N | `gmf_fft` | `gmf_fft_hp` | `dl_rfft_s16_hp` | `dsps_fft2r_sc16` |
|---|-----------|----------------|------------------|-------------------|
| 128 | **2 / 2** | 7 / 7 | 7 / 7 | 10 / 12 |
| 256 | **4 / 4** | **12 / 12** | 14 / 14 | 21 / 25 |
| 512 | **8 / 9** | **23 / 23** | 28 / 28 | 44 / 51 |
| 1024 | **17 / 18** | **50 / 47** | 57 / 57 | 91 / 104 |

N=1024 上 `gmf_fft` 仍是最快（约 17/18 µs）。`gmf_fft_hp` 约 50/47 µs（正/逆都约为 `dl_rfft_s16_hp` 的 0.88×）。逆变换用 PIE 把剩余 BFP 位移回折 Q15；稀疏 cosine 逆变换 47 µs，快于 `dl_rfft_s16_hp` 的 57 µs。宽带 random 逆变换仍约 43 µs。

## 编译与测试

```bash
cd test_apps

idf.py set-target esp32p4
idf.py build flash monitor

idf.py set-target esp32s3
idf.py build flash monitor
```
