# 录制音频上传 HTTP 服务器

- [English Version](./README.md)
- 例程难度：⭐

## 例程简介

本示例展示了通过 CODEC_DEV_RX IO 采集 codec 录音，经 encoder 编码后通过 HTTP IO 将数据上传到 HTTP 服务器。

- 示例使用单管道架构：`io_codec_dev` → `aud_enc` → `io_http`。
- 支持 AAC、G711A、G711U、AMRNB、AMRWB、OPUS、ADPCM、LC3、SBC、PCM 等编码格式，默认 PCM。

### 典型场景

- 语音/会议录音并实时上传到云端或本地 HTTP 服务。

### 运行机制

- **管道数据流**：单管道依次为 `io_codec_dev`（麦克风采集）→ `aud_enc`（按配置格式编码）→ `io_http`（HTTP 上传）。编码格式、采样率、通道数等由配置或 `DEFAULT_*` 宏决定。
- **与服务器交互**：
  1. **发起请求**：设备在开始上传前向配置的 HTTP SERVER URL（如 `http://192.168.1.101:8000/upload`）发起 POST 请求，使用 **Transfer-Encoding: chunked** 流式上传；在请求头中携带 `Content-Type`（如 `audio/pcm`）、`x-audio-sample-rates`、`x-audio-bits`、`x-audio-channel`，供服务器识别音频参数。
  2. **服务器接收**：以本仓库提供的 `server.py` 为例，服务器监听 `/upload`，解析上述请求头，按 chunked 协议循环读取请求体中的音频数据块并拼接，直至收到长度为 0 的 chunk。
  3. **保存文件并返回**：服务器根据 `Content-Type` 将接收到的数据保存为对应格式文件（如 PCM 存为 WAV，AAC 存为 .aac），文件名含时间戳与音频参数；随后返回 HTTP 200 及简要信息（如 `File xxx was written, size xxx`），设备端从响应中可确认上传完成。
- 若使用自建服务器，需支持 POST、chunked 接收，并至少解析上述请求头与 `Content-Type`，以便正确保存或转发音频流。

## 环境配置

### 硬件要求

- **开发板**：默认以 ESP32-S3-Korvo V3 为例，其他 ESP 音频板同样适用。
- **资源要求**：麦克风、Audio ADC、Wi-Fi。

### 默认 IDF 分支

本例程支持 IDF release/v5.4 (>= v5.4.3) 与 release/v5.5 (>= v5.5.2) 分支。

### 软件要求

本例程需在电脑端搭建 HTTP 服务器以接收上传的音频流。`${PROJECT_DIR}/` 下提供测试脚本 `server.py`，运行后可启动默认 HTTP 服务器并保存接收到的音频文件，命令如下：

```
python $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_record_http/server.py
```

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
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_record_http
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

本例程可以修改源文件中以下定义完成相关配置：

- `DEFAULT_AUDIO_TYPE`：音频格式（如 AAC、OPUS）
- `DEFAULT_RECORD_SAMPLE_RATE`：采样率（如 16000 Hz）
- `DEFAULT_RECORD_BITS`：采样位数（如 16 bits）
- `DEFAULT_RECORD_CHANNEL`：音频通道数
- `DEFAULT_MICROPHONE_GAIN`：录制麦克风增益
- `DEFAULT_RECORD_DURATION_MS`：录制时长（如 15000 ms）

```bash
idf.py menuconfig
```

仍需在 menuconfig 中修改以下配置以适配实际需求：

- `GMF APP Configuration` → `Example Connection Configuration` → `WiFi SSID`
- `GMF APP Configuration` → `Example Connection Configuration` → `WiFi Password`
- `GMF Example Configuration` → `HTTP SERVER URL`（如 `http://192.168.1.101:8000/upload`）

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

- 电脑与开发板连接同一 Wi-Fi，在电脑上运行 `server.py` 启动 HTTP 服务器（成功时输出类似 `Serving HTTP on 192.168.1.101 port 8000`）。
- 开发板上电后自动连接 Wi-Fi、初始化录音 codec，开始录制并上传；录制结束后停止并释放资源。

### 日志输出

- 上电运行后会依次打印挂载外设、连接 Wi-Fi、注册元素、创建 pipeline、注册 HTTP IO、绑定任务、监听事件、启动 pipeline、上传完成并销毁资源。

