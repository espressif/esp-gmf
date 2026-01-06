# GMF Loader

- [![Component Registry](https://components.espressif.com/components/espressif/gmf_loader/badge.svg)](https://components.espressif.com/components/espressif/gmf_loader)

- [English Version](./README.md)

GMF Loader 是一个辅助组件，允许用户通过 menuconfig 轻松自定义其 GMF pool。它提供两个关键功能：

- 初始化：启用选定的 GMF 功能并将相应的元素添加到用户的 pool 中
- 配置：允许在将元素添加到 pool 之前通过 menuconfig 设置默认参数

## 功能特点

- 可配置的 GMF IO 初始化：
  - IO Reader:
    - Codec Device RX
    - File Reader
    - HTTP Reader
    - Flash Reader
  - IO Writer:
    - Codec Device TX
    - File Writer
    - HTTP Writer

- 音频编解码器初始化支持：
  - 解码器：MP3、AAC、AMR-NB/WB、FLAC、WAV、M4A、TS、OPUS、G711、PCM、ADPCM
  - 编码器：AAC、AMR-NB/WB、G711、OPUS、ADPCM、PCM、ALAC

- 音频效果配置：
  - 自动电平控制 (ALC)
  - 均衡器 (EQ)
  - 声道转换
  - 位深度转换
  - 采样率转换
  - 淡入淡出效果
  - 音速效果
  - 声道交织/解交织
  - 音频混音

- AI 音频配置：
  - 音频回声消除 (AEC)
  - 音频前端 (AFE)

- 视频编解码器初始化支持：
  - 解码器：H264（软件）、MJPEG（软件/硬件）
  - 编码器：H264（软件/硬件）、MJPEG（软件/硬件）

- 视频效果配置：
  - 视频像素加速器 (PPA)
  - 帧率转换
  - 视频叠加
  - 视频裁剪
  - 视频缩放
  - 视频旋转
  - 视频颜色转换

- 其他元素配置：
  - 复制器：在元素之间复制数据

## 配置

配置选项分为以下几个部分：

- GMF IO 配置：配置 IO 读取器和写入器
- GMF 音频配置：配置音频编解码器、效果和 AI 功能
- GMF 视频配置：配置视频编解码器和效果
- GMF 其他配置：配置其他元素，如复制器

以下为配置菜单明细

- [Y] 表示该组件将默认添加到给定的GMF Pool 中
- [N] 表示该组件默认不添加到给定的GMF Pool 中

```text
ESP GMF Loader
├── GMF IO Configurations
│   ├── IO Reader
│   │   ├── Codec Device RX [Y]
│   │   ├── File Reader [Y]
│   │   ├── HTTP Reader [Y]
│   │   └── Flash Reader [Y]
│   └── IO Writer
│       ├── Codec Device TX [Y]
│       ├── File Writer [Y]
│       └── HTTP Writer [N]
│
├── GMF Audio Configurations
│   ├── GMF Audio Codec
│   │   ├── Decoders [Y]
│   │   │   ├── AAC [Y]
│   │   │   ├── MP3
│   │   │   ├── AMR-NB
│   │   │   ├── AMR-WB
│   │   │   ├── FLAC
│   │   │   ├── WAV
│   │   │   ├── M4A
│   │   │   ├── TS
│   │   │   ├── Raw Opus
│   │   │   ├── G711A
│   │   │   ├── G711U
│   │   │   ├── PCM
│   │   │   └── ADPCM
│   │   └── Encoders [Y]
│   │       ├── AAC [Y]
│   │       ├── AMR-NB/WB
│   │       ├── G711
│   │       ├── OPUS
│   │       ├── ADPCM
│   │       ├── PCM
│   │       └── ALAC
│   │
│   ├── GMF Audio Effects
│   │   ├── Automatic Level Control (ALC) [Y]
│   │   ├── Channel Conversion [Y]
│   │   ├── Bit Depth Conversion [Y]
│   │   ├── Sample Rate Conversion [Y]
│   │   ├── Channel Interleave [N]
│   │   ├── Channel Deinterleave [N]
│   │   ├── Audio Mixing [N]
│   │   ├── Equalizer (EQ) [N]
│   │   ├── Speed Effect [N]
│   │   └── Fade In/Out [N]
│   │
│   └── GMF AI Audio
│       ├── Audio Echo Cancellation (AEC) [Y]
│       └── Audio Front End (AFE) [N]
│
│── GMF Video Configurations
│   ├── GMF Video Codec
│   │   ├── Decoders [N]
│   │   │   ├── Auto [Y]
│   │   │   ├── Software H264
│   │   │   ├── Software MJPEG
│   │   │   └── Hardware MJPEG
│   │   └── Encoders [N]
│   │       ├── Auto [Y]
│   │       ├── Software H264
│   │       ├── Hardware H264
│   │       ├── Software MJPEG
│   │       └── Hardware MJPEG
│   │
│   └── GMF Video Effects
│       ├── Video PPA [N]
│       ├── Video FPS Convert [N]
│       ├── Video Overlay [N]
│       ├── Video Crop [N]
│       ├── Video Scale [N]
│       ├── Video Rotate [N]
│       └── Video Color Convert [N]
│
└── GMF Miscellaneous Configurations
    └── Copier [N]
```

## 使用方法

1. 在 "ESP GMF Loader" 下通过 menuconfig 启用所需的 GMF 功能
2. 配置已启用功能的参数
3. 调用 `gmf_loader_setup_all_defaults` 始化选定的 GMF 元素并将它们注册到 GMF Pool 中
4. 调用 `gmf_loader_teardown_all_defaults` 释放由 `gmf_loader_setup_all_defaults` 分配的资源，确保在调用此 API 之后销毁 pool

### 设置示例

以下是使用 `gmf_loader_setup_all_defaults` 设置 GMF 元素的基本示例：

```c
// 初始化 GMF 池
esp_gmf_pool_handle_t pool = NULL;
esp_err_t ret = esp_gmf_pool_init(&pool);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize GMF pool");
    return;
}

// 设置默认 GMF 元素
ret = gmf_loader_setup_all_defaults(pool);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to setup default GMF elements");
    esp_gmf_pool_deinit(pool);
    return;
}

// 创建带有 HTTP 读取器
esp_gmf_pipeline_handle_t pipeline = NULL;
const char *elements[] = {"aud_dec"};
ret = esp_gmf_pool_new_pipeline(pool, "io_http", elements, 1, "io_codec_dev", &pipeline);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create pipeline");
    gmf_loader_teardown_all_defaults(pool);
    esp_gmf_pool_deinit(pool);
    return;
}

// 设置输入 URL
esp_gmf_pipeline_set_in_uri(pipeline, "http://example.com/audio.mp3");

// 配置并初始化 GMF 任务
esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
cfg.thread.core = 0;  // 设置任务运行的核心
cfg.thread.stack = 5120;  // 设置任务栈大小
esp_gmf_task_handle_t task = NULL;
esp_gmf_task_init(&cfg, &task);

// 将任务绑定到管道
esp_gmf_pipeline_bind_task(pipeline, task);
esp_gmf_pipeline_loading_jobs(pipeline);

// 配置并启动管道
esp_gmf_pipeline_run(pipeline);

// 完成后清理
esp_gmf_task_deinit(task);
esp_gmf_pipeline_destroy(pipeline);
gmf_loader_teardown_all_defaults(pool);
esp_gmf_pool_deinit(pool);
```
