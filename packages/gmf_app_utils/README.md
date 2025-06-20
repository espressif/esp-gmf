# GMF Application Utilities

- [![Component Registry](https://components.espressif.com/components/espressif/gmf_app_utils/badge.svg)](https://components.espressif.com/components/espressif/gmf_app_utils)

- [中文版](./README_CN.md)

GMF Application Utilities (gmf_app_utils) is a utility package that provides common convenient APIs for ESP GMF application development. It includes configurations for common peripheral setups such as Wi-Fi, SD card, and AudioCodec initialization, as well as system management functions like performance monitoring interfaces and serial terminal command line interface (CLI).

The `gmf_app_utils` component also provides configuration options via **menuconfig**:

* **Configure network parameters (e.g., SSID and Password)**

  Navigate to: `GMF APP Configuration` → `Example Connection Configuration` → `WiFi SSID` and `WiFi Password`

* **Select the target development board**

  Navigate to: `GMF APP Configuration` → `Target Board`

* **Adjust the unit test task priority**

  Navigate to: `GMF APP Configuration` → `Unit Test`

## Features

### Peripheral Management (`esp_gmf_app_setup_peripheral.h`)
- Audio codec management
  - Configurable I2S modes (Standard, TDM, PDM)
  - Separate playback and recording configurations
- I2C interface management
- Storage management
  - SD card initialization and mounting
- Connectivity management
  - Wi-Fi initialization and connection
- Use menuconfig to select supported board
  - For other board support see [Other Board Support](#other-board-support) section

### System Tools (`esp_gmf_app_sys.h`)
Provides start/stop functionality for system resource monitoring, facilitating runtime performance tracking and resource usage monitoring. To use this feature, you need to enable `CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID` and `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS` in menuconfig.

### Command Line Interface (`esp_gmf_app_cli.h`)
Provides interactive CLI support where users can customize command prompts and register custom commands. It also registers some common system commands by default for direct user access, including:

- Default loaded system commands
  - `restart`: Software reset
  - `free`: View current free memory size
    - Display free size of internal memory and PSRAM
    - Show historical minimum free memory records
  - `tasks`: Display task running information and CPU usage
  - `log`: Set module log level
    - Usage: log <tag> <level 0-5>
    - Levels: NONE(0), ERROR(1), WARN(2), INFO(3), DEBUG(4), VERBOSE(5)
  - `io`: GPIO pin control
    - Usage: io <gpio_num> <level 0-1>
    - Set output level for specified GPIO pin

### Unit Test Utilities (`esp_gmf_app_unit_test.h`)
Provide common functions for GMF application unit tests

- Key Features
  - Multi-level memory leak detection (warning/critical thresholds)
  - Component-specific leak categorization (General/LWIP/NVS)
  - Flexible leak checking modes (default/custom/disabled)
  - Heap corruption detection and reporting
  - Memory pre-allocation for TCP/IP stack
  - The Task creation function of unit tests

- Usage
  - `setUp()` and `tearDown()` are automatically called before/after each test
  - Call `esp_gmf_app_test_case_uses_tcpip()` in tests that require network functionality
  - Use test annotations like `[leaks]` or `[leaks=1024]` to control leak checking
  - The main function calls' esp_gmf_app_test_main() 'to create the unit test Task

### Other Board Support

Peripheral management currently uses `codec_board` as a reference implementation for fast verification. If you want to use supported peripherals on a custom board, follow these steps:

1. **Get `codec_board`** and put into project `components` folder
   
   Can use either of following methods:
   
   1.1 Build firstly to trigger auto download then copy it into project folder:
   ```bash
   idf.py build
   mkdir -p components/codec_board
   cp -rf managed_components/tempotian__codec_board components/codec_board
   ```
   
   1.2 Download [codec_board](https://components.espressif.com/components/tempotian/codec_board/) from component registry and manual copy into project folder

2. **Add a new section** to `components/codec_board/board_cfg.txt` to describe your custom board:
   ```
   Board: MY_BOARD
   i2c: {sda: 1, scl: 2}
   i2s: {mclk: 42, bclk: 40, ws: 41, dout: 39}
   out: {codec: ES8311, pa: 38, use_mclk: 0, pa_gain: 6}
   ```

3. **Set the board type** in your application code before calling any peripheral APIs:
   ```c
   #include "codec_board.h"
   void app_main(void)
   {
       set_codec_board_type("MY_BOARD");
   }
   ```

4. **Rebuild to take effects**
   ```bash
   idf.py fullclean
   idf.py -p /dev/XXXXX flash monitor
   ```
