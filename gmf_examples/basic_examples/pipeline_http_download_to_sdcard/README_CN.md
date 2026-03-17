# HTTP 下载并存储到 microSD 卡

- [English Version](./README.md)
- 例程难度：⭐

## 例程简介

本示例展示了从指定 URL 通过 HTTP 下载文件并保存到 microSD 卡，完成后统计并打印下载与写入的整体速度。

- 示例使用 `io_http` 作为输入、`io_file` 作为输出，由 `copier` 将数据流写入本地文件。

### 典型场景

- 将固件、音频等资源从指定 URL 下载到设备本地存储，供离线使用或后续播放。
- 评估设备在真实网络下的 HTTP 下载与 SD 卡写入性能。

### 运行机制

- 单 pipeline，输入为 `io_http`、输出为 `io_file`，中间由 `copier` 从 HTTP 读入并写入文件；
- 通过 pipeline 事件等待 `OPENING` 与结束状态（`STOPPED`/`FINISHED`/`ERROR`），结束后从 HTTP io 获取总字节数并除以耗时得到速度。

## 环境配置

### 硬件要求

- **开发板**：默认以 ESP32-S3-Korvo V3 为例，其他 ESP 音频板同样适用。
- **资源要求**：Wi-Fi、microSD 卡。

### 默认 IDF 分支

本例程支持 IDF release/v5.4 (>= v5.4.3) 与 release/v5.5 (>= v5.5.2) 分支。

### 软件要求

- 默认下载 URL 为 Espressif 官方音频文件，无需额外服务端。若修改为自定义 URL，需确保该 URL 可被设备访问（如 HTTPS 证书有效）。

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
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_http_download_to_sdcard
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

```bash
idf.py menuconfig
```

在 menuconfig 中进行以下配置：

- `GMF APP Configuration` → `Example Connection Configuration` → `WiFi SSID`
- `GMF APP Configuration` → `Example Connection Configuration` → `WiFi Password`

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

- 上电后例程会自动：挂载 microSD 卡 → 连接 Wi-Fi → 从代码中配置的 URL 下载文件 → 将数据写入 microSD 卡指定路径 → 下载结束后打印整体速度并释放资源。
- 下载 URL 与保存路径可在 `main/http_download_to_sdcard.c` 中修改 `http_url` 和 `save_url`。默认下载 [ff-16b-2c-44100hz.opus](https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.opus)，保存为 `/sdcard/ff-16b-2c-44100hz.opus`。
- 将宏 `ONLY_ENABLE_HTTP` 设为 `true` 可仅测试 HTTP 下载（不写 SD 卡），用于排查网络或测量纯下载速度。

### 日志输出

- 正常流程会依次打印：挂载 SD 卡与连接 Wi-Fi、注册元素与创建 pipeline、启动 pipeline、等待结束事件，最后输出「Http download and write to sdcard speed: x.xx MB/s」并销毁资源。关键步骤以 `[ 1 ]`～`[ 6 ]` 标出。

