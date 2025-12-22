# ESP Board Manager

[English](README.md)

这是由 Espressif 开发的专注于开发板设备初始化的板级管理组件。使用 YAML 文件来描述主控制器和外部功能设备的配置，能够自动生成配置代码，简化了添加新板子的过程。提供了统一的设备管理接口，不仅提高了设备初始化代码的可重用性，还简化了应用程序对各种开发板的适配。

> **版本要求:** 兼容 ESP-IDF release/v5.4(>= v5.4.3) 和 release/v5.5(>= v5.5.2) 分支。

## 功能特性

* **YAML 配置**: 使用 YAML 文件对外设和设备进行配置
* **代码生成**: 从 YAML 配置自动生成 C 代码
* **灵活的板级管理**: 提供统一的初始化流程，并支持模块化板级定制
* **统一的 API 接口**: 在不同板子配置下使用一致的 API 访问外设和设备
* **自动依赖管理**: 根据外设和设备依赖关系自动更新 `idf_component.yml` 文件
* **可扩展架构**: 允许轻松集成新的外设和设备，包括对现有组件不同版本的支持
* **全面的错误管理**: 提供统一的错误代码和强大的错误处理，包含详细消息
* **低内存占用**: 在 RAM 中仅存储必要的运行时指针；配置数据保持为 flash 中的只读数据

## 项目结构

```
esp_board_manager/
├── src/             # 源文件
├── include/         # 公共头文件
├── private_inc/     # 私有头文件
├── peripherals/     # 外设实现（periph_gpio、periph_i2c 等）
├── devices/         # 设备实现（dev_audio_codec、dev_display_lcd 等）
├── boards/          # 板级特定配置（YAML 文件、Kconfig、setup_device.c）
├── generators/      # 代码生成系统
├── gen_codes/                  # 生成的文件（自动创建）
│   └── Kconfig.in              # 统一 Kconfig 菜单
├── CMakeLists.txt              # 组件构建配置
├── idf_component.yml           # 组件清单
├── gen_bmgr_config_codes.py    # 主代码生成脚本
├── idf_ext.py                  # IDF 动作扩展
├── README.md                   # 本文件
├── README_CN.md                # 中文版本
├── user project components/gen_bmgr_codes/ # 生成的板子配置文件（自动创建）
│   ├── gen_board_periph_config.c
│   ├── gen_board_periph_handles.c
│   ├── gen_board_device_config.c
│   ├── gen_board_device_handles.c
│   ├── gen_board_info.c
│   ├── CMakeLists.txt
│   └── idf_component.yml
```

## 快速开始

### 1. 添加并激活组件

#### 1.1 从组件仓库自动下载 ESP Board Manager 组件

- 直接使用 `idf.py add-dependency esp_board_manager` 将 **esp_board_manager** 添加为依赖组件

- 或是手动将以下内容添加到你的 `idf_component.yml` 文件:

```yaml
espressif/esp_board_manager:
  version: "*"
  require: public
```

运行 `idf.py set-target` 或 `idf.py menuconfig` 来自动将 **esp_board_manager** 组件下载到 `YOUR_PROJECT_ROOT_PATH/managed_components/espressif__esp_board_manager`。

> **注意:** 请查看目录 `YOUR_PROJECT_ROOT_PATH/managed_components/espressif__esp_board_manager` 确保组件已经被下载到本地后再进行下一步操作。

设置 `IDF_EXTRA_ACTIONS_PATH` 环境变量以包含 ESP Board Manager 目录：

**Ubuntu and Mac:**

```bash
export IDF_EXTRA_ACTIONS_PATH=YOUR_PROJECT_ROOT_PATH/managed_components/espressif__esp_board_manager
```

**Windows PowerShell:**

```powershell
$env:IDF_EXTRA_ACTIONS_PATH = "YOUR_PROJECT_ROOT_PATH/managed_components/espressif__esp_board_manager"
```

**Windows 命令提示符 (CMD):**

```cmd
set IDF_EXTRA_ACTIONS_PATH=YOUR_PROJECT_ROOT_PATH/managed_components/espressif__esp_board_manager
```

#### 1.2 使用本地的 ESP Board Manager 组件

将以下内容添加到你的 idf_component.yml 文件:

```yaml
espressif/esp_board_manager:
  override_path: /PATH/TO/YOUR_PATH/esp_board_manager
  version: "*"
  require: public
```

