---
name: lcd-touch-i2c-migration
description: Use when migrating ESP Board Manager board definitions or application code from legacy dev_lcd_touch_i2c / type lcd_touch_i2c to generic dev_lcd_touch with type lcd_touch and sub_type i2c, including YAML field mapping, setup_device.c touch factory updates, and compatibility checks.
---

# LCD Touch I2C Migration

Use this skill to migrate ESP Board Manager configurations from legacy `lcd_touch_i2c` to generic `lcd_touch` with `sub_type: i2c`.

## Migration Goals

- Board YAML uses `type: lcd_touch` and `sub_type: i2c`.
- I2C address candidates live at device root `peripherals[].i2c_addr`.
- `config.io_i2c_config.dev_addr` and nested `config.io_i2c_config.peripherals` are removed.
- APP code uses `dev_lcd_touch_config_t` / `dev_lcd_touch_handles_t`, not legacy `dev_lcd_touch_i2c_*` types.
- APP code uses `CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUPPORT` plus `CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUB_I2C_SUPPORT` for migrated I2C touch feature checks. Do not introduce or keep legacy `CONFIG_ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT` checks in migrated code.

## Before Editing

1. Inspect the target board files:
   - `board_devices.yaml`
   - `board_peripherals.yaml`
   - optional `setup_device.c`
   - application files that include `dev_lcd_touch_i2c.h` or use `dev_lcd_touch_i2c_*`
2. Search:
   - `type: lcd_touch_i2c`
   - `dev_lcd_touch_i2c`
   - `CONFIG_ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT`
   - `ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT`
3. Preserve user edits and unrelated board content.

## YAML Mapping

Old:

```yaml
- name: lcd_touch
  type: lcd_touch_i2c
  config:
    io_i2c_config:
      dev_addr: 0x15
      peripherals:
        - name: i2c_master
    touch_config: {}
```

New:

```yaml
- name: lcd_touch
  type: lcd_touch
  sub_type: i2c
  config:
    io_i2c_config: {}
    touch_config: {}
  peripherals:
    - name: i2c_master
      i2c_addr: 0x2a
```

Rules:

- Change `type: lcd_touch_i2c` to `type: lcd_touch`.
- Add `sub_type: i2c`.
- Move nested I2C peripheral entries from `config.io_i2c_config.peripherals` to root-level device `peripherals`.
- Move `config.io_i2c_config.dev_addr` to root-level `peripherals[].i2c_addr`.
- Remove `dev_addr` from `config.io_i2c_config`.
- Keep `chip`, `dependencies`, `config.touch_config`, and other `config.io_i2c_config` fields.
- If old `dev_addr` was a list, migrate it to `i2c_addr` list.
- If the old address appears to be 7-bit, convert it to 8-bit left-shifted form. Examples: `0x15 -> 0x2a`, `0x24 -> 0x48`, `0x5d -> 0xba`.
- New `i2c_addr` must be 8-bit left-shifted, even, and no larger than `0xfe`.

## setup_device.c Rules

The factory signature remains:

```c
esp_err_t lcd_touch_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_touch_config_t *touch_dev_config,
                                    esp_lcd_touch_handle_t *ret_touch)
```

If the board chooses a touch driver based on detected address:

- Include `esp_board_device.h`.
- Call `esp_board_device_get_i2c_effective_addr("<touch_device_name>", &touch_addr)`.
- Compare against 8-bit left-shifted addresses from YAML.
- Do not read old `dev_lcd_touch_i2c_config_t` fields to decide the chip.

Example:

```c
uint16_t touch_addr = 0;
esp_err_t ret = esp_board_device_get_i2c_effective_addr("lcd_touch", &touch_addr);
if (ret != ESP_OK) {
    return ret;
}
if (touch_addr == 0xba) {
    return esp_lcd_touch_new_i2c_gt911(io, touch_dev_config, ret_touch);
}
```

## Application Compatibility Checks

Legacy code may use this old feature switch:

```c
#if CONFIG_ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT
```

Treat it as a migration target. Replace it in migrated code with the new generic device and sub-type checks:

```c
#if CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUPPORT && CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUB_I2C_SUPPORT
```

Flag and migrate code that:

- Includes `dev_lcd_touch_i2c.h` for a board that now uses `lcd_touch`.
- Casts handles to `dev_lcd_touch_i2c_handles_t`.
- Casts configs to `dev_lcd_touch_i2c_config_t`.
- Reads old fields such as `i2c_name`, `i2c_addr`, or `io_i2c_config.dev_addr` through a legacy config struct.
- Uses `CONFIG_ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT` or `ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT` in migrated application code.

Use:

```c
dev_lcd_touch_handles_t
dev_lcd_touch_config_t
```

Migration patterns:

- Replace `#include "dev_lcd_touch_i2c.h"` with `#include "dev_lcd_touch.h"` for migrated boards.
- Replace `dev_lcd_touch_i2c_handles_t *touch = (dev_lcd_touch_i2c_handles_t *)touch_handle;` with `dev_lcd_touch_handles_t *touch = (dev_lcd_touch_handles_t *)touch_handle;`.
- Replace `dev_lcd_touch_i2c_config_t *cfg` with `dev_lcd_touch_config_t *cfg`.
- Map old config fields:
  - `cfg->i2c_name` -> `cfg->sub_cfg.i2c.i2c_name`
  - `cfg->i2c_addr` -> `cfg->sub_cfg.i2c.i2c_addr`
  - address count -> `cfg->sub_cfg.i2c.i2c_addr_count`
- Do not use `cfg->io_i2c_config.dev_addr` to identify the active chip; call `esp_board_device_get_i2c_effective_addr("<touch_device_name>", &addr)` and compare 8-bit addresses.
- If code must temporarily support both old and new boards, branch on `CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUPPORT` first and keep any `CONFIG_ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT` fallback isolated for legacy boards only.

Only keep legacy types for boards that intentionally still use `type: lcd_touch_i2c`.

## Validation

After migration:

1. Run Board Manager generation for the board:
   - `idf.py bmgr -b <board>`
   - or `python3 gen_bmgr_config_codes.py -b <board>` from the test app context.
2. Confirm generation has no `lcd_touch_i2c` deprecated parser warning.
3. Confirm `components/gen_bmgr_codes/board_manager.defaults` has:
   - `CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUPPORT=y`
   - `CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUB_I2C_SUPPORT=y`
4. Confirm migrated application code does not use `CONFIG_ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT` or legacy `dev_lcd_touch_i2c_*` types.
5. Build if an ESP-IDF environment is available.
6. Confirm there is no compile warning from legacy `dev_lcd_touch_i2c.h` for migrated boards.

## Reporting

Summarize:

- Files changed.
- YAML address conversions performed.
- Any application code that still uses legacy types.
- Generation/build commands run and their result.
