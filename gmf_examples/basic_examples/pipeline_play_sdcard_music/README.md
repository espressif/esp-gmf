
# Play multiple format music from microSD card

- [中文版](./README_CN.md)

## Example Brief

This example demonstrates playing music in various formats from a microSD card via FatFS IO. The music files are then decoded using a decoder element, processed by audio effects, and finally output through the CODEC_DEV_TX IO.

This example supports MP3, WAV, FLAC, AAC, M4A, TS, AMRNB, and AMRWB audio formats. By default, the MP3 format is selected.

## Example Set Up

### Default IDF Branch

This example supports IDF release/v5.3 and later branches.

### Configuration

This example requires a microSD card, with the user’s audio file renamed to `test` and stored on the card. The default format used in the example is MP3. Users can change the audio file to be played by calling the function `esp_gmf_pipeline_set_in_uri`.


### Build and Flash

Before compiling this example, ensure that the ESP-IDF environment is properly configured. If it is already set up, you can proceed to the next configuration step. If not, run the following script in the root directory of ESP-IDF to set up the build environment. For detailed steps on configuring and using ESP-IDF, please refer to the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/index.html)

```
./install.sh
. ./export.sh
```

Here are the summarized steps for compilation:

- Enter the location where the microSD card music playback test project is stored

```
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_play_sdcard_music
```

- Select the target chip for compilation. For example, to use the ESP32S3:

```
idf.py set-target esp32s3
```
- Select the compilation board, taking esp32 s3 Korvo V2 as an example:

```
idf.py menuconfig
In 'menuconfig', select 'GMF APP Configuration' -> 'Audio Board' -> 'ESP32-S3-Korvo V2', and then save and exit
```

- Build the Example

```
idf.py build
```

- Flash the program and run the monitor tool to view serial output (replace PORT with the port name):

```
idf.py -p PORT flash monitor
```

- Exit the debugging interface using ``Ctrl-]``

## How to use the Example

### Example Functionality

- After the example starts running, it will automatically play the music files stored on the microSD card, stop and exit after playback is complete, with the following output:

