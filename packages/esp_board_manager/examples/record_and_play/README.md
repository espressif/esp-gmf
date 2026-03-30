# Audio Loopback (Record and Play)

- [中文版](./README_CN.md)
- Example difficulty: ⭐⭐

## Example Brief

- This example runs real-time audio loopback: samples from the ADC (mic) are immediately played on the DAC (speaker) for a fixed time (`LOOPBACK_DURATION_SEC`).
- Technically it demonstrates simultaneous `esp_board_manager` use of `audio_dac` and `audio_adc`, two independent `esp_codec_dev_open` calls with the same `esp_codec_dev_sample_info_t`, and a read→write loop without SD storage.

### Typical Scenarios

- Quick validation that both capture and playback paths work on the target board.

### Run Flow

Init DAC → init ADC → open both codecs with same rate/channels/bits → optional gain/volume → repeat read buffer / write buffer until timeout → close codecs and deinit devices.

### File Structure

```
├── main
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   └── record_and_play.c
├── CMakeLists.txt               Project: bmgr_record_and_play
├── partitions.csv
├── sdkconfig.defaults
├── sdkconfig.defaults.esp32
├── sdkconfig.defaults.esp32s3
├── sdkconfig.defaults.esp32p4
├── README.md
└── README_CN.md
```

## Environment Setup

### Hardware Required

- Board with both Audio ADC and Audio DAC definitions (`audio_adc`, `audio_dac`), e.g. ESP32-S3-Korvo-2 V3.
- Microphone and speaker per board design.

### Default IDF Branch

This example supports IDF release/v5.4 (>= v5.4.3) and release/v5.5 (>= v5.5.2).

## Build and Flash

### Build Preparation

Before building this example, ensure the ESP-IDF environment is set up. If it is already set up, skip to the project directory steps below. If not, run the following in the ESP-IDF root directory. For full steps, see the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/index.html).

```
./install.sh
. ./export.sh
```

Short steps:

- Go to this example's project directory:

```
cd $YOUR_GMF_PATH/packages/esp_board_manager/examples/record_and_play
```

- Configure the `esp_board_manager` path to activate the environment (only needs to be executed once under the current terminal):

```
# Linux / macOS:
export IDF_EXTRA_ACTIONS_PATH=$YOUR_GMF_PATH/packages/esp_board_manager

# Windows PowerShell:
$env:IDF_EXTRA_ACTIONS_PATH = "$YOUR_GMF_PATH/packages/esp_board_manager"

# Windows CMD:
set IDF_EXTRA_ACTIONS_PATH=$YOUR_GMF_PATH/packages/esp_board_manager
```

- Generate board configuration and code for your board (example: `esp32_s3_korvo2_v3`):

```
idf.py gen-bmgr-config -b esp32_s3_korvo2_v3
```

Custom boards: see [Custom board](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md#custom-board).

- List supported boards:

```
idf.py gen-bmgr-config -l
```

### Project Configuration

- Board from `gen-bmgr-config`.
- Tune `DEFAULT_SAMPLE_RATE`, `DEFAULT_CHANNELS`, `DEFAULT_BITS_PER_SAMPLE`, `DEFAULT_PLAY_VOL`, `DEFAULT_REC_GAIN`, and `LOOPBACK_DURATION_SEC` in `record_and_play.c`.
- Reduce playback volume or mic gain if you hear feedback or howling.
- For development boards using ADC microphones, PDM speakers, or other speakers that do not rely on Codec chips, the relevant configurations need to be enabled in `menuconfig`:

    - `Component config` -> `Audio Codec Device Data Interface Configuration` -> `Support ADC continuous data interface`

    - `Component config` -> `Audio Codec Device Configuration` -> `Support Dummy Codec Chip`


### Build and Flash Commands

```
idf.py build
```

```
idf.py -p PORT flash monitor
```

Exit monitor: `Ctrl-]`

## How to Use the Example

### Functionality and Usage

- After boot, loopback runs for the configured duration; you should hear the microphone signal on the speaker.

### Log Output

```text
I (xxx) BMGR_RECORD_AND_PLAY: Audio loopback: record from ADC and play through DAC for 30 seconds
I (xxx) BMGR_RECORD_AND_PLAY: Audio loopback started: 16000 Hz, 2 ch, 16 bit
I (xxx) BMGR_RECORD_AND_PLAY: Loopback running... 1/30s, total 131072 bytes
...
I (xxx) BMGR_RECORD_AND_PLAY: Audio loopback completed. Total bytes transferred: ...
```

(Exact timestamps vary; adjust duration in source if needed.)

## Troubleshooting

### `idf.py gen-bmgr-config` has no `-b` option

`IDF_EXTRA_ACTIONS_PATH` must point to `esp_board_manager`. Verify:

```
# Linux / macOS:
echo $IDF_EXTRA_ACTIONS_PATH

# Windows PowerShell:
echo $env:IDF_EXTRA_ACTIONS_PATH

# Windows CMD:
echo %IDF_EXTRA_ACTIONS_PATH%
```

### Echo or feedback

Lower `DEFAULT_PLAY_VOL` or `DEFAULT_REC_GAIN` in `record_and_play.c`, or increase physical isolation between mic and speaker.

### Only one of ADC/DAC exists on board

This example requires both devices in the board YAML; choose a full audio reference board or extend your custom board definition.

### Custom board

See [esp_board_manager README](https://github.com/espressif/esp-gmf/blob/main/packages/esp_board_manager/README.md).
