# 多源音频播放器

- [English](./README.md)
- 例程难度 ⭐

## 例程简介

本例程实现了一个多源音频播放器，支持从 HTTP 网络、SD 卡中播放音乐，并且可以随时插入播放存储在 Flash 中的提示音（tone）。播放器通过命令行界面（CLI）提供交互式控制，可以在不同音频源之间切换，支持播放、暂停、恢复、停止等操作。

本例支持 MP3、WAV、FLAC、AAC、M4A、TS、AMRNB、AMRWB 音频格式，默认使用 MP3 格式。

## 示例创建

### IDF 默认分支

本例程支持 IDF release/v5.4(>= v5.4.3) and release/v5.5(>= v5.5.2) 分支。

### 预备知识

本例程支持三种音频源（播放 flash 音频将会打断当前播放，播放结束后恢复之前的播放）：
- **HTTP 音频源**：从网络 URL 播放音频（默认 URL：`https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.mp3`）
- **SD 卡音频源**：从 SD 卡文件播放音频（默认路径：`/sdcard/test.mp3`）
- **Flash 嵌入音频**：播放编译时嵌入到 Flash 中的音频文件（相关路径：`embed://tone/0_alarm.mp3`）

例程中使用的 flash 音频文件是[嵌入式二进制](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/api-guides/build-system.html#cmake-embed-data)格式，它是随代码一起编译下载到 flash 中。
Flash 嵌入音频的 `esp_embed_tone.h` 和 `esp_embed_tone.cmake` 文件是由 `gmf_io/mk_flash_embed_tone.py` 生成，如需要更换音频文件，需要运行脚本重新生成这两个文件，python 脚本命令如下：

```
python ../../../elements/gmf_io/mk_flash_embed_tone.py -p components/tone/
```

### 编译和下载

编译本例程前需要先确保已配置 ESP-IDF 的环境，如果已配置可跳到下一项配置，如果未配置需要先在 ESP-IDF 根目录运行下面脚本设置编译环境，有关配置和使用 ESP-IDF 完整步骤，请参阅 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/index.html)

```
./install.sh
. ./export.sh
```

下面是简略编译步骤：

- 进入多源音频播放器工程存放位置

```
cd gmf_examples/basic_examples/pipeline_play_multi_source_music
```

- 执行预编译脚本，根据提示选择编译芯片，自动设置 IDF Action 扩展

在 Linux / macOS 中运行以下命令：
```bash/zsh
source prebuild.sh
```

在 Windows 中运行以下命令：
```powershell
.\prebuild.ps1
```

- 配置 Wi-Fi 名称/密码

通过 `idf.py menuconfig` -> `GMF APP Configuration` -> `Example Connection Configuration` 配置 `WiFi SSID` 和 `WiFi Password`。如果 Wi-Fi 配置不正确，设备会持续重连，HTTP 在线音乐也无法正常播放。

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

### 示例输出

例程启动时的输出如下：

```
I (6212) MULTI_SOURCE_PLAYER: Initializing audio manager
I (6217) AUDIO_MANAGER: Initializing audio manager
I (6227) AUDIO_MANAGER: Audio manager initialized successfully
I (6228) MULTI_SOURCE_PLAYER: Initializing command interface
I (6233) MULTI_SOURCE_PLAYER: Setting up CLI

Type 'help' to get the list of commands.
Use UP/DOWN arrows to navigate through command history.
Press TAB when typing command name to auto-complete.
Audio>  I (6811) MULTI_SOURCE_PLAYER: Starting initial playback from SD card
I (6840) ESP_GMF_FILE: Open, dir:1, uri:/sdcard/test.mp3
I (6845) ESP_GMF_FILE: File size: 39019 byte, file position: 0
I (6847) ESP_GMF_PORT: ACQ IN, new self payload:0x3f80a75c, port:0x3f80a704, el:0x3f80a0c4-aud_dec
I (6849) AUDIO_MANAGER: Successfully switched to source 1
I (6862) MULTI_SOURCE_PLAYER: === Multi-Source Audio Player Ready ===
W (6866) ESP_GMF_ASMP_DEC: Not enough memory for out, need:2304, old: 1024, new: 2304
I (7130) AUDIO_MANAGER: Music info: sample_rates=16000, bits=16, ch=2
I (6870) MULTI_SOURCE_PLAYER: Available commands:
I (7181) MULTI_SOURCE_PLAYER:   play      - Start playback
I (7182) MULTI_SOURCE_PLAYER:   pause     - Pause playback
I (7203) MULTI_SOURCE_PLAYER:   resume    - Resume playback
I (7205) MULTI_SOURCE_PLAYER:   stop      - Stop playback
I (7206) MULTI_SOURCE_PLAYER:   switch    - Switch audio source (http or sdcard, or use without arg to toggle)
I (7218) MULTI_SOURCE_PLAYER:   tone      - Play flash tone (pauses current, plays tone, then resumes)
I (7233) MULTI_SOURCE_PLAYER:   get_vol   - Get current volume (0-100)
I (7234) MULTI_SOURCE_PLAYER:   set_vol   - Set volume (0-100)
I (7236) MULTI_SOURCE_PLAYER:   status    - Show playback status
I (7247) MULTI_SOURCE_PLAYER:   exit      - Exit the application
I (7249) MULTI_SOURCE_PLAYER:   help      - Show all available commands
I (7263) MULTI_SOURCE_PLAYER: Entering main application loop
Audio>
```