```c
I (1021) PLAY_SDCARD_MUSIC: [ 1 ] Mount sdcard
I (1027) gpio: GPIO[15]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (1036) gpio: GPIO[7]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (1045) gpio: GPIO[4]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
I (1095) PLAY_SDCARD_MUSIC: [ 2 ] Register all the elements and set audio information to play codec device
I (1101) ES8311: Work in Slave mode
I (1105) gpio: GPIO[48]| InputEn: 0| OutputEn: 1| OpenDrain: 0| Pullup: 0| Pulldown: 0| Intr:0
E (1109) i2s_common: i2s_channel_disable(1107): the channel has not been enabled yet
I (1117) I2S_IF: channel mode 0 bits:16/16 channel:2 mask:3
I (1123) I2S_IF: STD Mode 1 bits:16/16 channel:2 sample_rate:48000 mask:3
I (1147) Adev_Codec: Open codec device OK
I (1147) PLAY_SDCARD_MUSIC: [ 3 ] Create audio pipeline, add all elements to pipeline
W (1149) AUD_SDEC_REG: Overwrote ES decoder 6
W (1154) AUD_SDEC_REG: Overwrote ES decoder 7
W (1159) AUD_SDEC_REG: Overwrote ES decoder 8
W (1164) ESP_GMF_CODEC_HELPER: The new dec type is 6
I (1169) PLAY_SDCARD_MUSIC: [ 3.1 ] Set audio url to play
I (1176) PLAY_SDCARD_MUSIC: [ 3.2 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (1187) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcba870-0x3fcba870, wk:0x0, run:0]
I (1196) ESP_GMF_THREAD: The TSK_0x3fcba870 created on internal memory
I (1203) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcba870-0x3fcba870, wk:0x3c1a909c, run:0]
I (1212) PLAY_SDCARD_MUSIC: [ 3.3 ] Create envent group and listening event from pipeline
I (1221) PLAY_SDCARD_MUSIC: [ 4 ] Start audio_pipeline
I (1227) FILE_IO: Open, dir:1, uri:/sdcard/test.wav
I (1235) FILE_IO: File size: 3539168 byte, file position: 0
I (1239) PLAY_SDCARD_MUSIC: CB: RECV Pipeline EVT: el: OBJ_GET_TAG(event->from)-0x3c1a88b0, type: 2000, sub: ESP_GMF_EVENT_STATE_OPENING, payload: 0x0, size: 0, 0x3fcbb780
I (1255) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a909c,ctx:0x3c1a88e8, label:decoder_open]
I (1265) ESP_GMF_PORT: ACQ IN, new self payload:0x3c1a909c, port:0x3c1a8f48, el:0x3c1a88e8-decoder
I (1275) PLAY_SDCARD_MUSIC: [ 5 ] Wait stop event to the pipeline and stop all the pipeline
I (1275) ESP_GMF_PORT: ACQ OUT SET, new self payload:0x3c1a9308, p:0x3c1a8ae8, el:0x3c1a88e8-decoder
I (1295) ESP_GMF_ADEC: NOTIFY Info, rate: 44100, bits: 16, ch: 2 --> rate: 44100, bits: 16, ch: 2
I (1383) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a9104,ctx:0x3c1a89dc, label:rate_cvt_open]
I (1383) ESP_GMF_PORT: ACQ OUT SET, new self payload:0x3c1a9104, p:0x3c1a8c90, el:0x3c1a89dc-aud_rate_cvt
I (1393) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a916c,ctx:0x3c1a8b70, label:ch_cvt_open]
I (1402) PLAY_SDCARD_MUSIC: CB: RECV Pipeline EVT: el: OBJ_GET_TAG(event->from)-0x3c1a8d18, type: 3000, sub: ESP_GMF_EVENT_STATE_INITIALIZED, payload: 0x3fcbb3e0, size: 12, 0x3fcbb780
I (1419) PLAY_SDCARD_MUSIC: CB: RECV Pipeline EVT: el: OBJ_GET_TAG(event->from)-0x3c1a8d18, type: 2000, sub: ESP_GMF_EVENT_STATE_RUNNING, payload: 0x0, size: 0, 0x3fcbb780
I (1435) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a91bc,ctx:0x3c1a8d18, label:bit_cvt_open]
I (21322) FILE_IO: No more data, ret: 0
I (21322) ESP_GMF_ADEC: Total: 3538944 bytes(194)
I (21322) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcba870-0x3fcba870, wk:0x3c1a90d0, job:0x3c1a88e8-decoder_proc]
I (21332) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcba870-0x3fcba870, wk:0x3c1a9138, job:0x3c1a89dc-rate_cvt_proc]
I (21342) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcba870-0x3fcba870, wk:0x3c1a918c, job:0x3c1a8b70-ch_cvt_proc]
I (21353) ESP_GMF_TASK: Job is done, [tsk:TSK_0x3fcba870-0x3fcba870, wk:0x3c1a91f0, job:0x3c1a8d18-bit_cvt_proc]
I (21364) FILE_IO: CLose, 0x3c1a8ebc, pos = 3539168/3539168
I (21370) ESP_GMF_CODEC_IO: CLose, 0x3c1a8f8c, pos = 3851912/0
I (21377) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a90c0,ctx:0x3c1a88e8, label:decoder_close]
I (21387) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a9128,ctx:0x3c1a89dc, label:rate_cvt_close]
I (21397) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a915c,ctx:0x3c1a8b70, label:ch_cvt_close]
I (21408) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a9190,ctx:0x3c1a8d18, label:bit_cvt_close]
I (21418) PLAY_SDCARD_MUSIC: CB: RECV Pipeline EVT: el: OBJ_GET_TAG(event->from)-0x3c1a88b0, type: 2000, sub: ESP_GMF_EVENT_STATE_FINISHED, payload: 0x0, size: 0, 0x3fcbb780
I (21434) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcba870-0x3fcba870, wk:0x0, run:0]
I (21443) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcba870-0x3fcba870, wk:0x0, run:0]
W (21451) ESP_GMF_TASK: Already stopped, ESP_GMF_EVENT_STATE_FINISHED, [TSK_0x3fcba870,0x3fcba870]
I (21461) PLAY_SDCARD_MUSIC: [ 6 ] Destroy all the resources
```

## Troubleshooting

If your log has the following error message, this is because the audio file that needs to be played is not found in the microSD card, please follow the above *Configuration* section to rename the file.

```c
I (1221) PLAY_SDCARD_MUSIC: [ 4 ] Start audio_pipeline
I (1227) FILE_IO: Open, dir:1, uri:/sdcard/test3.wav
I (1241) FILE_IO: File size: 0 byte, file position: 0
I (1241) PLAY_SDCARD_MUSIC: CB: RECV Pipeline EVT: el: OBJ_GET_TAG(event->from)-0x3c1a88b0, type: 2000, sub: ESP_GMF_EVENT_STATE_OPENING, payload: 0x0, size: 0, 0x3fcbb780
I (1254) ESP_GMF_TASK: One times job is complete, del[wk:0x3c1a909c,ctx:0x3c1a88e8, label:decoder_open]
I (1264) ESP_GMF_PORT: ACQ IN, new self payload:0x3c1a909c, port:0x3c1a8f48, el:0x3c1a88e8-decoder
E (1274) FILE_IO: The error is happened in reading data, error msg: Bad file number
E (1282) ESP_GMF_ADEC: Read data error, ret: -1, line: 106
E (1288) ESP_GMF_TASK: Job failed[tsk:TSK_0x3fcba870-0x3fcba870:0x3c1a90d0-0x3c1a88e8-decoder_proc], ret:-1, st:ESP_GMF_EVENT_STATE_RUNNING
```
