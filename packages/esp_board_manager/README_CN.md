# ESP Board Manager

这是由 Espressif 开发的专注于开发板设备初始化的板级管理模块。它使用 YAML 文件来描述主控制器和外部功能设备的外设接口配置，并自动生成配置代码，大大简化了添加新板子的过程。它提供了统一的设备管理接口，不仅提高了设备初始化代码的可重用性，还大大方便了应用程序对各种开发板的适配。

## 功能特性

* **YAML 配置**: 使用 YAML 文件对外设、设备和板级定义进行声明式硬件配置
* **灵活的板级管理**: 提供统一的初始化流程，支持用户自定义定制
* **统一的 API 接口**: 在不同板级配置下访问外设和设备的一致 API
* **代码生成**: 从 YAML 配置自动生成 C 代码
* **自动依赖管理**: 根据外设和设备依赖关系自动更新 `idf_component.yml` 文件
* **多板级支持**: 使用模块化架构支持多个客户板级的轻松配置和切换
* **可扩展架构**: 允许轻松集成新的外设和设备，包括对现有组件不同版本的支持
* **全面的错误管理**: 提供统一的错误代码和强大的错误处理，包含详细消息
* **低内存占用**: 在 RAM 中仅存储必要的运行时指针；配置数据保持为 flash 中的只读数据

## 项目结构

```
esp_board_manager/
├── esp_board_manager.c        # 主板级管理器实现
├── esp_board_periph.c         # 外设管理实现
├── esp_board_devices.c        # 设备管理实现
├── esp_board_err.c            # 错误处理实现
├── include/
│   ├── esp_board_manager.h    # 主板级管理器 API
│   ├── esp_board_periph.h     # 外设管理
│   ├── esp_board_device.h     # 设备管理
│   ├── esp_board_manager_err.h        # 统一错误管理系统
│   └── esp_board_manager_defs.h       # 关键字列表
├── peripherals/               # 外设实现
│   ├── periph_gpio.c/py/h     # GPIO 外设
│   ├── ...
├── devices/                   # 设备实现
│   ├── dev_audio_codec/       # 音频编解码器设备
│   ├── ...
├── boards/                    # 默认板级特定配置
│   ├── echoear_core_board_v1_0/
│   │   ├── board_peripherals.yaml
│   │   ├── board_devices.yaml
│   │   ├── board_info.yaml
│   │   └── Kconfig
│   └── ...
├── generators/                # 代码生成系统
├── gen_codes/                 # 生成的文件（自动创建）
│   └── Kconfig.in             # 统一 Kconfig 菜单
├── user project components/gen_bmgr_codes/ # 生成的板级配置文件（自动创建）
│   ├── gen_board_periph_config.c
│   ├── gen_board_periph_handles.c
│   ├── gen_board_device_config.c
│   ├── gen_board_device_handles.c
│   ├── gen_board_info.c
│   ├── CMakeLists.txt
│   └── idf_component.yml
├── gen_bmgr_config_codes.py   # 主代码生成脚本（统一）
├── idf_ext.py                 # IDF action 扩展（v6.0+ 自动发现）
├── README.md                  # 此文件
└── README_CN.md               # 中文版本说明
```

## 快速开始

### 1. 设置 IDF Action 扩展

ESP Board Manager 现在支持 IDF action 扩展，提供与 ESP-IDF 构建系统的无缝集成。此功能允许您直接使用 `idf.py gen-bmgr-config` 命令，而无需手动运行 Python 脚本。

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

如果您将此组件添加到项目的 yml 依赖项中，可以使用以下路径：

```bash
export IDF_EXTRA_ACTIONS_PATH=YOUR_PROJECT_ROOT_PATH/managed_components/XXXX__esp_board_manager
```

> **注意:** 如果您使用 `idf.py add-dependency xxx` 将 **esp_board_manager** 添加为依赖组件，在首次构建或清理 `managed_components` 文件夹后，目录 `YOUR_PROJECT_ROOT_PATH/managed_components/XXXX__esp_board_manager` 将不可见。我们建议运行 `idf.py set-target`、`idf.py menuconfig` 或 `idf.py build` 来自动将 **esp_board_manager** 组件下载到 `YOUR_PROJECT_ROOT_PATH/managed_components/XXXX__esp_board_manager`。

