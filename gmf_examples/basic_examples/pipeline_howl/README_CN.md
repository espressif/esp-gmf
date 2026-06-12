# Pipeline HOWL（啸叫抑制）

- [English](./README.md)
- 例程难度：⭐

## 示例说明

本示例展示了一个卡拉 OK 音频处理流程：采用 **三条 GMF pipeline**，**两路音源**（伴奏、麦克风）分别处理后，经 ringbuf 送入混音 pipeline，由 `aud_mixer` 混音后经 DAC 播放。

- 伴奏 pipeline：`io_file`（SDCard）→ `aud_dec` → `aud_rate_cvt` → `aud_bit_cvt` → `aud_ch_cvt`
- 麦克风 pipeline：`io_codec_dev`（ADC）→ `aud_howl`
- 混音输出 pipeline：`aud_mixer` → `io_codec_dev`（DAC）

最终混音播放格式为 **16 kHz / 单声道 / 16 bit**。

### 典型场景

- KTV/卡拉 OK 伴奏 + 人声实时混音
- 麦克风近场扩声场景中的啸叫抑制验证
- 多路音频 pipeline 连接（databus/ringbuf）参考示例

## 环境配置

### 硬件要求

- **开发板**：ESP32-S3-Korvo V3 等带音频 ADC + DAC 的 ESP 音频开发板。
- **外设**：麦克风、扬声器、SD 卡。

### 默认 IDF 版本

与其它 GMF 基础示例一致，支持 IDF release/v5.4（≥ v5.4.3）与 release/v5.5（≥ v5.5.2）。

### 软件要求

- 准备一张 microSD 卡，将自备音源重命名为 `test`（扩展名与格式一致，如 `test.mp3`）并存入卡中。可通过 `DEFAULT_PLAY_URL` 更改播放文件路径。支持伴奏音频格式为 MP3、WAV、FLAC、AAC、M4A、TS、AMRNB、AMRWB 等，默认 MP3。
- 若需修改最终播放格式，可在 `main/pipeline_howl.c` 调整：
  - `HOWL_SAMPLE_RATE`
  - `HOWL_CHANNELS`
  - `HOWL_BITS`

## 编译和下载

### 编译准备

编译前请先完成 ESP-IDF 环境配置；若已配置可跳过。

```bash
./install.sh
. ./export.sh
```

下面是简略步骤：

- 进入本例程目录：

