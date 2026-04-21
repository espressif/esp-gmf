# ESP Video Render Linux 仿真

## 概述

`esp_video_render` 提供了基于 Linux 的仿真环境，可用于桌面开发、UI 验证和回归测试。

该仿真层尽可能复用了组件原有代码，并为平台相关部分提供了 PC 侧替代实现，从而可以在真正运行到 ESP 硬件之前，先在 Linux 上验证渲染逻辑、overlay 行为、widget 绘制以及部分视频处理流程。

## 仿真支持内容

当前 Linux 仿真环境覆盖以下能力：

- 核心视频渲染与合成逻辑
- 基于 dirty region 的局部重绘
- overlay、container 和 widget 合成
- 文本与图像 widget
- 双眼渲染逻辑
- 基于 SDL 的显示输出
- 可选的基于 GStreamer CLI 的视频处理仿真

## 架构概述

该仿真环境在尽量保留原始渲染流水线的前提下，用 Linux 友好的实现替换了 ESP 专有部分：

- **backend**  
  使用 SDL backend 完成帧缓冲显示

- **processor**  
  - 当 GStreamer CLI 工具可用时，使用 `proc_gstcli.c`
  - 当未启用 GStreamer 时，使用 `proc_emulation.c` 作为回退实现

- **stubs**  
  为组件依赖的 ESP / GMF 接口提供替代头文件和轻量实现

更多背景设计可参考 [`Design.md`](./Design.md)。

## 环境依赖

### 必需系统依赖

仿真构建通过 `pkg-config` 查找以下库：

- `sdl2`
- `freetype2`

在 Ubuntu 或 Debian 系统上，可使用：

```bash
sudo apt install libsdl2-dev libfreetype6-dev pkg-config ninja-build cmake
```

### 可选的 GStreamer 支持

GStreamer 为可选项。当以下工具存在时，构建脚本会自动启用 `EMU_WITH_GSTREAMER=ON`：

- `gst-launch-1.0`
- `gst-inspect-1.0`

在 Ubuntu 或 Debian 系统上，可使用：

```bash
sudo apt install gstreamer1.0-tools
```

如果系统中没有这些工具，仿真仍可构建，但依赖解码、裁剪、缩放等视频处理仿真的路径将使用回退实现。

## 构建

在 `packages/esp_video_render/emulation` 目录下执行：

```bash
./build.sh
```

生成物：

- `build/video_render_emulator`

### Debug 构建

若需要调试符号和关闭优化，可执行：

```bash
./build.sh -g
```

## 运行

```bash
cd build
./video_render_emulator
```

## WSL 与无头环境

在 WSL 或仅支持软件渲染的环境中，可以强制使用软件渲染：

```bash
export SDL_RENDER_DRIVER=software
export LIBGL_ALWAYS_SOFTWARE=1
```

对于 CI 等完全无头环境：

```bash
export SDL_VIDEODRIVER=dummy
./video_render_emulator
```

SDL 的 dummy 驱动会创建隐藏窗口，从而让自动化环境下的渲染和测试仍可执行。

## 自动化测试

### 构建并运行全部测试

```bash
./build.sh
cd build
ctest --output-on-failure
```

### 在无头模式下运行测试

```bash
export SDL_VIDEODRIVER=dummy
cd build
ctest --output-on-failure
```

## 可用测试目标

仿真构建会生成一组针对不同功能的测试程序。

### 默认构建的测试

- `emu_backend_sdl`  
  验证 SDL backend 以及 dirty region 更新路径。

- `emu_widgets_text`  
  验证文本 widget 渲染，包括 UTF-8 和 emoji 场景。

- `emu_widgets_image`  
  验证图像 widget 渲染。

- `emu_dual_eyes`  
  验证双眼渲染行为，并在资源存在时使用 MJPEG 测试素材。

- `emu_xiaozhi_panel`  
  验证较完整的 UI 面板组合流程。

- `emu_player_view`  
  验证视频播放器示例中的 view 层。

- `emu_intercom_ui`  
  验证对讲风格 UI 的组合流程。

### 仅在启用 GStreamer 时构建

- `emu_proc_matrix`  
  验证裁剪、缩放、颜色转换以及部分解码路径。

### 单独运行测试

```bash
cd build
./emu_test_backend_sdl
./emu_test_widgets_text
./emu_test_widgets_image
./emu_test_dual_eyes
./emu_test_xiaozhi_panel
./emu_test_player_view
./emu_test_intercom_ui
```

若启用了 GStreamer：

```bash
./emu_test_proc_matrix
```

## 典型用途

Linux 仿真环境适用于以下场景：

- 在 PC 上验证布局和 widget 渲染
- 不烧录硬件时调试 dirty region 和混合行为
- 更快地开发 overlay 密集型应用
- 为渲染逻辑建立桌面侧回归测试
- 在 CI 中验证双眼渲染或 UI 合成流程

## 当前限制

- 仿真目标主要面向 Linux 环境
- SDL backend 不是对真实面板 backend 的时序级精确模拟
- GStreamer 路径通过 CLI 子进程实现，而不是直接链接运行时 API
- 某些硬件相关行为进行了简化

## 技术支持

- 技术支持论坛：[esp32.com](https://esp32.com/viewforum.php?f=20)
- 问题反馈和功能建议：[GitHub issue](https://github.com/espressif/esp-gmf/issues)
