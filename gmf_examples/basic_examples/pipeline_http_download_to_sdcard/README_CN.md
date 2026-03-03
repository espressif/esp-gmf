# HTTP 下载并存储到 microSD 卡

- [English](./README.md)
- 例程难度 ⭐

## 例程简介

本示例展示了如何基于 GMF 框架实现网络文件的高速下载和本地保存功能，同时支持下载速度的测量。

**核心流程**：`copier` 元素通过 `io_http` 获取网络文件后，使用 `io_file` 将文件写入到 microSD 卡中。

## 示例创建

### IDF 默认分支

本例程支持 IDF release/v5.4(>= v5.4.3) and release/v5.5(>= v5.5.2) 分支。

### 硬件准备

- 一张 microSD 卡（下载的文件将自动存储到其中）
- 支持 microSD 卡的 ESP32 开发板

## 配置说明

### 默认配置

- **下载链接**：[https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.opus](https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.opus)
- **存储文件名**：`ff-16b-2c-44100hz.opus`
- **配置参数**：可通过示例代码中 `http_url` 和 `save_url` 进行自定义配置

> **提示**：启用 `ONLY_ENABLE_HTTP` 选项时，仅执行文件下载而不会写入 microSD 卡，可用于网络连接和下载性能测试。

## 编译和下载

### 环境准备

编译前请确保已正确配置 ESP-IDF 开发环境。如果尚未配置，请在 ESP-IDF 根目录下运行以下命令：

```bash
./install.sh
. ./export.sh
```

详细的 ESP-IDF 配置和使用指南，请参考 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/index.html)。

### 编译步骤

1. **进入项目目录**
   ```bash
   cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_http_download_to_sdcard
   ```

2. **执行预编译脚本**（根据提示选择编译芯片，自动设置 IDF Action 扩展）
   在 Linux / macOS 中运行以下命令：
   ```bash
   source prebuild.sh
   ```

   在 Windows 中运行以下命令：
   ```powershell
   .\prebuild.ps1
   ```

3. **配置项目参数**（以 ESP32-S3-Korvo-2 为例）
   ```bash
   idf.py menuconfig
   ```

   在 menuconfig 中进行以下配置：
   - `GMF APP Configuration` → `Example Connection Configuration` → `WiFi SSID`
   - `GMF APP Configuration` → `Example Connection Configuration` → `WiFi Password`

   > 配置完成后按 `s` 保存，然后按 `Esc` 退出。

4. **编译项目**
   ```bash
   idf.py build
   ```

5. **烧录并监控**（请将 PORT 替换为实际的串口名称）
   ```bash
   idf.py -p PORT flash monitor
   ```

6. **退出监控模式**
   使用 `Ctrl-]` 组合键退出串口监控界面。

## 如何使用例程

### 功能描述

程序启动后会自动执行以下流程：
1. 挂载 microSD 卡并连接 WiFi 网络
2. 从指定 URL 下载文件
3. 将下载的文件存储到 microSD 卡
4. 完成后自动停止并退出

### 运行示例

程序运行时会输出详细的执行日志，包括系统启动、网络连接、文件下载和存储过程。  

以下是完整的运行输出（以 ESP32-S3 为例）：

