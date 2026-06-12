# ESP Player 音视频播放

- [English Version](./README.md)
- 例程难度：⭐

## 例程简介

本示例演示如何使用 **ESP Player** 结合由 **esp-gmf** 提供的高级组件播放音视频：

- **esp_board_manager** 负责初始化 LCD、音频 DAC 与 microSD 卡。
- **esp_player** 负责解复用、解码、A/V 同步与播放控制。
- **esp_audio_render** 负责音频轨 PCM 输出到扬声器。
- **esp_video_render** 负责视频轨的显示处理，经 LCD backend 输出到屏幕。

### 典型场景

- 带 LCD 与扬声器的嵌入式设备上的本地 MP4 播放
- 验证 ESP Player 音视频链路（Extractor → Audio/Video Decoder → Render，含 A/V 同步）

### 支持的媒体格式

**本例程默认已启用**

| 格式 | 示例 | 说明 |
|------|------|------|
| MP4（H.264 + AAC，常见） | `.mp4` | `video_player_setup.h` 默认：`/sdcard/test.mp4` |

`sdkconfig.defaults` 默认启用 **MP4 提取器**、**H.264 视频软解**、**AAC 音频解码** 与 **M4A simple decoder**（容器内音频轨）。MP4 内为 PCM 等其它音频编码时，需在 menuconfig 中打开对应解码器。

**如需播放其他格式**

1. 执行 `idf.py menuconfig`。
2. 在 **Component config** 中：
   - **ESP Extractor** — 打开对应容器（例如 `AVI EXTRACTOR SUPPORT`）。
   - **ESP Video Codec** — 打开对应解码器（例如 `VIDEO DECODER SW MJPEG SUPPORT`）。
3. 如需更换 URL，编辑 `video_player_setup.h` 中的 `VIDEO_PLAYER_PLAY_URL`，保存后重新编译、烧录。

## 环境配置

### 硬件要求

- **开发板**：默认以 [ESP32-S3-Korvo-2](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2.html) 为例；亦可使用 [ESP32-P4-Function-EV-Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html) 等带 LCD 与 Audio DAC 的板型。
- **资源要求**：microSD 卡（FAT）、LCD 模块、Audio DAC、扬声器。
- 建议启用 PSRAM，以获得更稳定的解码与显示性能。

### 默认 IDF 分支

本例程支持 IDF release/v5.4 (>= v5.4.3) 与 release/v5.5 (>= v5.5.2) 分支。

### 软件要求

- 在 microSD 卡上放置与 `video_player_setup.h` 中 `VIDEO_PLAYER_PLAY_URL` 一致的 MP4 测试文件（默认 `/sdcard/test.mp4`，建议 H.264 + AAC）。

## 编译和下载

### 编译准备

编译本例程前需先确保已配置 ESP-IDF 环境；若已配置可跳过本段，直接进入工程目录。若未配置，请在 ESP-IDF 根目录运行以下脚本完成环境设置，完整步骤请参阅 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/index.html)。

```
./install.sh
. ./export.sh
```

下面是简略步骤：

- 进入本例程工程目录：

```
cd $YOUR_GMF_PATH/packages/esp_player/examples/video_player
```

