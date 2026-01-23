# Changelog

## 0.5.3

### Features

- Added [`play_embed_music`](examples/play_embed_music/README.md) example to show how to use board manager to initialize codec and play flash wav audio file
- Added support for M5STACK TAB5, supported devices includes:
  Codec, LCD, LCD Touch, Camera and SD card
- Added support for ESP_BOX_LITE, supported devices includes:
  Codec and LCD
- Adapt to the new version of `esp_io_expander` component by adding support for configuring IO output modes and enabling pull-up/pull-down resistors

### Bug Fixes
- Modified the effective scope of the `board_manager_global_callback` function to ensure that build-related operations can correctly include `board_manager.defaults`
- Fixed an issue where the `pa_pin` configuration of ES8156 did not take effect correctly
- Fixed ledc timer parsing script error for `timer_sel`

### Modifications
- Change the output path of `board_manager.defaults` to `components/gen_bmgr_config/` to exclude it from git diffs

## 0.5.2

### Bug Fixes

- Fixed an issue where the dev_display_lcd mirror_x/y and swap_xy configurations were not taking effect correctly
- Fixed compilation issue when the `sdkconfig.defaults` file not exists

## 0.5.1

### ⚠️ Important Changes

- Changed board defaults to use `board_manager.defaults` file instead of appending to `sdkconfig.defaults`. Board-specific configurations are now automatically applied via `SDKCONFIG_DEFAULTS` environment variable during build/menuconfig/reconfigure.
- Changed backup file from `sdkconfig.bmgr_board.backup` to `sdkconfig.bmgr_board.old`
- Use `dev_display_lcd` to replace `dev_display_lcd_spi` in echoear_core_board_v1_1/v1_2, esp32_s3_korvo2_v3, esp_box_3, m5stack_cores3, dual_eyes_board_v1_0
- Use `dev_fs_fat` to replace `dev_fatfs_sdcard` and `dev_fatfs_sdcard_spi` in echoear_core_board_v1_1/v1_2, esp32_s3_korvo2_v3, esp_box_3, m5stack_cores3, esp32_p4_function_ev, lyrat_mini_v1_1
- Add `dev_power_ctrl` device support for echoear_core_board_v1_1/v1_2, replace `dev_gpio_ctrl` used to apply power
- Removed support for `dev_display_lcd`, `dev_fatfs_sdcard`, `dev_fatfs_sdcard_spi` and related test code

### New Script

- Add a new script `create_board.py` to simplify the process of creating a new board.
  This script simplifies the process of copying configurations from different peripheral and device directories when users add new boards, provides an interactive new board creation process, allowing sequential selection of chip, device, and peripherals. After passing the peripheral dependency check, the script automatically creates four configuration files required for the board.
  - Usage:
    ```python
    # Create a new board in default path: components/<board_name>
    idf.py gen-bmgr-config -n <board_name>

    # Create a new board in specific path:
    idf.py gen-bmgr-config -n path/to/board/<board_name>
    ```
  - Note: It is recommended not to run create_board.py directly as there may be path issues.

### Features
- Added prebuild script for convenient compilation
- Added global callback to auto-inject board defaults via SDKCONFIG_DEFAULTS mechanism
- Supported `-D SDKCONFIG_DEFAULTS=xxx` parameter with automatic merging

### Modifications

- Added support for IDF v6.x (trial version)
  - Added adapter for periph_i2s
  - Added warning for periph_rmt, periph_pcnt, periph_mcpwm, dev_display_lcd_sub_dsi (not supported by IDF v6.x yet)
- Refine sub device support for dev_button, splitting the sub_type `adc` into `adc_single` and `adc_multi`, splitting the `multi_click` configuration into `multi_click` and `click_counts`. Keep the enabling of `multi_click` and the specific number of clicks as two separate configuration items, consistent with the configuration approach used for `long_press_start` and `long_press_up`
- Modify CMakeLists.txt to fix the driver/xxx.h header file undecleared issue in IDF v6.x
- Renamed `apply_board_sdkconfig_defaults` to `generate_board_manager_defaults` for clarity
- Removed legacy methods for sdkconfig.defaults manipulation

### Bug Fixes

