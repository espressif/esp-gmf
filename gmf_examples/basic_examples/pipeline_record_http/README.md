# Recording Audio Stream to HTTP Server

- [中文版](./README_CN.md)
- Basic Example ⭐

## Example Brief

This example demonstrates how to capture audio using CODEC_DEV_RX IO from the codec, encode the recorded sound using an encoder element, and upload the encoded data to the HTTP server via HTTP IO.

This example supports encoding the recording into AAC、G711A、G711U、AMRNB、AMRWB、OPUS、ADPCM、LC3、SBC and PCM audio formats, with PCM as the default format.

## Example Set Up

### Default IDF Branch

This example supports IDF release/v5.3 and later branches.

### Prerequisites

This example requires you to set up an HTTP server. The recorded and encoded audio stream will be uploaded to the HTTP server, and you can customize the server logic to handle the received audio stream.

A test script `server.py` is provided in the `${PROJECT_DIR}/` folder. You can run this script to set up a default HTTP server, which will receive audio streams and save as corresponding files. The Python command is as follows:

```
python $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_record_http/server.py
```

### Build and Flash

Before compiling this example, ensure that the ESP-IDF environment is properly configured. If it is already set up, you can proceed to the next configuration step. If not, run the following script in the root directory of ESP-IDF to set up the build environment. For detailed steps on configuring and using ESP-IDF, please refer to the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/index.html)

```
./install.sh
. ./export.sh
```

Here are the summarized steps for compilation:

- Enter the location where the audio recording and upload to the HTTP server test project is stored

```
cd $YOUR_GMF_PATH/gmf_examples/basic_examples/pipeline_record_http
```

- Select the target chip for compilation. For example, to use the ESP32S3:

```
idf.py set-target esp32s3
```