> **版本要求:** 兼容 ESP-IDF v5.4 和 v5.5 分支。**注意:** v5.4.2 或 v5.5.1 之前的版本可能会遇到 Kconfig 依赖问题。

> **注意:** IDF action 扩展自动发现功能从 ESP-IDF v6.0 开始可用。从 IDF v6.0 开始无需设置 `IDF_EXTRA_ACTIONS_PATH`，因为它会自动发现 IDF action 扩展。

如果遇到任何问题，请参考 [## 故障排除](#故障排除) 部分。


### 2. 扫描板级并选择板级

您可以使用 `-l` 选项发现可用的板级：

```bash
# 列出所有可用板级
idf.py gen-bmgr-config -l

# 或使用独立脚本
python YOUR_BOARD_MANAGER_PATH/gen_bmgr_config_codes.py -l
```

然后选择您的目标板级：
```bash
idf.py gen-bmgr-config -b YOUR_TARGET_BOARD
```

### 3. 在您的应用程序中使用

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
    // 初始化板级管理器
    ESP_LOGI(TAG, "Initializing board manager...");
    int ret = esp_board_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize board manager");
        return;
    }

    // 获取外设句柄
    void *spi_handle;
    ret = esp_board_manager_get_periph_handle("spi-1", &spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPI peripheral");
        return;
    }

    // 获取设备句柄
    dev_display_lcd_spi_handles_t *lcd_handle;
    ret = esp_board_manager_get_device_handle("lcd_display", &lcd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LCD device");
        return;
    }

    // 获取设备配置
    dev_audio_codec_config_t *device_config;
    ret = esp_board_manager_get_device_config("audio_dac", &device_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get device config");
        return;
    }

    // 打印板级信息
    esp_board_manager_print_board_info();

    // 打印板级管理器状态
    esp_board_manager_print();

    // 使用句柄...
}
```

### 4. 更多示例

有关综合使用示例，请参考 `test_apps/main/` 目录中的测试应用程序：

#### 设备示例
- **[`test_dev_audio_codec.c`](test_apps/main/test_dev_audio_codec.c)** - 带有 DAC/ADC、SD 卡播放、录音和回环测试的音频编解码器
- **[`test_dev_lcd_init.c`](test_apps/main/test_dev_lcd_init.c)** - LCD 显示初始化和基本控制
- **[`test_dev_lcd_lvgl.c`](test_apps/main/test_dev_lcd_lvgl.c)** - 带有 LVGL、触摸屏和背光控制的 LCD 显示屏
- **[`test_dev_pwr_ctrl.c`](test_apps/main/test_dev_pwr_ctrl.c)** - 基于 GPIO 的 LCD 和音频设备电源管理
- **[`test_dev_fatfs_sdcard.c`](test_apps/main/test_dev_fatfs_sdcard.c)** - SD 卡操作和 FATFS 文件系统测试
- **[`test_dev_fs_spiffs.c`](test_apps/main/test_dev_fs_spiffs.c)** - SPIFFS 文件系统测试
- **[`test_dev_custom.c`](test_apps/main/test_dev_custom.c)** - 自定义设备测试和配置
- **[`test_dev_gpio_expander.c`](test_apps/main/test_dev_gpio_expander.c)** - GPIO 扩展芯片测试
- **[`test_dev_camera`](test_apps/main/test_dev_camera.c)** - 测试 Camera sensor 的视频流捕获能力
- **[`test_dev_ledc.c`](test_apps/main/test_dev_ledc.c)** - 用于 PWM 和背光控制的 LEDC 设备

#### 外设示例
- **[`test_periph_i2c.c`](test_apps/main/periph/test_periph_i2c.c)** - 用于设备通信的 I2C 外设
- **[`test_periph_gpio.c`](test_apps/main/periph/test_periph_gpio.c)** - 用于数字 I/O 操作的 GPIO 外设
- **[`test_periph_uart.c`](test_apps/main/periph/test_periph_uart.c)** - 用于串行端口操作的 UART 外设
- **[`test_periph_adc.c`](test_apps/main/periph/test_periph_adc.c)** - 用于测量特定模拟 IO 管脚模拟信号的 ADC 外设
- **[`test_periph_anacmpr.c`](test_apps/main/periph/test_periph_anacmpr.c)** - 用于比较源信号与参考信号的模拟比较器外设
- **[`test_periph_dac.c`](test_apps/main/periph/test_periph_dac.c)** - 用于将数字值转换成模拟电压的 DAC 外设
- **[`test_periph_mcpwm.c`](test_apps/main/periph/test_periph_mcpwm.c)** - 多功能 PWM 生成器外设
- **[`test_periph_sdm.c`](test_apps/main/periph/test_periph_sdm.c)** - 用于脉冲密度调制的 SDM 外设

#### 用户接口

Board Manager 的设备名称推荐用于用户项目，而外设名称不推荐。这是因为设备名称是功能命名的，不包含额外信息，使其更容易适配多个板级。Board Manager 提供的设备名称是**保留关键字**，请参阅 [esp_board_manager_defs.h](./include/esp_board_manager_defs.h)。以下设备名称可用于用户应用程序：

| 设备名称 | 描述 |
|-------------|-------------|
| `audio_dac`, `audio_adc` | 音频编解码器设备 |
| `display_lcd` | LCD 显示设备 |
| `fs_sdcard` | SD 卡设备 |
| `fs_fat` | FAT 文件系统设备（支持 sub_type: sdmmc 或 spi） |
| `fs_spiffs` | SPIFFS 文件系统设备 |
| `lcd_touch` | 触摸屏设备 |
| `lcd_power` | LCD 电源控制 |
| `lcd_brightness` | LCD 亮度控制 |
| `gpio_expander` | GPIO 扩展芯片 |
| `camera` | 摄像头设备 |
| `power_ctrl` | 电源控制设备 |

## YAML 配置规则
有关详细的 YAML 配置规则和格式规范，请参阅 [设备和外设规则](docs/device_and_peripheral_rules.md)。

## 自定义板级

### 创建新板级

#### 创建板级文件夹和文件

1. **创建板级目录**
- 推荐板级目录放置在项目的 components 文件下，路径可参考: `esp_board_manager/test_apps/components/board_customer`
- 板级目录还可以放在其他位置，路径参考：`esp_board_manager/test_apps/test_custom_boards` 和 `esp_board_manager/test_apps/test_single_board`。这个路径运行命令时需要添加 `-c` 参数来指定路径，如 `test_apps` 下：`idf.py gen-bmgr-config -c test_single_board -l`

   ```bash
   mkdir boards/<board_name>
   cd boards/<board_name>
   ```

2. **创建必需文件**
   ```bash
   touch board_peripherals.yaml
   touch board_devices.yaml
   touch board_info.yaml
   touch Kconfig
   ```

3. **配置 Kconfig**
   ```kconfig
   config BOARD_<BOARD_NAME>
       bool "<Board Display Name>"
       depends on SOC_<CHIP_TYPE>  # optional
   ```

4. **添加板级信息**
   ```yaml
   # board_info.yaml
   board: <board_name>
   chip: <chip_type>
   version: <version>
   description: "<board description>"
   manufacturer: "<manufacturer_name>"
   ```

5. **定义外设**
   - 寻找 `boards` 中相似的外设配置YML文件进行参考
   - 基于 `peripherals` 下支持的外设 YML 进行配置
   每个外设的基本结构如下：
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
         # 外设特定配置
   ```

