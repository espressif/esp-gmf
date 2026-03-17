# HTTP Download and Store to microSD Card

- [中文版](./README_CN.md)
- Basic Example: ⭐

## Example Brief

This example demonstrates downloading a file from a given URL over HTTP and saving it to a microSD card, then printing the overall download and write speed.

- It uses `io_http` as input and `io_file` as output, with a `copier` element writing the stream to a local file.

### Typical Scenarios

- Downloading firmware, audio, or other resources from a URL to local storage for offline use or playback.
- Measuring HTTP download and SD card write performance on real networks.

### Run Flow

- Single pipeline: input `io_http`, output `io_file`, with `copier` in between reading from HTTP and writing to file. 
- The pipeline waits for `OPENING` and then for a terminal state (`STOPPED`/`FINISHED`/`ERROR`); after finish, total bytes from the HTTP io are divided by elapsed time to get the speed.

## Environment Setup

### Hardware Required

- **Board**: ESP32-S3-Korvo V3 by default; other ESP audio boards are also supported.
- **Resource requirements**: Wi-Fi, microSD card.

### Default IDF Branch

This example supports IDF release/v5.4 (>= v5.4.3) and release/v5.5 (>= v5.5.2).

### Software Requirements

- Default download URL points to Espressif official audio file; no extra server is required. If you change to a custom URL, ensure it is reachable (e.g. valid HTTPS certificate).

## Build and Flash

### Build Preparation

Before building this example, ensure the ESP-IDF environment is set up. If it is already set up, skip this paragraph and go to the project directory and run the pre-build script(s) as follows. If not, run the following in the ESP-IDF root directory to complete the environment setup. For full steps, see the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/index.html).

```
./install.sh
. ./export.sh
```

Short steps:

- Go to this example's project directory:

```
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_http_download_to_sdcard
```

- Run the pre-build script: follow the prompts to select the target chip, set up the IDF Action extension, and use `esp_board_manager` to select a supported board. For a custom board, see [Custom board](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md#custom-board).

On Linux / macOS:
```bash/zsh
source prebuild.sh
```

On Windows:
```powershell
.\prebuild.ps1
```

### Project Configuration

- Configure Wi-Fi SSID and password in menuconfig so the device can connect for HTTP download.

```bash
idf.py menuconfig
```

In menuconfig:

- `GMF APP Configuration` → `Example Connection Configuration` → `WiFi SSID`
- `GMF APP Configuration` → `Example Connection Configuration` → `WiFi Password`

> Press `s` to save and `Esc` to exit after configuration.

### Build and Flash Commands

- Build the example:

```
idf.py build
```

- Flash the firmware and run the serial monitor (replace PORT with your port name):

```
idf.py -p PORT flash monitor
```

- To exit the monitor, use `Ctrl-]`

## How to Use the Example

### Functionality and Usage

- After power-on the example will: mount the microSD card → connect to Wi-Fi → download from the configured URL → write data to the configured path on the microSD card → print overall speed when done and release resources.
- Change the download URL and save path in `main/http_download_to_sdcard.c` via `http_url` and `save_url`. Default: download [ff-16b-2c-44100hz.opus](https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.opus) and save as `/sdcard/ff-16b-2c-44100hz.opus`.
- Set the macro `ONLY_ENABLE_HTTP` to `true` to test HTTP download only (no SD write), e.g. for network debugging or measuring download speed alone.

### Log Output

- Normal run: mount SD card and connect Wi-Fi, register elements and create pipeline, start pipeline, wait for finish event, then print "Http download and write to sdcard speed: x.xx MB/s" and destroy resources. Key steps are marked with `[ 1 ]`–`[ 6 ]`.

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

## Troubleshooting

### microSD card mount failed

If the log shows SD init or mount failure, check: board supports SD, slot contact is good, microSD is FAT and not write-protected. Ensure SD is enabled correctly in the board configuration.

### Wi-Fi connection failed

If the device does not get an IP, confirm `WiFi SSID` and `WiFi Password` in menuconfig match your router, and the router band is 2.4 GHz as supported by the board.

### HTTP download failed or certificate error

If you see connection timeout or TLS certificate verification failure, ensure the device can reach the URL (e.g. ping the host) and that the ESP-IDF certificate bundle or custom certificates are configured. For your own HTTPS server, use a complete and trusted certificate chain.

### Testing download speed only

Set `ONLY_ENABLE_HTTP` to `true` in the source to run HTTP download without writing to the SD card, useful when the card is missing or faulty to verify network and download speed.

## Technical Support

For technical support, use the links below:

- Technical support: [esp32.com](https://esp32.com/viewforum.php?f=20) forum
- Issue reports and feature requests: [GitHub issue](https://github.com/espressif/esp-gmf/issues)

We will reply as soon as possible.
