# ESP Board Manager 设备与外设配置规则

## 0. `version` 字段：解析契约（Schema 版本）

对于**用户开发板** YAML，`version`（作为解析契约使用时）表示适用的**Board Manager 配置规则版本**——而非驱动 IC 的版本号或二进制发布版本号。

**当前版本标签：** **`1.0.0`** 匹配本版 Board Manager 所解析的 Schema；**省略** `version` 或使用 **`default`**（不区分大小写）即为当前代次的**别名**。
**破坏性 YAML 变更**将来可能引入**新的**标签；本版本对无法识别的标签会发出**警告**（请使用当前标签、`default` 或移除该字段）——这并不承诺只会存在 `1.0.0`。

- **另一种含义：** 在 `dependencies` 下，组件上的 `version` 是 **ESP-IDF Component Manager** 的版本约束（例如 `"*"`、`~1.5`）。

## 1. YAML 文件通用结构

### 1.1 开发板级配置

开发板配置 YAML 文件 `board_info.yaml` 必须包含：

```yaml
board: <board_name>
chip: <chip_type>
version: <version>    # Schema 版本标签
```

### 1.2 文件组织

- 外设配置定义在 `board_peripherals.yaml` 中
- 设备配置定义在 `board_devices.yaml` 中
- 两个文件都应放置在 `boards/` 下对应开发板的目录中

### 1.3 配置关键字

设备/外设配置模板（`*.yml` / `*.yaml`）中，对部分配置项的注释里添加了以下关键字标记：

- **`[IO]`** — 表示该配置项是一个 **GPIO 引脚号**，必须根据实际硬件原理图填写正确的引脚编号。常见于：
  - 外设模板中的引脚定义（如 I2C 的 `sda`/`scl`、I2S 的 `mclk`/`bclk`/`ws`/`din`/`dout`、SPI 的 `mosi`/`miso`/`sclk`、UART 的 `tx`/`rx` 等）
  - 设备模板中的 GPIO 引脚（如 LCD 的 `reset_gpio_num`、`cs_gpio_num`、`dc_gpio_num` 等）
  - 默认值通常为 `-1`（未配置）

- **`[TO_BE_CONFIRMED]`** — 表示该配置项的默认值是一个**占位值或通用值**，需要根据实际硬件确认后修改。常见于：
  - 芯片型号（如 `chip: es8311`、`chip: generic_lcd`）
  - 功能开关（如 `adc_enabled`、`dac_enabled`）
  - 音视频参数（如 `sample_rate_hz`、`data_bit_width`、`slot_mode`）
  - 显示参数（如分辨率 `h_size`/`v_size`、像素格式、色彩格式等）
  - 外设引用名称（如 `name: i2s_audio_out`）
  - 组件依赖版本

> **适配新开发板时，应重点检查所有带 `[IO]` 和 `[TO_BE_CONFIRMED]` 标记的配置项，确保它们与实际硬件匹配。**

## 2. 外设配置规则（`board_peripherals.yaml`）

### 2.1 必填字段

每个外设必须包含以下必填字段：

```yaml
peripherals:
  - name: <peripheral_name>    # 必填：唯一标识符
    type: <peripheral_type>    # 必填：类型标识符
    version: <version>         # 可选：使用的解析器版本
    role: <role>               # 条件必填：工作模式（如 master/slave、tx/rx、continuous/oneshot 等，取决于外设类型）
    format: <format_string>    # 条件必填：数据格式（当前仅 I2S 使用，如 std-out、tdm-in、pdm-out）
    config: <configuration>    # 必填：外设专属配置
```

### 2.2 名称格式规则

- 必须以 type 作为命名前缀
- 唯一且无歧义的字符串，小写字符，可以是数字和字母的组合，但不能仅为数字
- 示例名称：`gpio_lcd_reset`、`gpio_pa_control`、`gpio_power_audio`
- 在配置中必须唯一
- 名称中可以包含设备芯片信息以提高可读性

### 2.3 类型格式规则

- 在配置中必须唯一
- 格式：小写字母、数字和下划线
- 不能仅为数字
- 示例：`i2c`、`i2s`、`spi`、`audio_codec`、`aa_bb3_c0`

### 2.4 工作模式与数据格式规则

- `role`：定义外设的工作模式（如 master/slave、tx/rx、continuous/oneshot 等，取决于外设类型）
- `format`：使用连字符分隔的值来定义数据格式
- 示例：
  - I2S 格式：`std-out`、`std-in`、`tdm-out`、`tdm-in`、`pdm-out`、`pdm-in`

## 3. 设备配置规则（`board_devices.yaml`）

### 3.1 必填字段

