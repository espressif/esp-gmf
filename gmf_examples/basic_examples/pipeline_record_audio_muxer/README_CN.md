# 使用 Muxer 录制音频到 microSD 卡

- [English Version](./README.md)
- 例程难度：⭐

## 例程简介

本示例通过 CODEC_DEV_RX IO 从编解码器采集音频，经 `aud_enc` 编码后，由 muxer 元素将编码数据复用为多种容器格式，并保存到 microSD 卡。

- 管道架构：`io_codec_dev` → `aud_enc` → `aud_muxer`（文件输出）。
- 支持多种容器格式
- muxer 可配置为写文件或经数据总线输出；本示例为**文件输出**模式。

### 典型场景

- 需要带容器封装（MP4/TS/WAV 等）的本地录音存档，如会议记录、录音笔等。
- 需要 **无损或高保真归档** 时，可选用 **ALAC + MP4/CAF** 等组合（需先在工程中启用 ALAC 编码与对应 muxer）。

## 功能说明

### 容器与编码格式

- **`aud_muxer` 支持的容器格式**：TS、MP4、FLV、WAV、CAF、OGG、AVI。可以通过修改 `DEFAULT_RECODER_MUXER_TYPE`
- **编码与容器须匹配**：不同容器仅支持部分编码轨（例如 OGG 常见配合 OPUS）。可以参考[Muxer 文档](https://github.com/espressif/esp-adf-libs/blob/master/esp_muxer/README.md)
- **无损录制 ALAC**：在启用 ALAC 编码器的前提下，可将编码设为 ALAC，并选用支持该轨的容器（如 **MP4**、**CAF** 等，以组件实际支持为准），即可得到**压缩无损**归档；与**未压缩 PCM** 相比，ALAC 体积更小。
- **有损 / 无损区分（简述）**：
  - **有损**：AAC、OPUS、AMR、ADPCM、SBC、LC3、G711 等（压缩后为不可逆近似）。
  - **无损**：**ALAC**（压缩无损）、**PCM** / 线性 PCM 封装（如部分 WAV，无感知编解码损失）。

### 输出模式：流式输出 vs 文件输出

`aud_muxer` 通过配置项 `output_type`（类型 `esp_gmf_audio_muxer_output_type_t`）区分两种输出路径：

| 模式 | 枚举值 | 行为简述 |
|------|--------|----------|
| **流式输出** | `ESP_GMF_AUDIO_MUXER_OUTPUT_STREAMING`  | 复用后的数据经 **数据总线（databus）** 送到 pipeline **下游**；必须在创建 pipeline 时 **挂载输出 IO**（如 `io_file`、`io_http` 等），由该 IO 从总线取流并写入文件或网络。**仅配置 `output_type` 而不挂输出 IO 无法完成落盘/推流。** |
| **文件输出** | `ESP_GMF_AUDIO_MUXER_OUTPUT_FILE`  | 由 muxer 通过 **`url_pattern` 回调** 直接写存储，创建 pipeline 时 **输出侧 IO 可为 `NULL`**（本示例 `esp_gmf_pool_new_pipeline(..., NULL)`）。 |

### 文件切片（仅文件输出模式）

- 长时间录制时，若只写一个超大文件，不利于拷贝与异常恢复。开启切片后，muxer 会按时间长度 **分段生成多个文件**，文件名/路径由注册的 **`url_pattern` 回调**（本例为 `muxer_file_pattern_cb`，用户可以修改这个函数实现自定义文件存储名称以及路径的支持）决定，通常包含 **递增的 `slice_index`**（如 `esp_gmf_muxer_000.mp4`、`esp_gmf_muxer_001.mp4`）。
- **`slice_duration` 含义**：单段录音的 **目标时长**，单位为 **毫秒（ms）**。达到该时长（或组件定义的切段条件）后结束当前文件并开始下一段。例如设为 `60000` 表示每段约 60 秒一个文件。

## 环境配置

### 硬件要求

- **开发板**：默认以 ESP32-S3-Korvo V3 等音频板为例，其他 ESP 音频板同样适用。
- **资源要求**：microSD 卡、麦克风、Audio ADC。

### 默认 IDF 分支

本例程支持 IDF release/v5.4（>= v5.4.3）与 release/v5.5（>= v5.5.2）分支。

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
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_record_audio_muxer
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

本例程可在源码 `main/record_muxer.c` 中通过以下宏及相关 muxer 配置项调整行为：

- `DEFAULT_RECORD_SAMPLE_RATE`：采样率（如 48000 Hz）
- `DEFAULT_RECORD_CHANNEL`：声道数（如 2ch）
- `DEFAULT_RECORD_BITS`：采样位数（如 16bit）
- `DEFAULT_RECORD_BITRATE`：编码码率（如 90000bps）
- `DEFAULT_RECODER_CODEC_TYPE`：编码类型（如 `ESP_AUDIO_TYPE_ALAC`）
- `DEFAULT_RECODER_MUXER_TYPE`：muxer 容器类型（如 `ESP_MUXER_TYPE_MP4`）
- `DEFAULT_RECODER_MUXER_SLICE_DURATION`：切片时长（毫秒），会赋值给 muxer 的 `slice_duration`（示例默认 60000，即约 60 秒一段；是否支持 0 关闭切片以组件行为为准）

录制的复用文件将写入 microSD（路径模式见源码中 `muxer_file_pattern_cb`）。

### 编译与烧录

- 编译示例程序

```
idf.py build
```

- 烧录程序并运行 monitor 工具来查看串口输出（将 PORT 替换为端口名称）：

```
idf.py -p PORT flash monitor
```

- 退出调试界面使用 `Ctrl+]`

## 如何使用例程

### 功能和用法

- 上电后例程会挂载 microSD、初始化录音 codec，按配置的编码与 muxer 类型开始录制。
- 录制一段时间后自动停止，将封装后的文件写入卡上（如 `/sdcard/esp_gmf_muxer_000.mp4`），最后释放资源。

### 日志输出

- 正常流程会依次打印外设初始化、注册元素、创建带 encoder 与 muxer 的 pipeline、配置 muxer、绑定任务、监听事件、启动 pipeline、muxer 输出路径等。

```
I (844) main_task: Calling app_main()
I (846) REC_MUXER: [ 1 ] Setup peripheral for audio codec device and sdcard
I (853) DEV_FS_FAT_SUB_SDMMC: slot_config: cd=-1, wp=-1, clk=15, cmd=7, d0=4, d1=-1, d2=-1, d3=-1, d4=-1, d5=-1, d6=-1, d7=-1, width=1, flags=0x1
Name: SD
Type: SDHC
Speed: 40.00 MHz (limit: 40.00 MHz)
Size: 29818MB
CSD: ver=2, sector_size=512, capacity=61067264 read_bl_len=9
SSR: bus_width=1
I (913) DEV_FS_FAT: Filesystem mounted, base path: /sdcard
I (918) BOARD_MANAGER: Device fs_sdcard initialized
I (923) DEV_AUDIO_CODEC: ADC is ENABLED
I (927) PERIPH_I2S: I2S[0] TDM, RX, ws: 45, bclk: 9, dout: 8, din: 10
I (932) PERIPH_I2S: I2S[0] initialize success: 0x3c10d194
I (938) DEV_AUDIO_CODEC: Init audio_adc, i2s_name: i2s_audio_in, i2s_rx_handle:0x3c10d194, i2s_tx_handle:0x3c10cfac, data_if: 0x3fcee908
I (950) PERIPH_I2C: I2C master bus initialized successfully
I (958) ES7210: Work in Slave mode
I (965) ES7210: Enable ES7210_INPUT_MIC1
I (967) ES7210: Enable ES7210_INPUT_MIC2
I (970) ES7210: Enable ES7210_INPUT_MIC3
I (973) ES7210: Enable TDM mode
I (976) DEV_AUDIO_CODEC: Successfully initialized codec: audio_adc
I (978) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fceeb64, chip:es7210
I (985) BOARD_MANAGER: Device audio_adc initialized
I (990) BOARD_DEVICE: Device handle audio_adc found, Handle: 0x3fcea070 TO: 0x3fcea070
I (1000) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:1
I (1003) I2S_IF: TDM Mode 0 bits:16/16 channel:2 sample_rate:48000 mask:1
I (1009) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:1
I (1014) I2S_IF: TDM Mode 1 bits:16/16 channel:2 sample_rate:48000 mask:1
I (1022) ES7210: Bits 8
I (1030) ES7210: Enable ES7210_INPUT_MIC1
I (1033) ES7210: Enable ES7210_INPUT_MIC2
I (1036) ES7210: Enable ES7210_INPUT_MIC3
I (1039) ES7210: Enable TDM mode
I (1044) ES7210: Unmuted
I (1044) Adev_Codec: Open codec device OK
I (1044) REC_MUXER: [ 2 ] Register all the elements and set audio information to record codec device
I (1052) GMF_SETUP_AUD_MUXER: Muxer config: type=538989396, output_type=0, codec=541278529
I (1060) GMF_SETUP_AUD_MUXER: Audio muxer initialized, type: 538989396, codec: 541278529
I (1068) ESP_GMF_POOL: Registered items on pool:0x3c10d8dc, app_main-120
I (1074) ESP_GMF_POOL: IO, Item:0x3c10d9f4, H:0x3c10d8f0, TAG:io_codec_dev
I (1081) ESP_GMF_POOL: EL, Item:0x3c10db10, H:0x3c10da04, TAG:aud_enc
I (1087) ESP_GMF_POOL: EL, Item:0x3c10dc18, H:0x3c10db20, TAG:aud_muxer
I (1093) REC_MUXER: [ 3 ] Create audio pipeline with encoder and muxer
I (1100) REC_MUXER: [ 3.1 ] Configure muxer element
I (1104) REC_MUXER: [ 3.2 ] Reconfig audio encoder type and report information to the record pipeline
I (1113) REC_MUXER: [ 3.3 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (1123) ESP_GMF_TASK: Waiting to run... [tsk:gmf_rec_muxer-0x3fcede00, wk:0x0, run:0]
I (1131) ESP_GMF_TASK: Waiting to run... [tsk:gmf_rec_muxer-0x3fcede00, wk:0x3c10f05c, run:0]
I (1139) REC_MUXER: [ 3.4 ] Create event group and listening event from pipeline
I (1146) REC_MUXER: [ 4 ] Start audio_pipeline
I (1150) REC_MUXER: CB: RECV Pipeline EVT: el:NULL-0x3c10dc28, type:8192, sub:ESP_GMF_EVENT_STATE_OPENING, payload:0x0, size:0,0x0
I (1164) ESP_GMF_AENC: Open, type:ALAC, acquire in frame: 8192, out frame: 8200
I (1169) ESP_GMF_TASK: One times job is complete, del[wk:0x3c10f05c, ctx:0x3c10dc6c, label:aud_enc_open]
I (1178) ESP_GMF_PORT: ACQ IN, new self payload:0x3c10f05c, port:0x3c10dff4, el:0x3c10dc6c-aud_enc
I (1187) REC_MUXER: [ 5 ] Wait for a while to stop record pipeline
I (1263) ESP_GMF_MUXER: Open muxer element, type: 540299341, output_type: 1 sample_rate: 48000, channel: 1, bits: 16
I (1263) REC_MUXER: CB: RECV Pipeline EVT: el:aud_muxer-0x3c10dd78, type:8192, sub:ESP_GMF_EVENT_STATE_RUNNING, payload:0x0, size:0,0x0
I (1274) ESP_GMF_TASK: One times job is complete, del[wk:0x3c10f138, ctx:0x3c10dd78, label:aud_muxer_open]
I (1284) REC_MUXER: Muxer file pattern: /sdcard/esp_gmf_muxer_000.mp4 (slice index: 0)
I (1305) MP4_MUXER: Set track limit 6000

I (11220) ESP_GMF_CODEC_DEV: CLose, 0x3c10def0, pos = 901120/0
I (11221) ESP_GMF_TASK: One times job is complete, del[wk:0x3c10f080, ctx:0x3c10dc6c, label:aud_enc_close]
I (11242) ESP_GMF_TASK: One times job is complete, del[wk:0x3c10f0bc, ctx:0x3c10dd78, label:aud_muxer_close]
I (11242) REC_MUXER: CB: RECV Pipeline EVT: el:NULL-0x3c10dc28, type:8192, sub:ESP_GMF_EVENT_STATE_STOPPED, payload:0x0, size:0,0x0
I (11252) ESP_GMF_TASK: Waiting to run... [tsk:gmf_rec_muxer-0x3fcede00, wk:0x0, run:0]
I (11259) ESP_GMF_TASK: Waiting to run... [tsk:gmf_rec_muxer-0x3fcede00, wk:0x0, run:0]
I (11267) REC_MUXER: [ 6 ] Destroy all the resources
W (11272) GMF_SETUP_AUD_CODEC: Unregistering default encoder
I (11277) BOARD_DEVICE: Deinit device audio_adc ref_count: 0 device_handle:0x3fcea070
I (11288) BOARD_DEVICE: Device audio_adc config found: 0x3c0dd86c (size: 92)
I (11291) BOARD_PERIPH: Deinit peripheral i2s_audio_in ref_count: 0
E (11297) i2s_common: i2s_channel_disable(1262): the channel has not been enabled yet
W (11305) PERIPH_I2S: Caution: Releasing RX (0x0).
I (11309) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 0
I (11315) PERIPH_I2C: I2C master bus deinitialized successfully
I (11321) BOARD_MANAGER: Device audio_adc deinitialized
I (11326) BOARD_DEVICE: Deinit device fs_sdcard ref_count: 0 device_handle:0x3fce9a7c
I (11333) BOARD_DEVICE: Device fs_sdcard config found: 0x3c0dd818 (size: 84)
I (11340) DEV_FS_FAT: Sub device 'sdmmc' deinitialized successfully
I (11346) BOARD_MANAGER: Device fs_sdcard deinitialized
I (11351) main_task: Returned from app_main()
```

## 故障排除

### microSD 卡未挂载或无法写入

- 确认 microSD 卡已正确插入且格式为 FAT32。
- 若日志提示打开文件失败，请检查卡容量与写保护状态。

### 无录音、编码或 muxer 初始化失败

- 确认板级已正确配置录音 codec 与 I2S。
- 确认 **muxer 类型与编码类型** 组合合法（如 OGG 需 OPUS 等）。
- 若编码器初始化失败，请检查 menuconfig 中对应编码格式是否已启用。

### 相关参考

- 仅需编码后写裸流文件、无需容器封装时或者需要用到低功耗功能，可参考同目录 [pipeline_record_sdcard](../pipeline_record_sdcard/README_CN.md)。
- 当前示例只支持音频的录音和封装。如需支持视频相关能力的分装，可参考 [esp_capture](../../../packages/esp_capture/README_CN.md)。

## 技术支持

请按照下面的链接获取技术支持：

- 技术支持参见 [esp32.com](https://esp32.com/viewforum.php?f=20) 论坛
- 问题反馈与功能需求，请创建 [GitHub issue](https://github.com/espressif/esp-gmf/issues)

我们会尽快回复。