设置 `IDF_EXTRA_ACTIONS_PATH` 环境变量以包含 ESP Board Manager 目录：

**Ubuntu and Mac:**

```bash
export IDF_EXTRA_ACTIONS_PATH=/PATH/TO/YOUR_PATH/esp_board_manager
```

**Windows PowerShell:**

```powershell
$env:IDF_EXTRA_ACTIONS_PATH = "/PATH/TO/YOUR_PATH/esp_board_manager"
```

**Windows 命令提示符 (CMD):**

```cmd
set IDF_EXTRA_ACTIONS_PATH=/PATH/TO/YOUR_PATH/esp_board_manager
```

> **注意:** IDF action 扩展自动发现功能从 ESP-IDF v6.0 开始可用。从 IDF v6.0 开始无需设置 `IDF_EXTRA_ACTIONS_PATH`，因为它会自动发现 IDF action 扩展。

### 2. 扫描并选择板子

ESP Board Manager 支持 IDF action 扩展，提供与 ESP-IDF 构建系统的无缝集成。此功能允许您直接使用 `idf.py gen-bmgr-config` 命令，而无需手动运行 Python 脚本。

您可以使用 `-l` 选项验证组件路径配置是否正确，并打印可用的板子：

```bash
# 列出所有可用板子
idf.py gen-bmgr-config -l
```

然后通过名称或索引选择您的目标板子：

```bash
idf.py gen-bmgr-config -b YOUR_TARGET_BOARD
```

例如：

```bash
idf.py gen-bmgr-config -b echoear_core_board_v1_2  # 板子名称
idf.py gen-bmgr-config -b 3                        # 板子索引
```

如果需要切换其他的板子，可以执行以下命令，

> **注意:** 对于从仓库下载组件的用户，请先确保组件没有被删除，如果 `YOUR_PROJECT_ROOT_PATH/managed_components/espressif__esp_board_manager` 目录不存在了，请先执行`idf.py set-target` 或 `idf.py menuconfig` 来重新下载组件。

```bash
idf.py gen-bmgr-config -x  # 清除当前板子配置
idf.py gen-bmgr-config -b OTHER_BOARD
```

