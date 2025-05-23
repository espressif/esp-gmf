# ESP 视频采集示例

本示例展示了使用 ESP Capture 框架的各种视频采集功能。它包含了多种视频采集场景，包括基础采集、单帧采集、文件录制、叠加层和双路采集等。

## 功能特性

- 可配置时长的基础视频采集
- 单帧视频采集模式
- 支持 MP4 文件录制的视频采集
- 支持文本叠加的视频采集
- 双路视频采集（多格式输出）
- 自定义处理流水线
- 可配置的任务调度

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

### 基础视频采集

基础视频采集示例展示了简单的视频采集功能。它可以同时采集指定时长的视频和音频。

```c
video_capture_run(duration_ms);
```

### 单帧视频采集

本示例展示了如何在单帧模式下采集视频，适用于需要以特定间隔采集帧的场景。它每 500ms 采集一帧。

```c
video_capture_run_one_shot(duration_ms);
```

### 带文件录制的视频采集

本示例展示了如何采集视频和音频，并将其保存为 SD 卡上的 MP4 文件。录制会根据配置的时长自动分片。

```c
video_capture_run_with_muxer(duration_ms);
```

录制的文件将保存为 `/sdcard/vid_X.mp4`，其中 X 为分片索引。

### 带叠加层的视频采集

本示例展示了如何为视频流添加文本叠加层。它演示了：
1. 创建和配置文本叠加层
2. 动态更新叠加层内容
3. 管理叠加层区域和颜色

```c
video_capture_run_with_overlay(duration_ms);
```

### 双路视频采集

本示例展示了如何同时以两种不同格式采集视频。适用于需要多种输出格式的场景（例如，一个用于录制，一个用于流媒体）。

```c
video_capture_run_dual_path(duration_ms);
```

### 自定义处理流水线

本示例展示了如何通过以下方式自定义视频处理流水线：
1. 向采集流水线添加自定义元素
2. 配置元素参数
3. 通过连接关系构建自定义处理流水线

```c
video_capture_run_with_customized_process(duration_ms);
```

## 任务调度配置

本示例包含可配置的任务调度器，允许您优化不同采集组件的性能。[main.c](main/main.c) 中的 `capture_test_scheduler` 调度器配置演示了如何：

1. 为不同任务设置栈大小
2. 将任务分配到特定的 CPU 核心
3. 设置任务优先级
4. 优化资源使用

## 配置说明

本示例可以通过 [settings.h](main/settings.h) 中的以下设置进行配置：

- 视频格式和分辨率设置：
  - `VIDEO_SINK0_FMT`：主视频格式
  - `VIDEO_SINK0_WIDTH`：主视频宽度
  - `VIDEO_SINK0_HEIGHT`：主视频高度
  - `VIDEO_SINK0_FPS`：主视频帧率
- 音频设置：
  - `AUDIO_SINK0_FMT`：主音频格式
  - `AUDIO_SINK0_SAMPLE_RATE`：主音频采样率
  - `AUDIO_SINK0_CHANNEL`：主音频通道数
- 第二路设置（用于双路采集）：
  - `VIDEO_SINK1_FMT`：第二路视频格式
  - `VIDEO_SINK1_WIDTH`：第二路视频宽度
  - `VIDEO_SINK1_HEIGHT`：第二路视频高度
  - `VIDEO_SINK1_FPS`：第二路视频帧率

## 其他开发板支持
本示例使用 `gmf_app_utils` 进行简化的开发板初始化，以实现快速验证。您可以在菜单配置中的 `GMF APP Configuration` 下检查开发板兼容性。有关其他开发板支持的详细信息，请参阅 [gmf_app_utils 文档](https://github.com/espressif/esp-gmf/blob/main/packages/gmf_app_utils/README.md)。

或者，您也可以使用 [esp-bsp](https://github.com/espressif/esp-bsp/tree/master) 的 API 作为替代方案：
- 使用 `bsp_audio_codec_microphone_init()` 替代 `esp_gmf_app_get_record_handle()`
- 使用 `bsp_sdcard_mount()` 替代 `esp_gmf_app_setup_sdcard()`
- 使用 `BSP_CAMERA_DEFAULT_CONFIG` 替代 `get_camera_cfg()`

## 故障排除

1. 如果视频采集无法启动：
   - 检查摄像头是否正确初始化
   - 验证摄像头连接
   - 检查视频格式和分辨率设置

2. 如果音频采集失败：
   - 检查音频编解码器是否正确初始化
   - 验证麦克风连接
   - 检查音频格式和采样率设置

3. 如果文件录制失败：
   - 验证 SD 卡是否正确挂载
   - 检查 SD 卡是否有足够的可用空间

4. 如果出现性能问题：
   - 检查任务调度器配置
   - 调整任务优先级和核心分配
   - 监控栈使用情况并在必要时调整
