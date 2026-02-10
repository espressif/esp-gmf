# 播放 HTTP 音乐

- [English](./README.md)

## 例程简介

本例程介绍了使用 HTTP IO 获取在线 HTTP 流传输的 MP3 格式音乐，然后经过 decoder 元素解码，解码后数据进行音频效果处理后用 CODEC_DEV_TX IO 输出音乐。

## 示例创建

### IDF 默认分支

本例程支持 IDF release/v5.3 及以后的分支。

### 配置

本例程需要先配置 Wi-Fi 连接信息，通过运行 `menuconfig > Example Configuration` 填写 `Wi-Fi SSID` 和 `Wi-Fi Password`。

### 编译和下载

编译本例程前需要先确保已配置 ESP-IDF 的环境，如果已配置可跳到下一项配置，如果未配置需要先在 ESP-IDF 根目录运行下面脚本设置编译环境，有关配置和使用 ESP-IDF 完整步骤，请参阅 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/index.html)：

```
./install.sh
. ./export.sh
```

下面是简略编译步骤：

- 进入播放 HTTP 音乐测试工程存放位置

```
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_play_http_music
```

- 选择编译芯片，以 esp32s3 为例：

```
idf.py set-target esp32s3
```
- 选择编译板子，以 esp32 s3 Korvo V2 为例：

```
idf.py menuconfig
在 `menuconfig` 中选择 `GMF APP Configuration` -> `Audio Board` -> `ESP32-S3-Korvo V2`，
设定 'Example Configuration' -> '(myssid) WiFi SSID' -> '(myssid) WiFi Password', 然后保存退出
```

- 编译例子程序

```
idf.py build
```

- 烧录程序并运行 monitor 工具来查看串口输出 (替换 PORT 为端口名称)：

```
idf.py -p PORT flash monitor
```

- 退出调试界面使用 ``Ctrl-]``

## 如何使用例程

### 功能和用法

- 例程开始运行后，自动播放 HTTP 获取的音乐文件，播放完成后停止退出，打印如下：

