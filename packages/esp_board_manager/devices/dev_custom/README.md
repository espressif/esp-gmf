# Custom Device Type

The `dev_custom` device type provides a flexible way to create custom devices with dynamically generated configuration structures based on YAML configuration files.

## Features

- **Dynamic Structure Generation**: Automatically generates C structures based on YAML config parameters
- **Type Inference**: Automatically determines appropriate C types from YAML values
- **Peripheral Support**: Follows existing peripheral validation and handling rules
- **Flexible Configuration**: Supports any custom configuration parameters

## YAML Configuration

### Basic Structure

```yaml
- name: my_custom_device
  type: custom
  version: default
  config:
    # Custom configuration parameters
    param1: 123
    param2: "hello"
    param3: true
    param4: 3.14
  # Peripheral configuration (at device level, not inside config)
  peripherals:
    - name: gpio-1
    - name: i2c_master
```

### Supported Data Types

The parser automatically infers C types from YAML values based on the value range and type:

| YAML Type | C Type | Range | Example |
|-----------|--------|-------|---------|
| `bool` | `bool` | `true`, `false` | `true`, `false` |
| `int` | `int8_t` | -128 to 127 | `-100`, `50` |
| `int` | `uint8_t` | 0 to 255 | `200`, `0xFF` |
| `int` | `int16_t` | -32768 to 32767 | `-1000`, `15000` |
| `int` | `uint16_t` | 0 to 65535 | `50000`, `0xFFFF` |
| `int` | `int32_t` | -2147483648 to 2147483647 | `-1000000`, `2000000` |
| `int` | `uint32_t` | 0 to 4294967295 | `3000000000`, `0xFFFFFFFF` |
| `float` | `float` | Any float value | `3.14`, `2.5`, `-1.0` |
| `string` | `const char *` | Any string | `"hello"`, `"sensor"` |
| `other` | `void *` | Any other type | `null`, complex objects |

### Field Name Sanitization

YAML field names are automatically sanitized to valid C identifiers:

- Invalid characters (non-alphanumeric except underscore) are replaced with `_`
- Names starting with numbers get a `_` prefix
- Empty names become `field`
- Examples:
  - `sensor-id` → `sensor_id`
  - `2channel` → `_2channel`
  - `my.config` → `my_config`

### Structure Naming Rules

The generated C structure follows a specific naming convention:

**Pattern**: `dev_custom_{sanitized_device_name}_config_t`

Where `{sanitized_device_name}` is the device name from YAML after applying the same sanitization rules as field names.

**Examples**:
- Device name: `my_custom_device` → Structure: `dev_custom_my_custom_device_config_t`
- Device name: `temperature-sensor` → Structure: `dev_custom_temperature_sensor_config_t`
- Device name: `2channel_actuator` → Structure: `dev_custom_2channel_actuator_config_t`
- Device name: `sensor.id` → Structure: `dev_custom_sensor_id_config_t`

**Important Notes**:
- The structure name is automatically generated based on the `name` field in your YAML configuration
- You must use the exact generated structure name when declaring variables or casting pointers
- The sanitization ensures the structure name is a valid C identifier
- All custom device structures follow this consistent naming pattern for easy identification

### Generated Structure

For the example above, the following C structure will be generated:

```c
typedef struct {
    const char *name;           /*!< Custom device name */
    const char *type;           /*!< Device type: "custom" */
    const char *chip;           /*!< Chip name */
    int32_t     param1;         /*!< param1 */
    const char *param2;         /*!< param2 */
    bool        param3;         /*!< param3 */
    float       param4;         /*!< param4 */
    uint8_t     peripheral_count;  /*!< Number of peripherals */
    const char *peripheral_names[MAX_PERIPHERALS];  /*!< Peripheral names array */
} dev_custom_my_custom_device_config_t;
```

**Note**: For single peripheral devices, the structure uses `peripheral_name` instead of `peripheral_names[]`.

## Usage Examples

### Sensor Device

```yaml
- name: temperature_sensor
  type: custom
  version: default
  config:
    sensor_id: 0x48
    sample_rate: 10
    enable_filter: true
    filter_cutoff: 1.0
    timeout_ms: 500
    debug_mode: false
  peripherals:
    - name: i2c_master
```

### Actuator Device

```yaml
- name: servo_motor
  type: custom
  version: default
  config:
    actuator_type: "servo"
    min_position: 0
    max_position: 180
    default_position: 90
    speed: 1.5
    enable_feedback: true
  peripherals:
    - name: pwm-1
    - name: adc-1
```

## API Functions

The custom device provides standard device interface functions:

- `dev_custom_init(void *cfg, int cfg_size, void **device_handle)` - Initialize the device
- `dev_custom_deinit(void *device_handle)` - Deinitialize the device

### Static Device Registration

The custom device uses a static registration system based on linker sections, similar to ESP-IDF's plugin system. This approach is more efficient and user-friendly than dynamic registration.

### Function Pointer Types

