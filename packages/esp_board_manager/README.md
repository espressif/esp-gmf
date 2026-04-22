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
├── boards/          # Board-specific configurations (YAML files, setup_device.c)
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
│   ├── gen_board_metadata.yaml
│   ├── board_manager.defaults
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   ├── gen_board_device_custom.h    # Optional, only when custom device structs are generated
│   └── Kconfig.projbuild            # Generated for the currently selected board
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

ESP Board Manager supports IDF action extension, providing seamless integration with the ESP-IDF build system. Use **`idf.py bmgr`** with the same options as before; **`idf.py gen-bmgr-config`** remains available as a legacy alias. Do not combine `bmgr` and `gen-bmgr-config` in one `idf.py` line, and always pass the board with **`-b`** (board names are not accepted as a bare extra argument after the action).

You can use the `-l` option to verify that the component path configuration is correct and print available boards:

```bash
# List all available boards
idf.py bmgr -l
```

Then select your target board by name or index:

```bash
idf.py bmgr -b YOUR_TARGET_BOARD
```

For example:

```bash
idf.py bmgr -b esp_vocat_board_v1_2  # Board name
idf.py bmgr -b 3                     # Board index
```

If you need to switch to another board, you can execute the following commands:

> **Note:** For users who downloaded the component from the repository, please first ensure that the component has not been deleted. If the `YOUR_PROJECT_ROOT_PATH/managed_components/espressif__esp_board_manager` directory no longer exists, please first execute `idf.py set-target` or `idf.py menuconfig` to re-download the component.

```bash
idf.py bmgr -x  # Clear current board configuration
idf.py bmgr -b OTHER_BOARD
```