```c
I (1045) PLAY_HTTP_MUSIC: [ 1 ] Mount peripheral
E (1104) i2c.master: this port has not been initialized, please initialize it first
I (1112) CODEC_INIT: Set mater handle 0 0x3c1d236c
I (1116) CODEC_INIT: in:1 out:1 port: 1
I (1119) CODEC_INIT: Success to int i2c: 0
I (1123) CODEC_INIT: Init i2s 0 type: 3 mclk:16 bclk:9 ws:45 din:10 dout:8
I (1130) CODEC_INIT: tx:0x3c1d27e4 rx:0x3c1d2998
I (1134) CODEC_INIT: output init std ret 0
I (1138) CODEC_INIT: Input init std ret 0
I (1142) CODEC_INIT: Init i2s 0 ok
I (1145) CODEC_INIT: Success to init i2s: 0
I (1149) CODEC_INIT: Success to int i2c: 0
I (1153) CODEC_INIT: Success to init i2s: 0
I (1156) CODEC_INIT: Get out handle 0x3c1d27e4 port 0
I (1166) ES8311: Work in Slave mode
I (1172) ES7210: Work in Slave mode
I (1179) ES7210: Enable ES7210_INPUT_MIC1
I (1181) ES7210: Enable ES7210_INPUT_MIC3
I (1189) I2S_IF: channel mode 0 bits:16/16 channel:2 mask:3
I (1189) I2S_IF: STD Mode 1 bits:16/16 channel:2 sample_rate:48000 mask:3
I (1204) Adev_Codec: Open codec device OK
I (1206) I2S_IF: channel mode 0 bits:16/16 channel:2 mask:3
I (1206) I2S_IF: STD Mode 0 bits:16/16 channel:2 sample_rate:48000 mask:3
I (1210) ES7210: Bits 16
I (1218) ES7210: Enable ES7210_INPUT_MIC1
I (1221) ES7210: Enable ES7210_INPUT_MIC3
I (1228) ES7210: Unmuted
I (1228) Adev_Codec: Open codec device OK
I (1231) example_connect: Start example_connect.
I (1232) pp: pp rom version: e7ae62f
I (1232) net80211: net80211 rom version: e7ae62f
I (1238) wifi:wifi driver task: 3fcba5b4, prio:23, stack:6656, core=0
I (1244) wifi:wifi firmware version: cc37956
I (1247) wifi:wifi certification version: v7.0
I (1251) wifi:config NVS flash: enabled
I (1254) wifi:config nano formatting: disabled
I (1259) wifi:Init data frame dynamic rx buffer num: 32
I (1264) wifi:Init static rx mgmt buffer num: 5
I (1268) wifi:Init management short buffer num: 32
I (1272) wifi:Init static tx buffer num: 16
I (1276) wifi:Init tx cache buffer num: 32
I (1280) wifi:Init static tx FG buffer num: 2
I (1284) wifi:Init static rx buffer size: 1600
I (1288) wifi:Init static rx buffer num: 16
I (1292) wifi:Init dynamic rx buffer num: 32
I (1296) wifi_init: rx ba win: 16
I (1299) wifi_init: accept mbox: 6
I (1302) wifi_init: tcpip mbox: 32
I (1305) wifi_init: udp mbox: 6
I (1308) wifi_init: tcp mbox: 6
I (1311) wifi_init: tcp tx win: 5760
I (1314) wifi_init: tcp rx win: 5760
I (1318) wifi_init: tcp mss: 1440
I (1321) wifi_init: WiFi/LWIP prefer SPIRAM
I (1325) wifi_init: WiFi IRAM OP enabled
I (1328) wifi_init: WiFi RX IRAM OP enabled
I (1333) phy_init: phy_version 701,f4f1da3a,Mar  3 2025,15:50:10
I (1388) wifi:mode : sta (8c:bf:ea:86:71:04)
I (1389) wifi:enable tsf
I (1390) example_connect: Connecting to ESP-Audio...
I (1391) example_connect: Waiting for IP(s)
I (3866) wifi:new:<2,0>, old:<1,0>, ap:<255,255>, sta:<2,0>, prof:1, snd_ch_cfg:0x0
I (3867) wifi:state: init -> auth (0xb0)
I (3870) wifi:state: auth -> assoc (0x0)
I (3876) wifi:state: assoc -> run (0x10)
I (3979) wifi:[ADDBA]RX DELBA, reason:39, delete tid:0, initiator:1(originator)
I (3979) wifi:[ADDBA]RX DELBA, reason:39, delete tid:0, initiator:0(recipient)
I (4084) wifi:connected with ESP-Audio, aid = 2, channel 2, BW20, bssid = fc:2f:ef:ab:db:70
I (4084) wifi:security: WPA2-PSK, phy: bgn, rssi: -34
I (4086) wifi:pm start, type: 1
I (4089) wifi:set rx beacon pti, rx_bcn_pti: 0, bcn_timeout: 25000, mt_pti: 0, mt_time: 10000
I (4108) wifi:<ba-add>idx:0 (ifx:0, fc:2f:ef:ab:db:70), tid:0, ssn:5, winSize:64
I (4276) wifi:AP's beacon interval = 204800 us, DTIM period = 1
I (5106) esp_netif_handlers: example_netif_sta ip: 192.168.1.102, mask: 255.255.255.0, gw: 192.168.1.1
I (5106) example_connect: Got IPv4 event: Interface "example_netif_sta" address: 192.168.1.102
I (5231) example_connect: Got IPv6 event: Interface "example_netif_sta" address: fe80:0000:0000:0000:8ebf:eaff:fe86:7104, type: ESP_IP6_ADDR_IS_LINK_LOCAL
I (5233) example_common: Connected to example_netif_sta
I (5238) example_common: - IPv4 address: 192.168.1.102,
I (5243) example_common: - IPv6 address: fe80:0000:0000:0000:8ebf:eaff:fe86:7104, type: ESP_IP6_ADDR_IS_LINK_LOCAL
I (5254) PLAY_HTTP_MUSIC: [ 2 ] Register all the elements and set audio information to play codec device
I (5263) ESP_GMF_BLOCK: The block buf:0x3c1e3a24, end:0x3c1e5a24
I (5268) NEW_DATA_BUS: New block buf, num:1, item_cnt:8192, db:0x3c1e5a28
I (5275) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcc8d50, wk:0x0, run:0]
I (5282) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcc8d50, wk:0x3c1e5ab0, run:0]
I (5290) ESP_GMF_POOL: Registered items on pool:0x3c1e3678, app_main-57
I (5296) ESP_GMF_POOL: IO, Item:0x3c1e3724, H:0x3c1e368c, TAG:io_codec_dev
I (5303) ESP_GMF_POOL: IO, Item:0x3c1e37cc, H:0x3c1e3734, TAG:io_codec_dev
I (5309) ESP_GMF_POOL: IO, Item:0x3c1e3870, H:0x3c1e37dc, TAG:io_file
I (5315) ESP_GMF_POOL: IO, Item:0x3c1e3914, H:0x3c1e3880, TAG:io_file
I (5322) ESP_GMF_POOL: IO, Item:0x3c1e5af8, H:0x3c1e3924, TAG:io_http
I (5328) ESP_GMF_POOL: IO, Item:0x3c1e5ba8, H:0x3c1e5b08, TAG:io_embed_flash
I (5335) ESP_GMF_POOL: EL, Item:0x3c1e5cac, H:0x3c1e5bb8, TAG:aud_enc
I (5341) ESP_GMF_POOL: EL, Item:0x3c1e5dc8, H:0x3c1e5cbc, TAG:aud_dec
I (5347) ESP_GMF_POOL: EL, Item:0x3c1e5ebc, H:0x3c1e5dd8, TAG:aud_alc
I (5353) ESP_GMF_POOL: EL, Item:0x3c1e5f9c, H:0x3c1e5ecc, TAG:aud_ch_cvt
I (5359) ESP_GMF_POOL: EL, Item:0x3c1e6078, H:0x3c1e5fac, TAG:aud_bit_cvt
I (5366) ESP_GMF_POOL: EL, Item:0x3c1e615c, H:0x3c1e6088, TAG:aud_rate_cvt
I (5373) PLAY_HTTP_MUSIC: [ 3 ] Create http stream to read data
I (5379) ESP_GMF_BLOCK: The block buf:0x3c1e626c, end:0x3c1eb26c
I (5384) NEW_DATA_BUS: New block buf, num:1, item_cnt:20480, db:0x3c1eb270
I (5391) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcca184, wk:0x0, run:0]
I (5398) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcca184, wk:0x3c1ecafc, run:0]
I (5405) PLAY_HTTP_MUSIC: [ 4 ] Create audio pipeline
I (5411) ESP_GMF_BLOCK: The block buf:0x3c1ed184, end:0x3c1ef184
I (5416) NEW_DATA_BUS: New block buf, num:1, item_cnt:8192, db:0x3c1ef188
I (5422) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcca730, wk:0x0, run:0]
I (5430) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcca730, wk:0x3c1ef210, run:0]
I (5437) PLAY_HTTP_MUSIC: [ 4.1 ] Set audio url to play
I (5442) PLAY_HTTP_MUSIC: [ 5 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (5453) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fccb620-0x3fccb620, wk:0x0, run:0]
I (5460) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fccb620-0x3fccb620, wk:0x3c1ef3c0, run:0]
I (5468) PLAY_HTTP_MUSIC: [ 5.1 ] Create envent group and listening event from pipeline
I (5476) PLAY_HTTP_MUSIC: [ 5.2 ] Start audio_pipeline
I (5481) ESP_GMF_HTTP: HTTP Open, URI = https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.mp3
I (6215) ESP_GMF_HTTP: The total size is 2994349 bytes
I (6216) PLAY_HTTP_MUSIC: CB: RECV Pipeline EVT: el: OBJ_GET_TAG(event->from)-0x3c1ecb44, type: 2000, sub: ESP_GMF_EVENT_STATE_OPENING, payload: 0x0, size: 0, 0x3fccc910
I (6224) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1ef3c0, ctx:0x3c1ecb88, label:aud_dec_open]
I (6233) ESP_GMF_PORT: ACQ IN, new self payload:0x3c1ef3c0, port:0x3c1ef258, el:0x3c1ecb88-aud_dec
W (6243) ESP_GMF_ASMP_DEC: Not enough memory for out, need:4608, old: 1024, new: 4608
I (6252) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1f081c, ctx:0x3c1ecc94, label:aud_rate_cvt_open]
I (6259) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1f08f4, ctx:0x3c1ecde8, label:aud_ch_cvt_open]
I (6269) PLAY_HTTP_MUSIC: CB: RECV Pipeline EVT: el: OBJ_GET_TAG(event->from)-0x3c1ecf38, type: 3000, sub: ESP_GMF_EVENT_STATE_INITIALIZED, payload: 0x3fccc560, size: 16, 0x3fccc910
I (6285) PLAY_HTTP_MUSIC: CB: RECV Pipeline EVT: el: OBJ_GET_TAG(event->from)-0x3c1ecf38, type: 2000, sub: ESP_GMF_EVENT_STATE_RUNNING, payload: 0x0, size: 0, 0x3fccc910
I (6299) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1f081c, ctx:0x3c1ecf38, label:aud_bit_cvt_open]
I (6313) PLAY_HTTP_MUSIC: [ 5.3 ] Wait stop event to the pipeline and stop all the pipeline
W (179717) ESP_GMF_HTTP: No more data, errno: 0, read bytes: 2994349, rlen = 0
I (179717) ESP_GMF_TASK: Job is done, [tsk:io_http-0x3fcca730, wk:0x3c1ef210, job:0x3c1ed084-io_http_proc]
I (179722) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcca730, wk:0x0, run:0]
I (179860) ESP_GMF_BLOCK: Done on read, wanted:173, h:0x3c1ed14c, r:0x3c1ed984, w:0x3c1eda31, we:0x3c1ed184
W (179885) ESP_GMF_BLOCK: Done set on read, h:0x3c1ed14c, rd:0x3c1eda31, wr:0x3c1eda31, wr_e:0x3c1ed184
I (179885) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fccb620-0x3fccb620, wk:0x3c1ef3f8, job:0x3c1ecb88-aud_dec_proc]
I (179893) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fccb620-0x3fccb620, wk:0x3c1f08d0, job:0x3c1ecc94-aud_rate_cvt_proc]
I (179903) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fccb620-0x3fccb620, wk:0x3c1f0918, job:0x3c1ecde8-aud_ch_cvt_proc]
I (179913) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fccb620-0x3fccb620, wk:0x3c1f0950, job:0x3c1ecf38-aud_bit_cvt_proc]
W (179924) ESP_GMF_TASK: Already stopped, ESP_GMF_EVENT_STATE_FINISHED, [io_http,0x3fcca730]
I (179933) ESP_GMF_CODEC_DEV: CLose, 0x3c1ef298, pos = 33007104/0
I (179938) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1f081c, ctx:0x3c1ecb88, label:aud_dec_close]
I (179947) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1ef210, ctx:0x3c1ecc94, label:aud_rate_cvt_close]
I (179957) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1ef3e4, ctx:0x3c1ecde8, label:aud_ch_cvt_close]
I (179967) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1f0790, ctx:0x3c1ecf38, label:aud_bit_cvt_close]
I (179977) PLAY_HTTP_MUSIC: CB: RECV Pipeline EVT: el: OBJ_GET_TAG(event->from)-0x3c1ecb44, type: 2000, sub: ESP_GMF_EVENT_STATE_FINISHED, payload: 0x0, size: 0, 0x3fccc910
I (179992) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fccb620-0x3fccb620, wk:0x0, run:0]
I (180000) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fccb620-0x3fccb620, wk:0x0, run:0]
W (180007) ESP_GMF_TASK: Already stopped, ESP_GMF_EVENT_STATE_FINISHED, [TSK_0x3fccb620,0x3fccb620]
I (180016) PLAY_HTTP_MUSIC: [ 6 ] Destroy all the resources
```

## 故障排除

如果您的日志有如下的错误提示，请修改 `sdkconfig` 中对于SSL sever vacation 设置:

运行 `menuconfig > Component config > ESP LTS` 勾选 `allow potentially insecure options > Skip server verification by default`

```c
I (5477) PLAY_HTTP_MUSIC: [ 5.2 ] Start audio_pipeline
I (5482) ESP_GMF_HTTP: HTTP Open, URI = https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.mp3
E (5577) esp-tls-mbedtls: No server verification option set in esp_tls_cfg_t structure. Check esp_tls API reference
E (5577) esp-tls-mbedtls: Failed to set client configurations, returned [0x8017] (ESP_ERR_MBEDTLS_SSL_SETUP_FAILED)
E (5586) esp-tls: create_ssl_handle failed
E (5590) esp-tls: Failed to open new connection
E (5594) transport_base: Failed to open a new connection
E (5600) HTTP_CLIENT: Connection failed, sock < 0
E (5604) ESP_GMF_HTTP: Failed to open http stream
E (5608) ESP_GMF_IO: esp_gmf_io_open(66): esp_gmf_io_open failed
E (5614) ESP_GMF_PIPELINE: Failed to open the in port, ret:28674,[0x3fccb618-TSK_0x3fccb618]
```
