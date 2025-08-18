# LCD Touch I2C Device

This device type supports LCD touch controllers that communicate via I2C interface.

## Supported Chips

- **CST816S**: Capacitive touch controller with I2C interface
- **GT911**: Capacitive touch controller with I2C interface (supports dual addresses)

## I2C Address Configuration

The `dev_addr` field in `io_i2c_config` supports two formats:

### Single Address Format
```yaml
io_i2c_config:
  dev_addr: 0x15                    # Single I2C address
```

### Dual Address Format
```yaml
io_i2c_config:
  dev_addr: [0x14, 0x5D]            # [primary_address, secondary_address]
```

When using the array format:
- The first address (`0x14`) is used as the primary address
- The second address (`0x5D`) is used as the secondary address
- If only one address is provided in the array, the secondary address defaults to 0
- This is useful for devices like GT911 that may have different addresses depending on hardware configuration

## Configuration Structure

The `lcd_touch_i2c` device type uses the following configuration structure:

### YAML Configuration

```yaml
- name: lcd_touch
  chip: cst816s
  type: lcd_touch_i2c
  version: default
  dependencies:
    espressif/esp_lcd_touch_cst816s: "*"
  config:
    # esp_lcd_panel_io_i2c_config_t fields for touch communication
    io_i2c_config:
      dev_addr: 0x15                    # Touch I2C address (single value)
      # OR use array format for devices with multiple possible addresses:
      # dev_addr: [0x14, 0x5D]          # [primary_addr, secondary_addr]
      control_phase_bytes: 1            # Control phase bytes
      dc_bit_offset: 0                  # DC bit offset in control phase
      lcd_cmd_bits: 8                   # Bit-width of LCD command
      lcd_param_bits: 0                 # Bit-width of LCD parameter (0 for touch)
      scl_speed_hz: 100000              # I2C SCL frequency
      flags:
        dc_low_on_data: false           # DC level for data transfer
        disable_control_phase: true     # Disable control phase for touch

    # esp_lcd_touch_config_t fields for touch configuration
    touch_config:
      x_max: 320                        # Maximum X coordinate
      y_max: 240                        # Maximum Y coordinate
      rst_gpio_num: -1                  # Reset GPIO (GPIO_NUM_NC if shared with LCD)
      int_gpio_num: 10                  # Interrupt GPIO
      levels:
        reset: 0                        # Reset pin active level
        interrupt: 0                    # Interrupt pin active level
      flags:
        swap_xy: false                  # Swap X and Y coordinates
        mirror_x: false                 # Mirror X coordinates
        mirror_y: false                 # Mirror Y coordinates

  peripherals:
    - name: i2c-0                       # I2C peripheral for touch communication
      address: 0x15                     # Touch I2C address
      frequency: 100000                 # I2C frequency
    - name: gpio-10                     # Touch interrupt GPIO
      type: gpio
      role: input
      config:
        pin: 10                         # GPIO pin number
        mode: GPIO_MODE_INPUT
        pull_up: true
        pull_down: false
        intr_type: GPIO_INTR_NEGEDGE
```

## C Structure

The device generates the following C configuration structure:

```c
typedef struct {
    const char *name;
    const char *chip;
    const char *type;
    const char *i2c_name;
    const uint16_t i2c_addr[2];         /*!< I2C addresses [primary, secondary] */
    esp_lcd_panel_io_i2c_config_t io_i2c_config;
    esp_lcd_touch_config_t touch_config;
} dev_lcd_touch_i2c_config_t;
```

The `i2c_addr` array contains:
- `i2c_addr[0]`: Primary I2C address
- `i2c_addr[1]`: Secondary I2C address (0 if not used)

## Usage

The touch device can be initialized using the board manager:

```c
// Initialize touch device
int ret = esp_board_device_init("lcd_touch");
if (ret != 0) {
    ESP_LOGE(TAG, "Failed to initialize touch device: %d", ret);
    return ESP_FAIL;
}

// Get touch device handle
void *touch_handle = NULL;
ret = esp_board_device_get_handle("lcd_touch", &touch_handle);
if (ret != 0) {
    ESP_LOGE(TAG, "Failed to get touch device handle: %d", ret);
    return ESP_FAIL;
}

// Cast to touch handles structure
dev_lcd_touch_i2c_handles_t *touch_handles = (dev_lcd_touch_i2c_handles_t *)touch_handle;

// Use touch functionality
esp_lcd_touch_handle_t tp = touch_handles->touch_handle;
uint16_t x, y;
uint8_t point_num;
if (esp_lcd_touch_get_coordinates(tp, &x, &y, NULL, &point_num, 1)) {
    ESP_LOGI(TAG, "Touch detected at (%d, %d)", x, y);
}
```

## Dependencies

- `esp_lcd_touch`: Base touch functionality
- `esp_lcd_touch_cst816s`: CST816S specific driver
- `driver/i2c_master`: I2C master driver
- `esp_lcd_panel_io`: LCD panel IO interface
