# 如何创建自定义板子

## 支持的板子路径

ESP Board Manager 能够模块化自定义板子，支持将自定义板子文件放在以下三种不同的路径，为不同的部署场景提供灵活性：

1. **主板级目录**: 随组件提供的内置板子，路径如 `esp_board_manager/boards`
2. **用户项目组件**: 在项目组件中定义的自定义板子，路径如 `{PROJECT_ROOT}/components/board_name`，可参考: [`esp_board_manager/test_apps/components/test_board_e/`](../test_apps/components/test_board_e/)
3. **自定义路径**: 自定义位置的外部板子定义，如 [`esp_board_manager/test_apps/test_single_board`](../test_apps/test_single_board/)，执行命令时需要通过 `-c` 参数指定路径来设置自定义路径 `idf.py gen-bmgr-config -c test_single_board -l`

> **注意：** 如果使用从组件仓库自动下载 ESP Board Manager 组件，不建议直接在主板级目录下直接创建自定义板子，避免清理组件时误删除自定义板子配置

## 创建新板子

### 1. **创建板子目录**

- 推荐将板子目录放置在项目的 `components` 文件下 `{PROJECT_ROOT}/components/XXXX`，

   ```bash
   mkdir components/<board_name>
   cd components/<board_name>
   ```

### 2. **创建必需文件**

创建新板子有两种方式：

