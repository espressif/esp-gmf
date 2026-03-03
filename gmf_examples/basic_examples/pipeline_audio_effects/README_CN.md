# 音频特效播放演示

- [English](./README.md)
- 例程难度 ⭐

## 例程简介

本示例基于 Espressif Generic Media Framework (GMF)，演示如何构建和运行多种音频处理流水线。示例通过内置音频素材展示不同音频特效和混音功能，帮助开发者快速了解 GMF 框架的使用方法与效果呈现。

- 音频特效演示：播放音频时动态调整特效参数（如 EQ 增益、DRC 压缩曲线等），实时体验不同音效变化。

- 混音器演示：展示多路音频的实时混合与控制能力，模拟背景音乐与提示音同时播放的场景，实现淡入淡出效果。

## 示例创建

### IDF 默认分支

本例程支持 IDF release/v5.4(>= v5.4.3) and release/v5.5(>= v5.5.2) 分支。

### 预备知识

例程中使用的音频文件是[嵌入式二进制](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/api-guides/build-system.html#cmake-embed-data)格式，它是随代码一起编译下载到 flash 中。

本例程`${PROJECT_DIR}/components/music_src/`文件夹中提供了两个测试文件`manloud_48000_2_16_10.wav` 和`tone.mp3`。其中`esp_embed_tone.h` 和 `esp_embed_tone.cmake`文件是由`$YOUR_GMF_PATH/elements/gmf_io/mk_flash_embed_tone.py` 生成，如需要更换音频文件，需要运行脚本重新生成这两个文件，python 脚本命令如下：

```
python $YOUR_GMF_PATH/elements/gmf_io/mk_flash_embed_tone.py -p $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_audio_effects/components/music_src
```

### 配置说明

- **效果演示**：默认启用所有音频特效演示（ALC、Sonic、EQ、Fade、DRC、MBC 和混音器），用户可在 menuconfig 的 "Pipeline Audio Effects Example" 菜单中选择感兴趣的特效进行演示
- **音频类型**：示例中使用的音频文件为 WAV 和 MP3 格式，默认已注册对应的解码器。如需使用其他格式的音频文件，请在 menuconfig 中进入 "Audio Codec Configuration" → "Audio Decoder Configuration" 或 "Audio Simple Decoder Configuration" 菜单，启用相应音频解码器的支持选项

### 编译和下载

编译本例程前需要先确保已配置 ESP-IDF 的环境，如果已配置可跳到下一项配置，如果未配置需要先在 ESP-IDF 根目录运行下面脚本设置编译环境，有关配置和使用 ESP-IDF 完整步骤，请参阅 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/index.html)：

```
./install.sh
. ./export.sh
```

下面是简略编译步骤：

- 进入音频特效测试工程存放位置

