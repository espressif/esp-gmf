# ESP-GMF-Video

- [![Component Registry](https://components.espressif.com/components/espressif/gmf_video/badge.svg)](https://components.espressif.com/components/espressif/gmf_video)

- [中文版](./README_CN.md)

ESP GMF Video is a suite of video processing elements designed for a variety of tasks such as video encoding, and applying video effects. These elements can be combined to form a complete video processing pipeline.

## Supported Video Modules

The following table lists the currently supported video processing modules and their detailed information:

|Name|TAG|Function|Method|Input Port|Output Port|Hardware Acceleration|Dependent on Video Information|
|:----:|:----:|:-----:|:----:|:----:|:----:|:----:|:----|
| VIDEO_ENC | vid_enc | Video encoder: H264, MJPEG | `set_bitrate`<br>`set_dst_codec`<br>`get_src_fmts`<br>`preset`<br>`get_frame_size` | Single | Single | Yes | Yes |
| VIDEO_DEC | vid_dec | Video decoder: H264, MJPEG | `set_dst_fmt`<br>`set_src_codec`<br>`get_dst_fmts` | Single | Single | No | No |
| VIDEO_PPA | vid_ppa | Pixel processing accelerator: Color conversion,<br>Scaling, Cropping, Rotation | `set_dst_format`<br>`set_dst_resolution`<br>`set_rotation`<br>`set_cropped_rgn` | Single | Single | Yes | Yes |
| FPS_CVT | vid_fps_cvt | Frame rate conversion | `set_fps` | Single | Single | No | Yes |
| OVERLAY_MIXER | vid_overlay | Video overlay mixer | `overlay_enable`<br>`set_rgn`<br>`set_port`<br>`set_alpha` | Multiple | Single | No | Yes |
| COLOR_CVT | vid_color_cvt | Software color conversion | `set_dst_fmt` | Single | Single | No | Yes |
| CROP | vid_crop | Software video cropping | `set_crop_rgn` | Single | Single | No | Yes |
| SCALE | vid_scale | Software video scaling | `set_dst_res` | Single | Single | No | Yes |
| ROTATE | vid_rotate | Software video rotation | `set_angle` | Single | Single | No | Yes |

## Elements

### Video Encoder
The Video Encoder element uses the `esp_video_codec` to convert raw video frames into compressed video streams. It currently supports two codecs:
- **H264**
- **MJPEG**

### Video Decoder
The video decoder element use the `esp_video_codec` to convert compressed video streams into raw video frames. It currently supports two codecs：
- **H264**
- **MJPEG**

### Video PPA (Pixel Processing Accelerator)
The Video PPA element is a compact element, support multiple functions. It is currently available on the ESP32P4 board and includes following functionalities:
- **Color Conversion:**
  The ESP32P4 supports color conversion through two hardware modules:
  - **2D-DMA:** Automatically selected for better efficiency if supported.
  - **PPA:** Used as a fallback when 2D-DMA not supported
- **Resizing:**
  Resizing functionality is provided by the PPA module.
- **Cropping:**
  Cropping functionality is provided by the PPA module.
- **Rotation:**
  Supports rotations at 0°, 90°, 180°, and 270°.

### Video FPS Converter
This module adjusts the frame rate of the video. It decreases the input frame rate to a specified output rate, using the Presentation Time Stamp (PTS) embedded in the input data to accurately schedule frames.

### Video Overlay Mixer
The Video Overlay Mixer module allows users to overlay additional graphics onto a video frame. By receiving overlay data via a user-defined port, it can blend elements such as timestamps, watermarks, or other images into a designated region of the original video frame.

### Video Pixel Processor Elements
Following elements are wrapped for [Video Pixel Processor](https://github.com/espressif/esp-adf-libs/tree/master/esp_image_effects) which implemented software video processing.

#### Video Color Converter
Element to do software color conversion for video image

#### Video Cropper
Element to do software video cropper for video image

#### Video Scaler
Element to do software video scaler for video image

#### Video Rotator
Element to do software video rotator for video image

## ESP-GMF-Video Release and SoC Compatibility

The following table summarizes the support for ESP-GMF-Video elements across Espressif SoCs in the current release.
A check mark (&#10004;) indicates that the element is supported, while a cross mark (&#10006;) indicates it is not supported.

| Element         | ESP32       | ESP32-S2    | ESP32-S3    | ESP32-P4    |
|-----------------|:-----------:|:-----------:|:-----------:|:-----------:|
| Video PPA       | &#10006;    | &#10006;    | &#10006;    | &#10004;    |
| FPS Converter   | &#10004;    | &#10004;    | &#10004;    | &#10004;    |
| Overlay Mixer   | &#10004;    | &#10004;    | &#10004;    | &#10004;    |
| Video Decoder   | MJPEG only  | MJPEG only  | &#10004;    | &#10004;    |
| Video Encoder   | MJPEG only  | MJPEG only  | &#10004;    | &#10004;    |
| Video Pixel Processor  | &#10004;     | &#10004;    | &#10004;    | &#10004;    |

## Notes

- **Video PPA** is currently supported **only** on the ESP32-P4.
- **FPS Converter** and **Overlay Mixer** are supported on **all SoCs**.
- **Video Decoder** supports MJPEG decoding on ESP32 and ESP32-S2
- **Video Encoder** supports MJPEG encoding on ESP32 and ESP32-S2

## Usage
ESP GMF Video modules are often used together to build a complete video processing pipeline. For example, you might first convert its colors or size, adjust the frame rate, and overlay and finally output through video encoder. For a practical implementation, please refer to the example in [test_app](../test_apps/main/elements/gmf_video_el_test.c).
