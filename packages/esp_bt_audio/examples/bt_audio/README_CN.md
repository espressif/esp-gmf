# 基础蓝牙音频例程

- [English Version](./README.md)

- 例程难度：⭐⭐

## 例程简介

本例程通过 `esp_bt_audio` 模块初始化蓝牙音频，并使用 `esp_gmf_io_bt` 将蓝牙音频流与 GMF Pipeline 进行关联，从而实现经典蓝牙播放/写入，以及在目标芯片与蓝牙配置支持时的 LE Audio 单播或广播流程；同时通过串口指令展示蓝牙音频播放、LE 发现/连接与通话控制。当在支持 LCD 与触摸的板子上开启 `CONFIG_EXAMPLE_BT_UI_ENABLE` 时，例程还会额外提供基于 LVGL 的触屏界面，包含媒体播放器、拨号盘与音量条。

### 典型场景

- 蓝牙音箱（A2DP Sink）：手机连接设备后播放音乐，支持播放/暂停/上下曲、音量与元数据
- 蓝牙音源（A2DP Source）：设备发现并连接蓝牙耳机/音响，将本地或 microSD 音频推送到远端播放
- 蓝牙通话（HFP HF/PBAP Client）：接听/拒接来电、拨号，使用 AEC 提升清晰度，获取通讯录与通话记录
- LE Audio 音箱/耳机（TMAP）：设备暴露 LE Audio sink/source 能力，用于单播媒体或通话音频，也可按配置作为广播媒体接收端
- 本地触屏界面（可选）：基于 LVGL 的触屏 UI，包含启动画面、媒体播放器（封面、曲目标题/艺术家、播放控制、CIS/BIS 流类型指示）、拨号盘（数字键盘、拨打/挂断、来电/通话显示）与自动隐藏的音量条

### 预备知识

- 本例程涉及蓝牙相关概念和协议，请参阅蓝牙官方文档 [Bluetooth Specifications](https://www.bluetooth.com/specifications/specs/)
- 本例程使用 `esp_board_manager` 管理板级资源，配置方法见 [ESP Board Manager](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README_CN.md)

### 资源列表

- 默认使用带 Audio DAC/ADC、I2S、microSD 的音频开发板（如 lyrat_mini_v1_1）；A2DP Source 需准备 microSD 及测试音频文件

## 环境配置

### 硬件要求

- **开发板**：默认经典蓝牙配置使用 `lyrat_mini_v1_1`；LE Audio 需使用支持 BLE ISO/Bluetooth Audio 的 ESP 芯片与 controller 配置，并准备具备 I2S Codec 资源的音频板
- **外设**：Audio DAC、Audio ADC、I2S、microSD 卡（A2DP Source 角色需存放 `media0.mp3`、`media1.mp3`、`media2.mp3`）,LCD 与触摸面板（可选的 UI 需要）
- **蓝牙**：经典蓝牙（BR/EDR）用于 A2DP、AVRCP、HFP；LE Audio 需 NimBLE、Bluetooth Audio 与 ISO 支持

### 默认 IDF 分支

本例程支持 IDF release/v5.5(>= v5.5.2) 分支。

### 软件要求

- A2DP Source 角色需在 microSD 卡根目录放置三份测试音频：`media0.mp3`、`media1.mp3`、`media2.mp3`
- 使用 A2DP Sink 时需手机或其它 A2DP Source 设备；使用 A2DP Source 时需蓝牙耳机或音响
- 使用 LE Audio 时需开启 `CONFIG_BT_NIMBLE_ENABLED`、`CONFIG_BT_AUDIO`、`CONFIG_BT_ISO` 以及所需的 ESP-IDF LE Audio profile 选项，并准备与所选角色匹配的 LE Audio peer 或广播设备

## 编译和下载

### 编译准备

编译本例程前需先确保已配置 ESP-IDF 环境；若已配置可跳过本段，直接进入工程目录并运行相关预编译脚本。若未配置，请在 ESP-IDF 根目录运行以下脚本完成环境设置，完整步骤请参阅 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/index.html)。

```shell
./install.sh
. ./export.sh
```

下面是简略步骤：

- 进入本例程工程目录（以下为示例路径，请改为实际例程路径）：

```
cd $YOUR_GMF_PATH/packages/esp_bt_audio/examples/bt_audio
```

- 本例程使用 `esp_board_manager` 管理板级资源，需先添加板级支持。

> ESP-IDF 支持通过 `SDKCONFIG_DEFAULTS` 追加指定的 sdkconfig defaults 文件。在 S31 上请先选择 Classic 或 LE Audio defaults，再执行 `idf.py set-target` / 编译命令：
>
> ```text
> export SDKCONFIG_DEFAULTS=sdkconfig.defaults.esp32s31.classic
> # 或：
> export SDKCONFIG_DEFAULTS=sdkconfig.defaults.esp32s31.le
> # PowerShell：
> $env:SDKCONFIG_DEFAULTS = "sdkconfig.defaults.esp32s31.classic"
> ```