```
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_audio_effects
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

### 功能描述

- **单特效模式**：
  1. 从闪存读取并解码 WAV 文件 `manloud_48000_2_16_10.wav`；
  2. 播放前 4 秒保持原始音频；
  3. 延时结束后调用特效 API 动态更新参数，突出音效变化；
  4. 播放结束后自动停止并释放 GMF 资源。

- **混音模式**：
  1. 流水线 A 从闪存读取并解码背景音乐 `manloud_48000_2_16_10.wav`；
  2. 流水线 B 延时启动，读取并解码提示音 `tone.mp3`，并自动进行采样率、声道和位深对齐；
  3. 流水线 C 负责混合两路音频，并通过提示音事件实现淡入淡出控制；
  4. 当所有输入播放完成后，Mixer 及相关流水线自动退出并释放资源。

### 运行示例

以下是完整的运行输出（以 ESP32-S3 为例）：

```c
I (1360) PIPELINE_AUDIO_EFFECTS: [ 1 ] Prepare GMF pool
I (1361) PIPELINE_AUDIO_EFFECTS: [ 2 ] Build mixer pipelines
I (1363) PIPELINE_AUDIO_EFFECTS: [ 2.1 ] Set audio url to play for music pipeline
I (1370) PIPELINE_AUDIO_EFFECTS: [ 2.2 ] Connect pipelines via ring buffers
I (1377) NEW_DATA_BUS: New ringbuffer:0x3c3aab34, num:10, item_cnt:1024, db:0x3c3ad360
I (1385) NEW_DATA_BUS: New ringbuffer:0x3c3ad3c8, num:10, item_cnt:1024, db:0x3c3afbf4
I (1392) PIPELINE_AUDIO_EFFECTS: [ 2.3 ] Prepare tasks
I (1397) ESP_GMF_TASK: Waiting to run... [tsk:base_stream-0x3fcec184, wk:0x0, run:0]
I (1397) ESP_GMF_TASK: Waiting to run... [tsk:mixer_stream-0x3fcee774, wk:0x0, run:0]
I (1397) ESP_GMF_TASK: Waiting to run... [tsk:tone_stream-0x3fced47c, wk:0x0, run:0]
I (1404) ESP_GMF_TASK: Waiting to run... [tsk:base_stream-0x3fcec184, wk:0x3c3afd9c, run:0]
I (1419) ESP_GMF_TASK: Waiting to run... [tsk:tone_stream-0x3fced47c, wk:0x3c3afe44, run:0]
W (1397) ESP_GMF_PIPELINE: Element[aud_mixer-0x3c3aa8c0] not ready to register job, ret:0xffffdff8
I (1444) PIPELINE_AUDIO_EFFECTS: [ 2.4 ] Create event groups and set event handler for each pipeline
I (1453) ESP_GMF_TASK: Waiting to run... [tsk:mixer_stream-0x3fcee774, wk:0x3c3afef0, run:0]
I (1461) PIPELINE_AUDIO_EFFECTS: [ 3 ] Start mixer pipelines
I (1467) ESP_GMF_EMBED_FLASH: The read item is 0, embed://tone/0
I (1472) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3afd9c, ctx:0x3c3aa040, label:aud_dec_open]
I (1473) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3afef0, ctx:0x3c3aa8c0, label:aud_mixer_open]
I (1482) ESP_GMF_PORT: ACQ IN, new self payload:0x3c3afd9c, port:0x3c3aa1f8, el:0x3c3aa040-aud_dec
I (1491) ESP_GMF_PORT: ACQ IN, new self payload:0x3c3afef0, port:0x3c3afc9c, el:0x3c3aa8c0-aud_mixer
I (1500) ESP_ES_PARSER: The version of es_parser is v1.0.0
I (1513) ESP_GMF_PORT: ACQ IN, new self payload:0x3c3aff98, port:0x3c3afd2c, el:0x3c3aa8c0-aud_mixer
I (3473) ESP_GMF_EMBED_FLASH: The read item is 1, embed://tone/1
I (3473) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3afe44, ctx:0x3c3aa27c, label:aud_dec_open]
I (3476) ESP_GMF_PORT: ACQ IN, new self payload:0x3c3afe44, port:0x3c3aa83c, el:0x3c3aa27c-aud_dec
I (3485) ESP_ES_PARSER: The version of es_parser is v1.0.0
W (3492) ESP_GMF_ASMP_DEC: Not enough memory for out, need:1152, old: 1024, new: 1152
I (3499) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3b3068, ctx:0x3c3aa38c, label:aud_rate_cvt_open]
I (3508) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3b30b8, ctx:0x3c3aa4e8, label:aud_ch_cvt_open]
I (3518) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3b3108, ctx:0x3c3aa640, label:aud_bit_cvt_open]
W (7426) ESP_GMF_EMBED_FLASH: No more data, ret:0, pos: 22651/22651
I (7426) ESP_GMF_TASK: Job is done, [tsk:tone_stream-0x3fced47c, wk:0x3c3afe80, job:0x3c3aa27c-aud_dec_proc]
I (7430) ESP_GMF_TASK: Job is done, [tsk:tone_stream-0x3fced47c, wk:0x3c3b3090, job:0x3c3aa38c-aud_rate_cvt_proc]
I (7440) ESP_GMF_TASK: Job is done, [tsk:tone_stream-0x3fced47c, wk:0x3c3b30e0, job:0x3c3aa4e8-aud_ch_cvt_proc]
I (7450) ESP_GMF_TASK: Job is done, [tsk:tone_stream-0x3fced47c, wk:0x3c3b3130, job:0x3c3aa640-aud_bit_cvt_proc]
I (7460) ESP_GMF_TASK: Finish, strategy action: 0, [tsk:0x3fced47c-tone_stream]
I (7467) ESP_GMF_EMBED_FLASH: Closed, pos: 22651/22651
I (7472) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3afe68, ctx:0x3c3aa27c, label:aud_dec_close]
I (7481) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3afe90, ctx:0x3c3aa38c, label:aud_rate_cvt_close]
I (7491) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3b3090, ctx:0x3c3aa4e8, label:aud_ch_cvt_close]
I (7500) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3b30b8, ctx:0x3c3aa640, label:aud_bit_cvt_close]
I (7510) ESP_GMF_TASK: Waiting to run... [tsk:tone_stream-0x3fced47c, wk:0x0, run:0]
I (7517) ESP_GMF_TASK: Waiting to run... [tsk:tone_stream-0x3fced47c, wk:0x0, run:0]
W (11445) ESP_GMF_EMBED_FLASH: No more data, ret:0, pos: 1920044/1920044
I (11445) ESP_GMF_TASK: Job is done, [tsk:base_stream-0x3fcec184, wk:0x3c3afdd8, job:0x3c3aa040-aud_dec_proc]
I (11450) ESP_GMF_TASK: Finish, strategy action: 0, [tsk:0x3fcec184-base_stream]
I (11457) ESP_GMF_EMBED_FLASH: Closed, pos: 1920044/1920044
I (11463) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3afdc0, ctx:0x3c3aa040, label:aud_dec_close]
I (11472) ESP_GMF_TASK: Waiting to run... [tsk:base_stream-0x3fcec184, wk:0x0, run:0]
I (11475) ESP_GMF_CODEC_DEV: CLose, 0x3c3aa9e0, pos = 1914240/0
I (11480) ESP_GMF_TASK: Waiting to run... [tsk:base_stream-0x3fcec184, wk:0x0, run:0]
I (11485) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aff14, ctx:0x3c3aa8c0, label:aud_mixer_close]
I (11502) ESP_GMF_TASK: Waiting to run... [tsk:mixer_stream-0x3fcee774, wk:0x0, run:0]
I (11510) ESP_GMF_TASK: Waiting to run... [tsk:mixer_stream-0x3fcee774, wk:0x0, run:0]
I (11518) PIPELINE_AUDIO_EFFECTS: [ 4 ] Tear down mixer pipelines
W (11524) GMF_SETUP_AUD_CODEC: Unregistering default encoder
W (11529) GMF_SETUP_AUD_CODEC: Unregistering default decoder
I (11535) PIPELINE_AUDIO_EFFECTS: Mixer demo finished
I (11539) PIPELINE_AUDIO_EFFECTS: [ 1 ] Prepare GMF pool
I (11545) PIPELINE_AUDIO_EFFECTS: [ 2 ] Build pipeline: io_embed_flash -> aud_dec -> effect -> io_codec_dev
I (11554) PIPELINE_AUDIO_EFFECTS: [ 2.1 ] Set audio url to play
I (11559) PIPELINE_AUDIO_EFFECTS: [ 2.2 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (11570) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x0, run:0]
I (11578) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x3c3aa4ec, run:0]
I (11587) PIPELINE_AUDIO_EFFECTS: [ 3 ] Start playback and wait 4 s before enabling aud_alc
I (11595) ESP_GMF_EMBED_FLASH: The read item is 0, embed://tone/0
I (11600) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aa4ec, ctx:0x3c3aa040, label:aud_dec_open]
I (11610) ESP_GMF_PORT: ACQ IN, new self payload:0x3c3aa4ec, port:0x3c3aa364, el:0x3c3aa040-aud_dec
I (11618) ESP_ES_PARSER: The version of es_parser is v1.0.0
I (11629) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aabd4, ctx:0x3c3aa150, label:aud_alc_open]
I (15634) PIPELINE_AUDIO_EFFECTS: Applying aud_alc parameters
W (21605) ESP_GMF_EMBED_FLASH: No more data, ret:0, pos: 1920044/1920044
I (21605) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x3c3aa528, job:0x3c3aa040-aud_dec_proc]
I (21610) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x3c3aabfc, job:0x3c3aa150-aud_alc_proc]
I (21620) ESP_GMF_TASK: Finish, strategy action: 0, [tsk:0x3fcebd64-TSK_0x3fcebd64]
I (21628) ESP_GMF_EMBED_FLASH: Closed, pos: 1920044/1920044
I (21633) ESP_GMF_CODEC_DEV: CLose, 0x3c3aa3a4, pos = 1920000/0
I (21639) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aa510, ctx:0x3c3aa040, label:aud_dec_close]
I (21648) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aa538, ctx:0x3c3aa150, label:aud_alc_close]
I (21657) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x0, run:0]
I (21665) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x0, run:0]
I (21673) PIPELINE_AUDIO_EFFECTS: [ 4 ] Tear down pipeline
W (21678) GMF_SETUP_AUD_CODEC: Unregistering default encoder
W (21684) GMF_SETUP_AUD_CODEC: Unregistering default decoder
I (21689) PIPELINE_AUDIO_EFFECTS: [ 1 ] Prepare GMF pool
I (21695) PIPELINE_AUDIO_EFFECTS: [ 2 ] Build pipeline: io_embed_flash -> aud_dec -> effect -> io_codec_dev
I (21704) PIPELINE_AUDIO_EFFECTS: [ 2.1 ] Set audio url to play
I (21709) PIPELINE_AUDIO_EFFECTS: [ 2.2 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (21720) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x0, run:0]
I (21728) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x3c3aa514, run:0]
I (21737) PIPELINE_AUDIO_EFFECTS: [ 3 ] Start playback and wait 4 s before enabling aud_sonic
I (21745) ESP_GMF_EMBED_FLASH: The read item is 0, embed://tone/0
I (21751) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aa514, ctx:0x3c3aa040, label:aud_dec_open]
I (21760) ESP_GMF_PORT: ACQ IN, new self payload:0x3c3aa514, port:0x3c3aa38c, el:0x3c3aa040-aud_dec
I (21769) ESP_ES_PARSER: The version of es_parser is v1.0.0
I (21774) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aabf4, ctx:0x3c3aa150, label:aud_sonic_open]
I (25784) PIPELINE_AUDIO_EFFECTS: Applying aud_sonic parameters
W (33220) ESP_GMF_EMBED_FLASH: No more data, ret:0, pos: 1920044/1920044
I (33220) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x3c3aa550, job:0x3c3aa040-aud_dec_proc]
I (33225) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x3c3aac1c, job:0x3c3aa150-aud_sonic_proc]
I (33236) ESP_GMF_TASK: Finish, strategy action: 0, [tsk:0x3fcebd64-TSK_0x3fcebd64]
I (33243) ESP_GMF_EMBED_FLASH: Closed, pos: 1920044/1920044
I (33248) ESP_GMF_CODEC_DEV: CLose, 0x3c3aa3cc, pos = 2200776/0
I (33254) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aa538, ctx:0x3c3aa040, label:aud_dec_close]
I (33263) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aa560, ctx:0x3c3aa150, label:aud_sonic_close]
I (33273) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x0, run:0]
I (33281) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x0, run:0]
I (33288) PIPELINE_AUDIO_EFFECTS: [ 4 ] Tear down pipeline
W (33294) GMF_SETUP_AUD_CODEC: Unregistering default encoder
W (33299) GMF_SETUP_AUD_CODEC: Unregistering default decoder
I (33305) PIPELINE_AUDIO_EFFECTS: [ 1 ] Prepare GMF pool
I (33310) PIPELINE_AUDIO_EFFECTS: [ 2 ] Build pipeline: io_embed_flash -> aud_dec -> effect -> io_codec_dev
I (33319) PIPELINE_AUDIO_EFFECTS: [ 2.1 ] Set audio url to play
I (33325) PIPELINE_AUDIO_EFFECTS: [ 2.2 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (33336) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x0, run:0]
I (33343) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x3c3aa590, run:0]
I (33352) PIPELINE_AUDIO_EFFECTS: [ 3 ] Start playback and wait 4 s before enabling aud_eq
I (33360) ESP_GMF_EMBED_FLASH: The read item is 0, embed://tone/0
I (33366) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aa590, ctx:0x3c3aa040, label:aud_dec_open]
I (33375) ESP_GMF_PORT: ACQ IN, new self payload:0x3c3aa590, port:0x3c3aa408, el:0x3c3aa040-aud_dec
I (33384) ESP_ES_PARSER: The version of es_parser is v1.0.0
I (33390) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aac74, ctx:0x3c3aa150, label:aud_eq_open]
I (37398) PIPELINE_AUDIO_EFFECTS: Applying aud_eq parameters
W (43370) ESP_GMF_EMBED_FLASH: No more data, ret:0, pos: 1920044/1920044
I (43370) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x3c3aa5cc, job:0x3c3aa040-aud_dec_proc]
I (43375) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x3c3aac9c, job:0x3c3aa150-aud_eq_proc]
I (43385) ESP_GMF_TASK: Finish, strategy action: 0, [tsk:0x3fcebd64-TSK_0x3fcebd64]
I (43393) ESP_GMF_EMBED_FLASH: Closed, pos: 1920044/1920044
I (43398) ESP_GMF_CODEC_DEV: CLose, 0x3c3aa448, pos = 1920000/0
I (43404) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aa5b4, ctx:0x3c3aa040, label:aud_dec_close]
I (43413) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aa5dc, ctx:0x3c3aa150, label:aud_eq_close]
I (43422) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x0, run:0]
I (43430) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x0, run:0]
I (43438) PIPELINE_AUDIO_EFFECTS: [ 4 ] Tear down pipeline
W (43443) GMF_SETUP_AUD_CODEC: Unregistering default encoder
W (43449) GMF_SETUP_AUD_CODEC: Unregistering default decoder
I (43454) PIPELINE_AUDIO_EFFECTS: [ 1 ] Prepare GMF pool
I (43460) PIPELINE_AUDIO_EFFECTS: [ 2 ] Build pipeline: io_embed_flash -> aud_dec -> effect -> io_codec_dev
I (43469) PIPELINE_AUDIO_EFFECTS: [ 2.1 ] Set audio url to play
I (43474) PIPELINE_AUDIO_EFFECTS: [ 2.2 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (43485) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x0, run:0]
I (43493) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x3c3aa4dc, run:0]
I (43501) PIPELINE_AUDIO_EFFECTS: [ 3 ] Start playback and wait 4 s before enabling aud_fade
I (43510) ESP_GMF_EMBED_FLASH: The read item is 0, embed://tone/0
I (43515) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aa4dc, ctx:0x3c3aa040, label:aud_dec_open]
I (43525) ESP_GMF_PORT: ACQ IN, new self payload:0x3c3aa4dc, port:0x3c3aa354, el:0x3c3aa040-aud_dec
I (43533) ESP_ES_PARSER: The version of es_parser is v1.0.0
I (43539) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aabc4, ctx:0x3c3aa150, label:aud_fade_open]
I (47549) PIPELINE_AUDIO_EFFECTS: Applying aud_fade parameters
W (53520) ESP_GMF_EMBED_FLASH: No more data, ret:0, pos: 1920044/1920044
I (53520) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x3c3aa518, job:0x3c3aa040-aud_dec_proc]
I (53525) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x3c3aabec, job:0x3c3aa150-aud_fade_proc]
I (53535) ESP_GMF_TASK: Finish, strategy action: 0, [tsk:0x3fcebd64-TSK_0x3fcebd64]
I (53543) ESP_GMF_EMBED_FLASH: Closed, pos: 1920044/1920044
I (53548) ESP_GMF_CODEC_DEV: CLose, 0x3c3aa394, pos = 1920000/0
I (53554) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aa500, ctx:0x3c3aa040, label:aud_dec_close]
I (53563) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aa528, ctx:0x3c3aa150, label:aud_fade_close]
I (53573) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x0, run:0]
I (53580) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x0, run:0]
I (53588) PIPELINE_AUDIO_EFFECTS: [ 4 ] Tear down pipeline
W (53594) GMF_SETUP_AUD_CODEC: Unregistering default encoder
W (53599) GMF_SETUP_AUD_CODEC: Unregistering default decoder
I (53605) PIPELINE_AUDIO_EFFECTS: [ 1 ] Prepare GMF pool
I (53610) PIPELINE_AUDIO_EFFECTS: [ 2 ] Build pipeline: io_embed_flash -> aud_dec -> effect -> io_codec_dev
I (53619) PIPELINE_AUDIO_EFFECTS: [ 2.1 ] Set audio url to play
I (53624) PIPELINE_AUDIO_EFFECTS: [ 2.2 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (53636) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x0, run:0]
I (53643) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x3c3aa4fc, run:0]
I (53652) PIPELINE_AUDIO_EFFECTS: [ 3 ] Start playback and wait 4 s before enabling aud_drc
I (53660) ESP_GMF_EMBED_FLASH: The read item is 0, embed://tone/0
I (53666) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aa4fc, ctx:0x3c3aa040, label:aud_dec_open]
I (53675) ESP_GMF_PORT: ACQ IN, new self payload:0x3c3aa4fc, port:0x3c3aa374, el:0x3c3aa040-aud_dec
I (53684) ESP_ES_PARSER: The version of es_parser is v1.0.0
I (53736) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aabe4, ctx:0x3c3aa150, label:aud_drc_open]
I (57738) PIPELINE_AUDIO_EFFECTS: Applying aud_drc parameters
W (63710) ESP_GMF_EMBED_FLASH: No more data, ret:0, pos: 1920044/1920044
I (63710) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x3c3aa538, job:0x3c3aa040-aud_dec_proc]
I (63715) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x3c3aac0c, job:0x3c3aa150-aud_drc_proc]
I (63725) ESP_GMF_TASK: Finish, strategy action: 0, [tsk:0x3fcebd64-TSK_0x3fcebd64]
I (63733) ESP_GMF_EMBED_FLASH: Closed, pos: 1920044/1920044
I (63738) ESP_GMF_CODEC_DEV: CLose, 0x3c3aa3b4, pos = 1920000/0
I (63744) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aabe4, ctx:0x3c3aa040, label:aud_dec_close]
I (63753) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aac0c, ctx:0x3c3aa150, label:aud_drc_close]
I (63762) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x0, run:0]
I (63770) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x0, run:0]
I (63778) PIPELINE_AUDIO_EFFECTS: [ 4 ] Tear down pipeline
W (63783) GMF_SETUP_AUD_CODEC: Unregistering default encoder
W (63789) GMF_SETUP_AUD_CODEC: Unregistering default decoder
I (63794) PIPELINE_AUDIO_EFFECTS: [ 1 ] Prepare GMF pool
I (63800) PIPELINE_AUDIO_EFFECTS: [ 2 ] Build pipeline: io_embed_flash -> aud_dec -> effect -> io_codec_dev
I (63809) PIPELINE_AUDIO_EFFECTS: [ 2.1 ] Set audio url to play
I (63814) PIPELINE_AUDIO_EFFECTS: [ 2.2 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (63825) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x0, run:0]
I (63833) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x3c3aa544, run:0]
I (63842) PIPELINE_AUDIO_EFFECTS: [ 3 ] Start playback and wait 4 s before enabling aud_mbc
I (63850) ESP_GMF_EMBED_FLASH: The read item is 0, embed://tone/0
I (63855) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aa544, ctx:0x3c3aa040, label:aud_dec_open]
I (63865) ESP_GMF_PORT: ACQ IN, new self payload:0x3c3aa544, port:0x3c3aa3bc, el:0x3c3aa040-aud_dec
I (63873) ESP_ES_PARSER: The version of es_parser is v1.0.0
I (64068) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aac24, ctx:0x3c3aa150, label:aud_mbc_open]
I (68074) PIPELINE_AUDIO_EFFECTS: Applying aud_mbc parameters
W (74045) ESP_GMF_EMBED_FLASH: No more data, ret:0, pos: 1920044/1920044
I (74045) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x3c3aa580, job:0x3c3aa040-aud_dec_proc]
I (74050) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x3c3aac4c, job:0x3c3aa150-aud_mbc_proc]
I (74060) ESP_GMF_TASK: Finish, strategy action: 0, [tsk:0x3fcebd64-TSK_0x3fcebd64]
I (74068) ESP_GMF_EMBED_FLASH: Closed, pos: 1920044/1920044
I (74073) ESP_GMF_CODEC_DEV: CLose, 0x3c3aa3fc, pos = 1920000/0
I (74079) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aac24, ctx:0x3c3aa040, label:aud_dec_close]
I (74088) ESP_GMF_TASK: One times job is complete, del[wk:0x3c3aac4c, ctx:0x3c3aa150, label:aud_mbc_close]
I (74097) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x0, run:0]
I (74105) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcebd64-0x3fcebd64, wk:0x0, run:0]
I (74113) PIPELINE_AUDIO_EFFECTS: [ 4 ] Tear down pipeline
W (74118) GMF_SETUP_AUD_CODEC: Unregistering default encoder
W (74124) GMF_SETUP_AUD_CODEC: Unregistering default decoder
I (74135) BOARD_DEVICE: Deinit device audio_dac ref_count: 0 device_handle:0x3fce9a7c
I (74142) BOARD_DEVICE: Device audio_dac config found: 0x3c16a220 (size: 92)
I (74143) BOARD_PERIPH: Deinit peripheral i2s_audio_out ref_count: 0
E (74149) i2s_common: i2s_channel_disable(1256): the channel has not been enabled yet
W (74157) PERIPH_I2S: Caution: Releasing TX (0x0).
W (74161) PERIPH_I2S: Caution: RX (0x3c3a87ec) forced to stop.
E (74167) i2s_common: i2s_channel_disable(1256): the channel has not been enabled yet
I (74175) BOARD_PERIPH: Deinit peripheral i2c_master ref_count: 0
I (74180) PERIPH_I2C: I2C master bus deinitialized successfully
I (74186) BOARD_MANAGER: Device audio_dac deinitialized
I (74191) PIPELINE_AUDIO_EFFECTS: Effect demo finished
I (74196) main_task: Returned from app_main()
```