- Use `esp_board_manager` to select supported board and custom board, be sure to see [ESP Board Manager](https://components.espressif.com/components/espressif/esp_board_manager), taking ESP32-S3-Korvo2 V3.1 as an example:

```
export IDF_EXTRA_ACTIONS_PATH=./managed_components/espressif__esp_board_manager
idf.py gen-bmgr-config -b esp32_s3_korvo2_v3
```

- Configure network parameters (such as SSID and Password):

```
In 'menuconfig', select 'GMF APP Configuration' -> 'Example Connection Configuration' -> 'WiFi SSID' and 'WiFi Password'
```

- Configure example parameters (such as HTTP server url):

```
In 'menuconfig', select 'GMF Example Configuration' -> 'HTTP SERVER URL', eg: `http://192.168.1.101:8000/upload`, then save and exit
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

## Configuration

The example can be configured through the following settings:

- `DEFAULT_AUDIO_TYPE`: Audio format (e.g. AAC, OPUS)
- `DEFAULT_RECORD_SAMPLE_RATE`: Sample rate (e.g. 16000 Hz)
- `DEFAULT_RECORD_BITS`: Bits per sample (e.g. 16 bits)
- `DEFAULT_RECORD_CHANNEL`: Number of audio channels
- `DEFAULT_MICROPHONE_GAIN`: Microphone gain for recording
- `DEFAULT_RECORD_DURATION_MS`: Recording duration (e.g. 15000 ms)

## How to use the Example

### Example Functionality

- The example requires the computer and board to be connected to the same Wi-Fi. For setting up an HTTP server, please refer to the [Prerequisites](#Prerequisites) section. The script will running successfully, with the following output:

```
Serving HTTP on 192.168.1.101 port 8000
```

- After the example starts running, the recorded data will be encoded and automatically upload to the HTTP server, stop and exit after recording is complete, with the following output:

```c
I (966) REC_HTTP: [ 1 ] Mount peripheral
I (975) example_connect: Start example_connect.
I (976) pp: pp rom version: e7ae62f
I (977) net80211: net80211 rom version: e7ae62f
I (983) wifi:wifi driver task: 3fcb6838, prio:23, stack:6656, core=0
I (990) wifi:wifi firmware version: 48ea317a7
I (992) wifi:wifi certification version: v7.0
I (996) wifi:config NVS flash: enabled
I (999) wifi:config nano formatting: disabled
I (1003) wifi:Init data frame dynamic rx buffer num: 32
I (1008) wifi:Init static rx mgmt buffer num: 5
I (1012) wifi:Init management short buffer num: 32
I (1017) wifi:Init static tx buffer num: 16
I (1021) wifi:Init tx cache buffer num: 32
I (1025) wifi:Init static tx FG buffer num: 2
I (1029) wifi:Init static rx buffer size: 1600
I (1033) wifi:Init static rx buffer num: 16
I (1037) wifi:Init dynamic rx buffer num: 32
I (1041) wifi_init: rx ba win: 16
I (1044) wifi_init: accept mbox: 6
I (1047) wifi_init: tcpip mbox: 32
I (1050) wifi_init: udp mbox: 6
I (1053) wifi_init: tcp mbox: 6
I (1056) wifi_init: tcp tx win: 5760
I (1059) wifi_init: tcp rx win: 5760
I (1062) wifi_init: tcp mss: 1440
I (1065) wifi_init: WiFi/LWIP prefer SPIRAM
I (1069) wifi_init: WiFi IRAM OP enabled
I (1073) wifi_init: WiFi RX IRAM OP enabled
I (1078) phy_init: phy_version 680,a6008b2,Jun  4 2024,16:41:10
I (1126) wifi:mode : sta (98:a3:16:de:b3:84)
I (1127) wifi:enable tsf
I (1129) example_connect: Connecting to ESP_Audio2...
W (1129) wifi:Password length matches WPA2 standards, authmode threshold changes from OPEN to WPA2
I (1137) example_connect: Waiting for IP(s)
I (3852) wifi:new:<1,1>, old:<1,0>, ap:<255,255>, sta:<1,1>, prof:1, snd_ch_cfg:0x0
I (3853) wifi:state: init -> auth (0xb0)
I (3856) wifi:state: auth -> assoc (0x0)
I (3865) wifi:state: assoc -> run (0x10)
I (3908) wifi:connected with ESP_Audio2, aid = 2, channel 1, 40U, bssid = 04:f9:f8:cb:1e:a9
I (3909) wifi:security: WPA2-PSK, phy: bgn, rssi: -25
I (3911) wifi:pm start, type: 1

I (3914) wifi:dp: 1, bi: 102400, li: 3, scale listen interval from 307200 us to 307200 us
I (3922) wifi:set rx beacon pti, rx_bcn_pti: 0, bcn_timeout: 25000, mt_pti: 0, mt_time: 10000
I (3930) wifi:AP's beacon interval = 102400 us, DTIM period = 1
I (4570) wifi:<ba-add>idx:0 (ifx:0, 04:f9:f8:cb:1e:a9), tid:0, ssn:0, winSize:64
I (4975) example_connect: Got IPv6 event: Interface "example_netif_sta" address: fe80:0000:0000:0000:9aa3:16ff:fede:b384, type: ESP_IP6_ADDR_IS_LINK_LOCAL
I (5761) esp_netif_handlers: example_netif_sta ip: 192.168.1.100, mask: 255.255.255.0, gw: 192.168.1.1
I (5762) example_connect: Got IPv4 event: Interface "example_netif_sta" address: 192.168.1.100
I (5769) example_common: Connected to example_netif_sta
I (5773) example_common: - IPv4 address: 192.168.1.100,
I (5778) example_common: - IPv6 address: fe80:0000:0000:0000:9aa3:16ff:fede:b384, type: ESP_IP6_ADDR_IS_LINK_LOCAL
i2c: {sda: 17, scl: 18}
i2s: {mclk: 16, bclk: 9, ws: 45, din: 10, dout: 8}
out: {codec: ES8311, pa: 48, pa_gain: 6, use_mclk: 1, pa_gain: 6}
in: {codec: ES7210}
sdcard: {clk: 15, cmd: 7, d0: 4}
camera: {type: dvp, xclk: 40, pclk: 11, vsync: 21, href: 38, d0: 13, d1: 47, d2: 14, d3: 3, d4: 12, d5: 42, d6: 41, d7: 39}
lcd: {bus: spi, extend_io: tca9554, controller: st7789, spi_bus: 2, mirror_x: 1, mirror_y: 1, swap_xy: 0, color_inv: 0, width: 320, height: 240, ctrl: ext1, rst: ext2, cs: ext3, dc: 2, clk: 1, mosi: 0, cmd_bits: 8, param_bits: 8}
E (5837) i2c.master: this port has not been initialized, please initialize it first
I (5844) CODEC_INIT: Set master handle 0 0x3c1708cc
I (5848) CODEC_INIT: in:1 out:1 port: 1
I (5851) CODEC_INIT: Success to int i2c: 0
I (5855) CODEC_INIT: Init i2s 0 type: 3 mclk:16 bclk:9 ws:45 din:10 dout:8
I (5862) CODEC_INIT: tx:0x3c170ce0 rx:0x3c170e74
I (5867) CODEC_INIT: output init std ret 0
I (5871) CODEC_INIT: Input init std ret 0
I (5874) CODEC_INIT: Init i2s 0 ok
I (5877) CODEC_INIT: Success to init i2s: 0
I (5881) CODEC_INIT: Success to int i2c: 0
I (5884) CODEC_INIT: Success to init i2s: 0
I (5888) CODEC_INIT: Get out handle 0x3c170ce0 port 0
I (5898) ES8311: Work in Slave mode
I (5902) gpio: GPIO[48]| InputEn: 0| OutputEn: 1| OpenDrain: 0| Pullup: 0| Pulldown: 0| Intr:0 
I (5908) ES7210: Work in Slave mode
I (5915) ES7210: Enable ES7210_INPUT_MIC1
I (5917) ES7210: Enable ES7210_INPUT_MIC3
I (5925) I2S_IF: channel mode 0 bits:16/16 channel:2 mask:1
I (5926) I2S_IF: STD Mode 1 bits:16/16 channel:2 sample_rate:16000 mask:1
I (5942) Adev_Codec: Open codec device OK
I (5944) I2S_IF: channel mode 0 bits:16/16 channel:2 mask:1
I (5945) I2S_IF: STD Mode 0 bits:16/16 channel:2 sample_rate:16000 mask:1
I (5948) ES7210: Bits 16
I (5956) ES7210: Enable ES7210_INPUT_MIC1
I (5959) ES7210: Enable ES7210_INPUT_MIC3
I (5967) ES7210: Unmuted
I (5967) Adev_Codec: Open codec device OK
I (5969) REC_HTTP: [ 2 ] Register all the elements and set audio information to record codec device
I (5972) ESP_GMF_POOL: Registered items on pool:0x3c1710d8, app_main-171
I (5978) ESP_GMF_POOL: IO, Item:0x3c17118c, H:0x3c1710ec, TAG:io_codec_dev
I (5984) ESP_GMF_POOL: IO, Item:0x3c17123c, H:0x3c17119c, TAG:io_codec_dev
I (5991) ESP_GMF_POOL: IO, Item:0x3c17131c, H:0x3c17124c, TAG:io_http
I (5997) ESP_GMF_POOL: EL, Item:0x3c17141c, H:0x3c17132c, TAG:aud_enc
I (6003) REC_HTTP: [ 3 ] Create audio pipeline
I (6008) REC_HTTP: [ 3.1 ] Register the http io of the record pipeline
I (6014) REC_HTTP: [ 3.2 ] Reconfig audio encoder type and audio information and report information to the record pipeline
W (6025) ESP_GMF_PIPELINE: There is no thread for add jobs, pipe:0x3c17142c, tsk:0x0, [el:aud_enc-0x3c17149c]
I (6034) REC_HTTP: [ 3.3 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task
I (6044) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcc68b8-0x3fcc68b8, wk:0x0, run:0]
I (6052) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcc68b8-0x3fcc68b8, wk:0x3c17177c, run:0]
I (6060) REC_HTTP: [ 3.4 ] Create envent group and listening event from pipeline
I (6067) REC_HTTP: [ 4 ] Start audio_pipeline
I (6072) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcc7ba0, wk:0x3c171840, run:0]
I (6079) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcc7ba0, wk:0x3c171840, run:0]
I (6073) ESP_GMF_HTTP: HTTP Open, URI = http://192.168.1.101:8000/upload
W (6094) REC_HTTP: [ + ] HTTP client HTTP_STREAM_PRE_REQUEST
I (6099) ESP_GMF_BLOCK: The block buf:0x3c1724c4, end:0x3c1744c4
I (6104) NEW_DATA_BUS: New block buf, num:1, item_cnt:8192, db:0x3c1744c8
I (6138) REC_HTTP: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c17142c, type:8192, sub:ESP_GMF_EVENT_STATE_OPENING, payload:0x0, size:0,0x0
I (6140) REC_HTTP: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c17149c, type:12288, sub:ESP_GMF_EVENT_STATE_INITIALIZED, payload:0x3fcc77e4, size:16,0x0
I (6154) ESP_GMF_AENC: Open, type:PCM, acquire in frame: 320, out frame: 320
I (6161) REC_HTTP: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c17149c, type:8192, sub:ESP_GMF_EVENT_STATE_RUNNING, payload:0x0, size:0,0x0
I (6174) ESP_GMF_TASK: One times job is complete, del[wk:0x3c17177c, ctx:0x3c17149c, label:aud_enc_open]
I (6184) ESP_GMF_PORT: ACQ IN, new self payload:0x3c17177c, port:0x3c171608, el:0x3c17149c-aud_enc
I (21202) ESP_GMF_CODEC_DEV: CLose, 0x3c171568, pos = 482560/0
I (21203) ESP_GMF_TASK: Job is done, [tsk:io_http-0x3fcc7ba0, wk:0x3c171840, job:0x3c171648-io_http_proc]
I (21206) ESP_GMF_TASK: Waiting to run... [tsk:io_http-0x3fcc7ba0, wk:0x0, run:0]
W (21207) REC_HTTP: [ + ] HTTP client HTTP_STREAM_POST_REQUEST, write end chunked marker
W (21268) REC_HTTP: [ + ] HTTP client HTTP_STREAM_FINISH_REQUEST
W (21269) HTTP_CLIENT: esp_transport_read returned:-1 and errno:128 
I (21270) REC_HTTP: Got HTTP Response = File 20251016T122806Z_16000_16_1.wav was written, size 480000
I (21280) ESP_GMF_TASK: One times job is complete, del[wk:0x3c160a20, ctx:0x3c17149c, label:aud_enc_close]
I (21288) REC_HTTP: CB: RECV Pipeline EVT: el:OBJ_GET_TAG(event->from)-0x3c17142c, type:8192, sub:ESP_GMF_EVENT_STATE_STOPPED, payload:0x0, size:0,0x0
I (21301) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcc68b8-0x3fcc68b8, wk:0x0, run:0]
I (21308) ESP_GMF_TASK: Waiting to run... [tsk:TSK_0x3fcc68b8-0x3fcc68b8, wk:0x0, run:0]
I (21316) REC_HTTP: [ 5 ] Destroy all the resources
W (21321) GMF_SETUP_AUD_CODEC: Unregistering default encoder
I (21332) I2S_IF: Pending out channel for in channel running
E (21340) i2s_common: i2s_channel_disable(1187): the channel has not been enabled yet
E (21341) i2s_common: i2s_channel_disable(1187): the channel has not been enabled yet
I (21347) wifi:state: run -> init (0x0)
I (21351) wifi:pm stop, total sleep time: 1473106 us / 17437080 us

I (21356) wifi:<ba-del>idx:0, tid:0
I (21360) wifi:new:<1,0>, old:<1,1>, ap:<255,255>, sta:<1,1>, prof:1, snd_ch_cfg:0x0
I (21469) wifi:flush txq
I (21469) wifi:stop sw txq
I (21469) wifi:lmac stop hw txq
I (21471) wifi:Deinit lldesc rx mblock:16
I (21473) main_task: Returned from app_main()
```

- After the HTTP server successfully receives the recorded data, the script will output:

```
Do Post......
Audio information, sample rates: 16000, bits: 16, channel(s): 1
Total bytes received: 480000
192.168.1.101 - - [22/Oct/2025 20:15:53] "POST /upload HTTP/1.1" 200 -
```
