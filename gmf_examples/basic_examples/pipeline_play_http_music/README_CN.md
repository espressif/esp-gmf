# 播放 HTTP 音乐

- [English Version](./README.md)
- 例程难度：⭐

## 例程简介

本示例展示了通过 HTTP I/O 获取网络上的音频文件（不支持 m3u8 URL），经 decoder 解码及基本音效处理后通过 CODEC_DEV_TX I/O 输出播放。

- 示例使用单管道架构：`io_http` → decoder → 音效处理 → `io_codec_dev`。
- 支持 MP3 等格式，需设备连接 Wi-Fi。

### 典型场景

- 在线音乐、网络电台等从 HTTP URL 流式播放的场景。

## 环境配置

### 硬件要求

- **开发板**：默认以 ESP32-S3-Korvo V3 为例，其他 ESP 音频板同样适用。
- **资源要求**：Wi-Fi、Audio DAC、扬声器。

### 默认 IDF 分支

本例程支持 IDF release/v5.4 (>= v5.4.3) 与 release/v5.5 (>= v5.5.2) 分支。

## 编译和下载

### 编译准备

编译本例程前需先确保已配置 ESP-IDF 环境；若已配置可跳过本段，直接进入工程目录并运行相关预编译脚本。若未配置，请在 ESP-IDF 根目录运行以下脚本完成环境设置，完整步骤请参阅 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/index.html)。

```
./install.sh
. ./export.sh
```

下面是简略步骤：

- 进入本例程工程目录：

```
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_play_http_music
```

