# `dev_lcd_touch_i2c` 迁移到 `dev_lcd_touch`

`dev_lcd_touch_i2c` 是旧的 I2C 触摸设备类型。自 `esp_board_manager` v0.5.10 起，建议统一使用通用触摸设备 `dev_lcd_touch`，并通过 `sub_type: i2c` 声明 I2C 实现。

旧类型仍保留兼容，但会逐步废弃。新增或维护板级配置时，请优先迁移到 `type: lcd_touch` + `sub_type: i2c`。

## 为什么迁移

- 统一设备模型：触摸设备统一归类为 `lcd_touch`，总线类型由 `sub_type` 表达。
- 支持多地址探测：I2C 地址从旧的单一 `io_i2c_config.dev_addr` 迁移到设备级 `peripherals[].i2c_addr`，最多支持 4 个候选地址。
- 支持运行时查询有效地址：可通过 `esp_board_device_get_i2c_effective_addr()` 获取最终探测到的 8-bit 地址。

## YAML 迁移

### 旧写法

```yaml
- name: lcd_touch
  chip: cst816s
  type: lcd_touch_i2c
  dependencies:
    espressif/esp_lcd_touch_cst816s: "*"
  config:
    io_i2c_config:
      dev_addr: 0x15
      lcd_cmd_bits: 8
      flags:
        disable_control_phase: true
      peripherals:
        - name: i2c_master
    touch_config:
      x_max: 284
      y_max: 240
      rst_gpio_num: -1
      int_gpio_num: 3
      flags:
        swap_xy: true
        mirror_x: false
        mirror_y: true
```

### 新写法

```yaml
- name: lcd_touch
  chip: cst816s
  type: lcd_touch
  sub_type: i2c
  dependencies:
    espressif/esp_lcd_touch_cst816s: "*"
  config:
    io_i2c_config:
      lcd_cmd_bits: 8
      flags:
        disable_control_phase: true
    touch_config:
      x_max: 284
      y_max: 240
      rst_gpio_num: -1
      int_gpio_num: 3
      flags:
        swap_xy: true
        mirror_x: false
        mirror_y: true
  peripherals:
    - name: i2c_master
      i2c_addr: 0x2a
```

## 字段对应关系

| 旧字段 | 新字段 | 说明 |
|---|---|---|
| `type: lcd_touch_i2c` | `type: lcd_touch` + `sub_type: i2c` | 设备类型统一为 `lcd_touch`，总线类型放到 `sub_type` |
| `config.io_i2c_config.dev_addr` | `peripherals[].i2c_addr` | 新字段使用 8-bit 左移地址 |
| `config.io_i2c_config.peripherals[].name` | `peripherals[].name` | I2C 外设依赖移到设备根级 `peripherals` |
| `config.io_i2c_config.*` | `config.io_i2c_config.*` | 除 `dev_addr` 和嵌套 `peripherals` 外，其余字段保留 |
| `config.touch_config.*` | `config.touch_config.*` | 保持不变 |
| `dependencies` | `dependencies` | 保持不变 |

## I2C 地址规则

新 `lcd_touch` 的 `peripherals[].i2c_addr` 使用 8-bit 左移地址。

例如：

- 7-bit 地址 `0x15` 应写为 `0x2a`
- 7-bit 地址 `0x24` 应写为 `0x48`

如果一个板子可能贴不同触摸芯片，可以写多个候选地址：

```yaml
peripherals:
  - name: i2c_master
    i2c_addr: [0xba, 0x48]
```

Board Manager 会按顺序探测地址，并记录最终命中的 8-bit 地址。

## 板级 `setup_device.c` 迁移

触摸工厂函数签名保持不变：

```c
esp_err_t lcd_touch_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_touch_config_t *touch_dev_config,
                                    esp_lcd_touch_handle_t *ret_touch)
```

如果同一个板子需要根据探测到的地址选择不同触摸驱动，请使用：

```c
uint16_t touch_addr = 0;
esp_err_t ret = esp_board_device_get_i2c_effective_addr("lcd_touch", &touch_addr);
if (ret != ESP_OK) {
    return ret;
}

if (touch_addr == 0xba) {
    return esp_lcd_touch_new_i2c_gt911(io, touch_dev_config, ret_touch);
}

if (touch_addr == 0x48) {
    return esp_lcd_touch_new_i2c_tt21100(io, touch_dev_config, ret_touch);
}
```

`esp_board_device_get_i2c_effective_addr()` 返回的是 8-bit 左移地址，与 YAML 中的 `i2c_addr` 语义一致。

## APP 兼容注意事项

迁移后的 APP 应使用新的通用触摸设备宏和 I2C 子类型宏：

```c
#if CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUPPORT && CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUB_I2C_SUPPORT
// enable touch-related code
#endif
```

不要在迁移后的代码里继续使用旧的 `CONFIG_ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT`。它只作为旧 APP 的临时兼容手段保留，后续版本会删除。

旧宏也不表示旧的 `dev_lcd_touch_i2c` 结构体在新设备路径下仍然可用。迁移后不要继续使用旧类型：

```c
dev_lcd_touch_i2c_config_t
dev_lcd_touch_i2c_handles_t
```

新设备应使用：

```c
dev_lcd_touch_config_t
dev_lcd_touch_handles_t
```

旧 `dev_lcd_touch_i2c_config_t` 与新 `dev_lcd_touch_config_t` 结构布局不同，不能安全强转。旧 `dev_lcd_touch_i2c_handles_t` 和新 `dev_lcd_touch_handles_t` 虽然前两个字段都是 `touch_handle` 和 `io_handle`，但也不应依赖这种布局巧合继续强转。

