# ESP Board Manager Device and Peripheral Configuration Rules

## 0. The `version` field: parsing contract (schema version)

For **user board** YAML, `version` (when used as a parsing contract) means **which Board Manager configuration schema generation** applies—not a driver IC revision or a binary release number.

**Current generation tag:** **`1.0.0`** matches the schema this Board Manager release parses; **omitting** `version` or using **`default`** (case-insensitive) is equivalent to the current generation.
**Breaking YAML changes** may introduce **new** tags later; this release **warns** on unrecognized tags (use the current tag, `default`, or remove the field) — that is not a promise that only `1.0.0` will ever exist.

- **Different meaning:** under `dependencies`, `version` on a component is an **ESP-IDF Component Manager** dependency constraint (e.g. `"*"`, `~1.5`).

## 1. General YAML File Structure

### 1.1 Board Level Configuration
Board configuration YAML file `board_info.yaml` must include:
```yaml
board: <board_name>
chip: <chip_type>
version: <version>    # Schema generation tag
```

### 1.2 File Organization
- Peripheral configurations are defined in `board_peripherals.yaml`
- Device configurations are defined in `board_devices.yaml`
- Both files should be placed in the board's directory under `boards/`

### 1.3 Configuration Keywords

The device/peripheral configuration templates (`*.yml` / `*.yaml`) use the following keyword tags in comments for certain configuration items:

- **`[IO]`** — Indicates the item is a **GPIO pin number** that must be set according to the actual hardware schematic. Commonly found in:
  - Peripheral template pin definitions (e.g. I2C `sda`/`scl`, I2S `mclk`/`bclk`/`ws`/`din`/`dout`, SPI `mosi`/`miso`/`sclk`, UART `tx`/`rx`, etc.)
  - Device template GPIO pins (e.g. LCD `reset_gpio_num`, `cs_gpio_num`, `dc_gpio_num`, etc.)
  - Default value is typically `-1` (not configured)

- **`[TO_BE_CONFIRMED]`** — Indicates the default value is a **placeholder or generic value** that needs to be verified and updated based on actual hardware. Commonly found in:
  - Chip model (e.g. `chip: es8311`, `chip: generic_lcd`)
  - Feature toggles (e.g. `adc_enabled`, `dac_enabled`)
  - Audio/video parameters (e.g. `sample_rate_hz`, `data_bit_width`, `slot_mode`)
  - Display parameters (e.g. resolution `h_size`/`v_size`, pixel format, color format, etc.)
  - Peripheral reference names (e.g. `name: i2s_audio_out`)
  - Component dependency versions

> **When adapting for a new board, pay special attention to all configuration items tagged with `[IO]` and `[TO_BE_CONFIRMED]` to ensure they match the actual hardware.**

## 2. Peripheral Configuration Rules (`board_peripherals.yaml`)

### 2.1 Required Fields
Each peripheral must have the following fields:
```yaml
peripherals:
  - name: <peripheral_name>    # Required: Unique identifier
    type: <peripheral_type>    # Required: Type identifier
    version: <version>         # Optional: Parser version to use
    role: <role>               # Conditional: operating mode (e.g. master/slave, tx/rx, continuous/oneshot, depends on peripheral type)
    format: <format_string>    # Conditional: data format (currently only used by I2S, e.g. std-out, tdm-in, pdm-out)
    config: <configuration>    # Required: Peripheral-specific config
```

### 2.2 Name Format Rules
- Must use the `type` as a naming prefix
- Unique unambiguous strings, lowercase characters, can be combinations of numbers and letters, cannot be numbers alone
- Example names: `gpio_lcd_reset`, `gpio_pa_control`, `gpio_power_audio`
- Must be unique within the configuration
- Can include device chip information in the name for clarity

### 2.3 Type Format Rules
- Must be unique within the configuration
- Format: Lowercase letters, numbers, and underscores
- Cannot be numbers only
- Examples: `i2c`, `i2s`, `spi`, `audio_codec`, `aa_bb3_c0`

### 2.4 Role and Format Rules
- `role`: Defines peripheral's operating mode (e.g. master/slave, tx/rx, continuous/oneshot, depends on peripheral type)
- `format`: Defines data format using hyphen-separated values
- Examples:
  - I2S format: `tdm-in`, `tdm-out`, `std-out`, `std-in`, `pdm-out`, `pdm-in`

## 3. Device Configuration Rules (`board_devices.yaml`)