- 执行预编译脚本，根据提示选择编译芯片，自动设置 IDF Action 扩展，通过 `esp_board_manager` 选择支持的开发板，如需选择自定义开发板，详情参考：[自定义板子](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md#custom-board)

在 Linux / macOS 中运行以下命令：
```bash/zsh
source prebuild.sh
```

在 Windows 中运行以下命令：
```powershell
.\prebuild.ps1
```

### 项目配置

- 在 menuconfig 中配置 Wi-Fi SSID 与密码，以便设备连接网络进行 HTTP 下载。
- 若需调整音频效果相关参数（采样率、声道、位深等），可在 menuconfig 中配置 GMF Audio 相关选项。

```bash
idf.py menuconfig
```

在 menuconfig 中进行以下配置：

- `GMF APP Configuration` → `Example Connection Configuration` → `WiFi SSID`
- `GMF APP Configuration` → `Example Connection Configuration` → `WiFi Password`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `Channel Convert Destination Channel`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `Bit Convert Destination Bits`
- `ESP GMF Loader` → `GMF Audio Configurations` → `GMF Audio Effects` → `Rate Convert Destination Rate`

```bash
idf.py menuconfig
```

> 配置完成后按 `s` 保存，然后按 `Esc` 退出。

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

- 上电后例程会连接 Wi-Fi、初始化音频，从配置的 HTTP URL 拉取音频并播放，播放完成后停止并释放资源。

### 日志输出

- 正常流程会依次打印挂载外设、连接 Wi-Fi、注册元素、创建 HTTP 流、创建 pipeline、设置 url、绑定任务、监听事件、启动 pipeline、打开 HTTP 并播放、等待结束并销毁资源。关键步骤以 `[ 1 ]`～`[ 6 ]` 标出。

```c
I (900) main_task: Calling app_main()
I (900) PIPELINE_PLAY_HTTP_MUSIC: Func:app_main, Line:86, MEM Total:7332812 Bytes, Inter:287023 Bytes, Dram:287023 Bytes

I (911) PIPELINE_PLAY_HTTP_MUSIC: [ 1 ] Mount peripheral
I (916) PERIPH_I2S: I2S[0] TDM,  TX, ws: 45, bclk: 9, dout: 8, din: 10
I (922) PERIPH_I2S: I2S[0] initialize success: 0x3c13f770
I (927) DEV_AUDIO_CODEC: DAC is ENABLED
I (931) DEV_AUDIO_CODEC: Init audio_dac, i2s_name: i2s_audio_out, i2s_rx_handle:0x0, i2s_tx_handle:0x3c13f770, data_if: 0x3fcea7f4
I (943) PERIPH_I2C: I2C master bus initialized successfully
I (953) ES8311: Work in Slave mode
I (955) DEV_AUDIO_CODEC: Successfully initialized codec: audio_dac
I (957) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fceaa48, chip:es8311
I (964) BOARD_MANAGER: Device audio_dac initialized
I (969) BOARD_DEVICE: Device handle audio_dac found, Handle: 0x3fce9a7c TO: 0x3fce9a7c
I (977) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:3
I (982) I2S_IF: TDM Mode 1 bits:16/16 channel:2 sample_rate:48000 mask:3
I (1002) Adev_Codec: Open codec device OK
I (1009) example_connect: Start example_connect.
I (1009) pp: pp rom version: e7ae62f
I (1009) net80211: net80211 rom version: e7ae62f
I (1011) wifi:wifi driver task: 3fceea78, prio:23, stack:6656, core=0
I (1018) wifi:wifi firmware version: 4df78f2
I (1020) wifi:wifi certification version: v7.0
I (1024) wifi:config NVS flash: enabled
I (1027) wifi:config nano formatting: disabled
I (1032) wifi:Init data frame dynamic rx buffer num: 32
I (1037) wifi:Init static rx mgmt buffer num: 5
I (1041) wifi:Init management short buffer num: 32
I (1046) wifi:Init static tx buffer num: 16
I (1050) wifi:Init tx cache buffer num: 32
I (1053) wifi:Init static tx FG buffer num: 2
I (1057) wifi:Init static rx buffer size: 1600
I (1061) wifi:Init static rx buffer num: 16
I (1065) wifi:Init dynamic rx buffer num: 32
I (1070) wifi_init: rx ba win: 16
I (1072) wifi_init: accept mbox: 6
I (1075) wifi_init: tcpip mbox: 32
I (1079) wifi_init: udp mbox: 6
I (1081) wifi_init: tcp mbox: 6
I (1084) wifi_init: tcp tx win: 5760
I (1088) wifi_init: tcp rx win: 5760
I (1091) wifi_init: tcp mss: 1440
I (1094) wifi_init: WiFi/LWIP prefer SPIRAM
I (1098) wifi_init: WiFi IRAM OP enabled
I (1101) wifi_init: WiFi RX IRAM OP enabled
I (1106) phy_init: phy_version 711,97bcf0a2,Aug 25 2025,19:04:10
I (1151) wifi:mode : sta (7c:df:a1:e7:71:6c)
I (1151) wifi:enable tsf
I (1152) example_connect: Connecting to ESP-Audio...
W (1152) wifi:Password length matches WPA2 standards, authmode threshold changes from OPEN to WPA2
I (1160) example_connect: Waiting for IP(s)
I (3563) wifi:new:<4,0>, old:<1,0>, ap:<255,255>, sta:<4,0>, prof:1, snd_ch_cfg:0x0
I (3564) wifi:state: init -> auth (0xb0)
I (3571) wifi:state: auth -> assoc (0x0)
I (3574) wifi:state: assoc -> run (0x10)
I (3576) wifi:<ba-add>idx:0 (ifx:0, 18:31:bf:4b:8b:68), tid:0, ssn:0, winSize:64
I (3588) wifi:connected with ESP-Audio, aid = 4, channel 4, BW20, bssid = 18:31:bf:4b:8b:68
I (3588) wifi:security: WPA2-PSK, phy: bgn, rssi: -27
I (3590) wifi:pm start, type: 1

I (3593) wifi:dp: 1, bi: 102400, li: 3, scale listen interval from 307200 us to 307200 us
I (3601) wifi:set rx beacon pti, rx_bcn_pti: 0, bcn_timeout: 25000, mt_pti: 0, mt_time: 10000
I (3632) wifi:AP's beacon interval = 102400 us, DTIM period = 3
I (5009) example_connect: Got IPv6 event: Interface "example_netif_sta" address: fe80:0000:0000:0000:7edf:a1ff:fee7:716c, type: ESP_IP6_ADDR_IS_LINK_LOCAL
I (5879) esp_netif_handlers: example_netif_sta ip: 162.168.10.35, mask: 255.255.255.0, gw: 162.168.10.1
I (5879) example_connect: Got IPv4 event: Interface "example_netif_sta" address: 162.168.10.35
I (5885) example_common: Connected to example_netif_sta
I (5890) example_common: - IPv4 address: 162.168.10.35,
I (5895) example_common: - IPv6 address: fe80:0000:0000:0000:7edf:a1ff:fee7:716c, type: ESP_IP6_ADDR_IS_LINK_LOCAL
I (5905) PIPELINE_PLAY_HTTP_MUSIC: [ 2 ] Register all the elements and set audio information to play codec device
I (5915) ESP_GMF_POOL: Registered items on pool:0x3c14096c, app_main-103
I (5921) ESP_GMF_POOL: IO, Item:0x3c150b8c, H:0x3c150aa8, TAG:io_codec_dev
I (5928) ESP_GMF_POOL: IO, Item:0x3c150ca8, H:0x3c150b9c, TAG:io_http
I (5934) ESP_GMF_POOL: EL, Item:0x3c150dc8, H:0x3c150cb8, TAG:aud_dec
I (5940) ESP_GMF_POOL: EL, Item:0x3c150ec4, H:0x3c150dd8, TAG:aud_alc
I (5947) ESP_GMF_POOL: EL, Item:0x3c150fac, H:0x3c150ed4, TAG:aud_ch_cvt
I (5953) ESP_GMF_POOL: EL, Item:0x3c151090, H:0x3c150fbc, TAG:aud_bit_cvt
I (5959) ESP_GMF_POOL: EL, Item:0x3c15117c, H:0x3c1510a0, TAG:aud_rate_cvt
I (5966) PIPELINE_PLAY_HTTP_MUSIC: [ 3 ] Create audio pipeline
I (5972) PIPELINE_PLAY_HTTP_MUSIC: [ 4 ] Set audio url and format to play
I (5978) PIPELINE_PLAY_HTTP_MUSIC: [ 5 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (5989) ESP_GMF_TASK: Waiting to run... [tsk:pipeline_task-0x3fcca2d4, wk:0x0, run:0]
I (5997) ESP_GMF_TASK: Waiting to run... [tsk:pipeline_task-0x3fcca2d4, wk:0x3c1519cc, run:0]
I (6005) PIPELINE_PLAY_HTTP_MUSIC: [ 5.1 ] Create an event group and listen for events from the pipeline
I (6014) PIPELINE_PLAY_HTTP_MUSIC: [ 5.2 ] Start audio_pipeline
I (6020) ESP_GMF_BLOCK: The block buf:0x3c151b1c, end:0x3c159b1c
I (6026) NEW_DATA_BUS: New block buf, num:1, item_cnt:32768, db:0x3c159b20
I (6032) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fccb6d4, wk:0x0, run:0]
I (6039) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fccb6d4, wk:0x3c159b9c, run:0]
I (6033) ESP_GMF_HTTP: HTTP Open, URI = https://dl.espressif.com/dl/audio/gs-16b-2c-44100hz.mp3
I (6056) PIPELINE_PLAY_HTTP_MUSIC: [ 5.3 ] Wait stop event to the pipeline and stop all the pipeline
I (7044) esp-x509-crt-bundle: Certificate validated
I (8243) ESP_GMF_HTTP: The total size is 0 bytes
I (8578) esp-x509-crt-bundle: Certificate validated
I (9167) ESP_GMF_HTTP: The total size is 254415 bytes
I (9168) PIPELINE_PLAY_HTTP_MUSIC: Receive pipeline event: el: NULL, type: 2000, sub: ESP_GMF_EVENT_STATE_OPENING
I (9171) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1519cc, ctx:0x3c1511d0, label:aud_dec_open]
I (9180) ESP_GMF_PORT: ACQ IN, new self payload:0x3c1519cc, port:0x3c1517f4, el:0x3c1511d0-aud_dec
I (9240) ESP_ES_PARSER: The verion of es_parser is v1.0.0
W (9242) ESP_GMF_ASMP_DEC: Not enough memory for out, need:4608, old: 1024, new: 4608
I (9337) ESP_GMF_TASK: One times job is complete, del[wk:0x3c15afb0, ctx:0x3c1512e0, label:aud_rate_cvt_open]
I (9338) ESP_GMF_TASK: One times job is complete, del[wk:0x3c15b1a0, ctx:0x3c15143c, label:aud_ch_cvt_open]
I (9345) PIPELINE_PLAY_HTTP_MUSIC: Receive pipeline event: el: aud_bit_cvt, type: 3000, sub: ESP_GMF_EVENT_STATE_INITIALIZED
I (9356) PIPELINE_PLAY_HTTP_MUSIC: Receive pipeline event: el: aud_bit_cvt, type: 2000, sub: ESP_GMF_EVENT_STATE_RUNNING
I (9367) ESP_GMF_TASK: One times job is complete, del[wk:0x3c15b1f0, ctx:0x3c151594, label:aud_bit_cvt_open]
W (23465) ESP_GMF_HTTP: No more data, errno: 0, read bytes: 254415, rlen = 0
I (23465) ESP_GMF_HTTP: No more data, ret: 0
I (23465) ESP_GMF_TASK: Job is done, [tsk:io_http-0x3fccb6d4, wk:0x3c159b9c, job:0x3c1516e8-io_http_proc]
I (23474) ESP_GMF_TASK: Finish, strategy action: 0, [tsk:0x3fccb6d4-io_http]
I (23480) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fccb6d4, wk:0x0, run:0]
I (25158) ESP_GMF_BLOCK: Done on read, wanted:463, h:0x3c151ae4, r:0x3c153b1c, w:0x3c153ceb, we:0x3c151b1c
I (25190) ESP_GMF_TASK: Job is done, [tsk:pipeline_task-0x3fcca2d4, wk:0x3c151a08, job:0x3c1511d0-aud_dec_proc]
I (25192) ESP_GMF_TASK: Job is done, [tsk:pipeline_task-0x3fcca2d4, wk:0x3c15afd8, job:0x3c1512e0-aud_rate_cvt_proc]
I (25199) ESP_GMF_TASK: Job is done, [tsk:pipeline_task-0x3fcca2d4, wk:0x3c15b1c8, job:0x3c15143c-aud_ch_cvt_proc]
I (25218) ESP_GMF_TASK: Job is done, [tsk:pipeline_task-0x3fcca2d4, wk:0x3c15b218, job:0x3c151594-aud_bit_cvt_proc]
I (25219) ESP_GMF_TASK: Finish, strategy action: 0, [tsk:0x3fcca2d4-pipeline_task]
I (25228) ESP_GMF_CODEC_DEV: CLose, 0x3c151834, pos = 3044620/0
I (25232) ESP_GMF_TASK: One times job is complete, del[wk:0x3c15afd8, ctx:0x3c1511d0, label:aud_dec_close]
I (25242) ESP_GMF_TASK: One times job is complete, del[wk:0x3c15af54, ctx:0x3c1512e0, label:aud_rate_cvt_close]
I (25251) ESP_GMF_TASK: One times job is complete, del[wk:0x3c159b9c, ctx:0x3c15143c, label:aud_ch_cvt_close]
I (25261) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1519f0, ctx:0x3c151594, label:aud_bit_cvt_close]
I (25271) PIPELINE_PLAY_HTTP_MUSIC: Receive pipeline event: el: NULL, type: 2000, sub: ESP_GMF_EVENT_STATE_FINISHED
I (25281) ESP_GMF_TASK: Waiting to run... [tsk:pipeline_task-0x3fcca2d4, wk:0x0, run:0]
I (25289) ESP_GMF_TASK: Waiting to run... [tsk:pipeline_task-0x3fcca2d4, wk:0x0, run:0]
I (25296) PIPELINE_PLAY_HTTP_MUSIC: [ 6 ] Destroy all the resources
W (25303) GMF_SETUP_AUD_CODEC: Unregistering default decoder
I (25308) wifi:state: run -> init (0x0)
I (25311) wifi:pm stop, total sleep time: 16707894 us / 21717999 us

I (25318) wifi:<ba-del>idx:0, tid:0
I (25321) wifi:new:<4,0>, old:<4,0>, ap:<255,255>, sta:<4,0>, prof:1, snd_ch_cfg:0x0
I (25329) wifi:flush txq
I (25330) wifi:stop sw txq
I (25333) wifi:lmac stop hw txq
I (25337) wifi:Deinit lldesc rx mblock:16
I (25346) BOARD_DEVICE: Deinit device audio_dac ref_count: 0 device_handle:0x3fce9a7c
I (25352) BOARD_DEVICE: Device audio_dac config found: 0x3c116798 (size: 92)
I (25354) BOARD_PERIPH: Deinit peripheral i2s_audio_out ref_count: 0
E (25360) i2s_common: i2s_channel_disable(1262): the channel has not been enabled yet
W (25368) PERIPH_I2S: Caution: Releasing TX (0x0).
W (25372) PERIPH_I2S: Caution: RX (0x3c13f940) forced to stop.
E (25377) i2s_common: i2s_channel_disable(1262): the channel has not been enabled yet
I (25385) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 0
I (25391) PERIPH_I2C: I2C master bus deinitialized successfully
I (25397) BOARD_MANAGER: Device audio_dac deinitialized
I (25401) PIPELINE_PLAY_HTTP_MUSIC: Func:app_main, Line:154, MEM Total:7324412 Bytes, Inter:279327 Bytes, Dram:279327 Bytes

I (25413) main_task: Returned from app_main()
```

## 故障排除

### HTTPS/SSL 校验失败

若日志出现与 SSL server verification 相关的错误，可在 menuconfig 中放宽校验：`Component config` → `ESP-TLS` → 勾选 `Allow potentially insecure options` → `Skip server verification by default`（仅测试环境使用）。

## 技术支持

请按照下面的链接获取技术支持：

- 技术支持参见 [esp32.com](https://esp32.com/viewforum.php?f=20) 论坛
- 问题反馈与功能需求，请创建 [GitHub issue](https://github.com/espressif/esp-gmf/issues)

我们会尽快回复。
