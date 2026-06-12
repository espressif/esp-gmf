# ESP Player 音频播放

- [English Version](./README.md)
- 例程难度：⭐

## 例程简介

本示例演示如何使用 **ESP Player** 结合由 **esp-gmf** 提供的高级组件播放音频：

- **esp_board_manager** 负责初始化 codec 与 microSD 卡等板级外设。
- **esp_player** 负责解复用、解码、A/V 同步与播放控制。
- **esp_audio_render** 负责解码后 PCM 的混音与输出，经回调写入板载 Audio DAC。
- 默认单路播放（`AUDIO_PLAYER_STREAM_NUM` = 1）；可在 `audio_player_setup.h` 中设为 `2`–`4`，由多路 ESP Player 实例混音到同一 DAC。

### 典型场景

- 音频产品上的音乐播放
- 嵌入式设备上的提示音、通知音等解码后播放

### 支持的媒体格式

**本例程默认已启用**

| 格式 | 示例 | 说明 |
|------|------|------|
| MP3 | `.mp3`（裸 ES 流） | `audio_player_setup.h` 默认：`/sdcard/test.mp3` |
| AAC | `.aac`（裸 ES 流） | 修改 `audio_player_setup.h` 中的 `audio_player_play_urls[]` |

`sdkconfig.defaults` 中默认只打开 ES 裸流提取器，以及 MP3、AAC 解码器。`.m4a`、`.wav`、`.mp4` 等容器格式默认未启用。

**如需播放其他格式**

1. 执行 `idf.py menuconfig`。
2. 在 **Component config** 中：
   - **ESP Audio Codec** — 打开所需解码器（例如 `AUDIO DECODER FLAC SUPPORT`、`AUDIO DECODER PCM SUPPORT`）。
   - **ESP Extractor** — 打开对应容器/提取器（例如 `.m4a`/`.mp4` 需 `MP4 EXTRACTOR SUPPORT`，`.wav` 需 `WAV EXTRACTOR SUPPORT`）。
3. 在 `audio_player_setup.h` 中配置 `AUDIO_PLAYER_STREAM_NUM` 与 `audio_player_play_urls[]`，保存后重新编译、烧录。

为了内存优化，此示例仅启用 ES8311 编解码器，如果您的板卡使用的是其他编解码器，请在 **Component config → Audio Codec Device Configuration** 中打开对应芯片驱动。

## 环境配置

### 硬件要求

- **开发板**：默认以 [ESP32-S3-Korvo-2](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2.html) 为例，其他支持 `esp_board_manager` 且具备 Audio DAC / 扬声器的 ESP 音频板同样适用。
- **资源要求**：microSD 卡（FAT，默认演示）、Audio DAC、扬声器。

### 默认 IDF 分支

本例程支持 IDF release/v5.4 (>= v5.4.3) 与 release/v5.5 (>= v5.5.2) 分支。

### 软件要求

- 在 microSD 卡上放置与 `audio_player_setup.h` 中 `audio_player_play_urls[]` 一致的测试文件（默认 `/sdcard/test.mp3`）。使用网络流时无需 SD 卡，但需正确配置 Wi-Fi 等网络环境。

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
cd $YOUR_GMF_PATH/packages/esp_player/examples/audio_player
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

编辑 `audio_player_setup.h`：

- `AUDIO_PLAYER_STREAM_NUM` — 混音路数（`1`–`4`）
- `audio_player_play_urls[]` — 每路一条 URL（索引 `0` … `AUDIO_PLAYER_STREAM_NUM - 1`）
- `AUDIO_PLAYER_OUTPUT_VOLUME` — 播放音量（`0`–`100`，默认 `60`）

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

- 上电后例程挂载 microSD 卡，初始化 codec 与 ESP Player，播放 `audio_player_play_urls[]` 中的 URL，直至当前曲目结束并释放资源。
- 默认演示为 SD 卡本地文件（`/sdcard/test.mp3`）。使用 HTTP/HTTPS 或 HLS 时，请先在 menuconfig 的 **ESP Player → Input IO sources** 中打开对应 IO，再修改 `audio_player_play_urls[]`，并参阅 [ESP Player 组件说明](../../README_CN.md) 中的 URL 语法。
- 多路混音时在 `audio_player_setup.h` 将 `AUDIO_PLAYER_STREAM_NUM` 设为 `2`–`4`，并配置 `audio_player_play_urls[]` 的前 N 项；各路独立播放，混音输出到同一 DAC。

### 日志输出

- 正常流程会依次打印挂载 SD 卡、初始化媒体栈与 ESP Player、开始播放、等待结束并释放资源。关键步骤以 `[ 1 ]`～`[ 4 ]` 标出，并打印 `Playback started`、`Playback finished` 与 `audio_player example finished`。

```c
I (853) main_task: Calling app_main()
I (864) AUDIO_PLAYER: [ 1 ] Mount SD card
I (1089) AUDIO_PLAYER: [ 2 ] Set up media stack, audio render, and ESP Player
I (1118) AUDIO_PLAYER: [ 3 ] Play /sdcard/test.mp3
I (1123) AUDIO_PLAYER: Playback started
I (1181) AUDIO_PLAYER: [ 4 ] Wait until playback finishes
I (11292) AUDIO_PLAYER: Playback finished
I (11416) AUDIO_PLAYER: audio_player example finished
I (11420) main_task: Returned from app_main()
```