- Fixed an issue that find_project_root finding unrelated projects
- Fixed an issue where `CMakeLists.txt` generated on the Windows platform would cause compilation errors due to the `\` in paths

## 0.5.0

### ⚠️ Important Changes

- Board Switching Now Deletes `sdkconfig`: When switching boards, the script automatically backs up `sdkconfig` to `sdkconfig.bmgr_board.backup` and removes the original to prevent configuration pollution. Additionally, board-specific configurations from `boards/<board_name>/sdkconfig.defaults.board` are automatically appended to your project's `sdkconfig.defaults`.
- Simplified board selection - board name/index as direct parameter (gen_bmgr_config_codes.py only)
- Removed automatic sdkconfig modification (breaking change)

### Features

- Added `-x, --clean` command to clean generated files and reset CMakeLists.txt and idf_component.yml
- Enhanced board selection - board name/index as direct parameter (gen_bmgr_config_codes.py only)
- Added comprehensive test suite with Python pytest
- Added 7 test boards for testing coverage
- Enhanced board scanning mechanism for better discovery
- Enhanced board parameter handling in test cases
- Added support for `sdkconfig.defaults.board` file in each board directory. When switching boards, the script automatically appends board-specific configurations to the project's `sdkconfig.defaults` file.
- Added automatic `sdkconfig` backup to `sdkconfig.bmgr_board.backup` when switching boards
- Implement sub_type field for device configurations, add hierarchical Kconfig generation (ESP_BOARD_DEV_SUB<SUB_TYPE>_SUPPORT)
- Add unified entry registration system using linker sections
- Implement device extra function registration interface `EXTRA_FUNC_IMPLEMENT`
- Devices type support:
  - Create FS_FAT device with SDMMC/SPI sub-type implementations, `dev_fs_sdcard` and `dev_fs_sdcard_spi` while be deprecated in feature version
  - Create DISPLAY_LCD device with SPI/DSI sub-type implementations, `dev_display_lcd_spi` while be deprecated in feature version
  - Create BUTTON device with GPIO/ADC sub-type implementations
  - Create CAMERA device with DVP/CSI sub-type implementations
  - Create POWER_CTRL device, added `power_ctrl_device` attribute in `esp_board_device_desc_t`, for devices configured with this attribute, the power will be automatically turned on during initialization
- Board support:
  - Added lcd, camera support for `esp32_p4_function_ev` board
- Peripheral type support:
  - UART, ADC, RMT, PCNT, DAC, SDM, MCPWM, ANACMPR, LDO and DSI.
- Added [`play_sdcard_music`](example/play_sdcard_music/README.md) example to show how to use board manager to initialize codec and play wav audio files
- Added [`record_to_sdcard`](example/record_to_sdcard/README.md) example to show how to use board manager to initialize codec and record wav audio files

### Modifications
- Added `clk_src` configuration for `dev_audio_codec`
- Reorganized the peripherals directory structure
- Modify the periph role macro definition from string to enum
- Removed the unused periph type macro definition and added the commonly used periph name macro definition

### Bug Fixes
- Fixed pa active_level configuration for `esp32_p4_function_ev` board

## 0.4.8

### Bug Fixes

- Fixed the issue where the configuration of the periph_ledc was not correctly obtained during the initialization of dev_ledc_ctrl
- Added `need_reset` configuration to control whether to reset lcd during the initialization of `display_lcd_spi`
- Modified the default configuration of echoear_core_board_v1_2 to prevent screen flickering during initialization
- Fixed incorrect PA gain assigned for codec_device

### Features
- Added gpio_led support for echoear_core_board_v1_2

## 0.4.7

### Features

- Added board support for M5STACK CORES3
  * Complete YAML configuration files (board_info.yaml, board_peripherals.yaml, board_devices.yaml)
  * Support for AW88298 DAC, ES7210 ADC, ILI9342 LCD, FT5X06 LCD TOUCH, AW9523B GPIO EXPANDER, AXP2101 PMU and SD card devices
  * I2C, I2S, SPI and GPIO peripherals configuration
- Supported configuring invert_color during lcd_spi initialization

### Bug Fixes

- Fixed the issue where the codec's gain configuration was not taking effect correctly
- Fixed the issue with incorrect usage of handle for sdcard and sdcard_spi
- Modified the GPIO expander and customer device test cases to make them more generic
- Fixed the test app to allow sdcard_spi to also function properly in codec testing

## 0.4.6

### Bug Fixes

- Fixed the SD card mount failure issue

## 0.4.5

### Bug Fixes

- Fix `periph_i2s_deinit` channel delete
- Fix i2s config `total_slot` for esp32_s3_korvo2_v3

## 0.4.4

### Bug Fixes

- Delete build/config/sdkconfig.json after updating sdkconfig to ensure component manager uses the latest configuration

## v0.4.3

### Bug Fixes

- Reduce the default spi max_transfer_sz value for echoear_core_board_v1_2 to avoid allocating excessive memory

## v0.4.2

### Features

- Added full board support for ESP32 Lyrat Mini V1.1
  * Complete YAML configuration files (board_info.yaml, board_peripherals.yaml, board_devices.yaml)
  * Support for ES8311 DAC, ES7243E ADC, and SD card devices
  * I2C, I2S, and GPIO peripherals configuration
- Enhanced I2S peripheral support with improved configuration handling
- Added `board_utils.py` helper functions for board configuration

### Bug Fixes

- Fixed build errors caused by incorrect use of `extern "C"` guards in `esp_board_manager_includes.h`
- Removed unused dependency on `test_apps`
- Updated audio codec device implementation for better compatibility
- Appended `esp_board_manager_defs.h` to `esp_board_manager_includes.h` to ensure all necessary header files are included

## v0.4.1

### Features

- Renamed esp_board_manager_ext.py to idf_ext.py for compatibility with ESP-IDF v6.0 auto-discovery
- Update `Kconfig.in` with new configuration options to hide the board selection menu
- Update the README to include information about how to add new boards and the development roadmap
- Added the esp_board_periph_ref_handle and esp_board_periph_unref_handle APIs to obtain and release a handle
- Added esp_board_find_utils.h and esp_board_manager_includes.h
- Added `esp_board_device_get_config_by_handle()` API for finding device configuration by handle

### Bug Fixes

- Delete only `build/CMakeCache.txt` to avoid a full clean when switching boards
- Fixed the configuration of the sdmmc sdcard host slot, ensuring the configuration for the host slot in the yml file takes effect correctly
- Using slot 0 to drive the sdcard for p4_function board, avoiding conflict with wifi hosted
- Correct the error codes

## v0.4.0

### Features

- Add `init_skip` field to control device auto-initialization
- Add custom device type with test support
- Add gpio expander device type with test support
- Add camera device type with test support, support dvp bus type now
- Instead of modifying idf_component.yml, the board manager uses a Kconfig option to handle dynamic dependencies
- Add `adc_channel_labels` field to audio codec for better channel identification
- Add board selection priority documentation and sdkconfig troubleshooting guide
- Instead of copying board source files to gen_bmgr_codes, now reference them directly via SRC_DIRS and INCLUDE_DIRS to avoid duplication.
- Add device dependencies support with `${BOARD_PATH}` variable resolution
- Add sdspi sdcard device type
- Add `esp_board_device_find_by_handle()` API for finding device by handle
- Add board information output to CMakeLists.txt generation
- Update esp_codec_dev dependency to version `~1.5`
- Refine dependency for esp_codec_dev
- Update README with dependencies usage guide
- Update `CONFIG_IDF_TARGET` in sdkconfig according to the chip name specified in `board_info.yaml`
- Clear the build directory to get the correct dependencies when switching boards

### Supported Boards

- **ESP32-S3 Korvo2 V3**: Full LCD, LCD Touch, DVP Camera support
- **ESP32-P4 Function-EV**: Codec, SD card supported

## v0.3.0 (Initial Release)

### Core Features

- **ESP Board Manager**: Initial release of comprehensive board configuration management system
- **YAML-based Configuration**: Support for `board_peripherals.yaml` and `board_devices.yaml` configuration files
- **Automatic Code Generation**: Modular code generation system with 8-step streamlined process
- **Multi-path Board Scanning**: Support for Default, Customer, and Components board paths
- **Automatic Dependency Management**: Smart component dependency detection and `idf_component.yml` updates
- **SDK Configuration Automation**: Automatic ESP-IDF feature enabling based on board requirements

### Supported Boards

- **Echoear Core Board V1.0**: Full audio, LCD, touch, and SD card support
- **ESP-BOX-3**: ESP32-S3 development board with I2C, I2S, SPI, LEDC, and GPIO support
- **Dual Eyes Board V1.0**: Dual LCD display with touch support
- **ESP32-S3 Korvo2 V3**: Full audio and SD card support
- **ESP32-S3 Korvo2L**: Full audio and SD card support
- **ESP32 Lyrat Mini V1.1**: YAML configurations support
- **ESP32-C5 Spot**: YAML configurations support

### Peripheral Support

- **I2C**: Full support with type safety
- **I2S**: Complete audio interface support
- **SPI**: Full peripheral and device support
- **LEDC**: PWM and LED control support
- **GPIO**: GPIO support

### Device Support

- **Audio Codecs**: ES8311, ES7210, and other audio codec devices
- **SD Cards**: FATFS filesystem support
- **LCD Displays**: SPI LCD with LVGL integration
- **Touch Input**: I2C touch controller support
- **SPIFFS Filesystem**: Embedded filesystem support
- **LEDC Control**: LED brightness and PWM control
- **GPIO Control**: GPIO output control

### APP Interfaces

- **Board Manager API**: `esp_board_manager_init()`, `esp_board_manager_deinit()`, `esp_board_manager_print()`
- **Peripheral API**: `esp_board_manager_get_periph_config()`, `esp_board_manager_get_periph_handle()`
- **Device API**: `esp_board_manager_get_device_config()`, `esp_board_manager_get_device_handle()`
- **Error Handling**: Comprehensive error codes and macros (`ESP_BOARD_RETURN_ON_ERROR`)

### Documentation & Testing

- **Comprehensive Documentation**: README, API documentation, and configuration guides
- **Test Applications**: Complete test suite for all peripherals and devices
- **Configuration Rules**: Detailed YAML configuration rules and best practices
