# ESP Board Manager

This is a board management module developed by Espressif that focuses solely on the initialization of development board devices. It uses YAML files to describe the configuration of peripheral interfaces on the main controller and external functional devices, and automatically generates configuration code, greatly simplifying the process of adding new boards. It provides a unified device management interface, which not only improves the reusability of device initialization code, but also greatly facilitates application adaptation to various development boards.

## Features

* **YAML Configuration**: Declarative hardware configuration using YAML files for peripherals, devices, and board definitions
* **Flexible Board Management**: Provides a unified initialization process with support for user-defined customization
* **Unified API Interfaces**: Consistent APIs for accessing peripherals and devices across different board configurations
* **Code Generation**: Automatically generates C code from YAML configurations
* **Automatic Dependency Management**: Automatically updates `idf_component.yml` files based on peripheral and device dependencies
* **Multiple Board Support**: Supports easy configuration and switching between multiple customer boards using a modular architecture
* **Extensible Architecture**: Allows easy integration of new peripherals and devices, including support for different versions of existing components
* **Comprehensive Error Management**: Provides unified error codes and robust error handling with detailed messages
* **Low Memory Footprint**: Stores only essential runtime pointers in RAM; configuration data remains read-only in flash

## Project Structure

```
esp_board_manager/
├── esp_board_manager.c        # Main board manager implementation
├── esp_board_periph.c         # Peripheral management implementation
├── esp_board_devices.c        # Device management implementation
├── esp_board_err.c            # Error handling implementation
├── include/
│   ├── esp_board_manager.h    # Main board manager API
│   ├── esp_board_periph.h     # Peripheral management
│   ├── esp_board_device.h    # Device management
│   ├── esp_board_manager_err.h        # Unified error management system
│   └── esp_board_manager_defs.h       # Key characters
├── peripherals/               # Peripheral implementations
│   ├── periph_gpio.c/py/h     # GPIO peripheral
│   ├── ...
├── devices/                   # Device implementations
│   ├── dev_audio_codec/       # Audio codec device
│   ├── ...
├── boards/                    # Default board-specific configurations
│   ├── echoear_core_board_v1_0/
│   │   ├── board_peripherals.yaml
│   │   ├── board_devices.yaml
│   │   ├── board_info.yaml
│   │   └── Kconfig
│   └── ...
├── generators/                # Code generation system
├── gen_codes/                 # Generated files (auto-created)
│   └── Kconfig.in             # Unified Kconfig menu
├── user project components/gen_bmgr_codes/ # Generated board config files (auto-created)
│   ├── gen_board_periph_config.c
│   ├── gen_board_periph_handles.c
│   ├── gen_board_device_config.c
│   ├── gen_board_device_handles.c
│   ├── gen_board_info.c
│   ├── CMakeLists.txt
│   └── idf_component.yml
├── gen_bmgr_config_codes.py   # Main code generation script (unified)
├── esp_board_manager_ext.py   # IDF action extension
├── README.md                  # This file
└── README_CN.md               # Chinese version Readme
```

## Quick Start

### 1. Setup IDF Action Extension

The ESP Board Manager now supports IDF action extension, providing seamless integration with the ESP-IDF build system. Set the `IDF_EXTRA_ACTIONS_PATH` environment variable to include the ESP Board Manager directory:

**Ubuntu and Mac:**

```bash
export IDF_EXTRA_ACTIONS_PATH=/PATH/TO/YOUR_PATH/esp_board_manager
```

**Windows PowerShell:**

```powershell
$env:IDF_EXTRA_ACTIONS_PATH = "/PATH/TO/YOUR_PATH/esp_board_manager"
```

**Windows Command Prompt (CMD):**

```cmd
set IDF_EXTRA_ACTIONS_PATH=/PATH/TO/YOUR_PATH/esp_board_manager
```

If you add this component to your project's yml dependency, you can use the following path instead:

```bash
export IDF_EXTRA_ACTIONS_PATH=YOUR_PROJECT_ROOT_PATH/managed_components/XXXX__esp_board_manager
```