> **Note:** For more usage, see [Command Line Options](#command-line-options). Equivalent legacy commands use `idf.py gen-bmgr-config` instead of `idf.py bmgr`

At this point, the board configuration files will be automatically generated to the path `YOUR_PROJECT_ROOT_PATH/components/gen_bmgr_codes`. After this step, the files required for initializing the development board have been generated, and you can proceed to test in your application.

> **Note:** Ensure `components/gen_bmgr_codes` is up to date (run `idf.py bmgr -b <board>` or `idf.py gen-bmgr-config -b <board>`) before building; CMake or compile may fail otherwise. If you already have `sdkconfig` and it disagrees with `board_manager.defaults`, the extension prints **warnings** only—you can skip them with `ESP_BOARD_MANAGER_SKIP_SDKCONFIG_CHECK=1`.

**Generated Configuration Files:**

- `components/gen_bmgr_codes/gen_board_periph_config.c` - Peripheral configuration
- `components/gen_bmgr_codes/gen_board_periph_handles.c` - Peripheral handles
- `components/gen_bmgr_codes/gen_board_device_config.c` - Device configuration
- `components/gen_bmgr_codes/gen_board_device_handles.c` - Device handles
- `components/gen_bmgr_codes/gen_board_info.c` - Board metadata
- `components/gen_bmgr_codes/gen_board_metadata.yaml` - Unified board metadata for tooling, including devices, peripherals, dependencies, and occupied IO
- `components/gen_bmgr_codes/board_manager.defaults` - Board-specific sdkconfig defaults and selected board symbols
- `components/gen_bmgr_codes/CMakeLists.txt` - Build system configuration
- `components/gen_bmgr_codes/idf_component.yml` - Component dependencies
- `components/gen_bmgr_codes/gen_board_device_custom.h` - Optional custom device struct definitions
- `components/gen_bmgr_codes/Kconfig.projbuild` - Current-board Kconfig definitions

**Generated Metadata YAML**

`components/gen_bmgr_codes/gen_board_metadata.yaml` is generated together with the C sources and provides a unified machine-readable board summary for tooling and inspection.

Typical structure:

```yaml
version: 1
board: esp32_s3_korvo2_v3
chip: esp32s3

devices:
  display_lcd:
    type: display_lcd
    sub_type: spi
    peripherals:
    - spi_display
    dependencies:
      espressif/esp_lcd_ili9341: '*'
    io:
      dc_gpio_num: 10

peripherals:
  i2c_master:
    type: i2c
    role: master
    io:
      sda_io_num: 17
      scl_io_num: 18
```

Metadata rules:

- `devices.*.dependencies` mirrors the board YAML `dependencies` field.
- `devices.*.peripherals` lists dependent peripheral names only; peripheral IO is not expanded into the device entry.
- `devices.*.io` only contains IO configured directly by that device.
- `peripherals.*.io` only contains IO owned by that peripheral.
- Empty root fields such as `sub_type`, `role`, `format`, `dependencies`, `peripherals`, and `io` are omitted from the YAML output.

> **Note:** If you encounter problems, refer to the [Troubleshooting](#troubleshooting) section.

### 3. Use Prebuild Script

It is recommended that users read the above steps to understand the usage of `esp_board_manager`. For users wish to simplify the usage process, prebuild script for building the project with `esp_board_manager` is provided in [`tools`](tools).

The first time you compile the project, copy the scripts from [`tools`](tools) to `YOUR_PROJECT_ROOT_PATH`. After you run a script, it first checks whether the ESP-IDF version is supported, then lists the selectable chips so you can pick a target by number. After dependencies are downloaded, it scans component paths and sets `IDF_EXTRA_ACTIONS_PATH` so it includes the ESP Board Manager directory, then lists all boards so you can pick one by number or name.

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
    dev_display_lcd_handles_t *lcd_handle;
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
| [`ESP VoCat Board V1.0`](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp-vocat/user_guide_v1.0.html) | ESP32-S3 | ✅ ES8311 + ES7210 | ✅ SDMMC | ✅ ST77916 | ✅ CTS816S | - | - |
| [`ESP VoCat Board V1.2`](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp-vocat/user_guide_v1.2.html) | ESP32-S3 | ✅ ES8311 + ES7210 | ✅ SDMMC | ✅ ST77916 | ✅ CTS816S | - | - |
| Dual Eyes Board V1.0 | ESP32-S3 | ✅ ES8311 | ❌ | ✅ GC9A01 (dual) | - | - | - |
| [`ESP-BOX-3`](https://github.com/espressif/esp-box/blob/master/docs/hardware_overview/esp32_s3_box_3/hardware_overview_for_box_3.md) | ESP32-S3 | ✅ ES8311 + ES7210 | ✅ SDMMC | ✅ ST77916 | ✅ GT911 | - | - |
| [`ESP32-S3 Korvo2 V3`](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2.html) | ESP32-S3 | ✅ ES8311 + ES7210 | ✅ SDMMC | ✅ ILI9341 | ✅ TT21100 | ✅ DVP Camera | ✅ ADC button |
| ESP32-S3 Korvo2L | ESP32-S3 | ✅ ES8311 | ✅ SDMMC | ❌ | ❌ | ❌ | ❌ |
| [`Lyrat Mini V1.1`](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/get-started-esp32-lyrat-mini.html) | ESP32 | ✅ ES8388 | ✅ SDMMC | - | - | - | ✅ ADC button |
| [`ESP32-C5 Spot`](https://oshwhub.com/esp-college/esp-spot) | ESP32-C5 | ✅ ES8311 (dual) | - | - | - | - | - |
| [`ESP32-P4 Function-EV`](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html) | ESP32-P4 | ✅ ES8311 | ✅ SDMMC | ✅ EK79007 | ✅ GT911 | ✅ CSI Camera | - |
| [`M5STACK CORES3`](https://docs.m5stack.com/en/core/CoreS3) | ESP32-S3 | ✅ AW88298 + ES7210 | ✅ SDSPI | ✅ ILI9342C | ✅ FT5x06 | ❌ | - |
| [`M5STACK TAB5`](https://docs.m5stack.com/en/core/Tab5) | ESP32-P4 | ✅ ES8388 + ES7210 | ✅ SDMMC | ✅ ILI9881C | ✅ GT911 | SC202CS | - |
| [`ESP-BOX-LITE`](https://github.com/espressif/esp-box/blob/master/docs/hardware_overview/esp32_s3_box_lite/hardware_overview_for_lite.md) | ESP32-S3 | ✅ ES8156 + ES7243E | - | ✅ ST7789 | - | - | - |

Note: '✅' indicates supported, '❌' indicates not yet supported, '-' indicates the hardware does not have the corresponding capability.

### Supported Device Types

| Device Name | Description | Type | Subtype | Peripheral | Reference YAML | Examples |
|---|---|---|---|---|---|---|
| `audio_dac`<br/>`audio_adc` | Audio codec | audio_codec | - | i2s<br/>i2c | [`dev_audio_codec`](devices/dev_audio_codec/dev_audio_codec.yaml) | **[`test_dev_audio_codec.c`](test_apps/main/test_dev_audio_codec.c)** <br/>Audio codec with DAC/ADC, SD card playback, recording, and loopback testing |
| `display_lcd` | LCD | display_lcd | spi<br/>dsi | spi<br/>dsi | [`dev_display_lcd`](devices/dev_display_lcd/dev_display_lcd.yaml) | **[`test_dev_lcd_lvgl.c`](test_apps/main/test_dev_lcd_lvgl.c)** <br/>LCD display with LVGL, touch screen, and backlight control |
| `fs_fat` | FAT filesystem device | fs_fat | sdmmc<br/>spi | sdmmc<br/>spi | [`dev_fs_fat`](devices/dev_fs_fat/dev_fs_fat.yaml) | **[`test_dev_fs_fat.c`](test_apps/main/test_dev_fs_fat.c)** <br/>SD card operations and FATFS file system testing |
| `fs_spiffs` | SPIFFS filesystem device | fs_spiffs | - | - | [`dev_fs_spiffs`](devices/dev_fs_spiffs/dev_fs_spiffs.yaml) | **[`test_dev_fs_spiffs.c`](test_apps/main/test_dev_fs_spiffs.c)** <br/>SPIFFS file system testing |
| `lcd_touch` | Touch screen | lcd_touch_i2c | - | i2c | [`dev_lcd_touch_i2c`](devices/dev_lcd_touch_i2c/dev_lcd_touch_i2c.yaml) | **[`test_dev_lcd_lvgl.c`](test_apps/main/test_dev_lcd_lvgl.c)** <br/>LCD display with LVGL, touch screen, and backlight control |
| `sdcard_power_ctrl` | Power control device | power_ctrl | gpio | gpio | [`dev_power_ctrl`](devices/dev_power_ctrl/dev_power_ctrl.yaml) | - |
| `lcd_brightness` | LEDC control device | ledc_ctrl | - | ledc | [`dev_ledc_ctrl`](devices/dev_ledc_ctrl/dev_ledc_ctrl.yaml) | **[`test_dev_ledc.c`](test_apps/main/test_dev_ledc.c)** <br/>LEDC device for PWM and backlight control |
| `gpio_expander` | GPIO expander chip | gpio_expander | - | i2c | [`dev_gpio_expander`](devices/dev_gpio_expander/dev_gpio_expander.yaml) | **[`test_dev_gpio_expander.c`](test_apps/main/test_dev_gpio_expander.c)**<br/>GPIO expander chip testing |
| `camera` | Camera | camera | dvp<br/>csi | i2c<br/>ldo | [`dev_camera`](devices/dev_camera/dev_camera.yaml) | **[`test_dev_camera.c`](test_apps/main/test_dev_camera.c)** <br/>Testing Camera sensor's video stream capture capability |
| `button` | Button | button | gpio<br/>adc | gpio<br/>adc | [`dev_button`](devices/dev_button/dev_button.yaml) | **[`test_dev_button.c`](test_apps/main/test_dev_button.c)** <br/>Button testing |

> For the same device, we will no longer distinguish types by interface. For example, `dev_fatfs_sdcard` and `dev_fatfs_sdcard_spi` will be unified under `fs_fat` for management, and `dev_display_lcd_spi` will also be changed to use `dev_display_lcd` for management.

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

**Board Selection and Discovery:**
```bash
-b, --board BOARD                # Board name or index
-c, --customer-path PATH         # Custom boards directory (supports one or more paths)
-l, --list-boards                # List all available boards and exit
-n, --new-board ARG              # Create a new board (idf.py action only)
[BOARD]                          # Specify board name or index directly (standalone script only)
```

**Generation Modes:**
```bash
--peripherals-only               # Generate peripheral outputs only; skip device generation
--devices-only                   # Generate device outputs only; peripherals are still loaded for reference
--kconfig-only                   # Generate Kconfig files only; skip board code generation and sdkconfig cleanup
```

**SDKconfig Behavior:**
```bash
--skip-sdkconfig-check           # Skip sdkconfig consistency check
-x, --clean                      # Delete generated .c/.h files, reset generated CMakeLists.txt/idf_component.yml, and remove board_manager.defaults
```

**Log Control:**
```bash
--log-level LEVEL                # DEBUG, INFO, WARNING, ERROR (default: INFO)
```

### Method 1: Using as IDF Action Extension (Recommended)

Use **`idf.py bmgr`** (preferred) or **`idf.py gen-bmgr-config`** (legacy) with the same options, for example:

```bash
# List available boards
idf.py bmgr -l

# Specify board (name or index)
idf.py bmgr -b esp_vocat_board_v1_0
idf.py bmgr -b 1

# Use custom board
idf.py bmgr -b my_board -c /path/to/custom/boards

# Create a new board in the default components directory
idf.py bmgr -n my_new_board

# Create a new board in a specified path
idf.py bmgr -n path/to/boards/my_new_board

# Generate Kconfig files only
idf.py bmgr -b esp_vocat_board_v1_0 --kconfig-only

# Skip sdkconfig consistency check when reusing current sdkconfig
idf.py bmgr -b esp_vocat_board_v1_0 --skip-sdkconfig-check

# Clean generated files
idf.py bmgr -x
```

### Method 2: Using Standalone Script

You can also use the standalone script directly in the esp_board_manager directory, for example:

```bash
# List available boards
python gen_bmgr_config_codes.py -l

# Specify board with -b option (name or index)
python gen_bmgr_config_codes.py -b esp_vocat_board_v1_0
python gen_bmgr_config_codes.py -b 1

# Use custom board
python gen_bmgr_config_codes.py 1 -c /custom/boards
python gen_bmgr_config_codes.py -b my_board -c /path/to/custom/boards

# Generate peripherals only
python gen_bmgr_config_codes.py -b esp_vocat_board_v1_0 --peripherals-only

# Generate devices only
python gen_bmgr_config_codes.py -b esp_vocat_board_v1_0 --devices-only

# Generate Kconfig files only
python gen_bmgr_config_codes.py -b esp_vocat_board_v1_0 --kconfig-only

# Clean generated files
python gen_bmgr_config_codes.py --clean
```

Additional usage when using the standalone script directly:

```bash
# Read board selection from sdkconfig (if exists)
python gen_bmgr_config_codes.py

# Specify board as direct parameter (name or index)
# Using board naming or indexing parameters directly only works when the script is called directly
python gen_bmgr_config_codes.py esp32_s3_korvo2_v3
python gen_bmgr_config_codes.py 1
```

## Script Execution Flow

ESP Board Manager uses `gen_bmgr_config_codes.py` for code generation, which handles both Kconfig generation and board configuration generation in a unified workflow. The execution follows an 8-step process that transforms YAML configurations into source files and build metadata:

1. **Board Directory Scanning**: Discover boards in default, customer, and component directories
2. **Board Selection**: Read board selection from sdkconfig or command‑line arguments
3. **Configuration File Discovery**: Locate `board_peripherals.yaml` and `board_devices.yaml` for the selected board
4. **Peripheral Processing**: Parse peripheral configurations and generate peripheral C structures/handles
5. **Device Processing**: Parse device configurations, resolve dependencies, and generate device C structures/handles
6. **Kconfig Generation**: Generate static capability symbols in `gen_codes/Kconfig.in` and current-board symbols in `components/gen_bmgr_codes/Kconfig.projbuild`
7. **Board SDKconfig Handling**: When needed, validate preserved sdkconfig consistency and generate `components/gen_bmgr_codes/board_manager.defaults`
8. **Generated Component Setup**: Write board information, unified board metadata, and generated component files under `components/gen_bmgr_codes/`

**⚠️ Important**: When switching boards, the script automatically backs up and deletes the existing `sdkconfig` file during environment cleanup. This prevents residual configurations from the old board (for example, a different chip's `CONFIG_IDF_TARGET` or stale board symbols) from affecting the new board. The backup file is `components/gen_bmgr_codes/sdkconfig.bmgr_board.old`.

## Custom Board

`esp_board_manager` supports modular customization of your own development board. For specific usage methods, please refer to: [How to Create a Custom Board](docs/how_to_customize_board.md)

## Roadmap

Future development plans for ESP Board Manager (prioritized from high to low):

- **Support More Peripherals and Devices**: Add more peripherals, devices, and boards
- **Web Visual Configuration**: Combine with large models to achieve visual and intelligent board configuration through web interface
- **Documentation Enhancement**: Add more documentation, such as establishing clear rules to facilitate customer addition of peripherals and devices
- **Enhanced Validation**: Comprehensive YAML format checking, schema validation, input validation, and enhanced rule validation
- **Enhanced Data Structure**: Enhance data or YAML structure to improve performance
- **Version management**: Board-level and device/peripheral-level version management
- **Plugin Architecture**: Extensible plugin system for custom device and peripheral support

## Troubleshooting

### Cannot Find `esp_board_manager` Path

1. Check the `esp_board_manager` dependency in your project's main `idf_component.yml`
2. After adding the `esp_board_manager` dependency, run `idf.py menuconfig` or `idf.py build`. These commands will download `esp_board_manager` to `YOUR_PROJECT_ROOT_PATH/managed_components/`

### `idf.py bmgr` or `idf.py gen-bmgr-config` Command Not Found

If neither action is recognized:

1. Check that `IDF_EXTRA_ACTIONS_PATH` is set correctly (ESP-IDF before v6.0)
2. Restart your terminal session
3. On ESP-IDF v6.0+, ensure `esp_board_manager` is visible to the project so `idf_ext.py` is discovered

### CMake or compile errors (missing `gen_bmgr_codes`)

If the build fails because generated files under `components/gen_bmgr_codes` are missing or stale, run `idf.py bmgr -b YOUR_BOARD` (or `idf.py gen-bmgr-config -b YOUR_BOARD`). The IDF extension does not print a dedicated warning before CMake for missing files.

### `undefined reference to 'g_esp_board_*'` (e.g. `g_esp_board_devices`, `g_esp_board_device_handles`, `g_esp_board_peripherals`)

1. Ensure `idf.py bmgr -b YOUR_BOARD` (legacy: `idf.py gen-bmgr-config -b YOUR_BOARD`) **completed successfully** and `components/gen_bmgr_codes` is **complete**: besides generated `.c` / `.h` files, you must have **`CMakeLists.txt`** (and the usual `idf_component.yml`, `board_manager.defaults`, etc.). If the folder only has partial `.c` files or no `CMakeLists.txt`, ESP-IDF will not register it as a component, those sources are not compiled, and you still get the same undefined references at link time.
2. Check whether the project is using a minimal or trimmed build configuration, such as `idf_build_set_property(MINIMAL_BUILD ON)` or `set(COMPONENTS main)`. The former keeps only the minimal set of common components, while the latter builds only the explicitly listed components and their dependencies. In either case, if `gen_bmgr_codes` is not explicitly included in the build scope, the generated board code may be excluded from compilation, which can lead to undefined references such as `g_esp_board_*` at link time.

### Switching Development Boards

**Important:** When switching boards, the script automatically:

1. Backs up `sdkconfig` to `components/gen_bmgr_codes/sdkconfig.bmgr_board.old` and removes the original to prevent residual configurations from the old board (e.g., different chip's CONFIG_IDF_TARGET, different board's device settings) from affecting the new board
2. Generates `board_manager.defaults` file with board-specific configurations from `boards/<board_name>/sdkconfig.defaults.board`
3. The configurations will be automatically applied via `SDKCONFIG_DEFAULTS` environment variable during build/menuconfig/reconfigure

Always use `idf.py bmgr -b` or `idf.py gen-bmgr-config -b` (or `python gen_bmgr_config_codes.py`) for board switching. Using `idf.py menuconfig` may cause dependency errors.

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

This may be caused by leftover generated files from the board manager that were not cleared. **You can clean the generated files using `idf.py bmgr -x` or `idf.py gen-bmgr-config -x` (or `python gen_bmgr_config_codes.py -x`)** to delete generated `.c/.h` files, reset generated `CMakeLists.txt` / `idf_component.yml`, and remove `board_manager.defaults`.

## License

This project is licensed under the Modified MIT License - see the [LICENSE](./LICENSE) file for details.
