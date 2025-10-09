# Changelog

## Unreleased

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
- Update README with dependencies usage guide

### Supported Boards
- **ESP32-S3 Korvo2 V3**: Full LCD, LCD Touch, DVP Camera support

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