```c
I (1022) main_task: Calling app_main()
I (1023) REC_HTTP: [ 1 ] Mount peripheral
I (1033) example_connect: Start example_connect.
I (1033) pp: pp rom version: e7ae62f
I (1034) net80211: net80211 rom version: e7ae62f
I (1039) wifi:wifi driver task: 3fcee7a0, prio:23, stack:6656, core=0
I (1046) wifi:wifi firmware version: 4df78f2
I (1048) wifi:wifi certification version: v7.0
I (1052) wifi:config NVS flash: enabled
I (1056) wifi:config nano formatting: disabled
I (1060) wifi:Init data frame dynamic rx buffer num: 32
I (1065) wifi:Init static rx mgmt buffer num: 5
I (1069) wifi:Init management short buffer num: 32
I (1074) wifi:Init static tx buffer num: 16
I (1078) wifi:Init tx cache buffer num: 32
I (1082) wifi:Init static tx FG buffer num: 2
I (1086) wifi:Init static rx buffer size: 1600
I (1090) wifi:Init static rx buffer num: 16
I (1094) wifi:Init dynamic rx buffer num: 32
I (1098) wifi_init: rx ba win: 16
I (1101) wifi_init: accept mbox: 6
I (1104) wifi_init: tcpip mbox: 32
I (1107) wifi_init: udp mbox: 6
I (1110) wifi_init: tcp mbox: 6
I (1113) wifi_init: tcp tx win: 5760
I (1116) wifi_init: tcp rx win: 5760
I (1119) wifi_init: tcp mss: 1440
I (1122) wifi_init: WiFi/LWIP prefer SPIRAM
I (1126) wifi_init: WiFi IRAM OP enabled
I (1130) wifi_init: WiFi RX IRAM OP enabled
I (1135) phy_init: phy_version 711,97bcf0a2,Aug 25 2025,19:04:10
I (1180) wifi:mode : sta (7c:df:a1:e7:71:6c)
I (1181) wifi:enable tsf
I (1183) example_connect: Connecting to ESP-Audio...
W (1183) wifi:Password length matches WPA2 standards, authmode threshold changes from OPEN to WPA2
I (1191) example_connect: Waiting for IP(s)
I (3661) wifi:new:<4,0>, old:<1,0>, ap:<255,255>, sta:<4,0>, prof:1, snd_ch_cfg:0x0
I (3662) wifi:state: init -> auth (0xb0)
I (3664) wifi:state: auth -> assoc (0x0)
I (3669) wifi:state: assoc -> run (0x10)
I (3675) wifi:<ba-add>idx:0 (ifx:0, 18:31:bf:4b:8b:68), tid:0, ssn:0, winSize:64
I (3700) wifi:connected with ESP-Audio, aid = 4, channel 4, BW20, bssid = 18:31:bf:4b:8b:68
I (3701) wifi:security: WPA2-PSK, phy: bgn, rssi: -28
I (3703) wifi:pm start, type: 1

I (3706) wifi:dp: 1, bi: 102400, li: 3, scale listen interval from 307200 us to 307200 us
I (3714) wifi:set rx beacon pti, rx_bcn_pti: 0, bcn_timeout: 25000, mt_pti: 0, mt_time: 10000
I (3776) wifi:AP's beacon interval = 102400 us, DTIM period = 3
I (5033) example_connect: Got IPv6 event: Interface "example_netif_sta" address: fe80:0000:0000:0000:7edf:a1ff:fee7:716c, type: ESP_IP6_ADDR_IS_LINK_LOCAL
I (5916) esp_netif_handlers: example_netif_sta ip: 192.168.1.100, mask: 255.255.255.0, gw: 192.168.1.1
I (5916) example_connect: Got IPv4 event: Interface "example_netif_sta" address: 192.168.1.100
I (5923) example_common: Connected to example_netif_sta
I (5927) example_common: - IPv4 address: 192.168.1.100,
I (5932) example_common: - IPv6 address: fe80:0000:0000:0000:7edf:a1ff:fee7:716c, type: ESP_IP6_ADDR_IS_LINK_LOCAL
I (5943) DEV_AUDIO_CODEC: ADC is ENABLED
I (5948) PERIPH_I2S: I2S[0] TDM, RX, ws: 45, bclk: 9, dout: 8, din: 10
I (5952) PERIPH_I2S: I2S[0] initialize success: 0x3c1b0d70
I (5958) DEV_AUDIO_CODEC: Init audio_adc, i2s_name: i2s_audio_in, i2s_rx_handle:0x3c1b0d70, i2s_tx_handle:0x3c1b0bb4, data_if: 0x3fcc8d68
I (5970) PERIPH_I2C: I2C master bus initialized successfully
I (5978) ES7210: Work in Slave mode
I (5985) ES7210: Enable ES7210_INPUT_MIC1
I (5988) ES7210: Enable ES7210_INPUT_MIC2
I (5991) ES7210: Enable ES7210_INPUT_MIC3
I (5994) ES7210: Enable TDM mode
I (5997) DEV_AUDIO_CODEC: Successfully initialized codec: audio_adc
I (5998) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fcc8f90, chip:es7210
I (6006) BOARD_MANAGER: Device audio_adc initialized
I (6010) BOARD_DEVICE: Device handle audio_adc found, Handle: 0x3fcc4494 TO: 0x3fcc4494
I (6021) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:1
I (6024) I2S_IF: TDM Mode 0 bits:16/16 channel:2 sample_rate:16000 mask:1
I (6030) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:1
I (6037) I2S_IF: TDM Mode 1 bits:16/16 channel:2 sample_rate:16000 mask:1
I (6043) ES7210: Bits 8
I (6051) ES7210: Enable ES7210_INPUT_MIC1
I (6054) ES7210: Enable ES7210_INPUT_MIC2
I (6057) ES7210: Enable ES7210_INPUT_MIC3
I (6060) ES7210: Enable TDM mode
I (6065) ES7210: Unmuted
I (6065) Adev_Codec: Open codec device OK
I (6065) REC_HTTP: [ 2 ] Register all the elements and set audio information to record codec device
I (6073) ESP_GMF_POOL: Registered items on pool:0x3c1b0b7c, app_main-227
I (6079) ESP_GMF_POOL: IO, Item:0x3c1b1554, H:0x3c1b1470, TAG:io_codec_dev
I (6086) ESP_GMF_POOL: IO, Item:0x3c1b1670, H:0x3c1b1564, TAG:io_http
I (6092) ESP_GMF_POOL: EL, Item:0x3c1b1778, H:0x3c1b1680, TAG:aud_enc
I (6098) REC_HTTP: [ 3 ] Create audio pipeline
I (6103) REC_HTTP: [ 3.1 ] Register the http io of the record pipeline
I (6109) REC_HTTP: [ 3.2 ] Reconfig audio encoder type and audio information and report information to the record pipeline
I (6119) REC_HTTP: [ 3.3 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (6130) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcc82ec-0x3fcc82ec, wk:0x0, run:0]
I (6137) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcc82ec-0x3fcc82ec, wk:0x3c1b1b6c, run:0]
I (6145) REC_HTTP: [ 3.4 ] Create envent group and listening event from pipeline
I (6152) REC_HTTP: [ 4 ] Start audio_pipeline
I (6158) ESP_GMF_BLOCK: The block buf:0x3c1b1c54, end:0x3c1c1c54
I (6162) NEW_DATA_BUS: New block buf, num:1, item_cnt:65536, db:0x3c1c1c58
I (6169) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcc86e8, wk:0x0, run:0]
I (6176) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcc86e8, wk:0x3c1c1ce4, run:0]
I (6170) ESP_GMF_HTTP: HTTP Open, URI = http://192.168.1.101:8000/upload
W (6191) REC_HTTP: [ + ] HTTP client HTTP_STREAM_PRE_REQUEST
I (6206) REC_HTTP: CB: RECV Pipeline EVT: el:NULL-0x3c1b1788, type:8192, sub:ESP_GMF_EVENT_STATE_OPENING, payload:0x0, size:0,0x0
I (6208) REC_HTTP: CB: RECV Pipeline EVT: el:aud_enc-0x3c1b17cc, type:12288, sub:ESP_GMF_EVENT_STATE_INITIALIZED, payload:0x3fcd2d64, size:16,0x0
I (6220) ESP_GMF_AENC: Open, type:PCM, acquire in frame: 320, out frame: 320
I (6227) REC_HTTP: CB: RECV Pipeline EVT: el:aud_enc-0x3c1b17cc, type:8192, sub:ESP_GMF_EVENT_STATE_RUNNING, payload:0x0, size:0,0x0
I (6238) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1b1b6c, ctx:0x3c1b17cc, label:aud_enc_open]
I (6248) ESP_GMF_PORT: ACQ IN, new self payload:0x3c1b1b6c, port:0x3c1b19bc, el:0x3c1b17cc-aud_enc
I (21207) ESP_GMF_CODEC_DEV: CLose, 0x3c1b18c4, pos = 480640/0
W (22208) REC_HTTP: [ + ] HTTP client HTTP_STREAM_POST_REQUEST, write end chunked marker
W (22223) REC_HTTP: [ + ] HTTP client HTTP_STREAM_FINISH_REQUEST
W (22226) HTTP_CLIENT: esp_transport_read returned:-1 and errno:128
I (22227) REC_HTTP: Got HTTP Response = File 20260313T072547Z_16000_16_1.wav was written, size 478464
I (22234) ESP_GMF_TASK: Abort, strategy action: 0, [tsk:0x3fcc86e8-io_http]
I (22240) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcc86e8, wk:0x0, run:0]
I (22243) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a0a0c, ctx:0x3c1b17cc, label:aud_enc_close]
I (22256) REC_HTTP: CB: RECV Pipeline EVT: el:NULL-0x3c1b1788, type:8192, sub:ESP_GMF_EVENT_STATE_STOPPED, payload:0x0, size:0,0x0
I (22268) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcc82ec-0x3fcc82ec, wk:0x0, run:0]
I (22275) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcc82ec-0x3fcc82ec, wk:0x0, run:0]
I (22283) REC_HTTP: [ 5 ] Destroy all the resources
W (22288) GMF_SETUP_AUD_CODEC: Unregistering default encoder
I (22297) BOARD_DEVICE: Deinit device audio_adc ref_count: 0 device_handle:0x3fcc4494
I (22301) BOARD_DEVICE: Device audio_adc config found: 0x3c1572dc (size: 92)
I (22307) BOARD_PERIPH: Deinit peripheral i2s_audio_in ref_count: 0
E (22313) i2s_common: i2s_channel_disable(1262): the channel has not been enabled yet
W (22321) PERIPH_I2S: Caution: Releasing RX (0x0).
I (22325) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 0
I (22331) PERIPH_I2C: I2C master bus deinitialized successfully
I (22337) BOARD_MANAGER: Device audio_adc deinitialized
I (22343) wifi:state: run -> init (0x0)
I (22346) wifi:pm stop, total sleep time: 6621552 us / 18640403 us

I (22352) wifi:<ba-del>idx:0, tid:0
I (22355) wifi:new:<4,0>, old:<4,0>, ap:<255,255>, sta:<4,0>, prof:1, snd_ch_cfg:0x0
I (22395) wifi:flush txq
I (22395) wifi:stop sw txq
I (22395) wifi:lmac stop hw txq
I (22397) wifi:Deinit lldesc rx mblock:16
I (22399) main_task: Returned from app_main()
```

- 服务器端成功接收后示例输出：

```
Do Post......
Audio information, sample rates: 16000, bits: 16, channel(s): 1
Total bytes received: 480000
192.168.1.101 - - [22/Oct/2025 20:15:53] "POST /upload HTTP/1.1" 200 -
```

## 故障排除

### 无法连接 Wi-Fi 或上传失败

- 确认 menuconfig 中 WiFi SSID/Password 与当前网络一致。
- 确认 HTTP SERVER URL 为电脑本机 IP 与端口（如 `http://192.168.1.101:8000/upload`），且 `server.py` 已在该机运行。

### 无录音或 codec 初始化失败

- 确认板级已正确配置录音 codec 与 I2S；若出现编码器相关错误，请检查 menuconfig 中对应编码格式是否已启用。

## 技术支持

请按照下面的链接获取技术支持：

- 技术支持参见 [esp32.com](https://esp32.com/viewforum.php?f=20) 论坛
- 问题反馈与功能需求，请创建 [GitHub issue](https://github.com/espressif/esp-gmf/issues)

我们会尽快回复。
