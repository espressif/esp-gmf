# Migrating `dev_lcd_touch_i2c` to `dev_lcd_touch`

`dev_lcd_touch_i2c` is the legacy I2C touch device type. Starting from `esp_board_manager` v0.5.10, use the generic `dev_lcd_touch` device and select the I2C implementation with `sub_type: i2c`.

The legacy type is still kept for compatibility, but it is deprecated. New and maintained board definitions should use `type: lcd_touch` with `sub_type: i2c`.

## Why Migrate

- Unified device model: touch devices use the common `lcd_touch` type, and the bus type is represented by `sub_type`.
- Multiple address probing: I2C addresses move from the old single `io_i2c_config.dev_addr` field to root-level `peripherals[].i2c_addr`, with up to 4 candidate addresses.
- Runtime effective-address query: applications and board factory code can call `esp_board_device_get_i2c_effective_addr()` to read the selected 8-bit address.

## YAML Migration

### Old Style

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

### New Style

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

## Field Mapping

| Old Field | New Field | Notes |
|---|---|---|
| `type: lcd_touch_i2c` | `type: lcd_touch` + `sub_type: i2c` | Device type is unified as `lcd_touch`; bus type moves to `sub_type` |
| `config.io_i2c_config.dev_addr` | `peripherals[].i2c_addr` | New field uses an 8-bit left-shifted address |
| `config.io_i2c_config.peripherals[].name` | `peripherals[].name` | I2C peripheral dependency moves to root-level `peripherals` |
| `config.io_i2c_config.*` | `config.io_i2c_config.*` | Keep all fields except `dev_addr` and nested `peripherals` |
| `config.touch_config.*` | `config.touch_config.*` | Unchanged |
| `dependencies` | `dependencies` | Unchanged |

## I2C Address Rules

The new `lcd_touch` `peripherals[].i2c_addr` field uses 8-bit left-shifted addresses.

Examples:

- 7-bit address `0x15` should be written as `0x2a`
- 7-bit address `0x24` should be written as `0x48`
- 7-bit address `0x5d` should be written as `0xba`

If a board may use different touch chips, list multiple candidate addresses:

```yaml
peripherals:
  - name: i2c_master
    i2c_addr: [0xba, 0x48]
```

Board Manager probes the addresses in order and records the matched 8-bit address.

## Migrating Board `setup_device.c`

The touch factory signature does not change:

```c
esp_err_t lcd_touch_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_touch_config_t *touch_dev_config,
                                    esp_lcd_touch_handle_t *ret_touch)
```

If the same board needs to select different touch drivers based on the probed address, use:

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

`esp_board_device_get_i2c_effective_addr()` returns the 8-bit left-shifted address, matching the YAML `i2c_addr` semantics.

## Application Compatibility Notes

Migrated applications should use the new generic touch and I2C sub-type switches:

```c
#if CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUPPORT && CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUB_I2C_SUPPORT
// enable touch-related code
#endif
```

Do not keep using the legacy `CONFIG_ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT` switch in migrated code. It exists only as a temporary compatibility aid for old applications and will be removed in a future release.

The old macro also does not mean the legacy `dev_lcd_touch_i2c` structs are valid on the new device path. Do not continue using:

```c
dev_lcd_touch_i2c_config_t
dev_lcd_touch_i2c_handles_t
```

Use the new types instead:

```c
dev_lcd_touch_config_t
dev_lcd_touch_handles_t
```

The legacy `dev_lcd_touch_i2c_config_t` and the new `dev_lcd_touch_config_t` have different layouts and cannot be safely cast. The legacy `dev_lcd_touch_i2c_handles_t` and the new `dev_lcd_touch_handles_t` currently start with the same `touch_handle` and `io_handle` fields, but applications should not rely on that layout coincidence.

### Header Migration

If old code directly includes the legacy header:

```c
#include "dev_lcd_touch_i2c.h"
```

Migrate it to:

```c
#include "dev_lcd_touch.h"
```

If the application includes:

```c
#include "esp_board_manager_includes.h"
```

the include may not need to change, but struct types still need to be migrated as shown below.

### Handle Migration

Old code:

```c
void *touch_handle = NULL;
ESP_ERROR_CHECK(esp_board_manager_get_device_handle("lcd_touch", &touch_handle));

dev_lcd_touch_i2c_handles_t *touch = (dev_lcd_touch_i2c_handles_t *)touch_handle;
esp_lcd_touch_handle_t tp = touch->touch_handle;
esp_lcd_panel_io_handle_t io = touch->io_handle;
```