```c
I (782) main_task: Calling app_main()
I (782) APP_MAIN: Func:app_main, Line:52, MEM Total:8654916 Bytes, Inter:301227 Bytes, Dram:301227 Bytes

I (792) HTTP_DOWNLOAD_TO_SDCARD: [ 1 ] Mount sdcard and connect to wifi
I (798) DEV_FS_FAT_SUB_SDMMC: slot_config: cd=-1, wp=-1, clk=15, cmd=7, d0=4, d1=-1, d2=-1, d3=-1, d4=-1, d5=-1, d6=-1, d7=-1, width=1, flags=0x1
Name: SA32G
Type: SDHC
Speed: 40.00 MHz (limit: 40.00 MHz)
Size: 29544MB
CSD: ver=2, sector_size=512, capacity=60506112 read_bl_len=9
SSR: bus_width=1
I (858) DEV_FS_FAT: Filesystem mounted, base path: /sdcard
I (863) BOARD_MANAGER: Device fs_sdcard initialized
I (879) example_connect: Start example_connect.
I (879) pp: pp rom version: e7ae62f
I (879) net80211: net80211 rom version: e7ae62f
I (881) wifi:wifi driver task: 3fceec00, prio:23, stack:6656, core=0
I (890) wifi:wifi firmware version: 4df78f2
I (890) wifi:wifi certification version: v7.0
I (894) wifi:config NVS flash: enabled
I (897) wifi:config nano formatting: disabled
I (901) wifi:Init data frame dynamic rx buffer num: 128
I (906) wifi:Init static rx mgmt buffer num: 5
I (910) wifi:Init management short buffer num: 32
I (915) wifi:Init static tx buffer num: 8
I (919) wifi:Init tx cache buffer num: 32
I (922) wifi:Init static tx FG buffer num: 2
I (926) wifi:Init static rx buffer size: 1600
I (930) wifi:Init static rx buffer num: 16
I (934) wifi:Init dynamic rx buffer num: 128
I (939) wifi_init: rx ba win: 32
I (941) wifi_init: accept mbox: 6
I (944) wifi_init: tcpip mbox: 64
I (947) wifi_init: udp mbox: 64
I (950) wifi_init: tcp mbox: 64
I (953) wifi_init: tcp tx win: 16384
I (956) wifi_init: tcp rx win: 65535
I (959) wifi_init: tcp mss: 1440
I (962) wifi_init: WiFi/LWIP prefer SPIRAM
I (966) wifi_init: WiFi IRAM OP enabled
I (970) wifi_init: WiFi RX IRAM OP enabled
I (974) phy_init: phy_version 711,97bcf0a2,Aug 25 2025,19:04:10
I (1013) wifi:mode : sta (7c:df:a1:e7:71:6c)
I (1013) wifi:enable tsf
I (1014) example_connect: Connecting to ESP-Audio...
W (1015) wifi:Password length matches WPA2 standards, authmode threshold changes from OPEN to WPA2
I (1023) example_connect: Waiting for IP(s)
I (3429) wifi:new:<4,0>, old:<1,0>, ap:<255,255>, sta:<4,0>, prof:1, snd_ch_cfg:0x0
I (3430) wifi:state: init -> auth (0xb0)
I (3432) wifi:state: auth -> assoc (0x0)
I (3436) wifi:state: assoc -> run (0x10)
I (3444) wifi:<ba-add>idx:0 (ifx:0, 18:31:bf:4b:8b:68), tid:0, ssn:0, winSize:64
I (3457) wifi:connected with ESP-Audio, aid = 4, channel 4, BW20, bssid = 18:31:bf:4b:8b:68
I (3457) wifi:security: WPA2-PSK, phy: bgn, rssi: -22
I (3459) wifi:pm start, type: 1

I (3462) wifi:dp: 1, bi: 102400, li: 3, scale listen interval from 307200 us to 307200 us
I (3470) wifi:set rx beacon pti, rx_bcn_pti: 0, bcn_timeout: 25000, mt_pti: 0, mt_time: 10000
I (3490) wifi:AP's beacon interval = 102400 us, DTIM period = 3
I (4496) esp_netif_handlers: example_netif_sta ip: 162.168.10.35, mask: 255.255.255.0, gw: 162.168.10.1
I (4496) example_connect: Got IPv4 event: Interface "example_netif_sta" address: 162.168.10.35
I (4879) example_connect: Got IPv6 event: Interface "example_netif_sta" address: fe80:0000:0000:0000:7edf:a1ff:fee7:716c, type: ESP_IP6_ADDR_IS_LINK_LOCAL
I (4882) example_common: Connected to example_netif_sta
I (4887) example_common: - IPv4 address: 162.168.10.35,
I (4892) example_common: - IPv6 address: fe80:0000:0000:0000:7edf:a1ff:fee7:716c, type: ESP_IP6_ADDR_IS_LINK_LOCAL
I (4902) HTTP_DOWNLOAD_TO_SDCARD: [ 2 ] Register elements and setup io
I (4908) ESP_GMF_POOL: Registered items on pool:0x3c1485f4, app_main-68
I (4915) ESP_GMF_POOL: IO, Item:0x3c148710, H:0x3c148608, TAG:io_file
I (4921) ESP_GMF_POOL: IO, Item:0x3c14882c, H:0x3c148720, TAG:io_http
I (4927) ESP_GMF_POOL: EL, Item:0x3c1488d4, H:0x3c14883c, TAG:copier
I (4933) HTTP_DOWNLOAD_TO_SDCARD: [ 3 ] Create pipeline
I (4938) HTTP_DOWNLOAD_TO_SDCARD: [ 3.1 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (4949) ESP_GMF_TASK: Waiting to run... [tsk:pipeline_task-0x3fcc13e4, wk:0x0, run:0]
I (4957) ESP_GMF_TASK: Waiting to run... [tsk:pipeline_task-0x3fcc13e4, wk:0x3c14a0e0, run:0]
I (4965) HTTP_DOWNLOAD_TO_SDCARD: [ 3.2 ] Create an event group and listen for events from the pipeline
I (4974) HTTP_DOWNLOAD_TO_SDCARD: [ 4 ] Start pipeline
I (4982) ESP_GMF_BLOCK: The block buf:0x3c14a198, end:0x3c16a198
I (4985) NEW_DATA_BUS: New block buf, num:1, item_cnt:131072, db:0x3c16a19c
I (4992) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcc1b20, wk:0x0, run:0]
I (4992) ESP_GMF_HTTP: HTTP Open, URI = https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.opus
I (4998) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcc1b20, wk:0x3c16ae2c, run:0]
I (5673) esp-x509-crt-bundle: Certificate validated
I (7794) ESP_GMF_HTTP: The total size is 0 bytes
I (7937) esp-x509-crt-bundle: Certificate validated
I (8155) ESP_GMF_HTTP: The total size is 2598621 bytes
I (8156) ESP_GMF_FILE: Open, dir:2, uri:/sdcard/ff-16b-2c-44100hz.opus
I (8160) HTTP_DOWNLOAD_TO_SDCARD: CB: RECV Pipeline EVT: el:NULL-0x3c1488e4, type:8192, sub:ESP_GMF_EVENT_STATE_OPENING, payload:0x0, size:0,0x3fcc19d4
I (8184) HTTP_DOWNLOAD_TO_SDCARD: CB: RECV Pipeline EVT: el:copier-0x3c148928, type:8192, sub:ESP_GMF_EVENT_STATE_RUNNING, payload:0x0, size:0,0x3fcc19d4
I (8199) ESP_GMF_TASK: One times job is complete, del[wk:0x3c14a0e0, ctx:0x3c148928, label:copier_open]
I (8206) ESP_GMF_PORT: ACQ IN, new self payload:0x3c14a0e0, port:0x3c148acc, el:0x3c148928-copier
I (8221) HTTP_DOWNLOAD_TO_SDCARD: [ 5 ] Wait stop event to the pipeline and stop all the pipeline
W (10008) ESP_GMF_HTTP: No more data, errno: 0, read bytes: 2598621, rlen = 0
I (10008) ESP_GMF_HTTP: No more data, ret: 0
I (10009) ESP_GMF_TASK: Job is done, [tsk:io_http-0x3fcc1b20, wk:0x3c16ae2c, job:0x3c1489c0-io_http_proc]
I (10017) ESP_GMF_TASK: Finish, strategy action: 0, [tsk:0x3fcc1b20-io_http]
I (10024) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcc1b20, wk:0x0, run:0]
I (10045) ESP_GMF_BLOCK: Done on read, wanted:477, h:0x3c14a160, r:0x3c14e698, w:0x3c14e875, we:0x3c14a198
I (10045) ESP_GMF_TASK: Job is done, [tsk:pipeline_task-0x3fcc13e4, wk:0x3c14a118, job:0x3c148928-copier_proc]
I (10053) ESP_GMF_TASK: Finish, strategy action: 0, [tsk:0x3fcc13e4-pipeline_task]
I (10062) ESP_GMF_FILE: CLose, 0x3c148b0c, pos = 2598621/0
I (10075) ESP_GMF_TASK: One times job is complete, del[wk:0x3c16c890, ctx:0x3c148928, label:copier_close]
I (10075) HTTP_DOWNLOAD_TO_SDCARD: CB: RECV Pipeline EVT: el:NULL-0x3c1488e4, type:8192, sub:ESP_GMF_EVENT_STATE_FINISHED, payload:0x0, size:0,0x3fcc19d4
I (10088) ESP_GMF_TASK: Waiting to run... [tsk:pipeline_task-0x3fcc13e4, wk:0x0, run:0]
I (10096) ESP_GMF_TASK: Waiting to run... [tsk:pipeline_task-0x3fcc13e4, wk:0x0, run:0]
I (10104) HTTP_DOWNLOAD_TO_SDCARD: Http download and write to sdcard speed: 1.32 MB/s
I (10111) HTTP_DOWNLOAD_TO_SDCARD: [ 6 ] Destroy all the resources
I (10118) wifi:state: run -> init (0x0)
I (10121) wifi:pm stop, total sleep time: 3382428 us / 6658961 us

I (10127) wifi:<ba-del>idx:0, tid:0
I (10130) wifi:new:<4,0>, old:<4,0>, ap:<255,255>, sta:<4,0>, prof:1, snd_ch_cfg:0x0
I (10139) wifi:flush txq
I (10140) wifi:stop sw txq
I (10142) wifi:lmac stop hw txq
I (10146) wifi:Deinit lldesc rx mblock:16
I (10150) BOARD_DEVICE: Deinit device fs_sdcard ref_count: 0 device_handle:0x3fce9a7c
I (10156) BOARD_DEVICE: Device fs_sdcard config found: 0x3c104404 (size: 84)
I (10163) DEV_FS_FAT: Sub device 'sdmmc' deinitialized successfully
I (10169) BOARD_MANAGER: Device fs_sdcard deinitialized
I (10174) APP_MAIN_END: Func:app_main, Line:141, MEM Total:8646456 Bytes, Inter:294123 Bytes, Dram:294123 Bytes

I (10184) main_task: Returned from app_main()
```

