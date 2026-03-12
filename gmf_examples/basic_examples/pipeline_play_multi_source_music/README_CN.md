# 多源音频播放器

- [English Version](./README.md)
- 例程难度：⭐⭐

## 例程简介

本示例展示了基于 GMF 的多源音频播放器，支持从 HTTP、SD 卡播放音乐，并可随时插入播放 Flash 中的嵌入提示音（tone），提示音播放结束后恢复之前的播放。

- 可通过 CLI 进行播放、暂停、恢复、停止、切换音源、音量与状态查询等交互。
- 支持 MP3、WAV、FLAC、AAC、M4A、TS、AMRNB、AMRWB 等格式，默认使用 MP3。

### 典型场景

- 车载音乐和导航提示音切换、带提示音的音乐播放器等需要多音源与 CLI 控制的场景。

### 运行机制

- 多音源由 Audio Manager 统一调度；HTTP、SD 卡各对应 pipeline，Flash 提示音单独 pipeline，播放 tone 时暂停当前源、播完自动恢复。CLI 命令与状态通过任务间通信更新。

### 文件结构

- 本例程文件结构如下：

```
├── components
│   ├── gen_bmgr_codes                   # 开发板文件（自动生成）
│   │   ├── board_manager.defaults
│   │   ├── CMakeLists.txt
│   │   ├── gen_board_device_config.c
│   │   ├── gen_board_device_handles.c
│   │   ├── gen_board_info.c
│   │   ├── gen_board_periph_config.c
│   │   ├── gen_board_periph_handles.c
│   │   └── idf_component.yml
│   └── tone                             # 依赖的提示音文件资源
│       ├── alarm.mp3
│       ├── CMakeLists.txt
│       ├── dingdong.mp3
│       ├── esp_embed_tone.cmake
│       ├── esp_embed_tone.h
│       ├── haode.mp3
│       └── new_message.mp3
├── main
│   ├── audio_player                     # 音频控制逻辑
│   │   ├── audio_multi_source_player.c
│   │   └── audio_multi_source_player.h
│   ├── command_interface                # 命令集合
│   │   ├── audio_commands.c
│   │   └── audio_commands.h
│   ├── common
│   │   ├── audio_config.h
│   │   └── audio_types.h
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   └── play_multi_source_music.c
├── CMakeLists.txt
├── partitions.csv
├── prebuild.ps1
├── prebuild.sh                          # 预编译脚本（生成板级代码等）
├── pytest_play_multi_source_music.py    # 自动化测试脚本
├── README.md
└── README_CN.md
```

## 环境配置

### 硬件要求

- **开发板**：默认以 ESP32-S3-Korvo V3 为例，其他 ESP 音频板同样适用。
- **资源要求**：microSD 卡、Wi-Fi、Audio DAC、扬声器。

### 默认 IDF 分支

本例程支持 IDF release/v5.4 (>= v5.4.3) 与 release/v5.5 (>= v5.5.2) 分支。

### 软件要求

