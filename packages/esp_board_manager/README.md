# ESP Board Manager

[中文版](README_CN.md)

This is a board management component developed by Espressif that focuses on the initialization of development board devices. It uses YAML files to describe the configuration of the main controller and external functional devices, automatically generates configuration code, and simplifies the process of adding new boards. It provides a unified device management interface, which not only improves the reusability of device initialization code but also simplifies the adaptation of applications to various development boards.

> **Version Requirements:** Compatible with ESP-IDF release/v5.4(>= v5.4.3) and release/v5.5(>= v5.5.2) branches.

## Features

* **YAML Configuration**: Configure peripherals and devices using YAML files
* **Code Generation**: Automatically generate C code from YAML configurations
* **Flexible Board Management**: Provides a unified initialization process and supports modular board customization
* **Unified API Interface**: Use consistent APIs to access peripherals and devices across different board configurations
* **Automatic Dependency Management**: Automatically update `idf_component.yml` files based on peripheral and device dependencies
* **Extensible Architecture**: Allows easy integration of new peripherals and devices, including support for different versions of existing components
* **Comprehensive Error Management**: Provides unified error codes and robust error handling with detailed messages
* **Low Memory Footprint**: Stores only necessary runtime pointers in RAM; configuration data remains read-only in flash

## Project Structure

```
esp_board_manager/
├── src/             # Source files
├── include/         # Public header files
├── private_inc/     # Private header files
├── peripherals/     # Peripheral implementations (periph_gpio, periph_i2c, etc.)
├── devices/         # Device implementations (dev_audio_codec, dev_display_lcd, etc.)
├── boards/          # Board-specific configurations (YAML files, Kconfig, setup_device.c)
├── generators/      # Code generation system
├── gen_codes/                  # Generated files (auto-created)
│   └── Kconfig.in              # Unified Kconfig menu
├── CMakeLists.txt              # Component build configuration
├── idf_component.yml           # Component manifest
├── gen_bmgr_config_codes.py    # Main code generation script
├── idf_ext.py                  # IDF action extension
├── README.md                   # This file
├── README_CN.md                # Chinese version
├── user project components/gen_bmgr_codes/ # Generated board configuration files (auto-created)
│   ├── gen_board_periph_config.c
│   ├── gen_board_periph_handles.c
│   ├── gen_board_device_config.c
│   ├── gen_board_device_handles.c
│   ├── gen_board_info.c
│   ├── CMakeLists.txt
│   └── idf_component.yml
```

## Quick Start

### 1. Add and Activate Component

#### 1.1 Automatically Download ESP Board Manager Component from Component Registry

- Directly use `idf.py add-dependency esp_board_manager` to add **esp_board_manager** as a dependency component.

- Or manually add the following content to your `idf_component.yml`:

```yaml
espressif/esp_board_manager:
  version: "*"
  require: public
```

Run `idf.py set-target` or `idf.py menuconfig` to automatically download the **esp_board_manager** component to `YOUR_PROJECT_ROOT_PATH/managed_components/espressif__esp_board_manager`.

> **Note:** Please check the directory `YOUR_PROJECT_ROOT_PATH/managed_components/espressif__esp_board_manager` to ensure the component has been downloaded locally before proceeding.

Set the `IDF_EXTRA_ACTIONS_PATH` environment variable to include the ESP Board Manager directory:

**Ubuntu and Mac:**

```bash
export IDF_EXTRA_ACTIONS_PATH=YOUR_PROJECT_ROOT_PATH/managed_components/espressif__esp_board_manager
```

**Windows PowerShell:**

```powershell
$env:IDF_EXTRA_ACTIONS_PATH = "YOUR_PROJECT_ROOT_PATH/managed_components/espressif__esp_board_manager"
```

**Windows Command Prompt (CMD):**

```cmd
set IDF_EXTRA_ACTIONS_PATH=YOUR_PROJECT_ROOT_PATH/managed_components/espressif__esp_board_manager
```

#### 1.2 Use Local ESP Board Manager Component