## 故障排除

### microSD 卡挂载失败

若日志出现 SD 卡初始化或挂载失败，请检查：开发板是否支持 SD 卡、卡槽接触是否良好、microSD 卡是否为 FAT 格式且未写保护。确认板级配置中已正确启用 SD 卡设备。

### Wi-Fi 连接失败

若无法获取 IP，请确认 menuconfig 中 `WiFi SSID` 与 `WiFi Password` 与当前路由器一致，且路由器频段与开发板支持的 2.4GHz 一致。

### HTTP 下载失败或证书错误

若出现连接超时或 TLS 证书校验失败，请确认设备能访问目标 URL（如可 ping 通域名），且 ESP-IDF 的证书 bundle 或自定义证书已正确配置。若使用自建 HTTPS 服务，需保证证书链完整且受信任。

### 仅需测试下载速度时

将源码中 `ONLY_ENABLE_HTTP` 设为 `true` 可只跑 HTTP 下载不写 SD 卡，便于在不插卡或卡异常时验证网络与下载速度。

## 技术支持

请按照下面的链接获取技术支持：

- 技术支持参见 [esp32.com](https://esp32.com/viewforum.php?f=20) 论坛
- 问题反馈与功能需求，请创建 [GitHub issue](https://github.com/espressif/esp-gmf/issues)

我们会尽快回复。