```c
I (23) boot: ESP-IDF v5.4.1 2nd stage bootloader
I (23) boot: compile time Jul 10 2025 19:09:50
I (23) boot: Multicore bootloader
I (24) boot: chip revision: v0.1
I (26) boot: efuse block revision: v1.2
I (30) qio_mode: Enabling default flash chip QIO
I (34) boot.esp32s3: Boot SPI Speed : 80MHz
I (38) boot.esp32s3: SPI Mode       : QIO
I (42) boot.esp32s3: SPI Flash Size : 4MB
I (46) boot: Enabling RNG early entropy source...
I (50) boot: Partition Table:
I (53) boot: ## Label            Usage          Type ST Offset   Length
I (59) boot:  0 nvs              WiFi data        01 02 00009000 00006000
I (66) boot:  1 phy_init         RF data          01 01 0000f000 00001000
I (72) boot:  2 factory          factory app      00 00 00010000 00177000
I (79) boot: End of partition table
I (82) esp_image: segment 0: paddr=00010020 vaddr=3c0c0020 size=37824h (227364) map
I (123) esp_image: segment 1: paddr=0004784c vaddr=3fc9dc00 size=051a8h ( 20904) load
I (127) esp_image: segment 2: paddr=0004c9fc vaddr=40374000 size=0361ch ( 13852) load
I (130) esp_image: segment 3: paddr=00050020 vaddr=42000020 size=b1108h (725256) map
I (242) esp_image: segment 4: paddr=00101130 vaddr=4037761c size=164e8h ( 91368) load
I (260) esp_image: segment 5: paddr=00117620 vaddr=600fe100 size=0001ch (    28) load
I (270) boot: Loaded app from partition at offset 0x10000
I (270) boot: Disabling RNG early entropy source...
I (280) octal_psram: vendor id    : 0x0d (AP)
I (280) octal_psram: dev id       : 0x02 (generation 3)
I (280) octal_psram: density      : 0x03 (64 Mbit)
I (282) octal_psram: good-die     : 0x01 (Pass)
I (287) octal_psram: Latency      : 0x01 (Fixed)
I (291) octal_psram: VCC          : 0x01 (3V)
I (295) octal_psram: SRF          : 0x01 (Fast Refresh)
I (300) octal_psram: BurstType    : 0x01 (Hybrid Wrap)
I (305) octal_psram: BurstLen     : 0x01 (32 Byte)
I (309) octal_psram: Readlatency  : 0x02 (10 cycles@Fixed)
I (315) octal_psram: DriveStrength: 0x00 (1/1)
I (319) MSPI Timing: PSRAM timing tuning index: 5
I (323) esp_psram: Found 8MB PSRAM device
I (327) esp_psram: Speed: 80MHz
I (330) cpu_start: Multicore app
I (618) esp_psram: SPI SRAM memory test OK
I (627) cpu_start: Pro cpu start user code
I (627) cpu_start: cpu freq: 240000000 Hz
I (627) app_init: Application information:
I (627) app_init: Project name:     http_download_to_sdcard
I (632) app_init: App version:      688110b-dirty
I (636) app_init: Compile time:     Jul 10 2025 19:09:35
I (641) app_init: ELF file SHA256:  0e766a17e...
I (646) app_init: ESP-IDF:          v5.4.1
I (650) efuse_init: Min chip rev:     v0.0
I (653) efuse_init: Max chip rev:     v0.99
I (657) efuse_init: Chip rev:         v0.1
I (661) heap_init: Initializing. RAM available for dynamic allocation:
I (668) heap_init: At 3FCA7288 len 00042488 (265 KiB): RAM
I (673) heap_init: At 3FCE9710 len 00005724 (21 KiB): RAM
I (678) heap_init: At 3FCF0000 len 00008000 (32 KiB): DRAM
I (683) heap_init: At 600FE11C len 00001ECC (7 KiB): RTCRAM
I (688) esp_psram: Adding pool of 8192K of PSRAM memory to heap allocator
I (695) spi_flash: detected chip: gd
I (698) spi_flash: flash io: qio
W (701) spi_flash: Detected size(16384k) larger than the size in the binary image header(4096k). Using the size in the binary image header.
I (714) sleep_gpio: Configure to isolate all GPIO pins in sleep state
I (720) sleep_gpio: Enable automatic switching of GPIO sleep configuration
I (727) main_task: Started on CPU0
I (739) esp_psram: Reserving pool of 32K of internal memory for DMA/internal allocations
I (739) main_task: Calling app_main()
I (741) APP_MAIN: Func:app_main, Line:42, MEM Total:8665020 Bytes, Inter:311339 Bytes, Dram:311339 Bytes

I (750) HTTP_DOWNLOAD_TO_SDCARD: [ 1 ] Mount sdcard and connect to wifi
i2c: {sda: 17, scl: 18}
i2s: {mclk: 16, bclk: 9, ws: 45, din: 10, dout: 8}
out: {codec: ES8311, pa: 48, pa_gain: 6, use_mclk: 1, pa_gain: 6}
Codec 0 dir 2 type:1
in: {codec: ES7210}
Codec 1 dir 1 type:2
sdcard: {clk: 15, cmd: 7, d0: 4}
Sdcard clk:15 cmd:7 d0:4
camera: {type: dvp, xclk: 40, pclk: 11, vsync: 21, href: 38, d0: 13, d1: 47, d2: 14, d3: 3, d4: 12, d5: 42, d6: 41, d7: 39}
lcd: {bus: spi, extend_io: tca9554, controller: st7789, spi_bus: 2, mirror_x: 1, mirror_y: 1, swap_xy: 0, color_inv: 0, width: 320, height: 240, ctrl: ext1, rst: ext2, cs: ext3, dc: 2, clk: 1, mosi: 0, cmd_bits: 8, param_bits: 8}
use 4 0 0 0
I (812) gpio: GPIO[15]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (821) gpio: GPIO[7]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (829) gpio: GPIO[4]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (888) example_connect: Start example_connect.
I (889) pp: pp rom version: e7ae62f
I (889) net80211: net80211 rom version: e7ae62f
I (890) wifi:wifi driver task: 3fcb931c, prio:23, stack:6656, core=0
I (897) wifi:wifi firmware version: 79fa3f41ba
I (899) wifi:wifi certification version: v7.0
I (903) wifi:config NVS flash: enabled
I (907) wifi:config nano formatting: disabled
I (911) wifi:Init data frame dynamic rx buffer num: 128
I (916) wifi:Init static rx mgmt buffer num: 5
I (920) wifi:Init management short buffer num: 32
I (925) wifi:Init static tx buffer num: 8
I (929) wifi:Init tx cache buffer num: 32
I (932) wifi:Init static tx FG buffer num: 2
I (936) wifi:Init static rx buffer size: 1600
I (940) wifi:Init static rx buffer num: 16
I (944) wifi:Init dynamic rx buffer num: 128
I (948) wifi_init: rx ba win: 32
I (951) wifi_init: accept mbox: 6
I (954) wifi_init: tcpip mbox: 64
I (957) wifi_init: udp mbox: 64
I (960) wifi_init: tcp mbox: 64
I (962) wifi_init: tcp tx win: 16384
I (966) wifi_init: tcp rx win: 65535
I (969) wifi_init: tcp mss: 1440
I (972) wifi_init: WiFi/LWIP prefer SPIRAM
I (976) wifi_init: WiFi IRAM OP enabled
I (979) wifi_init: WiFi RX IRAM OP enabled
I (983) wifi_init: LWIP IRAM OP enabled
I (987) phy_init: phy_version 700,8582a7fd,Feb 10 2025,20:13:11
I (1024) wifi:mode : sta (7c:df:a1:e7:71:6c)
I (1025) wifi:enable tsf
I (1026) example_connect: Connecting to ESP-Audio...
W (1027) wifi:Password length matches WPA2 standards, authmode threshold changes from OPEN to WPA2
I (1034) example_connect: Waiting for IP(s)
I (3746) wifi:new:<4,0>, old:<1,0>, ap:<255,255>, sta:<4,0>, prof:1, snd_ch_cfg:0x0
I (3747) wifi:state: init -> auth (0xb0)
I (3749) wifi:state: auth -> assoc (0x0)
I (3754) wifi:state: assoc -> run (0x10)
I (3764) wifi:<ba-add>idx:0 (ifx:0, 18:31:bf:4b:8b:68), tid:0, ssn:0, winSize:64
I (3777) wifi:connected with ESP-Audio, aid = 2, channel 4, BW20, bssid = 18:31:bf:4b:8b:68
I (3778) wifi:security: WPA2-PSK, phy: bgn, rssi: -13
I (3779) wifi:pm start, type: 1

I (3782) wifi:dp: 1, bi: 102400, li: 3, scale listen interval from 307200 us to 307200 us
I (3790) wifi:set rx beacon pti, rx_bcn_pti: 0, bcn_timeout: 25000, mt_pti: 0, mt_time: 10000
I (3819) wifi:AP's beacon interval = 102400 us, DTIM period = 3
I (4888) example_connect: Got IPv6 event: Interface "example_netif_sta" address: fe80:0000:0000:0000:7edf:a1ff:fee7:716c, type: ESP_IP6_ADDR_IS_LINK_LOCAL
I (5852) esp_netif_handlers: example_netif_sta ip: 162.168.10.17, mask: 255.255.255.0, gw: 162.168.10.1
I (5852) example_connect: Got IPv4 event: Interface "example_netif_sta" address: 162.168.10.17
I (5858) example_common: Connected to example_netif_sta
I (5863) example_common: - IPv4 address: 162.168.10.17,
I (5868) example_common: - IPv6 address: fe80:0000:0000:0000:7edf:a1ff:fee7:716c, type: ESP_IP6_ADDR_IS_LINK_LOCAL
I (5878) HTTP_DOWNLOAD_TO_SDCARD: [ 2 ] Register elements and setup io
I (5887) ESP_GMF_BLOCK: The block buf:0x3c118578, end:0x3c138578
I (5890) NEW_DATA_BUS: New block buf, num:1, item_cnt:131072, db:0x3c1385a0
I (5897) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcc42c8, wk:0x0, run:0]
I (5897) ESP_GMF_POOL: Registered items on pool:0x3c1083dc, app_main-55
I (5904) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcc42c8, wk:0x3c138608, run:0]
I (5910) ESP_GMF_POOL: IO, Item:0x3c118468, H:0x3c1183dc, TAG:io_file
I (5924) ESP_GMF_POOL: IO, Item:0x3c1392d0, H:0x3c118478, TAG:io_http
I (5930) ESP_GMF_POOL: EL, Item:0x3c139374, H:0x3c1392e0, TAG:copier
I (5937) HTTP_DOWNLOAD_TO_SDCARD: [ 3 ] Create pipeline
I (5945) ESP_GMF_BLOCK: The block buf:0x3c13955c, end:0x3c15955c
I (5947) NEW_DATA_BUS: New block buf, num:1, item_cnt:131072, db:0x3c159560
I (5954) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcc4750, wk:0x0, run:0]
I (5954) HTTP_DOWNLOAD_TO_SDCARD: [ 3.1 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (5961) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcc4750, wk:0x3c15a1ec, run:0]
I (5972) ESP_GMF_TASK: Waiting to run... [tsk:pipeline_task-0x3fcc4a2c, wk:0x0, run:0]
I (5988) ESP_GMF_TASK: Waiting to run... [tsk:pipeline_task-0x3fcc4a2c, wk:0x3c15b7dc, run:0]
I (5973) HTTP_DOWNLOAD_TO_SDCARD: [ 3.2 ] Create event group and listening event from pipeline
I (6004) HTTP_DOWNLOAD_TO_SDCARD: [ 4 ] Start pipeline
I (6009) ESP_GMF_HTTP: HTTP Open, URI = https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.opus
I (8255) esp-x509-crt-bundle: Certificate validated
I (9462) ESP_GMF_HTTP: The total size is 0 bytes
I (9828) esp-x509-crt-bundle: Certificate validated
I (10062) ESP_GMF_HTTP: The total size is 2598621 bytes
I (10063) ESP_GMF_FILE: Open, dir:2, uri:/sdcard/ff-16b-2c-44100hz.opus
I (10070) HTTP_DOWNLOAD_TO_SDCARD: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c139384, type:8192, sub:ESP_GMF_EVENT_STATE_OPENING, payload:0x0, size:0,0x3fcc4d34
I (10078) HTTP_DOWNLOAD_TO_SDCARD: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c1393c8, type:8192, sub:ESP_GMF_EVENT_STATE_RUNNING, payload:0x0, size:0,0x3fcc4d34
I (10097) ESP_GMF_TASK: One times job is complete, del[wk:0x3c15b7dc, ctx:0x3c1393c8, label:copier_open]
I (10103) ESP_GMF_PORT: ACQ IN, new self payload:0x3c15b7dc, port:0x3c15a234, el:0x3c1393c8-copier
I (10127) HTTP_DOWNLOAD_TO_SDCARD: [ 5 ] Wait stop event to the pipeline and stop all the pipeline
W (11549) ESP_GMF_HTTP: No more data, errno: 0, read bytes: 2598621, rlen = 0
I (11550) ESP_GMF_TASK: Job is done, [tsk:io_http-0x3fcc4750, wk:0x3c15a1ec, job:0x3c13945c-io_http_proc]
I (11555) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcc4750, wk:0x0, run:0]
I (11563) ESP_GMF_BLOCK: Done on read, wanted:477, h:0x3c139524, r:0x3c13da5c, w:0x3c13dc39, we:0x3c13955c
I (11571) ESP_GMF_TASK: Job is done, [tsk:pipeline_task-0x3fcc4a2c, wk:0x3c15b810, job:0x3c1393c8-copier_proc]
W (11581) ESP_GMF_TASK: Already stopped, ESP_GMF_EVENT_STATE_FINISHED, [io_http,0x3fcc4750]
I (11590) ESP_GMF_FILE: CLose, 0x3c15a274, pos = 2598621/0
I (11608) ESP_GMF_TASK: One times job is complete, del[wk:0x3c15a1ec, ctx:0x3c1393c8, label:copier_close]
I (11609) HTTP_DOWNLOAD_TO_SDCARD: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c139384, type:8192, sub:ESP_GMF_EVENT_STATE_FINISHED, payload:0x0, size:0,0x3fcc4d34
I (11622) ESP_GMF_TASK: Waiting to run... [tsk:pipeline_task-0x3fcc4a2c, wk:0x0, run:0]
I (11629) ESP_GMF_TASK: Waiting to run... [tsk:pipeline_task-0x3fcc4a2c, wk:0x0, run:0]
W (11637) ESP_GMF_TASK: Already stopped, ESP_GMF_EVENT_STATE_FINISHED, [pipeline_task,0x3fcc4a2c]
I (11646) HTTP_DOWNLOAD_TO_SDCARD: Http download and write to sdcard speed: 1.63 MB/s
I (11653) HTTP_DOWNLOAD_TO_SDCARD: [ 6 ] Destroy all the resources
I (11660) wifi:state: run -> init (0x0)
I (11663) wifi:pm stop, total sleep time: 4434993 us / 7880884 us

I (11669) wifi:<ba-del>idx:0, tid:0
I (11672) wifi:new:<4,0>, old:<4,0>, ap:<255,255>, sta:<4,0>, prof:1, snd_ch_cfg:0x0
I (11681) wifi:flush txq
I (11682) wifi:stop sw txq
I (11684) wifi:lmac stop hw txq
I (11689) wifi:Deinit lldesc rx mblock:16
I (11692) gpio: GPIO[15]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (11699) gpio: GPIO[7]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (11708) gpio: GPIO[4]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (11716) APP_MAIN_END: Func:app_main, Line:129, MEM Total:8652264 Bytes, Inter:300163 Bytes, Dram:300163 Bytes

I (11726) main_task: Returned from app_main()

```
