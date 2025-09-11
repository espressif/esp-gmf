# ESP Board Manager Device and Peripheral Configuration Rules

## 1. General YAML File Structure

### 1.1 Board Level Configuration
Each board configuration YAML file must include:
```yaml
board: <board_name>
chip: <chip_type>
version: <version_number>  # Format: x.y.z, each field 0-99
```

### 1.2 File Organization
- Peripheral configurations are defined in `board_peripherals.yaml`
- Device configurations are defined in `board_devices.yaml`
- Both files should be placed in the board's directory under `boards/`

## 2. Peripheral Configuration Rules (`board_peripherals.yaml`)

### 2.1 Required Fields
Each peripheral must have the following required fields:
```yaml
peripherals:
  - name: <peripheral_name>    # Required: Unique identifier
    type: <peripheral_type>    # Required: Type identifier
    role: <role>              # Required: master/slave or specific role
    format: <format_string>   # Required: Format specification
    config: <configuration>   # Required: Peripheral-specific config
    version: <version>        # Optional: Parser version to use
```

### 2.2 Name Format Rules
- More flexible than peripheral names
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
- `role`: Defines peripheral's role (e.g., master/slave, host/device)
- `format`: Defines peripheral-specific format using hyphen-separated values
- Examples:
  - I2S format: `tdm-in`, `tdm-out`, `std-out`, `std-in`

## 3. Device Configuration Rules (`board_devices.yaml`)

### 3.1 Required Fields
```yaml
devices:
  - name: <device_name>       # Required: Unique identifier
    type: <device_type>       # Required: Type identifier
    chip: <chip_name>         # Optional: Name of chip
    version: <version>        # Optional: Parser version to use
    init_skip: false          # Optional: skip initialization when the manager initializes all devices.
                              # Default is false (do not skip initialization). Set to true to skip automatic initialization.
    config:
      <configurations>        # Required: Device-specific config
    peripherals:              # Optional: List of used peripherals
      - name: <periph_name>   # Required if peripherals section exists
    dependencies:             # Optional: Component dependencies
      <component_name>:
        require: <scope>
        version: <version>
```

### 3.2 Name Format Rules
- More flexible than peripheral names
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

### 4.2 Peripheral Configuration Override
- Devices can override peripheral configurations
- Original peripheral configuration serves as base
- Device-specific settings take precedence

## 5. The Specific Definitions

### 5.1 Audio Codec Channel Masks
- Binary string format, right-to-left layout
- ADC channel mask:
  - Format: `"xxxx"` (4 bits)
  - Example: `"0111"` enables MIC0, MIC1, MIC2
  - Bit positions: MIC0=bit0, MIC1=bit1, MIC2=bit2, MIC3=bit3

### 5.2 DAC Channel Masks
- Binary string format for stereo configuration
- Format: `"x"` or `"xx"` for left/right channels
- Example: `"1"` enables left channel only

## 6. Enumeration Value Usage Rules

### 6.1 Basic Principles
- Use enumeration values directly from ESP-IDF drivers and related components
- Do not use custom strings or numbers; use original enum definitions from driver header files
- Enumeration values are case-sensitive and must exactly match the header file definitions

### 6.2 Finding Enumeration Values
1. First check the IDF driver header files for the corresponding peripheral/device
2. Check the device component header file definitions
3. Reference example configuration files for enum usage

### 6.3 Important Notes
- Do not use numbers instead of enum values, even if the values are equivalent
- Do not define new enum values
- If you cannot find appropriate enum values, check if the driver version matches
- It is recommended to include comments explaining the enum values

### 6.4 Examples
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
