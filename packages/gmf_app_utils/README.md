# GMF Application Utilities

- [中文版](./README_CN.md)

GMF Application Utilities (gmf_app_utils) is a utility package that provides common convenient APIs for ESP GMF application development. It includes configurations for common peripheral setups such as Wi-Fi, SD card, and AudioCodec initialization, as well as system management functions like performance monitoring interfaces and serial terminal command line interface (CLI).

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