### 头文件迁移

旧代码如果直接包含旧头文件：

```c
#include "dev_lcd_touch_i2c.h"
```

迁移为：

```c
#include "dev_lcd_touch.h"
```

如果应用统一包含：

```c
#include "esp_board_manager_includes.h"
```

通常无需额外修改 include，但代码中的结构体类型仍需要按下面迁移。

### Handle 迁移

旧代码：

```c
void *touch_handle = NULL;
ESP_ERROR_CHECK(esp_board_manager_get_device_handle("lcd_touch", &touch_handle));

dev_lcd_touch_i2c_handles_t *touch = (dev_lcd_touch_i2c_handles_t *)touch_handle;
esp_lcd_touch_handle_t tp = touch->touch_handle;
esp_lcd_panel_io_handle_t io = touch->io_handle;
```

新代码：

```c
void *touch_handle = NULL;
ESP_ERROR_CHECK(esp_board_manager_get_device_handle("lcd_touch", &touch_handle));

dev_lcd_touch_handles_t *touch = (dev_lcd_touch_handles_t *)touch_handle;
esp_lcd_touch_handle_t tp = touch->touch_handle;
esp_lcd_panel_io_handle_t io = touch->io_handle;
```

### Config 迁移

旧代码：

```c
dev_lcd_touch_i2c_config_t *cfg = NULL;
ESP_ERROR_CHECK(esp_board_manager_get_device_config("lcd_touch", (void **)&cfg));

const char *i2c_name = cfg->i2c_name;
uint16_t primary_addr = cfg->i2c_addr[0];
uint16_t runtime_addr = cfg->io_i2c_config.dev_addr;
```

新代码：

```c
dev_lcd_touch_config_t *cfg = NULL;
ESP_ERROR_CHECK(esp_board_manager_get_device_config("lcd_touch", (void **)&cfg));

const char *i2c_name = cfg->sub_cfg.i2c.i2c_name;
size_t addr_count = cfg->sub_cfg.i2c.i2c_addr_count;
const uint16_t *addr_candidates = cfg->sub_cfg.i2c.i2c_addr;
```

注意：新配置中的 `cfg->sub_cfg.i2c.i2c_addr[]` 是候选地址列表，不一定是最终命中的地址。如果 APP 需要知道当前实际使用的触摸地址，请使用有效地址查询 API。

### 有效地址查询迁移

旧代码可能从旧 config 中读取 `io_i2c_config.dev_addr` 或 `i2c_addr[0]` 来判断触摸芯片。迁移后请改为：

```c
uint16_t touch_addr = 0;
esp_err_t ret = esp_board_device_get_i2c_effective_addr("lcd_touch", &touch_addr);
if (ret == ESP_OK) {
    // touch_addr is the selected 8-bit / left-shifted address
}
```

如果需要根据地址分支选择行为，请比较 8-bit 左移地址，例如 `0xba`、`0x48`，不要比较右移后的 7-bit 地址。

### 编译条件迁移

如果代码短期内必须同时兼容旧板子和新板子，可以按设备类型区分结构体，并把旧分支隔离清楚：

```c
#if CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUPPORT
dev_lcd_touch_handles_t *touch = (dev_lcd_touch_handles_t *)touch_handle;
#elif CONFIG_ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT
dev_lcd_touch_i2c_handles_t *touch = (dev_lcd_touch_i2c_handles_t *)touch_handle;
#endif
```

对于已经迁移到 `type: lcd_touch` 的板子，请使用 `CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUPPORT` 分支，并结合 `CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUB_I2C_SUPPORT` 判断 I2C 子类型。不要让迁移后的代码继续走旧分支。

## 验证步骤

1. 运行 `idf.py bmgr -b <board_name>` 重新生成配置。
2. 确认生成过程没有旧类型迁移警告；如果仍出现 `lcd_touch_i2c` deprecated warning，说明还有旧 YAML 未迁移。
3. 检查 `components/gen_bmgr_codes/board_manager.defaults` 中包含：

```ini
CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUPPORT=y
CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUB_I2C_SUPPORT=y
```

4. 确认迁移后的 APP 代码不再使用 `CONFIG_ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT` 或旧 `dev_lcd_touch_i2c_*` 类型。
5. 编译工程，确认不再包含旧 `dev_lcd_touch_i2c.h` 的 deprecated 编译警告。
6. 如果板级 factory 根据触摸芯片地址分支，确认 `esp_board_device_get_i2c_effective_addr()` 能返回预期的 8-bit 地址。

## 辅助迁移工具

仓库内提供了一个可选的 AI Skill，用于辅助完成 `dev_lcd_touch_i2c` 到 `dev_lcd_touch` 的迁移。该 Skill 会引导 AI 按固定流程检查板级 YAML、`setup_device.c` 以及 APP 侧旧类型/旧宏的使用，并按照本文档的字段映射和验证步骤完成迁移。

建议先阅读本文档，明确地址格式、兼容宏和 APP 代码迁移要求；如果希望让 AI 助手协助执行或复查迁移，可使用 [`lcd-touch-i2c-migration`](../tools/AI_SKILLS/lcd_touch_i2c_migration/SKILL.md)。

相关链接：

- 英文迁移说明：[Migrating `dev_lcd_touch_i2c` to `dev_lcd_touch`](lcd_touch_i2c_migration.md)
- AI 辅助迁移 Skill：[`lcd-touch-i2c-migration`](../tools/AI_SKILLS/lcd_touch_i2c_migration/SKILL.md)