Note:
> **Note:** If you use `idf.py add-dependency xxx` to add **esp_board_manager** as a dependency, the directory `YOUR_PROJECT_ROOT_PATH/managed_components/XXXX__esp_board_manager` will not be available on the first build or after cleaning the `managed_components` folder. We recommend running `idf.py set-target`, `idf.py menuconfig`, or `idf.py build` to automatically download the **esp_board_manager** component into `YOUR_PROJECT_ROOT_PATH/managed_components/XXXX__esp_board_manager`.

> **Version Requirement:** Supports ESP-IDF v5.4 and later versions

If you encounter any issues, please refer to the [## Troubleshooting](#troubleshooting) section.

### 2. Scan Boards and select board

You can discover available boards using the `-l` option:

```bash
# List all available boards
idf.py gen-bmgr-config -l

# Or use the standalone script
python YOUR_BOARD_MANAGER_PATH/gen_bmgr_config_codes.py -l
```

Then select your target board:
```bash
idf.py gen-bmgr-config -b YOUR_TARGET_BOARD
```

### 3. Use in Your Application

```c
#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_board_manager.h"
#include "esp_board_periph.h"
#include "esp_board_device.h"
#include "dev_audio_codec.h"
#include "dev_display_lcd_spi.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    // Initialize board manager
    ESP_LOGI(TAG, "Initializing board manager...");
    int ret = esp_board_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize board manager");
        return;
    }

    // Get peripheral handle
    void *spi_handle;
    ret = esp_board_manager_get_periph_handle("spi-1", &spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPI peripheral");
        return;
    }

    // Get device handle
    dev_display_lcd_spi_handles_t *lcd_handle;
    ret = esp_board_manager_get_device_handle("lcd_display", &lcd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LCD device");
        return;
    }

    // Get device configuration
    dev_audio_codec_config_t *device_config;
    ret = esp_board_manager_get_device_config("audio_dac", &device_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get device config");
        return;
    }

    // Print board information
    esp_board_manager_print_board_info();

    // Print board manager status
    esp_board_manager_print();

    // Use the handles...
}
```

### 4. More Examples

For comprehensive usage examples, refer to the test applications in the `test_apps/main/` directory:

#### Device Examples
- **[`test_dev_audio_codec.c`](test_apps/main/test_dev_audio_codec.c)** - Audio codec with DAC/ADC, SD card playback, recording, and loopback testing
- **[`test_dev_lcd_init.c`](test_apps/main/test_dev_lcd_init.c)** - LCD display initialization and basic control
- **[`test_dev_lcd_lvgl.c`](test_apps/main/test_dev_lcd_lvgl.c)** - LCD display with LVGL, touch screen, and backlight control
- **[`test_dev_pwr_ctrl.c`](test_apps/main/test_dev_pwr_ctrl.c)** - GPIO-based power management for LCD and audio devices
- **[`test_dev_fatfs_sdcard.c`](test_apps/main/test_dev_fatfs_sdcard.c)** - SD card operations and FATFS file system testing
- **[`test_dev_fs_spiffs.c`](test_apps/main/test_dev_fs_spiffs.c)** - SPIFFS file system testing
- **[`test_dev_custom.c`](test_apps/main/test_dev_custom.c)** - Custom device testing and configuration
- **[`test_dev_gpio_expander.c`](test_apps/main/test_dev_gpio_expander.c)** - GPIO expander device testing
- **[`test_dev_camera`](test_apps/main/test_dev_camera.c)** - Camera video stream capture capability testing

#### Peripheral Examples
- **[`test_periph_ledc.c`](test_apps/main/test_periph_ledc.c)** - LEDC peripheral for PWM and backlight control
- **[`test_periph_i2c.c`](test_apps/main/test_periph_i2c.c)** - I2C peripheral for device communication
- **[`test_periph_gpio.c`](test_apps/main/test_periph_gpio.c)** - GPIO peripheral for digital I/O operations

#### User Interface

Board Manager's device names are recommended for user projects, while peripheral names are not recommended. This is because device names are functionally named and do not contain additional information, making them easier to adapt to multiple boards. The device names provided by Board Manager are **reserved keywords**, see [esp_board_manager_defs.h](./include/esp_board_manager_defs.h). The following device names can be used in user applications:

| Device Name | Description |
|-------------|-------------|
| `audio_dac`, `audio_adc` | Audio codec devices |
| `display_lcd` | LCD display device |
| `fs_sdcard` | SD card device |
| `fs_spiffs` | SPIFFS filesystem device |
| `lcd_touch` | Touch screen device |
| `lcd_power` | LCD power control |
| `lcd_brightness` | LCD brightness control |
| `gpio_expander` | GPIO expander device |
| `camera_sensor` | Camera device |

## YAML Configuration Rules
For detailed YAML configuration rules and format specifications, please refer to [Device and Peripheral Rules](docs/device_and_peripheral_rules.md).

## Custom Board

### Creating a New Board

#### Create board folder and files

1. **Create Board Directory**
   ```bash
   mkdir boards/<board_name>
   cd boards/<board_name>
   ```

2. **Create Required Files**
   ```bash
   touch board_peripherals.yaml
   touch board_devices.yaml
   touch board_info.yaml
   touch Kconfig
   ```

3. **Configure Kconfig**
   ```kconfig
   config BOARD_<BOARD_NAME>
       bool "<Board Display Name>"
       depends on SOC_<CHIP_TYPE>  # optional
   ```

4. **Add Board Info**
   ```yaml
   # board_info.yaml
   board: <board_name>
   chip: <chip_type>
   version: <version>
   description: "<board description>"
   manufacturer: "<manufacturer_name>"
   ```

5. **Define Peripherals**
   ```yaml
   # board_peripherals.yaml
   board: <board_name>
   chip: <chip_type>
   version: <version>

   peripherals:
     - name: <peripheral_name>
       type: <peripheral_type>
       role: <peripheral_role>
       config:
         # Peripheral-specific configuration
   ```

6. **Define Devices**
   ```yaml
   # board_devices.yaml
   board: <board_name>
   chip: <chip_type>
   version: <version>

   devices:
     - name: <device_name>
       type: <device_type>
       init_skip: false  # Optional: skip auto-initialization (default: false)
       config:
         # Device-specific configuration
       peripherals:
         - name: <peripheral_name>
    ```

### Board Paths

The ESP Board Manager supports board configuration through three different path locations, providing flexibility for different deployment scenarios:

1. **Main Boards Directory**: Built-in boards provided with the package, path like `esp_board_manager/boards`
2. **User Project Components**: Custom boards defined in project components, path like `{PROJECT_ROOT}/components/{component_name}/boards`
3. **Custom Customer Path**: External board definitions in custom locations. User-specified path via `-c` argument in `gen_bmgr_config_codes.py` to set the custom path.

### Verify and Use New Board

1. **Verify Board Configuration**
   ```bash
    # Test if your board configuration is valid
    # For main boards & user project components (default paths)
    # Make sure IDF_EXTRA_ACTIONS_PATH is set according to the [Setup IDF Action Extension] section
    idf.py gen-bmgr-config -b <board_name>

    # For custom customer path (external locations)
    idf.py gen-bmgr-config -b <board_name> -c /path/to/custom/boards

   # Or use the standalone script
   cd  YOUR_ESP_BOARD_MANAGER_PATH
   python gen_bmgr_config_codes.py -b <board_name>
   python gen_bmgr_config_codes.py -b <board_name> -c /path/to/custom/boards
   ```

   **Check for Success**: If the board configuration is valid, the following files will be generated on project path:
   - `components/gen_bmgr_codes/gen_board_periph_handles.c` - Peripheral handle definitions
   - `components/gen_bmgr_codes/gen_board_periph_config.c` - Peripheral configuration structures
   - `components/gen_bmgr_codes/gen_board_device_handles.c` - Device handle definitions
   - `components/gen_bmgr_codes/gen_board_device_config.c` - Device configuration structures
   - `components/gen_bmgr_codes/gen_board_info.c` - Board metadata
   - `components/gen_bmgr_codes/CMakeLists.txt` - Build system configuration
   - `components/gen_bmgr_codes/idf_component.yml` - Component dependencies

   **If errors occur**: Check your YAML files for syntax errors, missing fields, or invalid configurations.

   **Note**: The script will automatically generate the Kconfig menu system when you run `idf.py gen-bmgr-config` for the first time.

## Supported Components

### Supported Peripheral types

| Peripheral | Type | Role | Status | Description | Reference YAML |
|------------|------|------|--------|-------------|----------------|
| GPIO | gpio | none | ✅ Supported | General purpose I/O | [`periph_gpio.yml`](peripherals/periph_gpio.yml) |
| I2C | i2c | master/slave | ✅ Supported | I2C communication | [`periph_i2c.yml`](peripherals/periph_i2c.yml) |
| SPI | spi | master/slave | ✅ Supported | SPI communication | [`periph_spi.yml`](peripherals/periph_spi.yml) |
| I2S | i2s | master/slave | ✅ Supported | Audio interface | [`periph_i2s.yml`](peripherals/periph_i2s.yml) |
| LEDC | ledc | none | ✅ Supported | LED control/PWM | [`periph_ledc.yml`](peripherals/periph_ledc.yml) |

### Supported Device types

| Device | Type | Chip | Peripheral | Status | Description | Reference YAML |
|--------|------|------|------------|--------|-------------|----------------|
| Audio Codec | audio_codec | ES8311/ES7210 | i2s/i2c | ✅ Supported | Audio codec with DAC/ADC | [`dev_audio_codec.yaml`](devices/dev_audio_codec/dev_audio_codec.yaml) |
| LCD Display | display_lcd_spi | ST77916/GC9A01 | spi | ✅ Supported | SPI LCD display | [`dev_display_lcd_spi.yaml`](devices/dev_display_lcd_spi/dev_display_lcd_spi.yaml) |
| Touch Screen | lcd_touch_i2c | FT5x06 | i2c | ✅ Supported | I2C touch screen | [`dev_lcd_touch_i2c.yaml`](devices/dev_lcd_touch_i2c/dev_lcd_touch_i2c.yaml) |
| SD Card | fatfs_sdcard | - | sdmmc | ✅ Supported | SD card storage | [`dev_fatfs_sdcard.yaml`](devices/dev_fatfs_sdcard/dev_fatfs_sdcard.yaml) |
| SPIFFS Filesystem | fs_spiffs | - | - | ✅ Supported | SPIFFS filesystem | [`dev_fs_spiffs.yaml`](devices/dev_fs_spiffs/dev_fs_spiffs.yaml) |
| GPIO Control | gpio_ctrl | - | gpio | ✅ Supported | GPIO control device | [`dev_gpio_ctrl.yaml`](devices/dev_gpio_ctrl/dev_gpio_ctrl.yaml) |
| LEDC Control | ledc_ctrl | - | ledc | ✅ Supported | LEDC control device | [`dev_ledc_ctrl.yaml`](devices/dev_ledc_ctrl/dev_ledc_ctrl.yaml) |
| Custom Device | custom | - | any | ✅ Supported | User-defined custom device | [`dev_custom.yaml`](devices/dev_custom/dev_custom.yaml) |
| GPIO Expander | gpio_expander | TCA9554/TCA95XX/HT8574 | i2c | ✅ Supported | GPIO expander | [`dev_gpio_expander.yaml`](devices/dev_gpio_expander/dev_gpio_expander.yaml) |
| Camera Sensor | camera | - | i2c | ✅ Supported | Camera sensor | [`dev_camera.yaml`](devices/dev_camera/dev_camera.yaml) |

### Supported Boards

| Board Name | Chip | Audio | SDCard | LCD | LCD Touch | Camera Sensor |
|------------|------|-------|--------|-----|-----------|-----------|
| Echoear Core Board V1.0 | ESP32-S3 | ✅ ES8311 + ES7210 | ✅ SDMMC | ✅ ST77916 | ✅ FT5x06 | ❌ |
| Dual Eyes Board V1.0 | ESP32-S3 | ✅ ES8311 | ❌ | ✅ GC9A01 (双) | ❌ | ❌ |
| ESP-BOX-3 | ESP32-S3 | ✅ ES8311 + ES7210 | ✅ SDMMC | ✅ ST77916 | ✅ FT5x06 | ❌ |
| ESP32-S3 Korvo2 V3 | ESP32-S3 | ✅ ES8311 + ES7210 | ✅ SDMMC | ✅ ILI9341 | ✅ TT21100 | ✅ SC030IOT |
| ESP32-S3 Korvo2L | ESP32-S3 | ✅ ES8311 | ✅ SDMMC | ❌ | ❌ | ❌ |
| Lyrat Mini V1.1 | ESP32 | ✅ ES8388 | ✅ SDMMC | ❌ | ❌ | ❌ |
| ESP32-C5 Spot | ESP32-C5 | ✅ ES8311 (双) | ❌ | ❌ | ❌ | ❌ |

## Board Manager Settings

### Automatic SDK Configuration Update

Control whether the board manager automatically updates sdkconfig based on detected device and peripheral types.

**Default**: Enabled (`y`)

**Disable via sdkconfig**:
```bash
# Use menuconfig
idf.py menuconfig
# Navigate to: Component config → ESP Board Manager Configuration → Board Manager Setting

# Set in sdkconfig
CONFIG_ESP_BOARD_MANAGER_AUTO_CONFIG_DEVICE_AND_PERIPHERAL=n
```
If you want to enable devices or peripherals type beyond what's defined in YAML, disable this option.

## Script Execution Process

The ESP Board Manager uses `gen_bmgr_config_codes.py` for code generation, which handles both Kconfig menu generation and board configuration generation in a unified workflow. This unified script provides 81% faster execution compared to the previous separate scripts.

### `gen_bmgr_config_codes.py` - Configuration Generator

Executes a comprehensive 8-step process to convert YAML configurations into C code and build system files:

1. **Board Directory Scanning**: Discovers boards across default, customer, and component directories
2. **Board Selection**: Reads board selection from sdkconfig or command line arguments
3. **Kconfig Generation**: Creates unified Kconfig menu system for board and component selection
4. **Configuration File Discovery**: Locates `board_peripherals.yaml` and `board_devices.yaml` for selected board
5. **Peripheral Processing**: Parses peripheral configurations and generates C structures
6. **Device Processing**: Processes device configurations, dependencies, and updates build files
7. **Project sdkconfig Configuration**: Updates project sdkconfig based on board device and peripheral types
8. **File Generation**: Creates all necessary C configuration and handle files in project folder `components/gen_bmgr_codes/`

#### Command Line Options

**Board Selection:**
```bash
-b, --board BOARD_NAME           # Specify board name directly (bypasses sdkconfig reading)
-c, --customer-path PATH         # Path to customer boards directory (use "NONE" to skip)
-l, --list-boards               # List all available boards and exit
```

**Generation Control:**
```bash
--kconfig-only                  # Only generate Kconfig menu system (skip board configuration generation)
--peripherals-only              # Only process peripherals (skip devices)
--devices-only                  # Only process devices (skip peripherals)
```

**The SDKconfig Configuration:**
```bash
--sdkconfig-only                # Only check sdkconfig features without enabling them
--disable-sdkconfig-auto-update # Disable automatic sdkconfig feature enabling (default is enabled)
```

**Logging Control:**
```bash
--log-level LEVEL               # Set log level: DEBUG, INFO, WARNING, ERROR (default: INFO)
```

#### Usage Examples


### Setup IDF Action Extension (Recommended)

The ESP Board Manager now supports IDF action extension, providing seamless integration with the ESP-IDF build system:

#### Installation

Set the `IDF_EXTRA_ACTIONS_PATH` environment variable to include the ESP Board Manager directory:

```bash
export IDF_EXTRA_ACTIONS_PATH="/PATH/TO/YOUR_PATH/esp_board_manager"
```

#### Usage

```bash
# Basic usage
idf.py gen-bmgr-config -b echoear_core_board_v1_0

# With custom boards
idf.py gen-bmgr-config -b my_board -c /path/to/custom/boards

# Generate Kconfig only
idf.py gen-bmgr-config --kconfig-only

# Process peripherals only
idf.py gen-bmgr-config -b echoear_core_board_v1_0 --peripherals-only

# List available boards
idf.py gen-bmgr-config -l

# Set log level to DEBUG for detailed output
idf.py gen-bmgr-config --log-level DEBUG
```

### Method 2: Standalone Script

You can also use the standalone script directly in the esp_board_manager directory.

**The following describes all parameters supported by `gen_bmgr_config_codes.py`, which also apply to `idf.py gen-bmgr-config`:**

**Basic Usage:**
```bash
# Use sdkconfig and default boards
python gen_bmgr_config_codes.py

# Specify board directly
python gen_bmgr_config_codes.py -b echoear_core_board_v1_0

# Add customer boards directory
python gen_bmgr_config_codes.py -c /path/to/custom/boards

# Both board and custom path
python gen_bmgr_config_codes.py -b my_board -c /path/to/custom/boards

# List available boards
python gen_bmgr_config_codes.py -l

# Disable automatic sdkconfig updates
python gen_bmgr_config_codes.py --disable-sdkconfig-auto-update

# Set log level to DEBUG for detailed output
python gen_bmgr_config_codes.py --log-level DEBUG
```

**Partial Generation:**
```bash
# Only process peripherals
python gen_bmgr_config_codes.py --peripherals-only

# Only process devices
python gen_bmgr_config_codes.py --devices-only

# Check sdkconfig features without enabling
python gen_bmgr_config_codes.py --sdkconfig-only
```

#### Generated Files

**Configuration Files:**
- `gen_codes/Kconfig.in` - Unified Kconfig menu for board selection
- `components/gen_bmgr_codes/gen_board_periph_config.c` - Peripheral configurations
- `components/gen_bmgr_codes/gen_board_periph_handles.c` - Peripheral handles
- `components/gen_bmgr_codes/gen_board_device_config.c` - Device configurations
- `components/gen_bmgr_codes/gen_board_device_handles.c` - Device handles
- `components/gen_bmgr_codes/gen_board_info.c` - Board metadata
- `components/gen_bmgr_codes/CMakeLists.txt` - Build system configuration
- `components/gen_bmgr_codes/idf_component.yml` - Component dependencies

**Note:** Generated files are organized as follows:
- Kconfig files are placed in the `gen_codes/` directory
- Board configuration files are placed in `components/gen_bmgr_codes/` directory to integrate with the ESP-IDF build system
- These directories are automatically created by the generation scripts if they don't exist and should not be manually modified

## Roadmap

Future development plans for the ESP Board Manager:
- **Build Workflow**: Improve the efficiency and streamline the build workflow
- **Version Management**: Support different version codes and parsers for devices and peripherals
- **Plugin Architecture**: Extensible plugin system for custom device and peripheral support
- **Enhanced Validation**: Comprehensive YAML format checking, schema validation, input validation, and enhanced rule validation with improved error reporting
- **Enhanced Initialization Flow**: Support for skipping specific components or calling initialization on different cores
- **Clear Definition Rules**: Establish clear rules to facilitate customer addition of peripherals and devices
- **Extended Component Support**: Add more peripherals, devices, and boards

## Troubleshooting

### No `esp_board_manager` path
1. Check the `esp_board_manager` dependency in your project's main `idf_component.yml`
2. Run `idf.py menuconfig` or `idf.py build` after adding the `esp_board_manager` dependency. These commands will download the `esp_board_manager` to `YOUR_PROJECT_ROOT_PATH/managed_components/`

### `idf.py gen-bmgr-config` Command Not Found

If `idf.py gen-bmgr-config` is not recognized:

1. Check that `IDF_EXTRA_ACTIONS_PATH` is set correctly
2. Restart your terminal session

### `undefined reference for g_board_devices and g_board_peripherals`
1. Make sure there is no `idf_build_set_property(MINIMAL_BUILD ON)` in your project, because MINIMAL_BUILD only performs a minimal build by including only the "common" components required by all other components.
2. Ensure your project has a `components/gen_bmgr_codes` folder with generated files. These files are generated by running `idf.py gen-bmgr-config -b YOUR_BOARD`.

### **Switching Boards**
Using `idf.py gen-bmgr-config -b` is strongly recommended. Using `idf.py menuconfig` to select boards may result in dependency errors.

## License

This project is licensed under the Modified MIT License - see the [LICENSE](./LICENSE) file for details.
