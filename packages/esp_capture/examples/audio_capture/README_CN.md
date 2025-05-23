# ESP 音频采集示例

本示例展示了使用 ESP Capture 框架的各种音频采集功能。它包含了多种音频采集场景，包括基础采集、回声消除（AEC）、文件录制和自定义音频处理等。

## 功能特性

- 可配置时长的基础音频采集
- 支持回声消除（AEC）的音频采集（仅支持 ESP32-S3 和 ESP32-P4）
- 支持 MP4 文件录制的音频采集
- 自定义处理流水线（包括自动电平控制 ALC）

## 硬件要求

- 推荐使用 [ESP32-S3-Korvo2](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2.html) 或 [esp32-p4-function-ev-board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html) 开发板
- 对于其他开发板，请参考 [其他开发板支持](#其他开发板支持) 小节

## 软件要求

- ESP-IDF v5.4 或更高版本
- ESP Capture 框架
- ESP GMF 框架

## 构建和烧录

```bash
idf.py -p /dev/XXXXX flash monitor
```

## 使用示例

### 基础音频采集

基础音频采集示例展示了简单的音频采集功能。它可以在指定时长内采集和处理音频帧。

```c
audio_capture_run(duration_ms);
```

### 带回声消除的音频采集

本示例启用了回声消除（AEC）功能，适用于麦克风和扬声器同时工作的场景，可以显著提升音频质量。AEC 功能仅在 ESP32-S3 和 ESP32-P4 上支持。

```c
audio_capture_run_with_aec(duration_ms);
```

### 带文件录制的音频采集

本示例展示了如何采集音频并保存为 SD 卡上的 MP4 文件。录制会根据配置的时长自动分片。

```c
audio_capture_run_with_muxer(duration_ms);
```

录制的文件将保存为 `/sdcard/cap_X.mp4`，其中 X 为分片索引。

### 自定义处理流水线

本示例展示了如何通过以下方式自定义音频处理流水线：
1. 向采集流水线添加自定义元素
2. 配置元素参数
3. 通过连接关系构建自定义处理流水线

```c
audio_capture_run_with_customized_process(duration_ms);
```

## 配置说明

本示例可以通过 [settings.h](main/settings.h) 中的以下设置进行配置：

- `AUDIO_CAPTURE_FORMAT`：音频格式（如 AAC、OPUS）
- `AUDIO_CAPTURE_SAMPLE_RATE`：采样率（如 16000 Hz）
- `AUDIO_CAPTURE_CHANNEL`：音频通道数

## 其他开发板支持

本示例使用 `gmf_app_utils` 进行简化的开发板初始化，以实现快速验证。您可以在菜单配置中的 `GMF APP Configuration` 下检查开发板兼容性。有关其他开发板支持的详细信息，请参阅 [gmf_app_utils 文档](https://github.com/espressif/esp-gmf/blob/main/packages/gmf_app_utils/README.md)。

或者，您也可以使用 [esp-bsp](https://github.com/espressif/esp-bsp/tree/master) 的 API 作为替代方案：
- 使用 `bsp_audio_codec_microphone_init()` 替代 `esp_gmf_app_get_record_handle()`

