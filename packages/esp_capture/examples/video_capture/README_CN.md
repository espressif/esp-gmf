# ESP 视频采集示例

- [English Version](./README.md)
- 例程难度：⭐⭐⭐

## 例程简介

- 本例程演示基于 `esp_capture` 的多场景视频采集能力
- 覆盖基础采集、单帧采集、叠加层、MP4 录制、自定义处理和双路输出等能力

### 典型场景

- 验证摄像头和麦克风同步采集
- 将音视频流分片录制到 SD 卡 MP4 文件
- 在视频流上动态添加文字叠加
- 同时输出两路不同格式视频用于录制与预览等场景

## 环境配置

### 硬件要求

- 推荐开发板：[ESP32-S3-Korvo2](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2.html) 或 [ESP32-P4-Function-EV-Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html)
- 摄像头模块和音频输入设备
- SD 卡（可选，保存录制文件需要）

### 默认 IDF 分支

本例程支持 IDF `release/v5.4` (>= v5.4.3) 和 `release/v5.5` (>= v5.5.2)。

## 编译和下载

### 编译准备

```bash
cd $YOUR_GMF_PATH/packages/esp_capture/examples/video_capture
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
  CAM[Camera 输入] --> VSRC[视频采集源]
  MIC[ADC 麦克风] --> ASRC[音频采集源]
  VSRC --> VPROC[叠加层 / 自定义处理]
  VPROC --> VENC[视频编码]
  ASRC --> AENC[音频编码]
  VENC --> SINK0[主视频输出]
  AENC --> SINKA[主音频输出]
  VENC --> SINK1[第二路视频输出 可选]
  SINK0 --> MUX[可选 MP4 复用]
  SINKA --> MUX
  MUX --> SD[/sdcard/vid_X.mp4]
```

### 功能和用法

`app_main()` 会按顺序执行以下 case（每个 10 秒）：

1. `video_capture_run`
2. `video_capture_run_one_shot`
3. `video_capture_run_with_overlay`
4. `video_capture_run_with_muxer`（仅在 SD 卡挂载成功时执行）
5. `video_capture_run_with_customized_process`
6. `video_capture_run_dual_path`

其他关键行为：

- 注册默认音频/视频编码器和 MP4 复用器
- 通过 `esp_capture_set_thread_scheduler` 设置线程调度参数

### 配置说明

`main/settings.h` 主要配置项：

- 主视频输出：`VIDEO_SINK0_FMT`、`VIDEO_SINK0_WIDTH`、`VIDEO_SINK0_HEIGHT`、`VIDEO_SINK0_FPS`
- 主音频输出：`AUDIO_SINK0_FMT`、`AUDIO_SINK0_SAMPLE_RATE`、`AUDIO_SINK0_CHANNEL`
- 双路视频输出：`VIDEO_SINK1_FMT`、`VIDEO_SINK1_WIDTH`、`VIDEO_SINK1_HEIGHT`、`VIDEO_SINK1_FPS`
- 可选第二路音频：`AUDIO_SINK1_FMT`、`AUDIO_SINK1_SAMPLE_RATE`、`AUDIO_SINK1_CHANNEL`

## 故障排除

### 视频采集无法启动

- 确认摄像头设备初始化成功
- 检查摄像头硬件连接与传感器可用性
- 检查格式、分辨率和帧率组合是否匹配

### 音视频模式下音频失败

- 确认音频 ADC 初始化成功
- 检查麦克风路径和音频 sink 参数

### 未生成 MP4 文件

- 确认 SD 卡挂载成功
- 检查存储空间和写权限

### 性能不稳定

- 在 `capture_test_scheduler` 中调整线程调度参数
- 适当降低分辨率/帧率或使用更轻量编码格式

## 技术支持

- 技术支持论坛：[esp32.com](https://esp32.com/viewforum.php?f=20)
- 问题反馈与功能建议：[GitHub issue](https://github.com/espressif/esp-gmf/issues)