> **注意:** 更多用法可以查看 [命令行选项](#命令行选项)

此时板子配置文件会自动生成到 `YOUR_PROJECT_ROOT_PATH/components/gen_bmgr_codes` 路径，执行到这一步，初始化开发板所需的文件就已经生成完毕，接下来可以在您的应用程序中进行测试。

**生成的配置文件:**

- `components/gen_bmgr_codes/gen_board_periph_config.c` - 外设配置
- `components/gen_bmgr_codes/gen_board_periph_handles.c` - 外设句柄
- `components/gen_bmgr_codes/gen_board_device_config.c` - 设备配置
- `components/gen_bmgr_codes/gen_board_device_handles.c` - 设备句柄
- `components/gen_bmgr_codes/gen_board_info.c` - 板子元数据
- `components/gen_bmgr_codes/CMakeLists.txt` - 构建系统配置
- `components/gen_bmgr_codes/idf_component.yml` - 组件依赖关系

> **注意:** 遇到问题可以查看 [故障排除](#故障排除) 部分

### 3. 使用预编译脚本

建议用户阅读上述步骤了解 `esp_board_manager` 的使用方法。对于希望简化使用过程的用户，在 [`tools`](tools) 路径下提供了使用 `esp_board_manager` 编译工程的预编译脚本。

第一次编译工程时，将 [`tools`](tools) 下的脚本拷贝到 `YOUR_PROJECT_ROOT_PATH`。执行脚本，脚本会首先检查 ESP-IDF 版本是否支持，然后列出可选择的芯片，用户输入序号选择目标芯片后，会将依赖的组件进行下载。下载组件后，脚本会扫描组件的路径，自动设置 `IDF_EXTRA_ACTIONS_PATH` 环境变量以包含 ESP Board Manager 目录。然后脚本会列出所有可选板子，用户输入序号或名称选择板子。

在 Linux / macOS 中运行以下命令：
```bash/zsh
source prebuild.sh
```

在 Windows 中运行以下命令：
```powershell
.\prebuild.ps1
```

后续更换板子时，仅需清除当前板子配置并重新选择板子。

> **注意:** 预编译脚本接管了上述的[添加并激活组件](#1-添加并激活组件)和[扫描并选择板子](#2-扫描并选择板子)中的步骤。

### 4. 在您的应用程序中使用

```c
#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_board_manager_includes.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    // 初始化板级管理器，这将自动初始化所有外设和设备
    ESP_LOGI(TAG, "Initializing board manager...");
    int ret = esp_board_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize board manager");
        return;
    }
    // 获取设备句柄，根据 esp_board_manager/boards/YOUR_TARGET_BOARD/board_devices.yaml 中的设备命名获取句柄
    dev_display_lcd_spi_handles_t *lcd_handle;
    ret = esp_board_manager_get_device_handle("display_lcd", &lcd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LCD device");
        return;
    }
    // 获取设备配置，根据 esp_board_manager/boards/YOUR_TARGET_BOARD/board_devices.yaml 中的设备命名获取设备配置
    dev_audio_codec_config_t *device_config;
    ret = esp_board_manager_get_device_config("audio_dac", &device_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get device config");
        return;
    }
    // 打印板子信息
    esp_board_manager_print_board_info();
    // 打印板级管理器状态
    esp_board_manager_print();
    // 使用句柄...
}
```

> **注意:** 在 [`example`](example) 路径下提供了使用 `esp_board_manager` 初始化设备并获取句柄进行使用的简单样例可供参考。

## 支持的组件

### 支持的板级

| 板子名称 | 芯片 | 音频 | SD卡 | LCD | LCD 触摸 | 摄像头 | 按键 |
|---|---|---|---|---|---|---|---|
| [`Echoear Core Board V1.0`](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s3/echoear/user_guide_v1.0.html) | ESP32-S3 | ✅ ES8311 + ES7210 | ✅ SDMMC | ✅ ST77916 | ✅ CTS816S | - | - |
| [`Echoear Core Board V1.2`](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s3/echoear/user_guide_v1.2.html) | ESP32-S3 | ✅ ES8311 + ES7210 | ✅ SDMMC | ✅ ST77916 | ✅ CTS816S | - | - |
| Dual Eyes Board V1.0 | ESP32-S3 | ✅ ES8311 | ❌ | ✅ GC9A01 (双) | - | - | - |
| [`ESP-BOX-3`](https://github.com/espressif/esp-box/blob/master/docs/hardware_overview/esp32_s3_box_3/hardware_overview_for_box_3_cn.md) | ESP32-S3 | ✅ ES8311 + ES7210 | ✅ SDMMC | ✅ ST77916 | ✅ GT911 | - | - |
| [`ESP32-S3 Korvo2 V3`](https://docs.espressif.com/projects/esp-adf/zh_CN/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2.html) | ESP32-S3 | ✅ ES8311 + ES7210 | ✅ SDMMC | ✅ ILI9341 | ✅ TT21100 | ✅ DVP Camera | ✅ ADC button |
| ESP32-S3 Korvo2L | ESP32-S3 | ✅ ES8311 | ✅ SDMMC | ❌ | ❌ | ❌ | ❌ |
| [`Lyrat Mini V1.1`](https://docs.espressif.com/projects/esp-adf/zh_CN/latest/design-guide/dev-boards/get-started-esp32-lyrat-mini.html) | ESP32 | ✅ ES8388 | ✅ SDMMC | - | - | - | ✅ ADC button |
| [`ESP32-C5 Spot`](https://oshwhub.com/esp-college/esp-spot) | ESP32-C5 | ✅ ES8311 (双) | - | - | - | - | - |
| [`ESP32-P4 Function-EV`](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html) | ESP32-P4 | ✅ ES8311 | ✅ SDMMC | ✅ EK79007 | ✅ GT911 | ✅ CSI Camera | - |
| [`M5STACK CORES3`](https://docs.m5stack.com/zh_CN/core/CoreS3) | ESP32-S3 | ✅ AW88298 + ES7210 | ✅ SDSPI | ✅ ILI9342C | ✅ FT5x06 | ❌ | - |

注：'✅' 表示已经支持，'❌' 表示尚未支持，'-' 表示硬件不具备相应的能力

### 支持的设备类型

| 设备名称 | 描述 | 类型 | 子类型 | 外设 | 参考 YAML | 示例 |
|---|---|---|---|---|---|---|
| `audio_dac`<br/>`audio_adc` | 音频编解码器 | audio_codec | - | i2s<br/>i2c | [`dev_audio_codec.yaml`](devices/dev_audio_codec/dev_audio_codec.yaml) | **[`test_dev_audio_codec.c`](test_apps/main/test_dev_audio_codec.c)** <br/>带有 DAC/ADC、SD 卡播放、录音和回环测试的音频编解码器 |
| `display_lcd` | SPI LCD 显示屏(将被弃用) | display_lcd_spi | - | spi | [`dev_display_lcd_spi.yaml`](devices/dev_display_lcd_spi/dev_display_lcd_spi.yaml) | **[`test_dev_lcd_init.c`](test_apps/main/test_dev_lcd_init.c)** <br/>LCD 显示初始化和基本控制 |
| `display_lcd` | LCD 显示设备 | display_lcd | spi<br/>dsi | spi<br/>dsi | [`dev_display_lcd.yaml`](devices/dev_display_lcd/dev_display_lcd.yaml) | **[`test_dev_lcd_lvgl.c`](test_apps/main/test_dev_lcd_lvgl.c)** <br/>带有 LVGL、触摸屏和背光控制的 LCD 显示屏 |
| `fs_sdcard` | SDMMC SD 卡(将被弃用) | fatfs_sdcard | - | sdmmc | [`dev_fatfs_sdcard.yaml`](devices/dev_fatfs_sdcard/dev_fatfs_sdcard.yaml) | **[`test_dev_fatfs_sdcard.c`](test_apps/main/test_dev_fatfs_sdcard.c)** <br/>SD 卡操作和 FATFS 文件系统测试 |
| `fs_sdcard` | SPI SD 卡(将被弃用) | fatfs_sdcard_spi | - | spi | [`dev_fatfs_sdcard_spi.yaml`](devices/dev_fatfs_sdcard_spi/dev_fatfs_sdcard_spi.yaml) | **[`test_dev_fatfs_sdcard.c`](test_apps/main/test_dev_fatfs_sdcard.c)** <br/>SD 卡操作和 FATFS 文件系统测试 |
| `fs_fat` | FAT 文件系统设备 | fs_fat | sdmmc<br/>spi | sdmmc<br/>spi | [`dev_fs_fat.yaml`](devices/dev_fs_fat/dev_fs_fat.yaml) | **[`test_dev_fatfs_sdcard.c`](test_apps/main/test_dev_fatfs_sdcard.c)** <br/>SD 卡操作和 FATFS 文件系统测试 |
| `fs_spiffs` | SPIFFS 文件系统设备 | fs_spiffs | - | - | [`dev_fs_spiffs.yaml`](devices/dev_fs_spiffs/dev_fs_spiffs.yaml) | **[`test_dev_fs_spiffs.c`](test_apps/main/test_dev_fs_spiffs.c)** <br/>SPIFFS 文件系统测试 |
| `lcd_touch` | 触摸屏 | lcd_touch_i2c | - | i2c | [`dev_lcd_touch_i2c.yaml`](devices/dev_lcd_touch_i2c/dev_lcd_touch_i2c.yaml) | **[`test_dev_lcd_lvgl.c`](test_apps/main/test_dev_lcd_lvgl.c)** <br/>带有 LVGL、触摸屏和背光控制的 LCD 显示屏 |
| `sdcard_power_ctrl` | 电源控制设备 | power_ctrl | gpio | gpio | [`dev_power_ctrl.yaml`](devices/dev_power_ctrl/dev_power_ctrl.yaml) | - |
| `lcd_brightness` | LEDC 控制设备 | ledc_ctrl | - | ledc | [`dev_ledc_ctrl.yaml`](devices/dev_ledc_ctrl/dev_ledc_ctrl.yaml) | **[`test_dev_ledc.c`](test_apps/main/test_dev_ledc.c)** <br/>用于 PWM 和背光控制的 LEDC 设备 |
| `gpio_expander` | GPIO 扩展芯片 | gpio_expander | - | i2c | [`dev_gpio_expander.yaml`](devices/dev_gpio_expander/dev_gpio_expander.yaml) | **[`test_dev_gpio_expander.c`](test_apps/main/test_dev_gpio_expander.c)**<br/>GPIO 扩展芯片测试 |
| `camera` | 摄像头 | camera | dvp<br/>csi | i2c | [`dev_camera.yaml`](devices/dev_camera/dev_camera.yaml) | **[`test_dev_camera.c`](test_apps/main/test_dev_camera.c)** <br/>测试 Camera sensor 的视频流捕获能力 |
| `button` | 按键 | button | gpio<br/>adc | gpio<br/>adc | [`dev_button.yaml`](devices/dev_button/dev_button.yaml) | **[`test_dev_button.c`](test_apps/main/test_dev_button.c)** <br/>按钮测试 |

> 对于同一种设备，我们将不再使用接口类型来区分类型。例如，`dev_fatfs_sdcard` 和 `dev_fatfs_sdcard_spi` 将统一使用 `fs_fat` 进行管理，`dev_display_lcd_spi` 也将改为使用 `dev_display_lcd` 进行管理。
> 这三种设备类型将在未来版本中被弃用。用户可以参照 [`dev_fatfs_sdcard.yaml`](./devices/dev_fatfs_sdcard/dev_fatfs_sdcard.yaml)、[`dev_fatfs_sdcard_spi.yaml`](./devices/dev_fatfs_sdcard_spi/dev_fatfs_sdcard_spi.yaml) 和 [`dev_display_lcd_spi.yaml`](./devices/dev_display_lcd_spi/dev_display_lcd_spi.yaml) 文件，了解如何将原有配置迁移到新的设备类型。

### 支持的外设类型

| 外设名称 | 描述 | 类型 | 角色 | 参考 YAML | 示例 |
|---|---|---|---|---|---|
| `i2c_master` | I2C 通信 | i2c | master<br/>slave | [`periph_i2c.yml`](peripherals/periph_i2c/periph_i2c.yml) | **[`test_periph_i2c.c`](test_apps/main/periph/test_periph_i2c.c)**<br/>用于设备通信的 I2C 外设 |
| `spi_master`<br/>`spi_display`<br/>... | SPI 通信 | spi | master<br/>slave | [`periph_spi.yml`](peripherals/periph_spi/periph_spi.yml) | - |
| `i2s_audio_out`<br/>`i2s_audio_in` | 音频接口 | i2s | master<br/>slave | [`periph_i2s.yml`](peripherals/periph_i2s/periph_i2s.yml) | - |
| `gpio_pa_control`<br/>`gpio_backlight_control`<br/>... | 通用 I/O | gpio | none | [`periph_gpio.yml`](peripherals/periph_gpio/periph_gpio.yml) | **[`test_periph_gpio.c`](test_apps/main/periph/test_periph_gpio.c)**<br/>用于数字 I/O 操作的 GPIO 外设 |
| `ledc_backlight` | LEDC 控制/PWM | ledc | none | [`periph_ledc.yml`](peripherals/periph_ledc/periph_ledc.yml) | - |
| `uart_1` | UART 通信 | uart | tx<br/>rx | [`periph_uart.yml`](peripherals/periph_uart/periph_uart.yml) | **[`test_periph_uart.c`](test_apps/main/periph/test_periph_uart.c)**<br/>用于串行端口操作的 UART 外设 |
| `adc_unit_1` | ADC 模数转换 | adc | oneshot<br/>continuous | [`periph_adc.yml`](peripherals/periph_adc/periph_adc.yml) | **[`test_periph_adc.c`](test_apps/main/periph/test_periph_adc.c)**<br/>用于测量特定模拟 IO 管脚模拟信号的 ADC 外设 |
| `rmt_tx`, `rmt_rx` | 红外遥控 | rmt | tx<br/>rx | [`periph_rmt.yml`](peripherals/periph_rmt/periph_rmt.yml) | **[`test_periph_rmt.c`](test_apps/main/periph/test_periph_rmt.c)**<br/>使用 RMT 外设控制 WS2812 LED 灯带 |
| `pcnt_unit` | 脉冲计数器 | pcnt | none | [`periph_pcnt.yml`](peripherals/periph_pcnt/periph_pcnt.yml) | **[`test_periph_pcnt.c`](test_apps/main/periph/test_periph_anacmpr.c)**<br/>使用 PCNT 外设解码差分信号 |
| `anacmpr_unit_0` | 模拟比较器 | anacmpr | none | [`periph_anacmpr.yml`](peripherals/periph_anacmpr/periph_anacmpr.yml) | **[`test_periph_anacmpr.c`](test_apps/main/periph/test_periph_anacmpr.c)**<br/>用于比较源信号与参考信号的模拟比较器外设 |
| `dac_channel_0` | 数模转换器 | dac | oneshot<br/>continuous<br/>cosine | [`periph_dac.yml`](peripherals/periph_dac/periph_dac.yml) | **[`test_periph_dac.c`](test_apps/main/periph/test_periph_dac.c)**<br/>用于将数字值转换成模拟电压的 DAC 外设 |
| `mcpwm_group_0` | PWM 生成器 | mcpwm | none | [`periph_mcpwm.yml`](peripherals/periph_mcpwm/periph_mcpwm.yml) | **[`test_periph_mcpwm.c`](test_apps/main/periph/test_periph_mcpwm.c)**<br/>多功能 PWM 生成器外设 |
| `sdm` | Sigma Delta 调制器 | sdm | none | [`periph_sdm.yml`](peripherals/periph_sdm/periph_sdm.yml) | **[`test_periph_sdm.c`](test_apps/main/periph/test_periph_sdm.c)**<br/>用于脉冲密度调制的 SDM 外设 |
| `ldo_mipi` | LDO 低压差线性稳压器 | ldo | none | [`periph_ldo.yml`](peripherals/periph_ldo/periph_ldo.yml) | - |
| `dsi_display` | MIPI-DSI | dsi | none | [`periph_dsi.yml`](peripherals/periph_dsi/periph_dsi.yml) | - |

> 对于常用的设备和外设名称，我们提供了相应的宏定义可以直接使用，请参考 [esp_board_manager_defs.h](include/esp_board_manager_defs.h)

## 命令行选项

**板子选择:**

```bash
-b, --board BOARD_NAME           # 直接指定板子名称（绕过 sdkconfig 读取）
-b, --board BOARD_INDEX          # 通过索引指定板子
-c, --customer-path PATH         # 客户板子目录路径（使用 "NONE" 跳过）
-l, --list-boards                # 列出所有可用板子并退出
```

**生成控制:**

```bash
--kconfig-only                   # 仅生成 Kconfig 菜单系统（跳过板子配置生成）
--peripherals-only               # 仅处理外设（跳过设备）
--devices-only                   # 仅处理设备（跳过外设）
```

**SDKconfig 配置:**

```bash
--sdkconfig-only                 # 仅检查 sdkconfig 功能而不启用它们
--disable-sdkconfig-auto-update  # 禁用自动 sdkconfig 功能启用（默认启用）
```

**日志控制:**

```bash
--log-level LEVEL                # 设置日志级别: DEBUG, INFO, WARNING, ERROR (默认: INFO)
```

### 方法1: 作为 IDF Action 扩展使用（推荐）

使用命令 `idf.py gen-bmgr-config` 后加命令行选项，例如下面的用法：

```bash
# 列出可用板子
idf.py gen-bmgr-config -l

# 指定板子（名称或索引）
idf.py gen-bmgr-config -b echoear_core_board_v1_0
idf.py gen-bmgr-config -b 1

# 使用自定义板子
idf.py gen-bmgr-config -b my_board -c /path/to/custom/boards

# 清理生成的文件
idf.py gen-bmgr-config -x

...
```

### 方法2: 使用独立脚本

您也可以在 esp_board_manager 目录中直接使用独立脚本，例如下面的用法

```bash
# 列出可用板子
python gen_bmgr_config_codes.py -l

# 使用 -b 选项指定板子（名称或索引）
python gen_bmgr_config_codes.py -b echoear_core_board_v1_0
python gen_bmgr_config_codes.py -b 1

# 使用自定义板子
python gen_bmgr_config_codes.py 1 -c /custom/boards
python gen_bmgr_config_codes.py -b my_board -c /path/to/custom/boards

# 清理生成的文件
python gen_bmgr_config_codes.py -x
```

直接使用独立脚本时还有部分额外的用法：

```bash
# 从 sdkconfig 读取板子选择（如果存在）
python gen_bmgr_config_codes.py

# 将板子作为直接参数指定（名称或索引）
# 直接参数（不使用 `-b`）仅在直接调用脚本时有效，由于 ESP-IDF 框架限制，`idf.py` 不支持。
python gen_bmgr_config_codes.py esp32_s3_korvo2_v3
python gen_bmgr_config_codes.py 1
```

## 脚本执行流程

ESP Board Manager 使用 `gen_bmgr_config_codes.py` 进行代码生成，它在统一的工作流程中处理 Kconfig 菜单生成和板子配置生成。执行全面的 8 步流程，将 YAML 配置转换为 C 代码和构建系统文件：

1. **板子目录扫描**: 在默认、客户和组件目录中发现板子
2. **板子选择**: 从 sdkconfig 或命令行参数读取板子选择
3. **Kconfig 生成**: 为板子和组件选择创建统一 Kconfig 菜单系统
4. **配置文件发现**: 定位所选板子的 `board_peripherals.yaml` 和 `board_devices.yaml`
5. **外设处理**: 解析外设配置并生成 C 结构
6. **设备处理**: 处理设备配置、依赖关系并更新构建文件
7. **项目 sdkconfig 配置**: 根据板子设备和外设类型更新项目 sdkconfig
8. **文件生成**: 在工程文件夹的 `components/gen_bmgr_codes/` 中创建所有必要的 C 配置和句柄文件

**⚠️ 重要提示：** 切换板子时，脚本会在第 1 步中自动备份并删除现有的 `sdkconfig` 文件以防止配置污染（`--kconfig-only` 时跳过）。

## 自定义板子

`esp_board_manager` 支持模块化定制自己的开发板，具体的使用方法请参考：[如何创建自定义板子](docs/how_to_customize_board_cn.md)

## 路线图

ESP Board Manager 的未来开发计划（优先级从高到低）：

- **支持更多外设和设备**: 添加更多外设、设备和板子
- **Web 可视化配置**: 结合大模型通过网页实现可视化和智能化的配置板子
- **完善文档**: 增加更多说明文档，如建立明确的规则以促进客户添加外设和设备
- **增强验证**: 全面的 YAML 格式检查、模式验证、输入验证和增强的规则验证
- **增强数据结构**: 增强数据或 YAML 结构以提高性能
- **版本管理**: 支持设备和外设的不同版本代码和解析器
- **插件架构**: 用于自定义设备和外设支持的可扩展插件系统

## 故障排除

### 找不到 `esp_board_manager` 路径

1. 检查项目主 `idf_component.yml` 中的 `esp_board_manager` 依赖项
2. 添加 `esp_board_manager` 依赖项后，运行 `idf.py menuconfig` 或 `idf.py build`。这些命令会将 `esp_board_manager` 下载到 `YOUR_PROJECT_ROOT_PATH/managed_components/`

### `idf.py gen-bmgr-config` 命令未找到

如果 `idf.py gen-bmgr-config` 无法识别：

1. 检查 `IDF_EXTRA_ACTIONS_PATH` 是否正确设置
2. 重新启动您的终端会话

### `undefined reference for g_board_devices and g_board_peripherals`

1. 确保您的项目中没有 `idf_build_set_property(MINIMAL_BUILD ON)`，因为 MINIMAL_BUILD 仅通过包含所有其他组件所需的"通用"组件来执行最小构建。
2. 确保您的项目有 `components/gen_bmgr_codes` 文件夹，其中包含生成的文件。这些文件是通过运行 `idf.py gen-bmgr-config -b YOUR_BOARD` 生成的。

### 切换开发板

**重要提示**：切换板子时，脚本会自动：

1. 将 `sdkconfig` 备份到 `sdkconfig.bmgr_board.backup` 并删除原文件，以防止配置污染
2. 将板子特定配置从 `boards/<board_name>/sdkconfig.defaults.board` 追加到您项目的 `sdkconfig.defaults`

切换板子时请始终使用 `idf.py gen-bmgr-config -b`（或 `python gen_bmgr_config_codes.py`）。使用 `idf.py menuconfig` 可能导致依赖错误。

### 依赖某些组件的问题

如果在运行 `idf.py set-target xxx`、`idf.py menuconfig` 或 `idf.py reconfigure` 时遇到以下错误：

```bash
ERROR: Because project depends on xxxxx which
doesn't match any versions, version solving failed.
```

或类似的错误：

```bash
Failed to resolve component 'esp_board_manager' required by component
  'gen_bmgr_codes': unknown name.
```

这可能是 board manager 上次残留的生成文件未被清除导致的。**您可以使用 `idf.py gen-bmgr-config -x`（或 `python gen_bmgr_config_codes.py -x`）清理生成的文件**，这将删除所有生成的 .c 和 .h 文件并重置 CMakeLists.txt 和 idf_component.yml。

### Undefined reference to 'g_esp_board_devices'

出现 `undefined reference to 'g_esp_board_device_handles'` 或 `undefined reference to 'g_esp_board_devices'` 错误是因为没有运行 `idf.py gen-bmgr-config -b YOUR_BOARD`。

## 许可证

本项目采用修改版 MIT 许可证 - 详情请参阅 [LICENSE](./LICENSE) 文件。