Add the following content to your `idf_component.yml`:

```yaml
espressif/esp_board_manager:
  override_path: /PATH/TO/YOUR_PATH/esp_board_manager
  version: "*"
  require: public
```

Set the `IDF_EXTRA_ACTIONS_PATH` environment variable to include the ESP Board Manager directory:

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

> **Note:** IDF action extension auto-discovery is available starting from ESP-IDF v6.0. From IDF v6.0 onwards, there is no need to set `IDF_EXTRA_ACTIONS_PATH` because it automatically discovers the IDF action extension.

### 2. Scan Boards and Select Board

ESP Board Manager supports IDF action extension, providing seamless integration with the ESP-IDF build system. This feature allows you to directly use the `idf.py gen-bmgr-config` command without manually running Python scripts.

You can use the `-l` option to verify that the component path configuration is correct and print available boards:

```bash
# List all available boards
idf.py gen-bmgr-config -l
```

Then select your target board by name or index:

```bash
idf.py gen-bmgr-config -b YOUR_TARGET_BOARD
```

For example:

```bash
idf.py gen-bmgr-config -b echoear_core_board_v1_2  # Board name
idf.py gen-bmgr-config -b 3                        # Board index
```

If you need to switch to another board, you can execute the following commands:

> **Note:** For users who downloaded the component from the repository, please first ensure that the component has not been deleted. If the `YOUR_PROJECT_ROOT_PATH/managed_components/espressif__esp_board_manager` directory no longer exists, please first execute `idf.py set-target` or `idf.py menuconfig` to re-download the component.

```bash
idf.py gen-bmgr-config -x  # Clear current board configuration
idf.py gen-bmgr-config -b OTHER_BOARD
```

