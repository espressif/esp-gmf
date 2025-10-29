# Changelog

## v0.4.1

### Features
- Renamed esp_board_manager_ext.py to idf_ext.py for compatibility with ESP-IDF v6.0 auto-discovery
- Update `Kconfig.in` with new configuration options to hide the board selection menu
- Update the README to include information about how to add new boards and the development roadmap
- Added the esp_board_periph_ref_handle and esp_board_periph_unref_handle APIs to obtain and release a handle
- Added esp_board_find_utils.h and esp_board_manager_includes.h

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