### 3.1 Required Fields
```yaml
devices:
  - name: <device_name>       # Required: Unique identifier
    type: <device_type>       # Required: Type identifier
    chip: <chip_name>         # Conditional: device chip name (e.g. LCD chip, IO expander chip, etc.)
    version: <version>        # Optional: Parser version to use
    sub_type: <sub_type>      # Conditional: some devices have sub-types (e.g. LCD has SPI, DSI, ParlIO)
    init_skip: false          # Optional: skip initialization when the manager initializes all devices.
                              # Default is false (do not skip initialization). Set to true to skip automatic initialization.
    config:
      <configurations>        # Required: Device-specific config
    peripherals:              # Conditional: some devices depend on specific peripherals such as I2C, SPI, GPIO, etc.
      - name: <periph_name>   # Conditional: if peripherals exists, fill in the corresponding peripheral name
    dependencies:             # Conditional: some devices require additional components (e.g. LCD, LCD Touch, etc.)
      <component_name>:
        require: <scope>
        version: <version_spec>
```

### 3.2 Name Format Rules
- More flexible than peripheral names; does not require `type` as a naming prefix
- Unique unambiguous strings, lowercase characters, can be combinations of numbers and letters, cannot be numbers alone
- Example names: `audio_dac`, `audio_adc`, `lcd_power`, `display_lcd`
- Must be unique within the configuration
- Can include device chip information in the name for clarity

### 3.3 Type Format Rules
- Must be unique within the configuration
- Format: Lowercase letters, numbers, and underscores
- Characters can be separated by underscores
- Cannot be numbers only
- Examples: `gpio_ctrl`, `audio_codec`, `lcd_touch_i2c`

### 3.4 Peripheral References
- Can reference peripherals defined in `board_peripherals.yaml`
- Must use exact peripheral names
- Can include additional configuration for the peripheral
- Supports YAML anchors and references for reuse

### 3.5 Dependencies
- Optional component dependencies specification
- Format:
```yaml
dependencies:
  <component_name>:
    require: public|private
    version: <version_spec>
```

## 4. Configuration Inheritance and Reuse

### 4.1 YAML Anchors and References
- Use `&anchor_name` to create reusable configurations
- Use `*anchor_name` to reference configurations
- Example:
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

### 4.2 Device-side Peripheral Additional Parameters

When a device references a peripheral in its `peripherals` list, it can carry **device-specific additional parameters** beyond the required `name` field. These additional parameters are complementary to the peripheral's own `config`, not overrides.

**Peripheral definitions** (`board_peripherals.yaml`) provide bus/hardware-level configuration, e.g.:
- I2C peripheral: `port`, `pins`, `clk_source`, etc.
- GPIO peripheral: `pin`, `mode`, `intr_type`, etc.

**Device-side additional parameters** (extra fields on `peripherals` entries in `board_devices.yaml`) provide device-specific information, e.g.:
- I2C slave address `address`, communication frequency `frequency`
- PA control gain `gain`, active level `active_level`

Example:
```yaml
# audio_dac device in board_devices.yaml
devices:
  - name: audio_dac
    type: audio_codec
    peripherals:
      - name: i2c_master
        address: 0x30          # Device-specific: I2C slave address of this chip
        frequency: 400000      # Device-specific: desired communication frequency
      - name: gpio_power_amp
        gain: 6                # Device-specific: PA gain
        active_level: 1        # Device-specific: PA enable active level
```

> **Note:** These additional parameters are read by the corresponding device parser (`devices/dev_*/dev_*.py`).
> The peripheral definition does not contain these fields, and they do not affect the peripheral's own configuration.

## 5. Enumeration Value Usage Rules

### 5.1 Basic Principles
- Use enumeration values directly from ESP-IDF drivers and related components
- Do not use custom strings or numbers; use original enum definitions from driver header files
- Enumeration values are case-sensitive and must exactly match the header file definitions

### 5.2 Finding Enumeration Values
1. First check the IDF driver header files for the corresponding peripheral/device
2. Check the device component header file definitions
3. Reference example configuration files for enum usage

### 5.3 Important Notes
- Do not use numbers instead of enum values, even if the values are equivalent
- Do not define new enum values
- If you cannot find appropriate enum values, check if the driver version matches
- It is recommended to include comments explaining the enum values

### 5.4 Examples
```yaml
# ✅ Correct Example - Using original enum values
gpio:
  mode: GPIO_MODE_OUTPUT
  intr_type: GPIO_INTR_DISABLE

# ❌ Incorrect Example - Using custom values
gpio:
  mode: "output"          # Error: Should use GPIO_MODE_OUTPUT
  intr_type: 0           # Error: Should use GPIO_INTR_DISABLE
```