本示例使用 [ESP Board Manager](https://github.com/espressif/esp-board-manager) 管理板级资源。推荐安装辅助工具 [`esp-bmgr-assist`](https://pypi.org/project/esp-bmgr-assist/) 作为默认入口。

在已激活的 ESP-IDF Python 环境下安装（同一环境只需安装一次）：

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

在 menuconfig 中选择蓝牙角色与可选功能：

```bash
idf.py menuconfig
```

在 menuconfig 中进行以下配置（示例）：

- `BT Audio Basic Example (GMF)` → `Classic Audio Roles Configuration` → 选择 A2DP 角色（A2DP Sink / A2DP Source）或 HFP HF 等
- `BT Audio Basic Example (GMF)` → `Enable LE Audio` → 在目标芯片支持时开启 LE Audio 例程流程
- 可选：`BT UI Configuration` → `Enable LVGL UI` → 开启本地触屏界面（需要 LCD 与触摸面板）
- 可选：`LE Audio Configuration` → 选择 LE Audio user case 和 TMAP roles，再配置 LE audio location、source capability 和 coordinated set size
- 若为 A2DP Source，确保 microSD 相关配置正确，且卡内已放置 `media0.mp3`、`media1.mp3`、`media2.mp3`

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

退出 monitor 可使用 `Ctrl-]`。

## 如何使用例程

### 功能和用法

- **角色与指令**：例程支持通过 menuconfig 选配经典蓝牙角色（A2DP Sink、A2DP Source、HFP HF、AVRCP Controller/Target）与 LE Audio TMAP 预设；编译烧录后，在串口输入 `help` 查看指令列表
- **A2DP Sink**：设备等待手机等连接，连接后可通过串口指令控制播放：`play`、`pause`、`stop`、`next`、`prev`，以及 `vol_set <0-100>` 设置音量
- **A2DP Source**：通过 `start_discovery`、`connect <mac>` 发现并连接蓝牙音响/耳机，使用 `start_media`、`stop_media` 控制推流启停
- **HFP HF**：支持来电接听/拒接、拨号，以及通话状态与话务状态上报；通话场景下通过 GMF 管道中的 AEC 元件进行回声消除
- **PBAP Client**: 通过 `pb_fetch` 指令获取通讯录和通话记录
- **LE Audio**：使用 `le_scan_start [timeout_ms]` 与 `le_scan_stop` 发现 LE Audio 设备，使用 `le_connect <addr_type> <mac_address> [timeout_ms]` 连接 peer，使用 `le_disconnect` 断开当前 LE ACL 链路。媒体、音量、通话与 stream 事件通过同一套 `esp_bt_audio` 事件路径上报
- **LVGL 触屏 UI**（需 `CONFIG_EXAMPLE_BT_UI_ENABLE=y`）：本地触屏界面在蓝牙连接前显示启动画面，连接后切换到包含两个标签页的主界面——**媒体播放器**（封面显示、曲目标题/艺术家、播放/暂停/上一曲/下一曲控件、CIS/BIS 流类型指示）与**拨号盘**（数字键盘、拨打/挂断、来电/通话状态显示）。音量变化时弹出自动隐藏的**音量条**
- **配合设备**：A2DP Sink 需手机或其它 A2DP Source；A2DP Source 需蓝牙耳机或音响；HFP 需支持 HFP AG 的手机；LE Audio 需兼容的 LE Audio peer 或广播设备

### 日志输出

以下为运行过程中的关键日志示例（板级与 GMF 初始化、蓝牙与 Pipeline 就绪）：

```c
I (1398) main_task: Calling app_main()
I (1423) PERIPH_I2C: I2C master bus initialized successfully
W (1425) PERIPH_I2S: I2S[0] STD already enabled, tx:0x3f800dd8, rx:0x3f800f94
I (1425) PERIPH_I2S: I2S[0] STD,  TX, ws: 25, bclk: 5, dout: 26, din: 35
I (1431) PERIPH_I2S: I2S[0] initialize success: 0x3f800dd8
I (1437) PERIPH_I2S: I2S[1] STD, RX, ws: 33, bclk: 32, dout: -1, din: 36
I (1443) PERIPH_I2S: I2S[1] initialize success: 0x3f80136c
I (1448) PERIPH_GPIO: Initialize success, pin: 13, set the default level: 1
I (1455) PERIPH_GPIO: Initialize success, pin: 19, default_level: 0
I (1461) PERIPH_GPIO: Initialize success, pin: 21, set the default level: 0
I (1467) PERIPH_GPIO: Initialize success, pin: 22, set the default level: 0
I (1474) PERIPH_GPIO: Initialize success, pin: 27, set the default level: 0
I (1481) PERIPH_GPIO: Initialize success, pin: 34, default_level: 0
I (1490) PERIPH_ADC: Create adc oneshot unit success
I (1491) BOARD_MANAGER: All peripherals initialized
I (1496) DEV_POWER_CTRL_SUB_GPIO: Initializing GPIO power control: gpio_sd_power
I (1503) BOARD_PERIPH: Reuse periph: gpio_sd_power, ref_count=2
I (1509) DEV_POWER_CTRL_SUB_GPIO: GPIO power control initialized successfully
I (1516) DEV_POWER_CTRL: Power control device initialized successfully, sub_type: gpio
I (1523) BOARD_PERIPH: Reuse periph: i2s_audio_out, ref_count=2
I (1529) DEV_AUDIO_CODEC: DAC is ENABLED
I (1533) DEV_AUDIO_CODEC: Init audio_dac, i2s_name: i2s_audio_out, i2s_rx_handle:0x0, i2s_tx_handle:0x3f800dd8, data_if: 0x3ffd66c4
I (1544) BOARD_PERIPH: Reuse periph: i2c_master, ref_count=2
I (1558) ES8311: Work in Slave mode
I (1561) DEV_AUDIO_CODEC: Successfully initialized codec: audio_dac
I (1562) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3ffd6844, chip:es8311
I (1570) DEV_AUDIO_CODEC: ADC is ENABLED
I (1573) BOARD_PERIPH: Reuse periph: i2s_audio_in, ref_count=2
I (1579) DEV_AUDIO_CODEC: Init audio_adc, i2s_name: i2s_audio_in, i2s_rx_handle:0x3f80136c, i2s_tx_handle:0x3f8011b0, data_if: 0x3ffd688c
I (1591) BOARD_PERIPH: Reuse periph: i2c_master, ref_count=3
I (1613) DEV_AUDIO_CODEC: Successfully initialized codec: audio_adc
I (1613) DEV_AUDIO_CODEC: Create esp_codec_dev success, dev:0x3ffd69c0, chip:es7243e
I (1615) BOARD_DEVICE: Device sdcard_power_ctrl config found: 0x3f433fb8 (size: 20)
I (1623) DEV_POWER_CTRL_SUB_GPIO: GPIO power control: ON, level: 0 for device: fs_sdcard
I (1630) DEV_FS_FAT_SUB_SDMMC: slot_config: cd=-1, wp=-1, clk=14, cmd=15, d0=2, d1=-1, d2=-1, d3=-1, d4=-1, d5=-1, d6=-1, d7=-1, width=1, flags=0x1
Name: BB1QT
Type: SDHC
Speed: 40.00 MHz (limit: 40.00 MHz)
Size: 30528MB
CSD: ver=2, sector_size=512, capacity=62521344 read_bl_len=9
SSR: bus_width=1
I (1851) DEV_FS_FAT: Filesystem mounted, base path: /sdcard
I (1857) BOARD_PERIPH: Reuse periph: adc_button, ref_count=2
I (1862) BOARD_PERIPH: Peripheral adc_button config found: 0x3f43410c (size: 52)
I (1869) DEV_BUTTON_SUB_ADC: Initializing 6 ADC buttons on unit 0, channel 3
I (1876) adc_button: ADC1 has been initialized
I (1880) adc_button: calibration scheme version is Line Fitting
I (1886) adc_button: Calibration Success
I (1889) button: IoT Button Version: 4.1.6
I (1893) DEV_BUTTON: Successfully initialized button: adc_button_group, sub_type: adc_multi
I (1901) BOARD_MANAGER: Board manager initialized
I (1906) BOARD_DEVICE: Device handle audio_dac found, Handle: 0x3ffd669c TO: 0x3ffd669c
I (1914) I2S_IF: channel mode 0 bits:16/16 channel:2 mask:3
I (1919) I2S_IF: STD Mode 1 bits:16/16 channel:2 sample_rate:48000 mask:3
I (1942) Adev_Codec: Open codec device OK
I (1942) BOARD_DEVICE: Device handle audio_adc found, Handle: 0x3ffd6874 TO: 0x3ffd6874
I (1943) I2S_IF: channel mode 0 bits:16/16 channel:2 mask:3
I (1948) I2S_IF: STD Mode 0 bits:16/16 channel:2 sample_rate:48000 mask:3
I (1954) I2S_IF: channel mode 0 bits:16/16 channel:2 mask:3
I (1959) I2S_IF: STD Mode 1 bits:16/16 channel:2 sample_rate:48000 mask:3
I (1967) Adev_Codec: Open codec device OK
I (1970) POOL_INIT: Registering GMF pool
I (1975) POOL_INIT: Registered: aud_aec
I (1977) BOARD_DEVICE: Device handle audio_dac found, Handle: 0x3ffd669c TO: 0x3ffd669c
I (1985) BOARD_DEVICE: Device handle audio_adc found, Handle: 0x3ffd6874 TO: 0x3ffd6874
I (1993) POOL_INIT: GMF pool initialization completed successfully
W (1999) ESP_GMF_THREAD: Make sure selected the `CONFIG_SPIRAM_BOOT_INIT` and `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY` by `make menuconfig`
I (2011) ESP_GMF_TASK: Waiting to run... [tsk:bt2codec_task-0x3ffd79bc, wk:0x0, run:0]
W (2019) ESP_GMF_THREAD: Make sure selected the `CONFIG_SPIRAM_BOOT_INIT` and `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY` by `make menuconfig`
I (2031) ESP_GMF_TASK: Waiting to run... [tsk:codec2bt_task-0x3ffd92c4, wk:0x0, run:0]
W (2032) ESP_GMF_THREAD: Make sure selected the `CONFIG_SPIRAM_BOOT_INIT` and `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY` by `make menuconfig`
I (2051) ESP_GMF_TASK: Waiting to run... [tsk:local2bt_task-0x3ffdab74, wk:0x0, run:0]
I (2051) BTDM_INIT: BT controller compile version [045a658]
I (2064) BTDM_INIT: Using main XTAL as clock source
I (2069) BTDM_INIT: Bluetooth MAC: a8:42:e3:66:1f:da
I (2075) phy_init: phy_version 4863,a3a4459,Oct 28 2025,14:30:06
I (2564) BT_AUD_HOST: Setting bluedroid discovery operations
I (2566) BT_AUD_AVRC_CT: CT init success
I (2568) BT_AUD_AVRC_TG: TG init success
W (2571) BT_BTC: A2DP Enable with AVRC
I (2576) BT_AUD_HOST: GAP event: 10
I (2578) BT_AUD_HOST: GAP event: 10
I (2579) BT_AUD_HOST: GAP event: 10
I (2581) BT_AUD_A2D_SINK: bt_a2d_event_cb unhandled event: 5
I (2586) BT_AUD_A2D_SINK: A2DP sink: initialized
I (2594) BT_AUD_HOST: GAP event: 10
I (2596) BT_AUD_HFP_HF: HF client init success
I (2599) BT_AUD_HOST: Setting bluedroid scan mode: connectable true, discoverable true
I (2606) BT_AUD_AVRC_CT: CT: Register notifications mask 0xff

Type 'help' to get the list of commands.
Use UP/DOWN arrows to navigate through command history.
Press TAB when typing command name to auto-complete.
I (2677) main_task: Returned from app_main()
BTAudio >
```

连接与媒体控制相关输出请以实际运行结果为准；若需减少无关 log，可在代码中通过 `esp_log_level_set()` 调整级别。

### 参考文献

- [ESP Board Manager](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README_CN.md)
- [esp-bmgr-assist 使用说明](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/docs/esp_bmgr_assist_cn.md)
- [ESP-IDF 编程指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/index.html)

## 故障排除

### microSD 或音频文件未找到（A2DP Source）

若日志出现文件打开失败或路径错误，请确认 microSD 已正确挂载，且根目录下存在 `media0.mp3`、`media1.mp3`、`media2.mp3`（或与代码中配置一致的文件名）。

### 蓝牙无法连接或无声音

- 确认 menuconfig 中蓝牙角色与目标设备角色匹配（Sink 对 Source，Source 对 Sink）
- 确认设备已配对/连接，且串口无连接失败或 A2DP/AVRCP 错误日志
- 若为 HFP，确认手机端已授权通话与音频
- 若为 LE Audio，确认目标芯片支持 BLE ISO/Bluetooth Audio，NimBLE 与所需的 ESP-IDF LE Audio profile 选项已开启，且 peer 角色与所选 TMAP 预设匹配

### 编译或板级相关错误

- 确认已安装 `esp-bmgr-assist`（`pip install esp-bmgr-assist`），并已执行 `idf.py set-target esp32`、`idf.py bmgr -b <board>`
- 若使用自定义板，请参考 [自定义板子](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/docs/how_to_customize_board_cn.md) 配置板型

### 无法获取通讯录或通话记录

- 经典蓝牙配对时，部分手机需允许设备获取通讯录，可在蓝牙设置中确认是否授权

## 技术支持

请按照下面的链接获取技术支持：

- 技术支持参见 [esp32.com](https://esp32.com/viewforum.php?f=20) 论坛
- 问题反馈与功能需求，请创建 [GitHub issue](https://github.com/espressif/esp-gmf/issues)

我们会尽快回复。