6. **定义设备**
   - 寻找 `boards` 中相似的设备配置YML文件进行参考
   - 基于 `devices` 下支持的设备 YML 进行配置
   每个设备的基本结构如下：
   ```yaml
   # board_devices.yaml
   board: <board_name>
   chip: <chip_type>
   version: <version>

   devices:
     - name: <device_name>
       type: <device_type>
       sub_type: <sub_type>   # 可选：子设备类型字符串，每个设备可能有自己的子类型或没有
       init_skip: false  # 可选：跳过自动初始化（默认：false）
       dependencies:     # 可选，定义组件依赖关系
         espressif/gmf_core:
            version: '*'  # 使用来自 espressif 组件注册表的版本
            override_path: ${BOARD_PATH}/gmf_core
            # 可选：允许您使用本地组件而不是从组件注册表下载的版本。
            # 您可以指定：
            #   - 绝对路径，或
            #   - 在 ${BOARD_PATH} 下的相对路径以便于管理
       config:
         # 设备特定配置
         sub_config:      # 可选：如果存在 sub_type，则提供子配置
           # 子类型特定配置
       peripherals:
         - name: <peripheral_name>
    ```
7. **`board` 目录中自定义代码的说明**
   - 在使用一些设备的时候，需要额外增加自定义代码，比如 `display_lcd`, `lcd_touch` and `custom` device.
   - 这是为了提高板子的适配，让用户根据自己板子情况选择 device 实际初始化函数，`display_lcd`, `lcd_touch` 参考 `esp_board_manager/boards/echoear_core_board_v1_2/setup_device.c`。

