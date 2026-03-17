# ESP 音频采集示例

- [English Version](./README.md)
- 例程难度：⭐⭐

## 例程简介

- 本例程演示基于 `esp_capture` 的常见音频采集能力。
- 包含基础采集、AEC 采集、MP4 录制和自定义处理流水线等场景。

### 典型场景

- 固定时长麦克风采集。
- 双工场景下通过 AEC 提升音频质量。
- 将采集数据分片录制到 SD 卡 MP4 文件。
- 在采集图中扩展自定义处理元素（如 ALC）。

## 环境配置

### 硬件要求

- 推荐开发板：[ESP32-S3-Korvo2](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2.html) 或 [ESP32-P4-Function-EV-Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html)
- 音频输入设备（ADC 麦克风）
- SD 卡（可选，保存录制文件需要）

### 默认 IDF 分支

本例程支持 IDF `release/v5.4` (>= v5.4.3) 和 `release/v5.5` (>= v5.5.2)。

## 编译和下载

### 编译准备

```bash
cd $YOUR_GMF_PATH/packages/esp_capture/examples/audio_capture
idf.py gen-bmgr-config -l
idf.py gen-bmgr-config -b esp32_s3_korvo2_v3
```

> [!NOTE]
> 如果切换为其他 `esp_board_manager` 支持的开发板，请按相同步骤执行并替换板型名称。
> 自定义开发板请参考 [自定义开发板指南](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/docs/how_to_customize_board_cn.md)。

### 编译与烧录

```bash
idf.py build
idf.py -p PORT flash monitor
```

## 如何使用例程

### 流程介绍

```mermaid
flowchart LR
  MIC[ADC 麦克风] --> CAP[esp_capture 音频源]
  CAP --> PROC[可选处理 / AEC / 自定义元素]
  PROC --> ENC[音频编码器]
  ENC --> USE[回调或输出 sink]
  ENC --> MUX[可选 MP4 复用]
  MUX --> SD[/sdcard/cap_X.mp4]
```

### 功能和用法

`app_main()` 会按顺序执行以下 case（每个 10 秒）：

1. `audio_capture_run`
2. `audio_capture_run_with_aec`
3. `audio_capture_run_with_muxer`（仅在 SD 卡挂载成功时执行）
4. `audio_capture_run_with_customized_process`

其他关键行为：

- 注册默认音频编码器（`esp_audio_enc_register_default`）
- 注册 MP4 复用器（`mp4_muxer_register`）
- 通过 `esp_capture_set_thread_scheduler` 设置线程调度参数

### 配置说明

`main/settings.h` 主要配置项：

- `AUDIO_CAPTURE_FORMAT`（默认：`ESP_CAPTURE_FMT_ID_AAC`）
- `AUDIO_CAPTURE_SAMPLE_RATE`（默认：`16000`）
- `AUDIO_CAPTURE_CHANNEL`（默认：`2`）

## 故障排除

### 采集启动失败

- 确认音频 ADC 设备初始化成功
- 确认板级音频输入硬件连接正常

### 未生成录制文件

- 确认 `app_main()` 中 SD 卡挂载成功
- 检查 SD 卡剩余空间和写权限

### AEC 分支不可用或效果不佳

- AEC 受芯片目标限制（主要支持 ESP32-S3 / ESP32-P4）
- 确认板级播放与采集路径配置正确

## 技术支持

- 技术支持论坛：[esp32.com](https://esp32.com/viewforum.php?f=20)
- 问题反馈与功能建议：[GitHub issue](https://github.com/espressif/esp-gmf/issues)