- 从主板级目录复制相似的板子文件到您在项目中创建的板子目录，然后**根据实际使用的板子进行修改**（推荐），可以参考以下内容来验证 [验证和使用新板子](#验证和使用新板子)

- 运行以下脚本，手动创建板子所需文件。

   ```bash
   touch board_peripherals.yaml
   touch board_devices.yaml
   touch board_info.yaml
   touch Kconfig
   touch sdkconfig.defaults.board  # 可选：板子特定的 SDK 配置默认值
   ```

> **注意：**
> 两种方法都依赖于设备和外设 YAML 文件中的配置，这些 YAML 文件；尽量使用了原始驱动参数，包括：
>
> - 支持哪些配置字段
> - 有效的枚举值
> - 允许的参数范围
>
> 换句话说，YAML 配置的设计旨在尽可能匹配适配底层驱动 API，可以复用现有的驱动知识。
>
> 有关确切的定义和支持的选项，请参考：
>
> - `esp_board_manager/devices/xxx/xxx.yaml`
> - `esp_board_manager/peripherals/xxx/xxx.yaml`

### 3. **配置文件结构**

**Kconfig**
   ```kconfig
   config BOARD_<BOARD_NAME>
       bool "<Board Display Name>"
       depends on SOC_<CHIP_TYPE>  # optional
   ```

**板子信息**
   ```yaml
   # board_info.yaml
   board: <board_name>
   chip: <chip_type>
   version: <version>
   description: "<board description>"
   manufacturer: "<manufacturer_name>"
   ```

**外设配置**
   - 根据 `boards` 中相似的外设 YML 文件进行参考和修改
   - 基于 `peripherals` 下支持的外设 YML 文件进行配置
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

**设备配置**
   - 根据 `boards` 中相似的设备 YML 文件进行参考和修改
   - 基于 `devices` 下支持的设备 YML 文件进行配置
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

> **⚙️ 关于 `dependencies` 使用说明**
>
> - `board_devices.yaml` 中的 `dependencies` 字段允许您为每个设备指定组件依赖关系。这对于需要自定义或本地组件版本的板子特别有用。
> - 这些依赖关系将被复制到 `gen_bmgr_codes` 文件夹中的 `idf_component.yml` 文件中。如果存在相同名称的依赖关系，根据 YAML 顺序，只保留最后一个。
> - 该字段支持所有组件注册表功能，包括 `override_path` 和 `path` 选项。更多详情请参考[组件依赖](https://docs.espressif.com/projects/idf-component-manager/en/latest/reference/manifest_file.html#component-dependencies)。
> - 使用相对路径作为本地路径时，请注意它们是相对于 `gen_bmgr_codes` 目录的。如果用户指定本地路径在板子目录，可使用 `${BOARD_PATH}` 来简化路径。参考示例：`./test_apps/test_custom_boards/my_boards/test_board1`。
>
> **⚙️ `${BOARD_PATH}` 变量：**
> - `${BOARD_PATH}` 是一个特殊变量，始终指向当前板子定义的根目录（即包含您的 `board_devices.yaml` 的文件夹）。
> - 在 `override_path` 或 `path` 字段中指定本地或板子特定组件路径时，始终使用 `${BOARD_PATH}`。更多详情请参考[本地目录依赖](https://docs.espressif.com/projects/idf-component-manager/en/latest/reference/manifest_file.html#local-directory-dependencies)。
> - ❌ **错误**：`{{BOARD_PATH}}` 或 `$BOARD_PATH`

### 4. **板子特定的 SDK 配置（可选）**

`sdkconfig.defaults.board` 文件用于定义板子的默认 SDK 配置。

   - 在板子目录中创建 `sdkconfig.defaults.board` 文件来定义板子特定的 SDK 配置默认值
   - 切换到此板子时，脚本会自动将这些设置**写入**到项目根目录的 `board_manager.defaults` 文件中
   - 这样可以确保板子特定的配置在 ESP-IDF 构建系统的各种操作（menuconfig、reconfigure 等）中通过 `SDKCONFIG_DEFAULTS` 环境变量自动应用

   示例：
   ```bash
   # sdkconfig.defaults.board
   # 示例: 使用八线 PSRAM 的板子（Octal，8-line）
   CONFIG_SPIRAM_MODE_OCT=y
   CONFIG_SPIRAM_SPEED_80M=y
   ```
   - 文件支持标准的 ESP-IDF sdkconfig 格式：
     - `CONFIG_XXX=y` 启用
     - `CONFIG_XXX=n` 或 `# CONFIG_XXX is not set` 禁用
     - `CONFIG_XXX="value"` 字符串值
   - 切换板子时，`board_manager.defaults` 文件会被重新生成，包含新板子的配置

   配置优先级：
1. `sdkconfig`（用户当前配置）
2. `sdkconfig.defaults`（项目默认值）
3. `board_manager.defaults`（板子特定配置，优先级高于 sdkconfig.defaults）
4. 组件自己的默认值

### 5. `board` 目录中自定义代码的说明

某些设备需要根据板子执行特定的初始化逻辑，仅通过 YAML 配置无法完整描述，例如 `display_lcd`、`lcd_touch` 等。

为了支持灵活的自定义板子，您可以在板子目录下提供自定义代码，使开发板能够：

- 为设备执行合适的初始化例程
- 处理和板子特定的接线、时序或上电序列
- 在必要时覆盖或扩展默认的设备初始化行为

有关具体实现示例可以参考：
[setup_device.c](esp_board_manager/boards/echoear_core_board_v1_2/setup_device.c)，该文件实现了 display_lcd 和 lcd_touch 设备的特定初始化流程

## YAML 配置规则

有关详细的 YAML 配置规则和格式规范，请参阅 [设备和外设规则](docs/device_and_peripheral_rules.md)。

## 板子选择优先级

当不同路径中存在同名板子时，ESP Board Manager 遵循特定的优先级顺序来确定使用哪个板子配置：

**优先级顺序（从高到低）：**

1. **自定义客户路径**（最高优先级）
   - 通过 `-c` 参数指定的板子
   - 示例：`idf.py gen-bmgr-config -b my_board -c /path/to/custom/boards`

2. **用户项目组件**（中等优先级）
   - 项目组件中的板子：`{PROJECT_ROOT}/components/{component_name}/boards`
   - 这些会覆盖同名的主板

3. **主板级目录**（最低优先级）
   - 内置板子：`esp_board_manager/boards`
   - 当没有其他版本时作为后备使用

**重要说明：**
- **无重复警告**：系统会静默使用高优先级板子，不会警告重复项

## `custom` 自定义设备说明

对于 esp_board_manager 还未包含的设备和外设，建议通过 `custom` 类型 device 进行添加

- 将设备的 `init` 和 `deinit` 函数代码放到开发板路径下，通过 `CUSTOM_DEVICE_IMPLEMENT("device_name", init_func, deinit_func)` 将设备注册到板级管理器中

- 在开发板路径 `board_devices.yaml` 中添加自定义设备的配置，其中 `type` 需要配置成 `custom`

- 执行命令 `idf.py gen-bmgr-config -b xxx` 生成开发板的配置代码后，在 `components/gen_bmgr_codes` 路径下会生成 `gen_board_device_custom.h` 头文件，供应用程序使用

- 可以参考 [`m5stack_cores3`](../boards/m5stack_cores3/power_manager.c) 。M5stack_cores3 使用自定义类型的设备定义了一个电源管理芯片，实现了 init 和 deinit 函数。

## 验证和使用新板子

**验证板子配置**

   ```bash
   # 测试您的板子配置是否有效
   # 确保 IDF_EXTRA_ACTIONS_PATH 已进行设置并仍然有效
   # 对于主板级和用户项目组件（默认路径）
   idf.py gen-bmgr-config -b <board_name>

   # 对于自定义客户路径（外部位置）
   idf.py gen-bmgr-config -b <board_name> -c /path/to/custom/boards

   # 或使用独立脚本
   cd $YOUR_ESP_BOARD_MANAGER_PATH
   python gen_bmgr_config_codes.py -b <board_name>
   python gen_bmgr_config_codes.py -b <board_name> -c /path/to/custom/boards
   ```

   **验证配置成功生成**: 如果板子配置有效，将在工程路径中生成以下文件：
   - `components/gen_bmgr_codes/gen_board_periph_handles.c` - 外设句柄定义
   - `components/gen_bmgr_codes/gen_board_periph_config.c` - 外设配置结构
   - `components/gen_bmgr_codes/gen_board_device_handles.c` - 设备句柄定义
   - `components/gen_bmgr_codes/gen_board_device_config.c` - 设备配置结构
   - `components/gen_bmgr_codes/gen_board_info.c` - 板子元数据
   - `components/gen_bmgr_codes/CMakeLists.txt` - 构建系统配置
   - `components/gen_bmgr_codes/idf_component.yml` - 组件依赖关系

   **如果出现错误**: 检查您的 YAML 文件是否有语法错误、缺失字段或无效配置。

> **注意**：当您首次运行 `idf.py gen-bmgr-config` 时，脚本将自动为选定的开发板生成 Kconfig 配置。