### 使用 CLI 命令示例

在串口终端中，你可以输入以下命令：

```
Audio>  switch http
I (28521) AUDIO_COMMANDS: Switching from SD card to http
I (28525) ESP_GMF_HTTP: HTTP Open, URI = https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.mp3
I (28532) NEW_DATA_BUS: New block buf, num:1, item_cnt:32768, db:0x3f80c2dc
I (30240) esp-x509-crt-bundle: Certificate validated
I (34493) ESP_GMF_HTTP: The total size is 0 bytes
I (35160) esp-x509-crt-bundle: Certificate validated
I (35614) ESP_GMF_HTTP: The total size is 2994349 bytes
I (35618) AUDIO_MANAGER: Successfully switched to source 0
Audio>  I (35619) ESP_GMF_PORT: ACQ IN, new self payload:0x3f80c508, port:0x3f80a704, el:0x3f80a0c4-aud_dec
W (35731) ESP_GMF_ASMP_DEC: Not enough memory for out, need:4608, old: 2304, new: 4608
I (36036) AUDIO_MANAGER: Music info: sample_rates=16000, bits=16, ch=2

Audio>  pause
I (94011) AUDIO_COMMANDS: Playback paused
Audio>  resume
I (98105) AUDIO_COMMANDS: Playback resumed

Audio>  set_vol 80
I (20523) AUDIO_COMMANDS: Volume set to: 80
Audio>  get_vol
I (23524) AUDIO_COMMANDS: Current volume: 80 (range: 0-100)

Audio>  switch sdcard
I (102220) AUDIO_COMMANDS: Switching from HTTP to sdcard
W (102226) ESP_GMF_HTTP: No more data, errno: 0, read bytes: 310272, rlen = 0
I (102235) ESP_GMF_CODEC_DEV: CLose, 0x3f80a814, pos = 1116788/0
I (102240) ESP_GMF_FILE: Open, dir:1, uri:/sdcard/test.mp3
I (102245) ESP_GMF_FILE: File size: 39019 byte, file position: 0
I (102247) AUDIO_MANAGER: Successfully switched to source 1
Audio>  I (102516) AUDIO_MANAGER: Music info: sample_rates=16000, bits=16, ch=2

Audio>  tone
I (111420) AUDIO_MANAGER: Starting flash tone playback
I (111423) ESP_GMF_EMBED_FLASH: The read item is 1, embed://tone/1
I (111425) AUDIO_MANAGER: Flash tone playback started
Audio>  W (112026) ESP_GMF_EMBED_FLASH: No more data, ret:0, pos: 8527/8527
I (112029) ESP_GMF_EMBED_FLASH: Closed, pos: 8527/8527
I (112030) ESP_GMF_CODEC_DEV: CLose, 0x3f814578, pos = 26752/0
I (112042) AUDIO_MANAGER: Flash playback finished, restoring original playback
I (112046) AUDIO_MANAGER: Restoring original playback: source=1, was_playing=3

Audio>  status
I (124635) AUDIO_COMMANDS: Playback Status:
I (124636) AUDIO_COMMANDS:   Source: SD card
I (124637) AUDIO_COMMANDS:   State: Stopped
I (124638) AUDIO_COMMANDS:   Flash tone playing: No
I (124649) AUDIO_COMMANDS:   Volume: 80 (range: 0-100)
```