8. **`custom` 自定义设备说明**
   - 对于 esp_board_manager 还未包含的设备和外设，建议通过 `custom` 类型 device 进行添加
   - 实现的代码放置在 `board` 目录下，参考 `esp_board_manager/boards/esp32_s3_korvo2l/custom_device.c`
   - 当该 board 被选择后 `gen_bmgr_codes` 目录下会生成 `gen_board_device_custom.h` 头文件，供应用程序使用

> **⚙️ 关于 `dependencies` 使用说明**
>
> - * `board_devices.yaml` 中的 `dependencies` 字段允许您为每个设备指定组件依赖关系。这对于需要自定义或本地版本组件的板级特别有用。
> - * 这些依赖关系将被复制到 `gen_bmgr_codes` 文件夹中的 `idf_component.yml` 文件中。如果存在相同名称的依赖关系，根据 YAML 顺序，只保留最后一个。
> - * 该字段支持所有组件注册表功能，包括 `override_path` 和 `path` 选项。更多详情请参考[组件依赖](https://docs.espressif.com/projects/idf-component-manager/en/latest/reference/manifest_file.html#component-dependencies)。
> - * 使用相对路径作为本地路径时，请注意它们是相对于 `gen_bmgr_codes` 目录的。如果用户指定本地路径在板级目录，可使用 `${BOARD_PATH}` 来简化路径。参考示例：`./test_apps/test_custom_boards/my_boards/test_board1`。
>
> **⚙️ `${BOARD_PATH}` 变量：**
> - * `${BOARD_PATH}` 是一个特殊变量，始终指向当前板级定义的根目录（即包含您的 `board_devices.yaml` 的文件夹）。
> - * 在 `override_path` 或 `path` 字段中指定本地或板级特定组件路径时，始终使用 `${BOARD_PATH}`。更多详情请参考[本地目录依赖](https://docs.espressif.com/projects/idf-component-manager/en/latest/reference/manifest_file.html#local-directory-dependencies)。
> - * ❌ **错误**：`{{BOARD_PATH}}` 或 `$BOARD_PATH`

### 板级路径

ESP Board Manager 支持通过三个不同的路径位置进行板级配置，为不同的部署场景提供灵活性：

1. **主板级目录**: 随包提供的内置板级，路径如 `esp_board_manager/boards`
2. **用户项目组件**: 在项目组件中定义的自定义板级，路径如 `{PROJECT_ROOT}/components/{component_name}/boards`
3. **自定义客户路径**: 自定义位置的外部板级定义。用户通过 `gen_bmgr_config_codes.py` 中的 `-c` 参数指定路径来设置自定义路径。

### 板级选择优先级

当不同路径中存在同名板级时，ESP Board Manager 遵循特定的优先级顺序来确定使用哪个板级配置：

**优先级顺序（从高到低）：**

1. **自定义客户路径**（最高优先级）
   - 通过 `-c` 参数指定的板级
   - 示例：`idf.py gen-bmgr-config -b my_board -c /path/to/custom/boards`

2. **用户项目组件**（中等优先级）
   - 项目组件中的板级：`{PROJECT_ROOT}/components/{component_name}/boards`
   - 这些会覆盖同名的主板级

3. **主板级目录**（最低优先级）
   - 内置板级：`esp_board_manager/boards`
   - 当没有其他版本时作为后备使用

**重要说明：**
- **无重复警告**：系统会静默使用高优先级板级，不会警告重复项

### 验证和使用新板级

1. **验证板级配置**
   ```bash
   # 测试您的板级配置是否有效
   # 对于主板级和用户项目组件（默认路径）
   # 确保 IDF_EXTRA_ACTIONS_PATH 已按照 [设置 IDF Action 扩展] 章节进行设置
   idf.py gen-bmgr-config -b <board_name>

   # 对于自定义客户路径（外部位置）
   idf.py gen-bmgr-config -b <board_name> -c /path/to/custom/boards

   # 或使用独立脚本
   cd  YOUR_ESP_BOARD_MANAGER_PATH
   python gen_bmgr_config_codes.py -b <board_name>
   python gen_bmgr_config_codes.py -b <board_name> -c /path/to/custom/boards
   ```

   **检查成功**: 如果板级配置有效，将在工程路径中生成以下文件：
   - `components/gen_bmgr_codes/gen_board_periph_handles.c` - 外设句柄定义
   - `components/gen_bmgr_codes/gen_board_periph_config.c` - 外设配置结构
   - `components/gen_bmgr_codes/gen_board_device_handles.c` - 设备句柄定义
   - `components/gen_bmgr_codes/gen_board_device_config.c` - 设备配置结构
   - `components/gen_bmgr_codes/gen_board_info.c` - 板级元数据
   - `components/gen_bmgr_codes/CMakeLists.txt` - 构建系统配置
   - `components/gen_bmgr_codes/idf_component.yml` - 组件依赖关系

   **如果出现错误**: 检查您的 YAML 文件是否有语法错误、缺失字段或无效配置。

   **注意**: 当您首次运行 `idf.py gen-bmgr-config` 时，脚本将自动生成 Kconfig 菜单系统。

## 支持的组件

### 支持的外设类型

| 外设 | 类型 | 角色 | 状态 | 描述 | 参考 YAML |
|------------|------|------|--------|-------------|----------------|
| GPIO | gpio | none | ✅ | 通用 I/O | [`periph_gpio.yml`](peripherals/periph_gpio/periph_gpio.yml) |
| I2C | i2c | master/slave | ✅ | I2C 通信 | [`periph_i2c.yml`](peripherals/periph_i2c/periph_i2c.yml) |
| SPI | spi | master/slave | ✅ | SPI 通信 | [`periph_spi.yml`](peripherals/periph_spi/periph_spi.yml) |
| I2S | i2s | master/slave | ✅ | 音频接口 | [`periph_i2s.yml`](peripherals/periph_i2s/periph_i2s.yml) |
| LEDC | ledc | none | ✅ | LED 控制/PWM | [`periph_ledc.yml`](peripherals/periph_ledc/periph_ledc.yml) |
| UART | uart | none | ✅ | UART 通信 | [`periph_uart.yml`](peripherals/periph_uart/periph_uart.yml) |
| ADC | adc | none | ✅ | ADC 模数转换 | [`periph_adc.yml`](peripherals/periph_adc/periph_adc.yml) |
| RMT | rmt | tx/rx | ✅ | 红外遥控 | [`periph_rmt.yml`](peripherals/periph_rmt/periph_rmt.yml) |
| PCNT | pcnt | none | ✅ | 脉冲计数器 | [`periph_pcnt.yml`](peripherals/periph_pcnt/periph_pcnt.yml) |
| ANACMPR | anacmpr | none | ✅ | 模拟比较器 | [`periph_anacmpr.yml`](peripherals/periph_anacmpr/periph_anacmpr.yml) |
| DAC | dac | none | ✅ | 数模转换器 | [`periph_dac.yml`](peripherals/periph_dac/periph_dac.yml) |
| MCPWM | mcpwm | none | ✅ | PWM 生成器 | [`periph_mcpwm.yml`](peripherals/periph_mcpwm/periph_mcpwm.yml) |
| SDM | sdm | none | ✅ | Sigma Delta 调制器 | [`periph_sdm.yml`](peripherals/periph_sdm/periph_sdm.yml) |

### 支持的设备类型

| 设备 | 类型 | 子类型 | 芯片 | 外设 | 状态 | 描述 | 参考 YAML |
|--------|------|----------|------|------------|--------|-------------|----------------|
| 音频编解码器 | audio_codec | - | ES8311/ES7210/ES8388 | i2s/i2c | ✅ | 带有 DAC/ADC 的音频编解码器 | [`dev_audio_codec.yaml`](devices/dev_audio_codec/dev_audio_codec.yaml) |
| SPI LCD 显示屏 | display_lcd_spi | - | ST77916/GC9A01 | spi | ✅ | SPI LCD 显示屏(与接口类型绑定,将被弃用) | [`dev_display_lcd_spi.yaml`](devices/dev_display_lcd_spi/dev_display_lcd_spi.yaml) |
| LCD 显示屏 | display_lcd | - | ILI9341/EK79007 | spi/dsi | ✅ | LCD 显示屏 | [`dev_display_lcd.yaml`](devices/dev_display_lcd/dev_display_lcd.yaml) |
| 触摸屏 | lcd_touch_i2c | - | FT5x06 | i2c | ✅ | I2C 触摸屏 | [`dev_lcd_touch_i2c.yaml`](devices/dev_lcd_touch_i2c/dev_lcd_touch_i2c.yaml) |
| SD 卡 | fatfs_sdcard | - | - | sdmmc | ✅ | SD 卡存储 | [`dev_fatfs_sdcard.yaml`](devices/dev_fatfs_sdcard/dev_fatfs_sdcard.yaml) |
| SPI SD 卡 | fatfs_sdcard_spi | - | - | spi | ✅ | SD 卡存储 | [`dev_fatfs_sdcard_spi.yaml`](devices/dev_fatfs_sdcard_spi/dev_fatfs_sdcard_spi.yaml) |
| FAT 文件系统 | fs_fat | sdmmc/spi | - | sdmmc/spi | ✅ | 带有 SDMMC 或 SPI 子设备的 FAT 文件系统 | [`dev_fs_fat.yaml`](devices/dev_fs_fat/dev_fs_fat.yaml) |
| SPIFFS 文件系统 | fs_spiffs | - | - | - | ✅ | SPIFFS 文件系统 | [`dev_fs_spiffs.yaml`](devices/dev_fs_spiffs/dev_fs_spiffs.yaml) |
| GPIO 控制 | gpio_ctrl | - | - | gpio | ✅ | GPIO 控制设备 | [`dev_gpio_ctrl.yaml`](devices/dev_gpio_ctrl/dev_gpio_ctrl.yaml) |
| LEDC 控制 | ledc_ctrl | - | - | ledc | ✅ | LEDC 控制设备 | [`dev_ledc_ctrl.yaml`](devices/dev_ledc_ctrl/dev_ledc_ctrl.yaml) |
| [自定义设备](devices/dev_custom/README.md) | custom | - | - | any | ✅ | 用户定义的自定义设备 | [`dev_custom.yaml`](devices/dev_custom/dev_custom.yaml) |
| GPIO 扩展芯片 | gpio_expander | - | TCA9554/TCA95XX/HT8574 | i2c | ✅ | GPIO 扩展芯片 | [`dev_gpio_expander.yaml`](devices/dev_gpio_expander/dev_gpio_expander.yaml) |
| 摄像头 | camera | - | - | i2c | ✅ | 摄像头设备 | [`dev_camera.yaml`](devices/dev_camera/dev_camera.yaml) |
| 电源控制设备 | power_ctrl | gpio | - | gpio | ✅ | 电源控制设备 | [`dev_power_ctrl.yaml`](devices/dev_power_ctrl/dev_power_ctrl.yaml) |


> 对于同一设备，我们将不再通过接口类型来区分不同的设备类型。例如，`dev_fatfs_sdcard` 和 `dev_fatfs_sdcard_spi` 将统一在 fs_fat 下进行管理，`dev_display_lcd_spi` 也将改为使用 `dev_display_lcd` 进行管理。
> 这三种设备类型将在未来版本中被弃用。用户可以参照 [`dev_fatfs_sdcard.yaml`](./devices/dev_fatfs_sdcard/dev_fatfs_sdcard.yaml)、[`dev_fatfs_sdcard_spi.yaml`](./devices/dev_fatfs_sdcard_spi/dev_fatfs_sdcard_spi.yaml) 和 [`dev_display_lcd_spi.yaml`](./devices/dev_display_lcd_spi/dev_display_lcd_spi.yaml) 文件，了解如何将原有配置迁移到新的设备类型。

### 支持的板级

| 板级名称 | 芯片 | 音频 | SD卡 | LCD | LCD 触摸 | 摄像头 |
|------------|------|-------|--------|-----|-----------|-----------|
| [`Echoear Core Board V1.0`](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s3/echoear/user_guide_v1.2.html) | ESP32-S3 | ✅ ES8311 + ES7210 | ✅ SDMMC | ✅ ST77916 | ✅ FT5x06 | - |
| Dual Eyes Board V1.0 | ESP32-S3 | ✅ ES8311 | ❌ | ✅ GC9A01 (双) | - | - |
| [`ESP-BOX-3`](https://github.com/espressif/esp-box/blob/master/docs/hardware_overview/esp32_s3_box_3/hardware_overview_for_box_3_cn.md) | ESP32-S3 | ✅ ES8311 + ES7210 | ✅ SDMMC | ✅ ST77916 | ✅ FT5x06 | - |
| [`ESP32-S3 Korvo2 V3`](https://docs.espressif.com/projects/esp-adf/zh_CN/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2.html) | ESP32-S3 | ✅ ES8311 + ES7210 | ✅ SDMMC | ✅ ILI9341 | ✅ TT21100 | ✅ DVP Camera |
| ESP32-S3 Korvo2L | ESP32-S3 | ✅ ES8311 | ✅ SDMMC | ❌ | ❌ | ❌ |
| [`Lyrat Mini V1.1`](https://docs.espressif.com/projects/esp-adf/zh_CN/latest/design-guide/dev-boards/get-started-esp32-lyrat-mini.html) | ESP32 | ✅ ES8388 | ✅ SDMMC | - | - | - |
| [`ESP32-C5 Spot`](https://oshwhub.com/esp-college/esp-spot) | ESP32-C5 | ✅ ES8311 (双) | - | - | - | - |
| [`ESP32-P4 Function-EV`](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html) | ESP32-P4 | ✅ ES8311 | ✅ SDMMC | ✅ EK79007 | ✅ GT911 | ✅ CSI Camera |
| [`M5STACK CORES3`](https://docs.m5stack.com/zh_CN/core/CoreS3) | ESP32-S3 | ✅ AW88298 + ES7210 | ✅ SDSPI | ✅ ILI9342C | ✅ FT5x06 | ❌ |

注：'✅' 表示已经支持，'❌' 表示尚未支持，'-' 表示硬件不具备相应的能力

## 板级管理器设置

### 自动 SDK 配置更新

控制板级管理器是否根据检测到的设备和外设类型自动更新 sdkconfig。

**默认**: 启用 (`y`)

**通过 sdkconfig 禁用**:
```bash
# 使用 menuconfig
idf.py menuconfig
# 导航到: Component config → ESP Board Manager Configuration → Board Manager Setting

# 在 sdkconfig 中设置
CONFIG_ESP_BOARD_MANAGER_AUTO_CONFIG_DEVICE_AND_PERIPHERAL=n
```
如果您想启用超出 YAML 中定义的设备或外设类型，请禁用此选项。

## 脚本执行流程

ESP Board Manager 使用 `gen_bmgr_config_codes.py` 进行代码生成，它在统一的工作流程中处理 Kconfig 菜单生成和板级配置生成。这个统一脚本比之前的分离脚本提供 81% 更快的执行速度。

### `gen_bmgr_config_codes.py` - 配置生成器

执行全面的 8 步流程，将 YAML 配置转换为 C 代码和构建系统文件：

1. **板级目录扫描**: 在默认、客户和组件目录中发现板级
2. **板级选择**: 从 sdkconfig 或命令行参数读取板级选择
3. **Kconfig 生成**: 为板级和组件选择创建统一 Kconfig 菜单系统
4. **配置文件发现**: 定位所选板级的 `board_peripherals.yaml` 和 `board_devices.yaml`
5. **外设处理**: 解析外设配置并生成 C 结构
6. **设备处理**: 处理设备配置、依赖关系并更新构建文件
7. **项目 sdkconfig 配置**: 根据板级设备和外设类型更新项目 sdkconfig
8. **文件生成**: 在工程文件夹的 `components/gen_bmgr_codes/` 中创建所有必要的 C 配置和句柄文件

#### 命令行选项

**板级选择:**
```bash
-b, --board BOARD_NAME           # 直接指定板级名称（绕过 sdkconfig 读取）
-c, --customer-path PATH         # 客户板级目录路径（使用 "NONE" 跳过）
-l, --list-boards               # 列出所有可用板级并退出
```

**生成控制:**
```bash
--kconfig-only                  # 仅生成 Kconfig 菜单系统（跳过板级配置生成）
--peripherals-only              # 仅处理外设（跳过设备）
--devices-only                  # 仅处理设备（跳过外设）
```

**SDKconfig 配置:**
```bash
--sdkconfig-only                # 仅检查 sdkconfig 功能而不启用它们
--disable-sdkconfig-auto-update # 禁用自动 sdkconfig 功能启用（默认启用）
```

**日志控制:**
```bash
--log-level LEVEL               # 设置日志级别: DEBUG, INFO, WARNING, ERROR (默认: INFO)
```

#### 使用示例


### 设置 IDF Action 扩展（推荐）

ESP Board Manager 现在支持 IDF action 扩展，提供与 ESP-IDF 构建系统的无缝集成：

#### 安装

设置 `IDF_EXTRA_ACTIONS_PATH` 环境变量以包含 ESP Board Manager 目录：

```bash
export IDF_EXTRA_ACTIONS_PATH="/PATH/TO/YOUR_PATH/esp_board_manager"
```

#### 使用

```bash
# 基本使用
idf.py gen-bmgr-config -b echoear_core_board_v1_0

# 使用自定义板级
idf.py gen-bmgr-config -b my_board -c /path/to/custom/boards

# 仅生成 Kconfig
idf.py gen-bmgr-config --kconfig-only

# 仅处理外设
idf.py gen-bmgr-config -b echoear_core_board_v1_0 --peripherals-only

# 列出可用板级
idf.py gen-bmgr-config -l

# 设置日志级别为 DEBUG 以获取详细输出
idf.py gen-bmgr-config --log-level DEBUG
```

### 方法 2: 独立脚本

您也可以在 esp_board_manager 目录中直接使用独立脚本。

**以下描述了 `gen_bmgr_config_codes.py` 支持的所有参数，这些参数也适用于 `idf.py gen-bmgr-config`：**

**基本使用:**
```bash
# 使用 sdkconfig 和默认板级
python gen_bmgr_config_codes.py

# 直接指定板级
python gen_bmgr_config_codes.py -b echoear_core_board_v1_0

# 添加客户板级目录
python gen_bmgr_config_codes.py -c /path/to/custom/boards

# 板级和自定义路径
python gen_bmgr_config_codes.py -b my_board -c /path/to/custom/boards

# 列出可用板级
python gen_bmgr_config_codes.py -l

# 禁用自动 sdkconfig 更新
python gen_bmgr_config_codes.py --disable-sdkconfig-auto-update

# 设置日志级别为 DEBUG 以获取详细输出
python gen_bmgr_config_codes.py --log-level DEBUG
```

**部分生成:**
```bash
# 仅处理外设
python gen_bmgr_config_codes.py --peripherals-only

# 仅处理设备
python gen_bmgr_config_codes.py --devices-only

# 检查 sdkconfig 功能而不启用
python gen_bmgr_config_codes.py --sdkconfig-only
```

#### 生成的文件

**配置文件:**
- `gen_codes/Kconfig.in` - 板级选择的统一 Kconfig 菜单
- `components/gen_bmgr_codes/gen_board_periph_config.c` - 外设配置
- `components/gen_bmgr_codes/gen_board_periph_handles.c` - 外设句柄
- `components/gen_bmgr_codes/gen_board_device_config.c` - 设备配置
- `components/gen_bmgr_codes/gen_board_device_handles.c` - 设备句柄
- `components/gen_bmgr_codes/gen_board_info.c` - 板级元数据
- `components/gen_bmgr_codes/CMakeLists.txt` - 构建系统配置
- `components/gen_bmgr_codes/idf_component.yml` - 组件依赖关系

**注意:** 生成的文件按以下方式组织：
- Kconfig 文件放在 `gen_codes/` 目录中
- 板级配置文件放在工程目录的 `components/gen_bmgr_codes/` 中以与 ESP-IDF 构建系统集成
- 如果这些目录不存在，生成脚本会自动创建，不应手动修改

## 路线图

ESP Board Manager 的未来开发计划（优先级从高到低）：
- **支持更多外设和设备**: 添加更多外设、设备和板级
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

### **切换开发板**
必须使用 `idf.py gen-bmgr-config -b`。使用 `idf.py menuconfig` 选择板级可能会导致依赖错误。

### "sdkconfig 文件未找到"

如果您看到错误 `sdkconfig file not found at [path]`，这意味着 ESP Board Manager 将根据您选择的板级 YAML 文件创建默认的设备和外设依赖关系。

**注意:** 当您在项目中首次运行 `idf.py menuconfig`、`idf.py set-target xxx` 或 `idf.py build` 时，ESP-IDF 会自动生成 `sdkconfig` 文件。

## 许可证

本项目采用修改版 MIT 许可证 - 详情请参阅 [LICENSE](./LICENSE) 文件。
