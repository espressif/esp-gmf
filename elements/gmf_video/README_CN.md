# ESP-GMF-Video

- [![Component Registry](https://components.espressif.com/components/espressif/gmf_video/badge.svg)](https://components.espressif.com/components/espressif/gmf_video)

- [English Version](./README.md)

ESP GMF Video 是一套专为视频编解码和视频转换等设计的视频处理模块。这些模块可以组合在一起，形成完整的视频处理流水线。

## 支持的视频模块

下表列出了当前支持的视频处理模块及其详细信息：

| 名称 | 标签 | 功能 |  函数方法   | 输入端口 | 输出端口 | 硬件加速 |依赖视频信息 |
|:----:|:-----:|:----:|:----:|:----:|:----:|:----:|:----|
| VIDEO_ENC | vid_enc | 视频编码器：H264, MJPEG | `set_bitrate`<br>`set_dst_codec`<br>`get_src_fmts`<br>`preset`<br>`get_frame_size` | 单个 | 单个 | 是 | 是 |
| VIDEO_DEC | vid_dec | 视频解码器：H264, MJPEG | `set_dst_fmt`<br>`set_src_codec`<br>`get_dst_fmts` | 单个 | 单个 | 否 | 否 |
| VIDEO_PPA | vid_ppa | 像素处理加速器：颜色转换，缩放，<br>裁剪，旋转 | `set_dst_format`<br>`set_dst_resolution`<br>`set_rotation`<br>`set_cropped_rgn` | 单个 | 单个 | 是 | 是 |
| FPS_CVT | vid_fps_cvt | 帧率转换 | `set_fps` | 单个 | 单个 | 否 | 是 |
| OVERLAY_MIXER | vid_overlay | 视频叠加混合器 | `overlay_enable`<br>`set_rgn`<br>`set_port`<br>`set_alpha` | 多个 | 单个 | 否 | 是 |
| COLOR_CVT | vid_color_cvt | 软件颜色转换 | `set_dst_fmt` | 单个 | 单个 | 否 | 是 |
| CROP | vid_crop | 软件视频裁剪 | `set_crop_rgn` | 单个 | 单个 | 否 | 是 |
| SCALE | vid_scale | 软件视频缩放 | `set_dst_res` | 单个 | 单个 | 否 | 是 |
| ROTATE | vid_rotate | 软件视频旋转 | `set_angle` | 单个 | 单个 | 否 | 是 |

## 元素

### 视频编码器
视频编码器模块使用 `esp_video_codec` 将原始视频帧转换为压缩视频流。目前支持以下两种编码格式：
- **H264**
- **MJPEG**

### 视频解码器
视频编码器模块使用 `esp_video_codec` 将压缩视频流解码为原始视频帧。目前支持以下两种解码格式：
- **H264**
- **MJPEG**

### 视频像素加速器 (PPA)
视频像素加速器模块是一个复合模块，目前仅适用于 ESP32P4 ，包含以下功能：
- **颜色转换：**
  ESP32P4 通过两个硬件模块支持颜色转换：
  - **2D-DMA：** 如果支持，将自动选择以提高效率。
  - **PPA：** 当 2D-DMA 不支持时作为备用方案。
- **缩放：**
  由 PPA 模块提供缩放功能。
- **裁剪:**
  由 PPA 模块提供裁剪功能。
- **旋转：**
  支持 0°、90°、180° 和 270° 旋转。

### 视频帧率转换
此模块用于调整视频的帧率。它会根据输入数据中嵌入的演示时间戳（PTS）来准确地调整视频帧，从而将输入帧率降低到指定的输出帧率。

### 视频叠加混合器
视频叠加混合器模块允许用户在视频帧上叠加额外的图像。它通过用户定义的端口接收叠加数据，并将诸如时间戳、水印或其他图像等元素混合到原始视频帧的指定区域。

### 视频像素处理器元素
以下元素是为[视频像素处理器]（https://github.com/espressif/esp-adf-libs/tree/master/esp_image_effects）包装的，它实现了软件视频处理。

#### 视频彩色转换器
软件实现视频图像不同颜色转换

#### 视频剪辑器
软件实现视频图像任意位置及大小的裁剪

#### 视频缩放器
软件实现视频图像放大和缩小

#### 视频旋转器
软件实现视频图像任意角度的旋转

# ESP-GMF-Video SoC 兼容性

下表总结了当前版本中 ESP-GMF-Video 元素在乐鑫各 SoC 上的支持情况。
&#10004; 表示支持，&#10006; 表示不支持。

| 元素            |   ESP32     |  ESP32-S2   |  ESP32-S3   |  ESP32-P4   |
|----------------|:-----------:|:-----------:|:-----------:|:-----------:|
| 视频像素加速器   | &#10006;    | &#10006;    | &#10006;    | &#10004;    |
| 帧率转换器      | &#10004;    | &#10004;    | &#10004;    | &#10004;    |
| 叠加混合器      | &#10004;    | &#10004;    | &#10004;    | &#10004;    |
| 视频解码器      | 仅支持 MJPEG | 仅支持 MJPEG | &#10004;    | &#10004;    |
| 视频编码器      | 仅支持 MJPEG | 仅支持 MJPEG | &#10004;    | &#10004;    |
| 视频像素处理器  | &#10004;     | &#10004;    | &#10004;    | &#10004;    |

## 备注

- **视频像素加速器** 目前仅在 **ESP32-P4** 上支持。
- **帧率转换器** 和 **叠加混合器** 在**所有 SoC**上均支持。
- **视频解码器** 在 ESP32 和 ESP32-S2 上仅支持 MJPEG 解码。
- **视频编码器** 在 ESP32 和 ESP32-S2 上仅支持 MJPEG 编码。

## 使用方法
ESP GMF Video 模块通常组合使用，以构建完整的视频处理流水线。例如，您可以先调整帧率，然后转换颜色或调整大小，叠加特效，最终通过视频编码器输出。有关具体用法，请参考 [test_app](../test_apps/main/elements/gmf_video_el_test.c) 示例。