## 故障排除

### 无法打开文件或流

- 检查 SD 卡内文件名是否与 `audio_player_setup.h` 中 `audio_player_play_urls[]` 一致（本地文件），或网络是否可用（HTTP/HTTPS）。

### 播放开始后很快报错（`ESP_PLAYER_EVENT_ERROR`）

**现象**：已看到 `[stream N] playback started`，随后很快出现 event 回调里的 ERROR 日志，并打印 `Audio playback failed`。

**示例日志**（以 stream 0 为例）：

```text
I (xxxx) AUDIO_PLAYER: [stream 0] playback started
E (xxxx) ESP_PLAYER_PIPE_EVT: Extractor error
E (xxxx) ESP_PLAYER_EXTRACTOR: Open extractor error, ret: -1
E (xxxx) AUDIO_PLAYER: [stream 0] playback error (error_source=1)
E (xxxx) AUDIO_PLAYER: Audio playback failed
```

`error_source` 来自 `esp_player_types.h` 中的 `esp_player_error_source_t`，由 `ESP_PLAYER_EVENT_ERROR` 事件携带。本例程为纯音频播放，常见取值为：

| `error_source` | 枚举 | 含义 | 常见 TAG / 关键字 | 处理建议 |
|----------------|------|------|-------------------|----------|
| `1` | `ESP_PLAYER_ERROR_SOURCE_EXTRACTOR` | 解复用/读流失败 | `ESP_PLAYER_PIPE_EVT: Extractor error`；`ESP_PLAYER_EXTRACTOR: Open extractor error` / `Parse stream error` / `Read frame error`；`ESP_PLAYER: Invalid media URI` / `Unsupported format:` | 确认 SD 卡路径与 `audio_player_play_urls[]` 一致、文件未损坏；容器格式（如 `.m4a`/`.mp4`/`.wav`）见 [支持的媒体格式](#支持的媒体格式) 并在 menuconfig 打开对应 **ESP Extractor**；扩展名与文件内容不符时也会在此阶段失败 |
| `2` | `ESP_PLAYER_ERROR_SOURCE_AUDIO_DECODER` | 音频解码失败 | `ESP_PLAYER_PIPE_EVT: Audio decoder error`；`ESP_PLAYER_PIPELINE: Cannot create audio decoder` | 容器已打开但码流无法解码：在 menuconfig 的 **ESP Audio Codec** 中启用对应解码器（参见 [支持的媒体格式](#支持的媒体格式)）；确认文件编码与 URL 扩展名一致 |
| `3` | `ESP_PLAYER_ERROR_SOURCE_AUDIO_RENDER` | 音频渲染/输出失败 | `ESP_PLAYER_PIPE_EVT: Audio render error`；`ESP_PLAYER_AUDIO_RENDER: Open audio render stream error` / `Write audio render stream error`；`ESP_PLAYER_STATE: Failed to start audio renderer` | 检查是否已执行 `idf.py bmgr -b <board>`、板级 Audio DAC 配置（参见 [无声音且无报错](#无声音且无报错)）；多路混音时确认 `AUDIO_PLAYER_STREAM_NUM` 未超过 render 支持的路数 |

**排查顺序**：

1. 在 monitor 中搜索 `playback error (error_source=`，记下 `error_source` 数值。
2. 向上查找同一时间段内、上表中对应 TAG 的 `E (...)` 行，定位具体失败点。
3. 按上表处理；若涉及格式支持，参见 [支持的媒体格式](#支持的媒体格式) 打开 extractor / decoder 的 `CONFIG_*` 后重新编译。

> **说明**：若未出现 `playback started`，而是 `stream N start failed`，属于 `esp_player_set_data_src()` / `esp_player_run()` 同步返回失败，请优先检查 URL 与 SD 卡挂载（见 [无法打开文件或流](#无法打开文件或流)），而非上表中的 runtime `error_source`。

### 无声音且无报错

- 确认已执行 `idf.py bmgr -b <board>`，`audio_player_setup.h` 中 `AUDIO_PLAYER_OUTPUT_VOLUME` 合理，并检查 DAC / 功放硬件与板级 yaml 中的 codec 配置。

### board_manager 编译或选板失败

- 在已激活的 IDF Python 环境中安装 `esp-bmgr-assist`，执行 `idf.py bmgr -b <board>` 后重新编译。请参考 [esp-bmgr-assist](https://docs.espressif.com/projects/esp-board-manager/zh_CN/latest/tools/esp-bmgr-assist.html#)。

### 相关参考

- 组件 API、URL 与进阶用法：[ESP Player 组件说明](../../README_CN.md)

## 技术支持

请按照下面的链接获取技术支持：

- 技术支持参见 [esp32.com](https://esp32.com/viewforum.php?f=20) 论坛
- 问题反馈与功能需求，请创建 [GitHub issue](https://github.com/espressif/esp-gmf/issues)

我们会尽快回复。