```yaml
devices:
  - name: <device_name>       # 必填：唯一标识符
    type: <device_type>       # 必填：类型标识符
    chip: <chip_name>         # 条件必填：设备芯片名称（如 LCD 芯片、IO 扩展芯片等）
    version: <version>        # 可选：使用的解析器版本
    sub_type: <sub_type>      # 条件必填：部分设备存在子类型（如 LCD 分为 SPI、DSI、ParlIO）
    init_skip: false          # 可选：管理器初始化所有设备时是否跳过该设备的初始化。
                              # 默认为 false（不跳过初始化）。设置为 true 则跳过自动初始化。
    config:
      <configurations>        # 必填：设备专属配置
    peripherals:              # 条件必填：部分设备运行依赖指定的外设，如I2C，SPI，GPIO 等
      - name: <periph_name>   # 条件必填：若 peripherals 存在则填写对应 peripheral 的 name
    dependencies:             # 条件必填：部分设备依赖额外的组件（如 LCD、LCD Touch 等）
      <component_name>:
        require: <scope>
        version: <version_spec>
```

### 3.2 名称格式规则

- 比外设名称更加灵活，不要求 type 作为命名前缀
- 唯一且无歧义的字符串，小写字符，可以是数字和字母的组合，但不能仅为数字
- 示例名称：`audio_dac`、`audio_adc`、`lcd_power`、`display_lcd`
- 在配置中必须唯一
- 名称中可以包含设备芯片信息以提高可读性

### 3.3 类型格式规则

- 在配置中必须唯一
- 格式：小写字母、数字和下划线
- 字符可以用下划线分隔
- 不能仅为数字
- 示例：`gpio_ctrl`、`audio_codec`、`lcd_touch_i2c`

### 3.4 外设引用

- 可以引用 `board_peripherals.yaml` 中定义的外设
- 必须使用精确的外设名称
- 可以为外设附加额外的配置
- 支持 YAML 锚点和引用以实现复用

### 3.5 依赖项

- 可选的组件依赖规范
- 格式：

```yaml
dependencies:
  <component_name>:
    require: public|private
    version: <version_spec>
```

## 4. 配置继承与复用

### 4.1 YAML 锚点与引用

- 使用 `&anchor_name` 创建可复用的配置
- 使用 `*anchor_name` 引用配置
- 示例：

```yaml
config: &base_config
  setting1: value1
  setting2: value2

devices:
  - name: device1
    config: *base_config
  - name: device2
    config:
      <<: *base_config
      setting3: value3
```

### 4.2 设备侧外设附加参数

设备在 `peripherals` 列表中引用外设时，除了必须的 `name` 字段外，还可以携带**设备专属的附加参数**。这些附加参数与外设本身的 `config` 是互补关系，而非覆盖关系。

**外设定义**（`board_peripherals.yaml`）提供总线/硬件层配置，例如：

- I2C 外设：`port`、`pins`、`clk_source` 等
- GPIO 外设：`pin`、`mode`、`intr_type` 等

**设备侧附加参数**（`board_devices.yaml` 中 `peripherals` 条目上的额外字段）提供设备专属信息，例如：

- I2C 从机地址 `address`、通信频率 `frequency`
- PA 控制的增益 `gain`、有效电平 `active_level`

示例：

```yaml
# board_devices.yaml 中的 audio_dac 设备
devices:
  - name: audio_dac
    type: audio_codec
    peripherals:
      - name: i2c_master
        address: 0x30          # 设备专属：该芯片的 I2C 从机地址
        frequency: 400000      # 设备专属：期望的通信频率
      - name: gpio_power_amp
        gain: 6                # 设备专属：PA 增益
        active_level: 1        # 设备专属：PA 使能的有效电平
```

> **注意：** 这些附加参数由对应的设备解析器（`devices/dev_*/dev_*.py`）负责读取。
> 外设定义中并不包含这些字段，它们不会对外设自身的配置产生影响。

## 5. 枚举值使用规则

### 5.1 基本原则

- 直接使用 ESP-IDF 驱动及相关组件中的枚举值
- 不要使用自定义字符串或数字；使用驱动头文件中的原始枚举定义
- 枚举值区分大小写，必须与头文件定义完全匹配

### 5.2 如何查找枚举值

1. 首先检查对应外设/设备的 IDF 驱动头文件
2. 检查设备组件的头文件定义
3. 参考示例配置文件中的枚举用法

### 5.3 重要注意事项

- 即使数值等价，也不要用数字替代枚举值
- 不要定义新的枚举值
- 如果找不到合适的枚举值，请检查驱动版本是否匹配
- 建议添加注释说明枚举值的含义

### 5.4 示例

```yaml
# ✅ 正确示例 - 使用原始枚举值
gpio:
  mode: GPIO_MODE_OUTPUT
  intr_type: GPIO_INTR_DISABLE

# ❌ 错误示例 - 使用自定义值
gpio:
  mode: "output"          # 错误：应使用 GPIO_MODE_OUTPUT
  intr_type: 0           # 错误：应使用 GPIO_INTR_DISABLE
```