> **Note:** For more usage, see [Command Line Options](#command-line-options)

At this point, the board configuration files will be automatically generated to the path `YOUR_PROJECT_ROOT_PATH/components/gen_bmgr_codes`. After this step, the files required for initializing the development board have been generated, and you can proceed to test in your application.

**Generated Configuration Files:**

- `components/gen_bmgr_codes/gen_board_periph_config.c` - Peripheral configuration
- `components/gen_bmgr_codes/gen_board_periph_handles.c` - Peripheral handles
- `components/gen_bmgr_codes/gen_board_device_config.c` - Device configuration
- `components/gen_bmgr_codes/gen_board_device_handles.c` - Device handles
- `components/gen_bmgr_codes/gen_board_info.c` - Board metadata
- `components/gen_bmgr_codes/CMakeLists.txt` - Build system configuration
- `components/gen_bmgr_codes/idf_component.yml` - Component dependencies

> **Note:** If you encounter problems, refer to the [Troubleshooting](#troubleshooting) section.

### 3. Use Prebuild Script

It is recommended that users read the above steps to understand the usage of `esp_board_manager`. For users wish to simplify the usage process, prebuild script for building the project with `esp_board_manager` is provided in [`tools`](tools).

The first time you compile the project, copy script from [`tools`](tools) to `YOUR_PROJECT_ROOT_PATH`. Execute script，the script will first check if the ESP-IDF version is supported. Then, it will list the available chips, and user select the target chip by entering the corresponding number. After downloading the required components, the script will scan the component paths and automatically set the IDF_EXTRA_ACTIONS_PATH environment variable to include the ESP Board Manager directory. The script will then list all available boards, user select the target board by entering thr corresponding number or board name.

On Linux / macOS, run following command:
```bash/zsh
source prebuild.sh
```

On Windows, run following command:
```powershell
.\prebuild.ps1
```

For later board changes, you only need to clear the current board configuration and reselect the board.

> **Note:** The prebuild script takes over steps related to [Add and Activate Component](#1-add-and-activate-component) and [Scan Boards and Select Board](#2-scan-boards-and-select-board).

### 4. Use in Your Application

```c
#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_board_manager_includes.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    // Initialize board manager, which will automatically initialize all peripherals and devices
    ESP_LOGI(TAG, "Initializing board manager...");
    int ret = esp_board_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize board manager");
        return;
    }
    // Get device handle, according to the device naming in esp_board_manager/boards/YOUR_TARGET_BOARD/board_devices.yaml
    dev_display_lcd_spi_handles_t *lcd_handle;
    ret = esp_board_manager_get_device_handle("display_lcd", &lcd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LCD device");
        return;
    }
    // Get device configuration, according to the device naming in esp_board_manager/boards/YOUR_TARGET_BOARD/board_devices.yaml
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
    // Use handles...
}
```

> **Note:** Simple examples using `esp_board_manager` to initialize devices and obtain handles for use are provided in the [`example`](example) path for reference.

## Supported Components

### Supported Boards

| Board Name | Chip | Audio | SD Card | LCD | LCD Touch | Camera | Button |
|---|---|---|---|---|---|---|---|
| [`Echoear Core Board V1.0`](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/echoear/user_guide_v1.0.html) | ESP32-S3 | ✅ ES8311 + ES7210 | ✅ SDMMC | ✅ ST77916 | ✅ CTS816S | - | - |
| [`Echoear Core Board V1.2`](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/echoear/user_guide_v1.2.html) | ESP32-S3 | ✅ ES8311 + ES7210 | ✅ SDMMC | ✅ ST77916 | ✅ CTS816S | - | - |
| Dual Eyes Board V1.0 | ESP32-S3 | ✅ ES8311 | ❌ | ✅ GC9A01 (dual) | - | - | - |
| [`ESP-BOX-3`](https://github.com/espressif/esp-box/blob/master/docs/hardware_overview/esp32_s3_box_3/hardware_overview_for_box_3.md) | ESP32-S3 | ✅ ES8311 + ES7210 | ✅ SDMMC | ✅ ST77916 | ✅ GT911 | - | - |
| [`ESP32-S3 Korvo2 V3`](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2.html) | ESP32-S3 | ✅ ES8311 + ES7210 | ✅ SDMMC | ✅ ILI9341 | ✅ TT21100 | ✅ DVP Camera | ✅ ADC button |
| ESP32-S3 Korvo2L | ESP32-S3 | ✅ ES8311 | ✅ SDMMC | ❌ | ❌ | ❌ | ❌ |
| [`Lyrat Mini V1.1`](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/get-started-esp32-lyrat-mini.html) | ESP32 | ✅ ES8388 | ✅ SDMMC | - | - | - | ✅ ADC button |
| [`ESP32-C5 Spot`](https://oshwhub.com/esp-college/esp-spot) | ESP32-C5 | ✅ ES8311 (dual) | - | - | - | - | - |
| [`ESP32-P4 Function-EV`](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html) | ESP32-P4 | ✅ ES8311 | ✅ SDMMC | ✅ EK79007 | ✅ GT911 | ✅ CSI Camera | - |
| [`M5STACK CORES3`](https://docs.m5stack.com/en/core/CoreS3) | ESP32-S3 | ✅ AW88298 + ES7210 | ✅ SDSPI | ✅ ILI9342C | ✅ FT5x06 | ❌ | - |


Note: '✅' indicates supported, '❌' indicates not yet supported, '-' indicates the hardware does not have the corresponding capability.

### Supported Device Types

| Device Name | Description | Type | Subtype | Peripheral | Reference YAML | Examples |
|---|---|---|---|---|---|---|
| `audio_dac`<br/>`audio_adc` | Audio codec | audio_codec | - | i2s<br/>i2c | [`dev_audio_codec`](devices/dev_audio_codec/dev_audio_codec.yaml) | **[`test_dev_audio_codec.c`](test_apps/main/test_dev_audio_codec.c)** <br/>Audio codec with DAC/ADC, SD card playback, recording, and loopback testing |
| `display_lcd` | SPI LCD (deprecated) | display_lcd_spi | - | spi | [`dev_display_lcd_spi`](devices/dev_display_lcd_spi/dev_display_lcd_spi.yaml) | **[`test_dev_lcd_init.c`](test_apps/main/test_dev_lcd_init.c)** <br/>LCD display initialization and basic control |
| `display_lcd` | LCD | display_lcd | spi<br/>dsi | spi<br/>dsi | [`dev_display_lcd`](devices/dev_display_lcd/dev_display_lcd.yaml) | **[`test_dev_lcd_lvgl.c`](test_apps/main/test_dev_lcd_lvgl.c)** <br/>LCD display with LVGL, touch screen, and backlight control |
| `fs_sdcard` | SDMMC SD card (deprecated) | fatfs_sdcard | - | sdmmc | [`dev_fatfs_sdcard`](devices/dev_fatfs_sdcard/dev_fatfs_sdcard.yaml) | **[`test_dev_fatfs_sdcard.c`](test_apps/main/test_dev_fatfs_sdcard.c)** <br/>SD card operations and FATFS file system testing |
| `fs_sdcard` | SPI SD card (deprecated) | fatfs_sdcard_spi | - | spi | [`dev_fatfs_sdcard_spi`](devices/dev_fatfs_sdcard_spi/dev_fatfs_sdcard_spi.yaml) | **[`test_dev_fatfs_sdcard.c`](test_apps/main/test_dev_fatfs_sdcard.c)** <br/>SD card operations and FATFS file system testing |
| `fs_fat` | FAT filesystem device | fs_fat | sdmmc<br/>spi | sdmmc<br/>spi | [`dev_fs_fat`](devices/dev_fs_fat/dev_fs_fat.yaml) | **[`test_dev_fatfs_sdcard.c`](test_apps/main/test_dev_fatfs_sdcard.c)** <br/>SD card operations and FATFS file system testing |
| `fs_spiffs` | SPIFFS filesystem device | fs_spiffs | - | - | [`dev_fs_spiffs`](devices/dev_fs_spiffs/dev_fs_spiffs.yaml) | **[`test_dev_fs_spiffs.c`](test_apps/main/test_dev_fs_spiffs.c)** <br/>SPIFFS file system testing |
| `lcd_touch` | Touch screen | lcd_touch_i2c | - | i2c | [`dev_lcd_touch_i2c`](devices/dev_lcd_touch_i2c/dev_lcd_touch_i2c.yaml) | **[`test_dev_lcd_lvgl.c`](test_apps/main/test_dev_lcd_lvgl.c)** <br/>LCD display with LVGL, touch screen, and backlight control |
| `sdcard_power_ctrl` | Power control device | power_ctrl | gpio | gpio | [`dev_power_ctrl`](devices/dev_power_ctrl/dev_power_ctrl.yaml) | - |
| `lcd_brightness` | LEDC control device | ledc_ctrl | - | ledc | [`dev_ledc_ctrl`](devices/dev_ledc_ctrl/dev_ledc_ctrl.yaml) | **[`test_dev_ledc.c`](test_apps/main/test_dev_ledc.c)** <br/>LEDC device for PWM and backlight control |
| `gpio_expander` | GPIO expander chip | gpio_expander | - | i2c | [`dev_gpio_expander`](devices/dev_gpio_expander/dev_gpio_expander.yaml) | **[`test_dev_gpio_expander.c`](test_apps/main/test_dev_gpio_expander.c)**<br/>GPIO expander chip testing |
| `camera` | Camera | camera | dvp<br/>csi | i2c | [`dev_camera`](devices/dev_camera/dev_camera.yaml) | **[`test_dev_camera.c`](test_apps/main/test_dev_camera.c)** <br/>Testing Camera sensor's video stream capture capability |
| `button` | Button | button | gpio<br/>adc | gpio<br/>adc | [`dev_button`](devices/dev_button/dev_button.yaml) | **[`test_dev_button.c`](test_apps/main/test_dev_button.c)** <br/>Button testing |

> For the same device, we will no longer distinguish types by interface. For example, `dev_fatfs_sdcard` and `dev_fatfs_sdcard_spi` will be unified under `fs_fat` for management, and `dev_display_lcd_spi` will also be changed to use `dev_display_lcd` for management.
> These three device types will be deprecated in future versions. Users can refer to [`dev_fatfs_sdcard.yaml`](./devices/dev_fatfs_sdcard/dev_fatfs_sdcard.yaml), [`dev_fatfs_sdcard_spi.yaml`](./devices/dev_fatfs_sdcard_spi/dev_fatfs_sdcard_spi.yaml), and [`dev_display_lcd_spi.yaml`](./devices/dev_display_lcd_spi/dev_display_lcd_spi.yaml) to learn how to migrate their original configurations to the new device types.

### Supported Peripheral Types

| Peripheral Name | Description | Type | Role | Reference YAML | Examples |
|---|---|---|---|---|---|
| `i2c_master` | I2C communication | i2c | master<br/>slave | [`periph_i2c`](peripherals/periph_i2c/periph_i2c.yml) | **[`test_periph_i2c.c`](test_apps/main/periph/test_periph_i2c.c)**<br/>I2C peripheral for device communication |
| `spi_master`<br/>`spi_display`<br/>... | SPI communication | spi | master<br/>slave | [`periph_spi`](peripherals/periph_spi/periph_spi.yml) | - |
| `i2s_audio_out`<br/>`i2s_audio_in` | Audio interface | i2s | master<br/>slave | [`periph_i2s`](peripherals/periph_i2s/periph_i2s.yml) | - |
| `gpio_pa_control`<br/>`gpio_backlight_control`<br/>... | General I/O | gpio | none | [`periph_gpio`](peripherals/periph_gpio/periph_gpio.yml) | **[`test_periph_gpio.c`](test_apps/main/periph/test_periph_gpio.c)**<br/>GPIO peripheral for digital I/O operations |
| `ledc_backlight` | LEDC control/PWM | ledc | none | [`periph_ledc`](peripherals/periph_ledc/periph_ledc.yml) | - |
| `uart_1` | UART communication | uart | tx<br/>rx | [`periph_uart`](peripherals/periph_uart/periph_uart.yml) | **[`test_periph_uart.c`](test_apps/main/periph/test_periph_uart.c)**<br/>UART peripheral for serial port operations |
| `adc_unit_1` | ADC analog-to-digital conversion | adc | oneshot<br/>continuous | [`periph_adc`](peripherals/periph_adc/periph_adc.yml) | **[`test_periph_adc.c`](test_apps/main/periph/test_periph_adc.c)**<br/>ADC peripheral for measuring analog signals on specific analog IO pins |
| `rmt_tx`, `rmt_rx` | Infrared remote control | rmt | tx<br/>rx | [`periph_rmt`](peripherals/periph_rmt/periph_rmt.yml) | **[`test_periph_rmt.c`](test_apps/main/periph/test_periph_rmt.c)**<br/>Using RMT peripherals to control the WS2812 LED strip |
| `pcnt_unit` | Pulse counter | pcnt | none | [`periph_pcnt`](peripherals/periph_pcnt/periph_pcnt.yml) | **[`test_periph_pcnt.c`](test_apps/main/periph/test_periph_anacmpr.c)**<br/>Use the PCNT peripheral to decode the differential signals |
| `anacmpr_unit_0` | Analog comparator | anacmpr | none | [`periph_anacmpr`](peripherals/periph_anacmpr/periph_anacmpr.yml) | **[`test_periph_anacmpr.c`](test_apps/main/periph/test_periph_anacmpr.c)**<br/>Analog comparator peripheral for comparing source signals with reference signals |
| `dac_channel_0` | Digital-to-analog converter | dac | oneshot<br/>continuous<br/>cosine | [`periph_dac`](peripherals/periph_dac/periph_dac.yml) | **[`test_periph_dac.c`](test_apps/main/periph/test_periph_dac.c)**<br/>DAC peripheral for converting digital values to analog voltage |
| `mcpwm_group_0` | PWM generator | mcpwm | none | [`periph_mcpwm`](peripherals/periph_mcpwm/periph_mcpwm.yml) | **[`test_periph_mcpwm.c`](test_apps/main/periph/test_periph_mcpwm.c)**<br/>Multi-function PWM generator peripheral |
| `sdm` | Sigma Delta modulator | sdm | none | [`periph_sdm`](peripherals/periph_sdm/periph_sdm.yml) | **[`test_periph_sdm.c`](test_apps/main/periph/test_periph_sdm.c)**<br/>SDM peripheral for pulse density modulation |
| `ldo_mipi` | LDO low-dropout linear regulator | ldo | none | [`periph_ldo`](peripherals/periph_ldo/periph_ldo.yml) | - |
| `dsi_display` | MIPI-DSI | dsi | none | [`periph_dsi`](peripherals/periph_dsi/periph_dsi.yml) | - |

> For commonly used device and peripheral names, we provide corresponding macro definitions that can be used directly. Please refer to [esp_board_manager_defs.h](include/esp_board_manager_defs.h).

## Command Line Options

**Board Selection:**
```bash
-b, --board BOARD_NAME           # Directly specify board name (bypass sdkconfig reading)
-b, --board BOARD_INDEX          # Specify board by index
-c, --customer-path PATH         # Customer board directory path (use "NONE" to skip)
-l, --list-boards                # List all available boards and exit
```

**Generation Control:**
```bash
--kconfig-only                   # Only generate Kconfig menu system (skip board configuration generation)
--peripherals-only               # Only process peripherals (skip devices)
--devices-only                   # Only process devices (skip peripherals)
```

**SDKconfig Configuration:**
```bash
--sdkconfig-only                 # Only check sdkconfig features without enabling them
--disable-sdkconfig-auto-update  # Disable automatic sdkconfig feature enabling (enabled by default)
```

**Log Control:**
```bash
--log-level LEVEL                # Set log level: DEBUG, INFO, WARNING, ERROR (default: INFO)
```

### Method 1: Using as IDF Action Extension (Recommended)

Use the command `idf.py gen-bmgr-config` followed by command line options, for example:

```bash
# List available boards
idf.py gen-bmgr-config -l

# Specify board (name or index)
idf.py gen-bmgr-config -b echoear_core_board_v1_0
idf.py gen-bmgr-config -b 1

# Use custom board
idf.py gen-bmgr-config -b my_board -c /path/to/custom/boards

# Clean generated files
idf.py gen-bmgr-config -x

...
```

### Method 2: Using Standalone Script

You can also use the standalone script directly in the esp_board_manager directory, for example:

```bash
# List available boards
python gen_bmgr_config_codes.py -l

# Specify board with -b option (name or index)
python gen_bmgr_config_codes.py -b echoear_core_board_v1_0
python gen_bmgr_config_codes.py -b 1

# Use custom board
python gen_bmgr_config_codes.py 1 -c /custom/boards
python gen_bmgr_config_codes.py -b my_board -c /path/to/custom/boards

# Clean generated files
python gen_bmgr_config_codes.py -x
```

Additional usage when using the standalone script directly:

```bash
# Read board selection from sdkconfig (if exists)
python gen_bmgr_config_codes.py

# Specify board as direct parameter (name or index)
# Direct parameter (without `-b`) only works when calling the script directly, not with `idf.py` due to ESP-IDF framework limitations.
python gen_bmgr_config_codes.py esp32_s3_korvo2_v3
python gen_bmgr_config_codes.py 1
```

## Script Execution Flow

ESP Board Manager uses `gen_bmgr_config_codes.py` for code generation, which handles both Kconfig menu generation and board configuration generation in a unified workflow. The execution follows a comprehensive 8‑step process that transforms YAML configurations into C code and build system files:

1. **Board Directory Scanning**: Discover boards in default, customer, and component directories
2. **Board Selection**: Read board selection from sdkconfig or command‑line arguments
3. **Kconfig Generation**: Create a unified Kconfig menu system for board and component selection
4. **Configuration File Discovery**: Locate `board_peripherals.yaml` and `board_devices.yaml` for the selected board
5. **Peripheral Processing**: Parse peripheral configurations and generate C structures
6. **Device Processing**: Process device configurations, dependencies, and update build files
7. **Project sdkconfig Configuration**: Update project sdkconfig based on board device and peripheral types
8. **File Generation**: Create all necessary C configuration and handle files in the project folder's `components/gen_bmgr_codes/`

**⚠️ Important**: When switching boards, the script automatically backs up and deletes the existing `sdkconfig` file in step 1 to prevent configuration pollution (skipped when using `--kconfig-only`).

## Custom Board

`esp_board_manager` supports modular customization of your own development board. For specific usage methods, please refer to: [How to Create a Custom Board](docs/how_to_customize_board.md)

## Roadmap

Future development plans for ESP Board Manager (prioritized from high to low):

- **Support More Peripherals and Devices**: Add more peripherals, devices, and boards
- **Web Visual Configuration**: Combine with large models to achieve visual and intelligent board configuration through web interface
- **Documentation Enhancement**: Add more documentation, such as establishing clear rules to facilitate customer addition of peripherals and devices
- **Enhanced Validation**: Comprehensive YAML format checking, schema validation, input validation, and enhanced rule validation
- **Enhanced Data Structure**: Enhance data or YAML structure to improve performance
- **Version Management**: Support different version codes and parsers for devices and peripherals
- **Plugin Architecture**: Extensible plugin system for custom device and peripheral support

## Troubleshooting

### Cannot Find `esp_board_manager` Path

1. Check the `esp_board_manager` dependency in your project's main `idf_component.yml`
2. After adding the `esp_board_manager` dependency, run `idf.py menuconfig` or `idf.py build`. These commands will download `esp_board_manager` to `YOUR_PROJECT_ROOT_PATH/managed_components/`

### `idf.py gen-bmgr-config` Command Not Found

If `idf.py gen-bmgr-config` is not recognized:

1. Check that `IDF_EXTRA_ACTIONS_PATH` is set correctly
2. Restart your terminal session

### `undefined reference for g_board_devices and g_board_peripherals`

1. Make sure there is no `idf_build_set_property(MINIMAL_BUILD ON)` in your project, because MINIMAL_BUILD only performs a minimal build by including only the "common" components required by all other components.
2. Ensure your project has a `components/gen_bmgr_codes` folder with generated files. These files are generated by running `idf.py gen-bmgr-config -b YOUR_BOARD`.

### Switching Development Boards

**Important:** When switching boards, the script automatically:

1. Backs up `sdkconfig` to `sdkconfig.bmgr_board.backup` and removes the original to prevent configuration pollution
2. Appends board-specific configurations from `boards/<board_name>/sdkconfig.defaults.board` to your project's `sdkconfig.defaults`

Always use `idf.py gen-bmgr-config -b` (or `python gen_bmgr_config_codes.py`) for board switching. Using `idf.py menuconfig` may cause dependency errors.

### Dependency Issues with Some Components

If you encounter the following errors when running `idf.py set-target xxx`, `idf.py menuconfig`, or `idf.py reconfigure`:

```bash
ERROR: Because project depends on xxxxx which
doesn't match any versions, version solving failed.
```

Or similar errors:

```bash
Failed to resolve component 'esp_board_manager' required by component
  'gen_bmgr_codes': unknown name.
```

This may be caused by leftover generated files from the board manager that were not cleared. **You can clean the generated files using `idf.py gen-bmgr-config -x` (or `python gen_bmgr_config_codes.py -x`)** to remove all generated .c and .h files and reset CMakeLists.txt and idf_component.yml.

### Undefined reference to 'g_esp_board_devices'

The `undefined reference to 'g_esp_board_device_handles'` or `undefined reference to 'g_esp_board_devices'` error occurs because `idf.py gen-bmgr-config -b YOUR_BOARD` was not run.

## License

This project is licensed under the Modified MIT License - see the [LICENSE](./LICENSE) file for details.