本示例使用 [ESP Board Manager](https://github.com/espressif/esp-board-manager) 管理板级资源。推荐安装辅助工具 [`esp-bmgr-assist`](https://pypi.org/project/esp-bmgr-assist/) 作为默认入口。

- 在已激活的 ESP-IDF Python 环境下安装（同一环境只需安装一次）：

```bash
pip install esp-bmgr-assist
pip install --upgrade esp-bmgr-assist
```

- 查看支持的板子：

```bash
idf.py bmgr -l
```

  输出示例：

```text
ℹ️  Main Boards:
  [1] dual_eyes_board_v1_0
  [2] esp32_c3_lyra
  [3] esp32_c5_spot
  [4] esp32_p4_function_ev
  [5] esp32_s3_korvo2_v3
  [6] esp32_s3_korvo2l
  [7] esp_box_3
  [8] esp_box_lite
  [9] esp_hi
```

- 选择开发板：

```bash
idf.py bmgr -b <board_index|board_name>
```

  例如选择 `esp32_s3_korvo2_v3`：

```bash
idf.py bmgr -b 5
# 或
idf.py bmgr -b esp32_s3_korvo2_v3
```

  首次执行 `idf.py bmgr` 时，组件会根据本工程 `main/idf_component.yml` 中声明的 `espressif/esp_board_manager` 依赖自动下载。

> [!NOTE]
> 如果切换为其他 `esp_board_manager` 支持的开发板，请按相同步骤执行并替换板型名称/索引。
> 自定义开发板请参考 [自定义开发板指南](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/docs/how_to_customize_board_cn.md)。
> `esp_board_manager` 更多信息请参考 [ESP_BOARD_MANAGER 入门指南](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README_CN.md)

### 项目配置

编辑 `video_player_setup.h`：

- `VIDEO_PLAYER_PLAY_URL` — 媒体 URL（默认 `/sdcard/test.mp4`）
- `VIDEO_PLAYER_OUTPUT_VOLUME` — 播放音量（`0`–`100`，默认 `60`）

如需启用其它媒体格式，请通过 `idf.py menuconfig` 在 **Component config** 中配置（见上文 **支持的媒体格式**）。

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

- 上电后例程挂载 microSD 卡，初始化 LCD、codec 与 ESP Player，播放 `VIDEO_PLAYER_PLAY_URL` 指定的 MP4（音画同播），直至文件结束并释放资源。
- 默认演示为 SD 卡本地文件（`/sdcard/test.mp4`）。使用 HTTP/HTTPS 或 HLS 时，请先在 menuconfig 的 **ESP Player → Input IO sources** 中打开对应 IO，再修改 `VIDEO_PLAYER_PLAY_URL`，并参阅 [ESP Player 组件说明](../../README_CN.md)。

### 日志输出

- 正常流程会依次打印挂载 SD 卡、初始化媒体栈与 ESP Player、开始播放、等待结束并释放资源。关键步骤以 `[ 1 ]`～`[ 4 ]` 标出，并打印 `Playback started`、`Playback finished` 与 `video_player example finished`。

```c
I (853) main_task: Calling app_main()
I (864) VIDEO_PLAYER: [ 1 ] Mount SD card
I (1089) VIDEO_PLAYER: [ 2 ] Set up media stack, audio/video render, and ESP Player
I (1118) VIDEO_PLAYER: [ 3 ] Play /sdcard/test.mp4
I (1123) VIDEO_PLAYER: Playback started
I (1181) VIDEO_PLAYER: [ 4 ] Wait until playback finishes
I (45292) VIDEO_PLAYER: Playback finished
I (45416) VIDEO_PLAYER: video_player example finished
I (45420) main_task: Returned from app_main()
```

## 故障排除

### 无法打开文件

- 检查 SD 卡内文件名是否与 `video_player_setup.h` 中 `VIDEO_PLAYER_PLAY_URL` 一致，并确认 SD 卡已正确挂载（FAT）。

### video_player_setup failed / No display_lcd

- 确认所选板级 yaml 包含 **display_lcd** 设备，并已执行 `idf.py bmgr -b <board>` 选择带 LCD 的板型。

### 播放开始后很快报错（`ESP_PLAYER_EVENT_ERROR`）

**现象**：已看到 `Playback started`，随后很快出现 event 回调里的 ERROR 日志，并打印 `Video playback failed`。

**示例日志**：

```text
I (xxxx) VIDEO_PLAYER: Playback started
E (xxxx) ESP_PLAYER_PIPE_EVT: Extractor error
E (xxxx) ESP_PLAYER_EXTRACTOR: Parse stream error, ret: -1
E (xxxx) VIDEO_PLAYER: Playback error (error_source=1)
E (xxxx) VIDEO_PLAYER: Video playback failed
```

`error_source` 来自 `esp_player_types.h` 中的 `esp_player_error_source_t`，由 `ESP_PLAYER_EVENT_ERROR` 事件携带。本例程为 A/V 同步播放（`ESP_PLAYER_MASK_AV`），常见取值为：

| `error_source` | 枚举 | 含义 | 常见 TAG / 关键字 | 处理建议 |
|----------------|------|------|-------------------|----------|
| `1` | `ESP_PLAYER_ERROR_SOURCE_EXTRACTOR` | 解复用/读流失败 | `ESP_PLAYER_PIPE_EVT: Extractor error`；`ESP_PLAYER_EXTRACTOR: Open extractor error` / `Parse stream error` / `Read frame error`；`ESP_PLAYER: Invalid media URI` / `Unsupported format:` | 确认 SD 卡路径与 `VIDEO_PLAYER_PLAY_URL` 一致、MP4 未损坏；容器非 MP4 时参见 [支持的媒体格式](#支持的媒体格式) 并在 menuconfig 打开对应 **ESP Extractor** |
| `2` | `ESP_PLAYER_ERROR_SOURCE_AUDIO_DECODER` | 音频轨解码失败 | `ESP_PLAYER_PIPE_EVT: Audio decoder error`；`ESP_PLAYER_PIPELINE: Cannot create audio decoder` | MP4 已打开但音频轨无法解码：在 menuconfig 的 **ESP Audio Codec** 中启用对应解码器（默认 AAC；PCM 等需额外打开）；参见 [有画面无声音](#有画面无声音) |
| `3` | `ESP_PLAYER_ERROR_SOURCE_AUDIO_RENDER` | 音频渲染/输出失败 | `ESP_PLAYER_PIPE_EVT: Audio render error`；`ESP_PLAYER_AUDIO_RENDER: Open audio render stream error` / `Write audio render stream error`；`ESP_PLAYER_STATE: Failed to start audio renderer` | 检查 `idf.py bmgr -b <board>`、板级 Audio DAC 与 `VIDEO_PLAYER_OUTPUT_VOLUME`（参见 [有画面无声音](#有画面无声音)） |
| `4` | `ESP_PLAYER_ERROR_SOURCE_VIDEO_DECODER` | 视频轨解码失败 | `ESP_PLAYER_PIPE_EVT: Video decoder error`；`ESP_PLAYER_PIPELINE: video_decoder: esp_gmf_pipeline_report_info(VIDEO) failed` | 在 menuconfig 的 **ESP Video Codec** 中启用与 MP4 视频编码匹配的解码器（默认 H.264 软解）；参见 [支持的媒体格式](#支持的媒体格式) |
| `5` | `ESP_PLAYER_ERROR_SOURCE_VIDEO_RENDER` | 视频显示/渲染失败 | `ESP_PLAYER_PIPE_EVT: Video render error`；`ESP_PLAYER_VIDEO_RENDER: Failed to open video render stream` / `Invalid configuration or render handle`；`VIDEO_PLAYER_SETUP: Failed to set LCD display backend` | 确认板级含 display_lcd 且已 `idf.py bmgr -b <board>`（见 [video_player_setup failed / No display_lcd](#video_player_setup-failed--no-display_lcd)）；RGB/DSI 板型建议启用 PSRAM（见 [有画面但卡顿](#有画面但卡顿) 及各 target 的 `sdkconfig.defaults.*`） |

**排查顺序**：

1. 在 monitor 中搜索 `Playback error (error_source=`，记下 `error_source` 数值。
2. 向上查找同一时间段内、上表中对应 TAG 的 `E (...)` 行，定位具体失败点。
3. 按上表处理；若涉及格式支持，参见 [支持的媒体格式](#支持的媒体格式) 打开 extractor / decoder 的 `CONFIG_*` 后重新编译。

> **说明**：若未出现 `Playback started`，而是 `Playback start failed`，属于 `esp_player_set_data_src()` / `esp_player_run()` 同步返回失败，请优先检查 URL 与 SD 卡挂载（见 [无法打开文件](#无法打开文件)），而非上表中的 runtime `error_source`。若在 `[ 2 ]` 阶段即出现 `video_player_setup failed`，属于 LCD / render 初始化失败，见 [video_player_setup failed / No display_lcd](#video_player_setup-failed--no-display_lcd)。

### 有画面无声音

- 确认板级含 Audio DAC、`video_player_setup.h` 中 `VIDEO_PLAYER_OUTPUT_VOLUME` 合理，且 MP4 音频轨为已启用的编码（默认 AAC）；其它编码需在 menuconfig 打开对应音频解码器。

### 有画面但卡顿

- 确认已启用 PSRAM，或改用较低分辨率 / 码率的 MP4 文件。

### board_manager 编译或选板失败

- 在已激活的 IDF Python 环境中安装 `esp-bmgr-assist`，执行 `idf.py bmgr -b <board>` 后重新编译。请参考 [esp-bmgr-assist](https://docs.espressif.com/projects/esp-board-manager/zh_CN/latest/tools/esp-bmgr-assist.html#)。

### 相关参考

- 组件 API、URL 与进阶用法：[ESP Player 组件说明](../../README_CN.md)

## 技术支持

请按照下面的链接获取技术支持：

- 技术支持参见 [esp32.com](https://esp32.com/viewforum.php?f=20) 论坛
- 问题反馈与功能需求，请创建 [GitHub issue](https://github.com/espressif/esp-gmf/issues)

我们会尽快回复。
