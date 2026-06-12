# GMF Loader

- [![Component Registry](https://components.espressif.com/components/espressif/gmf_loader/badge.svg)](https://components.espressif.com/components/espressif/gmf_loader)

- [中文版](./README_CN.md)

The GMF Loader is a helper component that allows users to easily customize their GMF pool through menuconfig. It provides two key capabilities:

- Initialization: Enables selected GMF features and adds corresponding elements to the user's pool
- Configuration: Allows setting default parameters for enabled elements via menuconfig before they are added to the pool

## Features

- Configurable initialization of GMF IO:
  - IO Reader:
    - Codec Device RX
    - File Reader
    - HTTP Reader
    - Flash Reader
  - IO Writer:
    - Codec Device TX
    - File Writer
    - HTTP Writer

- Audio codec initialization support:
  - Decoders: MP3, AAC, AMR-NB/WB, FLAC, WAV, M4A, TS, OGG, OPUS, G711, PCM, ADPCM, LC3, SBC, ALAC, VORBIS, G722
  - Encoders: AAC, AMR-NB/WB, G711, OPUS, ADPCM, PCM, ALAC, LC3, SBC, G722

- Audio effects configuration:
  - Automatic Level Control (ALC)
  - Equalizer (EQ)
  - Channel conversion
  - Bit depth conversion
  - Sample rate conversion
  - Adaptive audio format conversion (ASRC)
  - Fade effects
  - Sonic effects
  - Channel interleave/deinterleave
  - Audio mixing
  - Dynamic Range Control (DRC)
  - Multi-band Compressor (MBC)
  - Howling Suppression (HOWL)

- AI Audio features configuration:
  - Audio Echo Cancellation (AEC) element
  - Audio Wake Net (WN) element
  - Audio Front End (AFE) element

- Audio Muxer configuration:
  - Support multiple container formats: TS, MP4, FLV, WAV, CAF, OGG
  - Support multiple audio codecs: AAC, PCM, MP3, ADPCM, G711A, G711U, AMR-NB, AMR-WB, ALAC, OPUS
  - Support streaming mode (TS, FLV) and file mode

- Video codec initialization support:
  - Decoders: H264 (SW), MJPEG (SW/HW)
  - Encoders: H264 (SW/HW), MJPEG (SW/HW)

- Video effects configuration:
  - Video PPA (Pixel Processing Accelerator)
  - FPS Conversion
  - Video Overlay
  - Video Crop
  - Video Scale
  - video Rotate
  - Video Color Convert

- Miscellaneous elements configuration:
  - Copier: Copy data between elements with configurable number of copies

## Configuration

The configuration options are organized into the following sections:

- GMF IO Configurations: Configure IO readers and writers
- GMF Audio Configurations: Configure audio codecs, effects, AI features, and Muxer
- GMF Video Configurations: Configure video codecs and effects
- GMF Miscellaneous Configurations: Configure misc elements

The following shows the configuration menu details:

- [Y] The component is added to the given GMF Pool by default
- [N] The component is not added to the given GMF Pool by default

```text
ESP GMF Loader
├── GMF IO
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
│   │   │   ├── ADPCM
│   │   │   ├── SBC
│   │   │   ├── VORBIS
│   │   │   ├── ALAC
│   │   │   └── LC3
│   │   └── Encoders [Y]
│   │       ├── AAC [Y]
│   │       ├── AMR-NB/WB
│   │       ├── G711
│   │       ├── OPUS
│   │       ├── ADPCM
│   │       ├── PCM
│   │       ├── ALAC
│   │       ├── SBC
│   │       └── LC3
│   │
│   ├── GMF Audio Effects
│   │   ├── Automatic Level Control (ALC) [Y]
│   │   ├── Channel Conversion [Y]
│   │   ├── Bit Depth Conversion [Y]
│   │   ├── Sample Rate Conversion [Y]
│   │   ├── ASRC [N]
│   │   ├── Channel Interleave [N]
│   │   ├── Channel Deinterleave [N]
│   │   ├── Audio Mixing [N]
│   │   ├── Equalizer (EQ) [N]
│   │   ├── Speed Effect [N]
│   │   ├── Fade In/Out [N]
│   │   ├── Dynamic Range Control [N]
│   │   ├── Multi-Band Compressor [N]
│   │   └── Howling Suppression (HOWL) [N]
│   │
│   ├── GMF AI Audio
│   │   ├── Audio Echo Cancellation (AEC) [Y]
|   │   ├── Audio Wake Net (WN) [N]
│   │   └── Audio Front End (AFE) [N]
│   │
│   └── GMF Audio Muxer [N]
│       ├── Enable TS Muxer [Y]
│       ├── Enable MP4 Muxer [N]
│       ├── Enable FLV Muxer [N]
│       ├── Enable WAV Muxer [N]
│       ├── Enable CAF Muxer [N]
│       ├── Enable OGG Muxer [N]
│       ├── Select Default Muxer Type
│       │   └── TS Muxer [Y]
│       ├── Select Default Audio Codec
│       │   └── AAC [Y]
│       ├── Audio Sample Rate (16000)
│       ├── Audio Channel (1)
│       ├── Audio Bits Per Sample (16)
│       ├── Audio Min Packet Duration (10 ms)
│       └── Enable Streaming Mode [Y]
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

## Usage

1. Enable desired GMF features through menuconfig under "ESP GMF Loader"
2. Configure parameters for enabled features
3. Call `gmf_loader_setup_all_defaults` to initialize the selected GMF elements and register them to the GMF pool
4. Call `gmf_loader_teardown_all_defaults` to release allocated resources by `gmf_loader_setup_all_defaults`, make sure pool destroyed after call this API

### Setup Example

Here's a basic example of setting up GMF elements using `gmf_loader_setup_all_defaults`:

```c
// Initialize GMF pool
esp_gmf_pool_handle_t pool = NULL;
esp_err_t ret = esp_gmf_pool_init(&pool);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize GMF pool");
    return;
}

// Setup default GMF elements
ret = gmf_loader_setup_all_defaults(pool);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to setup default GMF elements");
    esp_gmf_pool_deinit(pool);
    return;
}

// Create a pipeline with HTTP reader
esp_gmf_pipeline_handle_t pipeline = NULL;
const char *elements[] = {"aud_dec"};
ret = esp_gmf_pool_new_pipeline(pool, "io_http", elements, 1, "io_codec_dev", &pipeline);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create pipeline");
    gmf_loader_teardown_all_defaults(pool);
    esp_gmf_pool_deinit(pool);
    return;
}

// Set input URL
esp_gmf_pipeline_set_in_uri(pipeline, "http://example.com/audio.mp3");

// Configure and initialize GMF task
esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
cfg.thread.core = 0;  // Set task running core
cfg.thread.stack = 5120;  // Set task stack size
esp_gmf_task_handle_t task = NULL;
esp_gmf_task_init(&cfg, &task);

// Bind task to pipeline
esp_gmf_pipeline_bind_task(pipeline, task);
esp_gmf_pipeline_loading_jobs(pipeline);

// Configure and start the pipeline
esp_gmf_pipeline_run(pipeline);

// Clean up when done
esp_gmf_task_deinit(task);
esp_gmf_pipeline_destroy(pipeline);
gmf_loader_teardown_all_defaults(pool);
esp_gmf_pool_deinit(pool);
```