```c
typedef int (*custom_device_init_func_t)(void *config, int cfg_size, void **device_handle);
typedef int (*custom_device_deinit_func_t)(void *device_handle);

// Custom device handle structure
typedef struct {
    const char *device_name;                    /*!< Device name for lookup */
    void *user_handle;                          /*!< User-defined device handle */
} custom_device_handle_t;
```

### Device Descriptor Structure

```c
typedef struct {
    const char *device_name;                    /*!< Custom device name (must match YAML config) */
    custom_device_init_func_t init_func;        /*!< Device initialization function */
    custom_device_deinit_func_t deinit_func;    /*!< Device deinitialization function */
} custom_device_desc_t;
```

### Usage Example

```c
#include "dev_custom.h"

// Custom device initialization function
int my_sensor_init(void *config, int cfg_size, void **device_handle) {
    dev_custom_my_custom_sensor_config_t *cfg = (dev_custom_my_custom_sensor_config_t *)config;

    // Validate config size
    if (cfg_size != sizeof(dev_custom_my_custom_sensor_config_t)) {
        ESP_LOGE("MY_SENSOR", "Invalid config size: %d, expected: %d",
                 cfg_size, sizeof(dev_custom_my_custom_sensor_config_t));
        return -1;
    }

    // Initialize your custom sensor hardware
    ESP_LOGI("MY_SENSOR", "Initializing sensor %s with ID 0x%02X", cfg->name, cfg->sensor_id);

    // Allocate user device handle
    my_sensor_handle_t *user_handle = malloc(sizeof(my_sensor_handle_t));
    if (!user_handle) {
        return -1;
    }

    // Initialize hardware, configure peripherals, etc.
    // ...

    // Set the user handle (the custom_device_handle_t is managed by the framework)
    *device_handle = user_handle;
    return 0;
}

// Custom device deinitialization function
int my_sensor_deinit(void *device_handle) {
    my_sensor_handle_t *user_handle = (my_sensor_handle_t *)device_handle;

    if (user_handle) {
        // Cleanup hardware, free resources, etc.
        // ...

        free(user_handle);
    }

    return 0;
}

// Register the custom device using the macro
// This automatically places the descriptor in the special linker section
CUSTOM_DEVICE_IMPLEMENT(my_custom_sensor, my_sensor_init, my_sensor_deinit);
```

### CMakeLists.txt Configuration

To use custom devices, you need to include the linker script in your component's CMakeLists.txt:

```cmake
idf_component_register(SRCS "my_custom_device.c"
                       INCLUDE_DIRS "."
                       REQUIRES dev_custom
                       WHOLE_ARCHIVE)
```

The `WHOLE_ARCHIVE` flag ensures that the custom device descriptors are not optimized away by the linker.

## Peripheral Detection and Validation

The custom device type includes comprehensive peripheral detection and validation:

### Peripheral Validation Rules

1. **Existence Check**: When `peripherals_dict` is provided, all referenced peripherals are validated
2. **Error Handling**: Invalid peripherals raise `ValueError` with descriptive messages
3. **Flexible Format**: Supports both string and dictionary peripheral definitions:

   ```yaml
   peripherals:
     - name: i2c_master        # Dictionary format
     - gpio-1                  # String format
   ```

### Peripheral Structure Generation

- **Single Peripheral**: Uses `peripheral_name` field (not array)
- **Multiple Peripherals**: Uses `peripheral_names[MAX_PERIPHERALS]` array
- **Peripheral Count**: Always includes `peripheral_count` field
- **Maximum Peripherals**: Limited to `MAX_PERIPHERALS` (defined as 4 in dev_custom.h)

### Validation Example

```python
# Valid configuration
config = {
    'peripherals': [{'name': 'i2c_master'}, {'name': 'gpio-1'}]
}
peripherals_dict = {'i2c_master': 'valid', 'gpio-1': 'valid'}
# ✅ Parses successfully

# Invalid configuration
config = {
    'peripherals': [{'name': 'invalid_peripheral'}]
}
peripherals_dict = {'i2c_master': 'valid'}
# ❌ Raises ValueError: "Custom device test_device references undefined peripheral 'invalid_peripheral'"
```

## Implementation Notes

1. **Field Name Sanitization**: YAML keys are automatically converted to valid C identifiers using regex replacement
2. **Peripheral Validation**: All referenced peripherals are validated against available peripherals dictionary
3. **Type Inference**: C types are automatically inferred from Python value types and ranges
4. **Memory Management**: Configuration structures are dynamically allocated and managed
5. **Type Safety**: Generated structures maintain type safety based on YAML values
6. **Error Handling**: Comprehensive error messages for invalid configurations

## Limitations

- Maximum 4 peripherals per device (defined by `MAX_PERIPHERALS` constant in dev_custom.h)
- Field names must be valid C identifiers after sanitization
- Configuration size is limited by available memory
- No support for complex nested structures (arrays, structs)
- Peripheral validation only works when `peripherals_dict` is provided
- Only basic data types supported: bool, int, float, string