```bash
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_howl
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

> [!NOTE]
> 如果切换为其他 `esp_board_manager` 支持的开发板，请按相同步骤执行并替换板型名称/索引。
> 自定义开发板请参考 [自定义开发板指南](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/docs/how_to_customize_board_cn.md)。
> `esp_board_manager` 更多信息请参考 [ESP_BOARD_MANAGER 入门指南](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README_CN.md)

### 项目配置（可选）

- 若需调整音频效果相关参数（mixer的weight， howl的判别参数等），可在 menuconfig 中配置 GMF Audio 相关选项：

```bash
idf.py menuconfig
```

在 menuconfig 中进行以下配置：

1) 关于 HOWL 的配置
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `HOWL PAPR Threshold × 10`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `HOWL PHPR Threshold × 10`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `HOWL PNPR Threshold × 10`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `HOWL IMSD Threshold × 10`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `Enable HOWL IMSD`

2) 关于 MIXER 的配置
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `Source1 Parameters`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `Source12 Parameters`

> 配置完成后按 `s` 保存，然后按 `Esc` 退出。

### SD 卡音乐文件

- 请将伴奏文件放到 SD 卡，并在 `main/pipeline_howl.c` 中设置：
  - `HOWL_MUSIC_URI`
- 当前代码默认路径：`/sdcard/test.mp3`

## 编译与烧录

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

- 上电后示例会初始化 DAC、ADC 与 SD 卡，创建三条 pipeline，并自动开始：
  - 伴奏解码 + 重采样/位深/声道转换
  - 麦克风采集 + HOWL 处理
  - 混音后播放
- 当伴奏文件播放结束后，示例会停止全部 pipeline 并释放资源。

### 日志输出

- 正常流程会依次打印挂载 SD 卡、注册元素、创建 pipeline、设置 url、绑定任务、监听事件、启动 pipeline、等待结束并销毁资源。关键步骤以 `[ 1 ]`～`[ 7 ]` 标出。

```c
I (906) main_task: Calling app_main()
I (908) PIPELINE_HOWL: [ 1 ] Init peripherals
I (913) PERIPH_I2S: I2S[0] STD,  TX, ws: 45, bclk: 9, dout: 8, din: 10
I (918) PERIPH_I2S: I2S[0] initialize success: 0x3c1709bc
I (924) DEV_AUDIO_CODEC: DAC is ENABLED
I (927) PERIPH_I2C: I2C master bus initialized successfully
I (938) ES8311: Work in Slave mode
I (941) DEV_AUDIO_CODEC: Successfully initialized codec: audio_dac
I (941) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fceb574, chip:es8311
I (949) BOARD_MANAGER: Device audio_dac initialized
I (953) BOARD_DEVICE: Device handle audio_dac found, Handle: 0x3fce9a80 TO: 0x3fce9a80
I (961) I2S_IF: No paired data, current mode: playback
I (966) I2S_IF: STD: TX, data_bit: 16, slot_bit: 16, ws_width: 16, slot_mode: MONO, slot_mask: 0x1
I (974) I2S_IF: STD: TX, sample_rate_hz: 16000, mclk_multiple: 256, clk_src: 6
I (997) Adev_Codec: Open codec device OK
I (997) PERIPH_I2S: I2S[0] STD, RX, ws: 45, bclk: 9, dout: 8, din: 10
I (997) PERIPH_I2S: I2S[0] initialize success: 0x3c170b78
I (1001) DEV_AUDIO_CODEC: ADC over I2S is enabled
I (1005) BOARD_PERIPH: Reuse periph: i2c_master, ref_count=2
I (1016) ES8311: Work in Slave mode
I (1019) DEV_AUDIO_CODEC: Successfully initialized codec: audio_adc
I (1020) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fceb0f4, chip:es8311
I (1027) BOARD_MANAGER: Device audio_adc initialized
I (1032) BOARD_DEVICE: Device handle audio_adc found, Handle: 0x3fcea6e0 TO: 0x3fcea6e0
I (1040) I2S_IF: Current mode(record) and peer mode have same sample_rate 16000, channel 2, bits_per_sample 16
I (1049) I2S_IF: STD: RX, data_bit: 16, slot_bit: 16, ws_width: 16, slot_mode: MONO, slot_mask: 0x1
I (1058) I2S_IF: STD: RX, sample_rate_hz: 16000, mclk_multiple: 256, clk_src: 6
I (1079) Adev_Codec: Open codec device OK
I (1079) DEV_FS_FAT_SUB_SDMMC: slot_config: cd=-1, wp=-1, clk=6, cmd=7, d0=4, d1=-1, d2=-1, d3=-1, d4=-1, d5=-1, d6=-1, d7=-1, width=1, flags=0x1
W (1128) SD_HOST: input line delay not supported, fallback to 0 delay
Name: SD16G
Type: SDHC
Speed: 40.00 MHz (limit: 40.00 MHz)
Size: 15238MB
CSD: ver=2, sector_size=512, capacity=31207424 read_bl_len=9
SSR: bus_width=1
I (1136) DEV_FS_FAT: Filesystem mounted, base path: /sdcard
I (1142) BOARD_MANAGER: Device fs_sdcard initialized
I (1146) PIPELINE_HOWL: [ 2 ] Register pool
I (1151) ESP_GMF_POOL: Registered items on pool:0x3c172a24, app_main-165
I (1157) ESP_GMF_POOL: IO, Item:0x3c172ab0, H:0x3c172cc4, TAG:io_codec_dev
I (1163) ESP_GMF_POOL: IO, Item:0x3c172e50, H:0x3c172d80, TAG:io_codec_dev
I (1170) ESP_GMF_POOL: IO, Item:0x3c172f6c, H:0x3c172e60, TAG:io_file
I (1176) ESP_GMF_POOL: IO, Item:0x3c173088, H:0x3c172f7c, TAG:io_file
I (1182) ESP_GMF_POOL: IO, Item:0x3c1731a8, H:0x3c173098, TAG:io_http
I (1188) ESP_GMF_POOL: IO, Item:0x3c1732b8, H:0x3c1731b8, TAG:io_embed_flash
I (1195) ESP_GMF_POOL: EL, Item:0x3c1733d8, H:0x3c1732c8, TAG:aud_dec
I (1201) ESP_GMF_POOL: EL, Item:0x3c1734d4, H:0x3c1733e8, TAG:aud_alc
I (1207) ESP_GMF_POOL: EL, Item:0x3c1735bc, H:0x3c1734e4, TAG:aud_ch_cvt
I (1214) ESP_GMF_POOL: EL, Item:0x3c1736a0, H:0x3c1735cc, TAG:aud_bit_cvt
I (1220) ESP_GMF_POOL: EL, Item:0x3c17378c, H:0x3c1736b0, TAG:aud_rate_cvt
I (1227) ESP_GMF_POOL: EL, Item:0x3c1738bc, H:0x3c17379c, TAG:aud_mixer
I (1233) ESP_GMF_POOL: EL, Item:0x3c1739d0, H:0x3c1738cc, TAG:aud_howl
I (1240) PIPELINE_HOWL: [ 3 ] Build pipelines
I (1244) PIPELINE_HOWL: [ 3.1 ] Config music source and decoder
I (1249) PIPELINE_HOWL: [ 3.2 ] Config howl parameters
I (1254) PIPELINE_HOWL: [ 3.3 ] Config mixer parameters
I (1259) PIPELINE_HOWL: [ 3.4 ] Connect music/mic pipelines to mixer
I (1265) NEW_DATA_BUS: New ringbuffer:0x3c174660, num:10, item_cnt:1024, db:0x3c176e8c
I (1273) NEW_DATA_BUS: New ringbuffer:0x3c176ef8, num:10, item_cnt:1024, db:0x3c179724
I (1280) PIPELINE_HOWL: [ 4 ] Create tasks and load jobs
I (1286) ESP_GMF_TASK: Waiting to run... [tsk:howl_music-0x3fcec480, wk:0, run:0]
I (1293) ESP_GMF_TASK: Waiting to run... [tsk:howl_music-0x3fcec480, wk:0x3c1798d0, run:0]
I (1301) ESP_GMF_TASK: Waiting to run... [tsk:howl_mic-0x3fced67c, wk:0, run:0]
W (1301) ESP_GMF_PIPELINE: Element[aud_howl-0x3c1740cc] not ready to register job, ret:0xffffdff8
I (1316) ESP_GMF_TASK: Waiting to run... [tsk:howl_mix-0x3fcee878, wk:0, run:0]
W (1323) ESP_GMF_PIPELINE: Element[aud_mixer-0x3c174350] not ready to register job, ret:0xffffdff8
I (1332) ESP_GMF_TASK: Waiting to run... [tsk:howl_mic-0x3fced67c, wk:0x3c179998, run:0]
I (1332) ESP_GMF_TASK: Waiting to run... [tsk:howl_mix-0x3fcee878, wk:0x3c179a20, run:0]
I (1332) PIPELINE_HOWL: [ 5 ] Run howl pipelines
I (1352) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: NULL-0x3c17430c, type: 2000, sub: ESP_GMF_EVENT_STATE_OPENING, payload: 0, size: 0, ctx:0x3fceeac0
I (1365) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: aud_mixer-0x3c174350, type: 3000, sub: ESP_GMF_EVENT_STATE_INITIALIZED, payload: 0x3fcaaf70, size: 16, ctx:0x3fceeac0
I (1380) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: aud_mixer-0x3c174350, type: 2000, sub: ESP_GMF_EVENT_STATE_RUNNING, payload: 0, size: 0, ctx:0x3fceeac0
I (1393) ESP_GMF_TASK: One times job is complete, del[wk:0x3c179a20, ctx:0x3c174350, label:aud_mixer_open]
I (1403) ESP_GMF_PORT: ACQ IN, new self payload:0x3c179a20, port:0x3c1797d0, el:0x3c174350-aud_mixer
I (1412) ESP_GMF_FILE: Open, dir:1, uri:/sdcard/test.mp3
I (1417) PIPELINE_HOWL: [ 6 ] Wait music end then stop all pipelines
I (1417) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: -0x3c174088, type: 2000, sub: ESP_GMF_EVENT_STATE_OPENING, payload: 0, size: 0, ctx:0x3fceea98
I (1442) ESP_GMF_PORT: ACQ IN, new self payload:0x3c17aa04, port:0x3c179860, el:0x3c174350-aud_mixer
I (1447) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: aud_howl-0x3c1740cc, type: 3000, sub: ESP_GMF_EVENT_STATE_INITIALIZED, payload: 0x3fcee590, size: 16, ctx:0x3fceea98
I (1448) ESP_GMF_FILE: File size: 3664281 byte, file position: 0
I (1459) ESP_GMF_HOWL: Howl opened, 0x3c1740cc, frame_bytes 1024
I (1465) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: -0x3c1739e0, type: 2000, sub: ESP_GMF_EVENT_STATE_OPENING, payload: 0, size: 0, ctx:0x3fceea70
I (1471) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: aud_howl-0x3c1740cc, type: 2000, sub: ESP_GMF_EVENT_STATE_RUNNING, payload: 0, size: 0, ctx:0x3fceea98
I (1483) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1798d0, ctx:0x3c173a24, label:aud_dec_open]
I (1497) ESP_GMF_TASK: One times job is complete, del[wk:0x3c179998, ctx:0x3c1740cc, label:aud_howl_open]
I (1506) ESP_GMF_PORT: ACQ IN, new self payload:0x3c1798d0, port:0x3c174048, el:0x3c173a24-aud_dec
I (1515) ESP_GMF_PORT: ACQ IN, new self payload:0x3c179998, port:0x3c1742cc, el:0x3c1740cc-aud_howl
I (1525) ESP_ES_PARSER: The version of es_parser is v1.0.1
W (1539) ESP_GMF_ASMP_DEC: Not enough memory for out, need:4608, old: 1024, new: 4608
I (1689) ESP_GMF_TASK: One times job is complete, del[wk:0x3c17ac1c, ctx:0x3c173b34, label:aud_rate_cvt_open]
I (1690) ESP_GMF_TASK: One times job is complete, del[wk:0x3c184260, ctx:0x3c173c90, label:aud_bit_cvt_open]
I (1697) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: aud_ch_cvt-0x3c173de4, type: 3000, sub: ESP_GMF_EVENT_STATE_INITIALIZED, payload: 0x3fced3a0, size: 16, ctx:0x3fceea70
I (1712) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: aud_ch_cvt-0x3c173de4, type: 2000, sub: ESP_GMF_EVENT_STATE_RUNNING, payload: 0, size: 0, ctx:0x3fceea70
I (1726) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1842b0, ctx:0x3c173de4, label:aud_ch_cvt_open]
I (230320) ESP_GMF_FILE: No more data, ret: 0
I (230322) ESP_GMF_TASK: Job is done, [tsk:howl_music-0x3fcec480, wk:0x3c17990c, job:0x3c173a24-aud_dec_proc]
I (230324) ESP_GMF_TASK: Job is done, [tsk:howl_music-0x3fcec480, wk:0x3c183f24, job:0x3c173b34-aud_rate_cvt_proc]
I (230333) ESP_GMF_TASK: Job is done, [tsk:howl_music-0x3fcec480, wk:0x3c184288, job:0x3c173c90-aud_bit_cvt_proc]
I (230376) ESP_GMF_TASK: Job is done, [tsk:howl_music-0x3fcec480, wk:0x3c1842d8, job:0x3c173de4-aud_ch_cvt_proc]
I (230376) ESP_GMF_TASK: Finish, strategy action: 0, [tsk:0x3fcec480-howl_music]
I (230382) ESP_GMF_FILE: CLose, 0x3c173f3c, pos = 3664281/3664281
I (230388) ESP_GMF_TASK: One times job is complete, del[wk:0x3c183f24, ctx:0x3c173a24, label:aud_dec_close]
I (230398) ESP_GMF_TASK: One times job is complete, del[wk:0x3c184260, ctx:0x3c173b34, label:aud_rate_cvt_close]
I (230407) ESP_GMF_TASK: One times job is complete, del[wk:0x3c184288, ctx:0x3c173c90, label:aud_bit_cvt_close]
I (230417) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1798f4, ctx:0x3c173de4, label:aud_ch_cvt_close]
I (230427) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: -0x3c1739e0, type: 2000, sub: ESP_GMF_EVENT_STATE_FINISHED, payload: 0, size: 0, ctx:0x3fceea70
I (230440) ESP_GMF_TASK: Waiting to run... [tsk:howl_music-0x3fcec480, wk:0, run:0]
I (230447) ESP_GMF_TASK: Waiting to run... [tsk:howl_music-0x3fcec480, wk:0, run:0]
I (230463) ESP_GMF_CODEC_DEV: CLose, 0x3c1741d0, pos = 7327744/0
I (230463) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1799bc, ctx:0x3c1740cc, label:aud_howl_close]
I (230470) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: -0x3c174088, type: 2000, sub: ESP_GMF_EVENT_STATE_STOPPED, payload: 0, size: 0, ctx:0x3fceea98
I (230483) ESP_GMF_TASK: Waiting to run... [tsk:howl_mic-0x3fced67c, wk:0, run:0]
I (230490) ESP_GMF_TASK: Waiting to run... [tsk:howl_mic-0x3fced67c, wk:0, run:0]
I (230496) ESP_GMF_CODEC_DEV: CLose, 0x3c174470, pos = 7328640/0
I (230503) ESP_GMF_TASK: One times job is complete, del[wk:0x3c179a44, ctx:0x3c174350, label:aud_mixer_close]
I (230513) PIPELINE_HOWL: CB: RECV Pipeline EVT: el: NULL-0x3c17430c, type: 2000, sub: ESP_GMF_EVENT_STATE_STOPPED, payload: 0, size: 0, ctx:0x3fceeac0
I (230526) ESP_GMF_TASK: Waiting to run... [tsk:howl_mix-0x3fcee878, wk:0, run:0]
I (230533) ESP_GMF_TASK: Waiting to run... [tsk:howl_mix-0x3fcee878, wk:0, run:0]
I (230540) PIPELINE_HOWL: [ 7 ] Destroy resources
W (230545) GMF_SETUP_AUD_CODEC: Unregistering default decoder
I (230551) BOARD_DEVICE: Deinit device fs_sdcard ref_count: 0 device_handle:0x3fceb144
I (230558) BOARD_DEVICE: Device fs_sdcard config found: 0x3c1107dc (size: 84)
I (230565) DEV_FS_FAT: Sub device 'sdmmc' deinitialized successfully
I (230571) BOARD_MANAGER: Device fs_sdcard deinitialized
I (230577) I2S_IF: Pending out channel for in channel running
I (230581) BOARD_DEVICE: Deinit device audio_dac ref_count: 0 device_handle:0x3fce9a80
I (230590) BOARD_DEVICE: Device audio_dac config found: 0x3c1109b0 (size: 384)
I (230596) BOARD_PERIPH: Deinit peripheral i2s_audio_out ref_count: 0
W (230602) PERIPH_I2S: Caution: Releasing TX (0).
I (230607) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 1
W (230612) BOARD_PERIPH: Peripheral i2c_master still has 1 references, not deinitializing
I (230620) BOARD_MANAGER: Device audio_dac deinitialized
I (230630) BOARD_DEVICE: Deinit device audio_adc ref_count: 0 device_handle:0x3fcea6e0
I (230639) BOARD_DEVICE: Device audio_adc config found: 0x3c110830 (size: 384)
I (230640) BOARD_PERIPH: Deinit peripheral i2s_audio_in ref_count: 0
E (230646) i2s_common: i2s_channel_disable(1290): the channel has not been enabled yet
W (230654) PERIPH_I2S: Caution: Releasing RX (0).
I (230658) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 0
I (230664) PERIPH_I2C: I2C master bus deinitialized successfully
I (230670) BOARD_MANAGER: Device audio_adc deinitialized
I (230675) main_task: Returned from app_main()
```

## 故障排查

### SD 卡文件找不到或无法播放

- 检查 SD 卡是否挂载成功；
- 检查 `HOWL_MUSIC_URI` 路径与文件扩展名是否匹配；
- 可先用 `pipeline_play_sdcard_music` 示例验证板级 SD 卡与解码链路。

### 麦克风路超时（`HOWL_mic` timeout）

- 检查 ADC 是否初始化成功；
- 确认麦克风路上报的 `ESP_GMF_INFO_SOUND` 与采集参数一致（本示例为 16k/1ch/16bit）；
- 确认 HOWL 已启用且正常打开；
- 确认混音输入端未出现长期阻塞（ringbuf 连接和等待参数）。

## 技术支持

- 论坛：[esp32.com](https://esp32.com/viewforum.php?f=20)
- 问题反馈：[esp-gmf GitHub Issues](https://github.com/espressif/esp-gmf/issues)

我们会尽快回复。