New code:

```c
void *touch_handle = NULL;
ESP_ERROR_CHECK(esp_board_manager_get_device_handle("lcd_touch", &touch_handle));

dev_lcd_touch_handles_t *touch = (dev_lcd_touch_handles_t *)touch_handle;
esp_lcd_touch_handle_t tp = touch->touch_handle;
esp_lcd_panel_io_handle_t io = touch->io_handle;
```

### Config Migration

Old code:

```c
dev_lcd_touch_i2c_config_t *cfg = NULL;
ESP_ERROR_CHECK(esp_board_manager_get_device_config("lcd_touch", (void **)&cfg));

const char *i2c_name = cfg->i2c_name;
uint16_t primary_addr = cfg->i2c_addr[0];
uint16_t runtime_addr = cfg->io_i2c_config.dev_addr;
```

New code:

```c
dev_lcd_touch_config_t *cfg = NULL;
ESP_ERROR_CHECK(esp_board_manager_get_device_config("lcd_touch", (void **)&cfg));

const char *i2c_name = cfg->sub_cfg.i2c.i2c_name;
size_t addr_count = cfg->sub_cfg.i2c.i2c_addr_count;
const uint16_t *addr_candidates = cfg->sub_cfg.i2c.i2c_addr;
```

Note: `cfg->sub_cfg.i2c.i2c_addr[]` is the candidate address list, not necessarily the matched runtime address. If the application needs the actual selected touch address, use the effective-address query API.

### Effective Address Migration

Old code may read `io_i2c_config.dev_addr` or `i2c_addr[0]` from the legacy config to identify the touch chip. Migrate it to:

```c
uint16_t touch_addr = 0;
esp_err_t ret = esp_board_device_get_i2c_effective_addr("lcd_touch", &touch_addr);
if (ret == ESP_OK) {
    // touch_addr is the selected 8-bit / left-shifted address
}
```

When branching by address, compare 8-bit left-shifted addresses such as `0xba` or `0x48`, not right-shifted 7-bit addresses.

### Compile Condition Migration

If code must temporarily support both legacy and migrated boards, select the struct type by device type and keep the legacy branch isolated:

```c
#if CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUPPORT
dev_lcd_touch_handles_t *touch = (dev_lcd_touch_handles_t *)touch_handle;
#elif CONFIG_ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT
dev_lcd_touch_i2c_handles_t *touch = (dev_lcd_touch_i2c_handles_t *)touch_handle;
#endif
```

For boards already migrated to `type: lcd_touch`, use the `CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUPPORT` branch and the `CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUB_I2C_SUPPORT` sub-type switch. Do not keep migrated code on the legacy branch.

## Validation Steps

1. Run `idf.py bmgr -b <board_name>` to regenerate the board configuration.
2. Confirm the generation output does not contain a legacy-type migration warning. If a `lcd_touch_i2c` deprecated warning still appears, some YAML still uses the legacy type.
3. Check `components/gen_bmgr_codes/board_manager.defaults` contains:

```ini
CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUPPORT=y
CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUB_I2C_SUPPORT=y
```

4. Confirm migrated application code does not use `CONFIG_ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT` or legacy `dev_lcd_touch_i2c_*` types.
5. Build the project and confirm there is no deprecated compile warning from the legacy `dev_lcd_touch_i2c.h` header.
6. If board factory code branches by touch-chip address, confirm `esp_board_device_get_i2c_effective_addr()` returns the expected 8-bit address.

## Assisted Migration Tool

The repository provides an optional AI Skill to help migrate from `dev_lcd_touch_i2c` to `dev_lcd_touch`. The Skill guides an AI assistant through checking board YAML files, `setup_device.c`, and application-side legacy type or macro usage, then follows the field mapping and validation steps described in this document.

Read this guide first to understand the address format, compatibility macros, and application-code migration requirements. If you want an AI assistant to perform or review the migration, use [`lcd-touch-i2c-migration`](../tools/AI_SKILLS/lcd_touch_i2c_migration/SKILL.md).

Related links:

- Chinese migration guide: [`dev_lcd_touch_i2c` 迁移到 `dev_lcd_touch`](lcd_touch_i2c_migration_cn.md)
- AI migration Skill: [`lcd-touch-i2c-migration`](../tools/AI_SKILLS/lcd_touch_i2c_migration/SKILL.md)