- **HTTP 音源**：默认 URL 为 `https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.mp3`，可在配置或代码中修改。
- **SD 卡音源**：默认路径 `/sdcard/test.mp3`，请将自备音频放入 SD 卡并命名。
- **Flash 嵌入音频**：使用[嵌入式二进制](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/api-guides/build-system.html#cmake-embed-data)；`esp_embed_tone.h` 与 `esp_embed_tone.cmake` 由 `gmf_io/mk_flash_embed_tone.py` 生成，更换音频后需重新运行脚本，例如：

```
python $YOUR_GMF_PATH/elements/gmf_io/mk_flash_embed_tone.py -p $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_play_multi_source_music/components/tone/
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
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_play_multi_source_music
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

例程启动后会：
1. 初始化 WiFi 连接（用于 HTTP 音频源）
2. 初始化 SD 卡（用于 SD 卡音频源）
3. 创建音频管道并注册 CLI 命令
4. 默认从 SD 卡开始播放音频

启动后，可以通过串口终端使用以下 CLI 命令控制播放器：

- `play` - 开始播放当前音频源
- `pause` - 暂停播放
- `resume` - 恢复播放
- `stop` - 停止播放
- `switch [http|sdcard]` - 切换音频源（不指定参数时在 HTTP 和 SD 卡之间切换）
- `get_vol` - 获取当前音量（0-100）
- `set_vol <0-100>` - 设置音量（0-100）
- `status` - 显示当前播放状态
- `tone` - 播放 Flash 中的嵌入音频（会暂停当前播放，播放完成后自动恢复）
- `exit` - 退出应用程序
- `help` - 显示所有可用命令

### 日志输出

- 启动后会初始化 Audio Manager、CLI，默认从 SD 卡开始播放，并输出可用命令

```
I (946) main_task: Calling app_main()
I (947) MULTI_SOURCE_PLAYER: === Multi-Source Audio Player ===
I (952) MULTI_SOURCE_PLAYER: Initializing SD card
I (956) DEV_FS_FAT_SUB_SDMMC: slot_config: cd=-1, wp=-1, clk=15, cmd=7, d0=4, d1=-1, d2=-1, d3=-1, d4=-1, d5=-1, d6=-1, d7=-1, width=1, flags=0x1
Name: SA32G
Type: SDHC
Speed: 40.00 MHz (limit: 40.00 MHz)
Size: 29544MB
CSD: ver=2, sector_size=512, capacity=60506112 read_bl_len=9
SSR: bus_width=1
I (1017) DEV_FS_FAT: Filesystem mounted, base path: /sdcard
I (1022) BOARD_MANAGER: Device fs_sdcard initialized
I (1027) MULTI_SOURCE_PLAYER: Initializing audio codec
I (1033) PERIPH_I2S: I2S[0] TDM,  TX, ws: 45, bclk: 9, dout: 8, din: 10
I (1038) PERIPH_I2S: I2S[0] initialize success: 0x3c1f7df8
I (1043) DEV_AUDIO_CODEC: DAC is ENABLED
I (1047) DEV_AUDIO_CODEC: Init audio_dac, i2s_name: i2s_audio_out, i2s_rx_handle:0x0, i2s_tx_handle:0x3c1f7df8, data_if: 0x3fcead8c
I (1059) PERIPH_I2C: I2C master bus initialized successfully
I (1069) ES8311: Work in Slave mode
I (1072) DEV_AUDIO_CODEC: Successfully initialized codec: audio_dac
I (1073) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3fceafe0, chip:es8311
I (1080) BOARD_MANAGER: Device audio_dac initialized
I (1086) I2S_IF: channel mode 2 bits:16/16 channel:2 mask:3
I (1091) I2S_IF: TDM Mode 1 bits:16/16 channel:2 sample_rate:16000 mask:3
I (1112) Adev_Codec: Open codec device OK
I (1112) MULTI_SOURCE_PLAYER: Peripheral devices initialized successfully
I (1119) example_connect: Start example_connect.
I (1119) pp: pp rom version: e7ae62f
I (1120) net80211: net80211 rom version: e7ae62f
I (1125) wifi:wifi driver task: 3fcee81c, prio:23, stack:6656, core=0
I (1132) wifi:wifi firmware version: 4df78f2
I (1133) wifi:wifi certification version: v7.0
I (1138) wifi:config NVS flash: enabled
I (1141) wifi:config nano formatting: disabled
I (1145) wifi:Init data frame dynamic rx buffer num: 32
I (1150) wifi:Init static rx mgmt buffer num: 5
I (1155) wifi:Init management short buffer num: 32
I (1159) wifi:Init dynamic tx buffer num: 32
I (1163) wifi:Init static tx FG buffer num: 2
I (1167) wifi:Init static rx buffer size: 1600
I (1171) wifi:Init static rx buffer num: 10
I (1175) wifi:Init dynamic rx buffer num: 32
I (1180) wifi_init: rx ba win: 6
I (1182) wifi_init: accept mbox: 6
I (1185) wifi_init: tcpip mbox: 32
I (1188) wifi_init: udp mbox: 6
I (1191) wifi_init: tcp mbox: 6
I (1194) wifi_init: tcp tx win: 5760
I (1197) wifi_init: tcp rx win: 5760
I (1201) wifi_init: tcp mss: 1440
I (1204) wifi_init: WiFi IRAM OP enabled
I (1207) wifi_init: WiFi RX IRAM OP enabled
I (1212) phy_init: phy_version 711,97bcf0a2,Aug 25 2025,19:04:10
I (1253) wifi:mode : sta (7c:df:a1:e7:71:6c)
I (1253) wifi:enable tsf
I (1255) example_connect: Connecting to ESP-Audio...
W (1256) wifi:Password length matches WPA2 standards, authmode threshold changes from OPEN to WPA2
I (1263) example_connect: Waiting for IP(s)
I (3733) wifi:new:<4,0>, old:<1,0>, ap:<255,255>, sta:<4,0>, prof:1, snd_ch_cfg:0x0
I (3734) wifi:state: init -> auth (0xb0)
I (3737) wifi:state: auth -> assoc (0x0)
I (3742) wifi:state: assoc -> run (0x10)
I (3749) wifi:<ba-add>idx:0 (ifx:0, 18:31:bf:4b:8b:68), tid:0, ssn:0, winSize:64
I (3760) wifi:connected with ESP-Audio, aid = 4, channel 4, BW20, bssid = 18:31:bf:4b:8b:68
I (3760) wifi:security: WPA2-PSK, phy: bgn, rssi: -30
I (3762) wifi:pm start, type: 1

I (3765) wifi:dp: 1, bi: 102400, li: 3, scale listen interval from 307200 us to 307200 us
I (3773) wifi:set rx beacon pti, rx_bcn_pti: 0, bcn_timeout: 25000, mt_pti: 0, mt_time: 10000
I (3816) wifi:AP's beacon interval = 102400 us, DTIM period = 3
I (5120) example_connect: Got IPv6 event: Interface "example_netif_sta" address: fe80:0000:0000:0000:7edf:a1ff:fee7:716c, type: ESP_IP6_ADDR_IS_LINK_LOCAL
I (5953) esp_netif_handlers: example_netif_sta ip: 162.168.10.35, mask: 255.255.255.0, gw: 162.168.10.1
I (5953) example_connect: Got IPv4 event: Interface "example_netif_sta" address: 162.168.10.35
I (5960) example_common: Connected to example_netif_sta
I (5964) example_common: - IPv4 address: 162.168.10.35,
I (5969) example_common: - IPv6 address: fe80:0000:0000:0000:7edf:a1ff:fee7:716c, type: ESP_IP6_ADDR_IS_LINK_LOCAL
I (5979) MULTI_SOURCE_PLAYER: Initializing audio manager
I (5984) AUDIO_MULTI_SRC_PLAYER: Initializing audio manager
I (5992) AUDIO_MULTI_SRC_PLAYER: Audio manager initialized successfully
I (5996) MULTI_SOURCE_PLAYER: Initializing command interface
I (6002) MULTI_SOURCE_PLAYER: Setting up CLI

Type 'help' to get the list of commands.
Use UP/DOWN arrows to navigate through command history.
Press TAB when typing command name to auto-complete.
I (6064) MULTI_SOURCE_PLAYER: Starting initial playback from SD card
I (6078) ESP_GMF_FILE: Open, dir:1, uri:/sdcard/test.mp3
I (6079) AUDIO_MULTI_SRC_PLAYER: Successfully switched to source 1
I (6082) ESP_GMF_FILE: File size: 39019 byte, file position: 0
I (6093) ESP_GMF_PORT: ACQ IN, new self payload:0x3c1fa934, port:0x3c1fa8dc, el:0x3c1fa268-aud_dec
I (6093) MULTI_SOURCE_PLAYER: === Multi-Source Audio Player Ready ===
I (6102) ESP_ES_PARSER: The verion of es_parser is v1.0.0
W (6111) ESP_GMF_ASMP_DEC: Not enough memory for out, need:2304, old: 1024, new: 2304
Audio>  I (6270) AUDIO_MULTI_SRC_PLAYER: Music info: sample_rates=16000, bits=16, ch=2
I (6112) MULTI_SOURCE_PLAYER: Available commands:
I (6285) MULTI_SOURCE_PLAYER:   play      - Start playback
I (6286) MULTI_SOURCE_PLAYER:   pause     - Pause playback
I (6297) MULTI_SOURCE_PLAYER:   resume    - Resume playback
I (6298) MULTI_SOURCE_PLAYER:   stop      - Stop playback
I (6299) MULTI_SOURCE_PLAYER:   switch    - Switch audio source (http or sdcard, or use without arg to toggle)
I (6311) MULTI_SOURCE_PLAYER:   tone      - Play flash tone (pauses current, plays tone, then resumes)
I (6323) MULTI_SOURCE_PLAYER:   get_vol   - Get current volume (0-100)
I (6324) MULTI_SOURCE_PLAYER:   set_vol   - Set volume (0-100)
I (6337) MULTI_SOURCE_PLAYER:   status    - Show playback status
I (6337) MULTI_SOURCE_PLAYER:   exit      - Exit the application
I (6349) MULTI_SOURCE_PLAYER:   help      - Show all available commands
I (6350) MULTI_SOURCE_PLAYER: Entering main application loop
Audio>
```

### 使用 CLI 命令示例

在串口终端可输入：`switch http` / `switch sdcard` 切换音源，`pause` / `resume` 暂停/恢复，`set_vol 80` / `get_vol` 设置/查询音量，`tone` 播放 Flash 提示音（会暂断当前播放再恢复），`status` 查看状态，`exit` 退出。

```
Audio>  tone
I (10746) AUDIO_MULTI_SRC_PLAYER: Starting flash tone playback
I (10747) AUDIO_MULTI_SRC_PLAYER: Pausing current playback
I (10758) AUDIO_MULTI_SRC_PLAYER: Flash tone playback started
I (10759) ESP_GMF_EMBED_FLASH: The read item is 0, embed://tone/0
I (10760) ESP_GMF_PORT: ACQ IN, new self payload:0x3c206534, port:0x3c206350, el:0x3c1fc97c-aud_dec
I (10772) ESP_ES_PARSER: The verion of es_parser is v1.0.0
W (10785) ESP_GMF_ASMP_DEC: Not enough memory for out, need:4608, old: 1024, new: 4608
Audio>  tonW (13850) ESP_GMF_EMBED_FLASH: No more data, ret:0, pos: 36018/36018
I (13851) ESP_GMF_EMBED_FLASH: Closed, pos: 36018/36018
I (13852) ESP_GMF_CODEC_DEV: CLose, 0x3c206390, pos = 190636/0
I (13865) AUDIO_MULTI_SRC_PLAYER: Flash playback finished, restoring original playback
I (13866) AUDIO_MULTI_SRC_PLAYER: Restoring original playback: source=1, was_playing=1
I (13878) AUDIO_MULTI_SRC_PLAYER: Resuming original playback
Audio>  tone
I (14494) AUDIO_MULTI_SRC_PLAYER: Starting flash tone playback
I (14495) AUDIO_MULTI_SRC_PLAYER: Pausing current playback
I (14507) AUDIO_MULTI_SRC_PLAYER: Flash tone playback started
I (14508) ESP_GMF_EMBED_FLASH: The read item is 1, embed://tone/1
I (14509) ESP_ES_PARSER: The verion of es_parser is v1.0.0
Audio>  W (14987) ESP_GMF_EMBED_FLASH: No more data, ret:0, pos: 8527/8527
I (14989) ESP_GMF_EMBED_FLASH: Closed, pos: 8527/8527
I (14989) ESP_GMF_CODEC_DEV: CLose, 0x3c206390, pos = 25080/0
I (15002) AUDIO_MULTI_SRC_PLAYER: Flash playback finished, restoring original playback
I (15002) AUDIO_MULTI_SRC_PLAYER: Restoring original playback: source=1, was_playing=1
I (15014) AUDIO_MULTI_SRC_PLAYER: Resuming original playback
I (17493) ESP_GMF_FILE: No more data, ret: 0
I (17497) ESP_GMF_FILE: CLose, 0x3c1fc484, pos = 39019/39019
I (17498) ESP_GMF_CODEC_DEV: CLose, 0x3c1faa28, pos = 493816/0
Audio>  switch
I (20119) AUDIO_COMMANDS: Switching from SD card to http
I (20121) AUDIO_MULTI_SRC_PLAYER: Successfully switched to source 0
I (20121) NEW_DATA_BUS: New block buf, num:1, item_cnt:32768, db:0x3c1fa8dc
I (20134) ESP_GMF_HTTP: HTTP Open, URI = https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.mp3
Audio>  I (21261) esp-x509-crt-bundle: Certificate validated
I (23379) ESP_GMF_HTTP: The total size is 0 bytes
I (23719) esp-x509-crt-bundle: Certificate validated
I (23938) ESP_GMF_HTTP: The total size is 2994349 bytes
I (23940) ESP_GMF_PORT: ACQ IN, new self payload:0x3c2042a0, port:0x3c1fc7f4, el:0x3c1fa268-aud_dec
I (23983) ESP_ES_PARSER: The verion of es_parser is v1.0.0
W (23987) ESP_GMF_ASMP_DEC: Not enough memory for out, need:4608, old: 2304, new: 4608
I (24154) AUDIO_MULTI_SRC_PLAYER: Music info: sample_rates=16000, bits=16, ch=2
Audio>
Audio>  tone
I (31157) AUDIO_MULTI_SRC_PLAYER: Starting flash tone playback
I (31158) AUDIO_MULTI_SRC_PLAYER: Pausing current playback
I (31161) AUDIO_MULTI_SRC_PLAYER: Flash tone playback started
I (31161) ESP_GMF_EMBED_FLASH: The read item is 2, embed://tone/2
I (31175) ESP_ES_PARSER: The verion of es_parser is v1.0.0
Audio>  W (31594) ESP_GMF_EMBED_FLASH: No more data, ret:0, pos: 6384/6384
I (31595) ESP_GMF_EMBED_FLASH: Closed, pos: 6384/6384
I (31596) ESP_GMF_CODEC_DEV: CLose, 0x3c206390, pos = 27896/0
I (31608) AUDIO_MULTI_SRC_PLAYER: Flash playback finished, restoring original playback
I (31609) AUDIO_MULTI_SRC_PLAYER: Restoring original playback: source=0, was_playing=1
I (31621) AUDIO_MULTI_SRC_PLAYER: Resuming original playback
Audio>  status
I (35241) AUDIO_COMMANDS: Playback Status:
I (35242) AUDIO_COMMANDS:   Source: HTTP
I (35242) AUDIO_COMMANDS:   State: Playing
I (35243) AUDIO_COMMANDS:   Flash tone playing: No
I (35254) AUDIO_COMMANDS:   Volume: 80 (range: 0-100)
```

## 故障排除

### HTTP 无法播放或持续重连

- 确认 menuconfig 中 WiFi SSID/Password 正确，且设备与路由器在同一网络；HTTPS URL 若出现证书校验失败，可参考 pipeline_play_http_music 例程的 SSL 故障排除。

### SD 卡或 Flash 提示音无法播放

- SD 卡：确认卡内存在 `/sdcard/test.mp3`（或所配置路径）且格式正确。Flash 提示音：确认已运行 `mk_flash_embed_tone.py` 生成 `esp_embed_tone.h` / `esp_embed_tone.cmake`，且 `components/tone/` 下有所需音频。

## 技术支持

请按照下面的链接获取技术支持：

- 技术支持参见 [esp32.com](https://esp32.com/viewforum.php?f=20) 论坛
- 问题反馈与功能需求，请创建 [GitHub issue](https://github.com/espressif/esp-gmf/issues)

我们会尽快回复。
