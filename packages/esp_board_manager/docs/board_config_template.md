# Board Manager Board Configuration Template (Minimal Template)

This document describes the configuration fields of `board_peripherals.yaml` and `board_devices.yaml`, along with **common minimal configuration templates**, to help with understanding and creating new boards.
**Note: This document only shows minimal configuration templates. If something does not work correctly, please refer to the full configuration for debugging!**

**YAML Rules:**

```yaml
# board_peripherals.yaml
peripherals:
  - name: <peripheral_name>    # Required: unique identifier
    type: <peripheral_type>    # Required: type identifier
    role: <role>               # Conditional: operating mode (e.g. master/slave, tx/rx, continuous/oneshot, depends on peripheral type)
    format: <format_string>    # Conditional: data format (currently only used by I2S, e.g. std-out, tdm-in, pdm-out)
    config: <configuration>    # Required: peripheral-specific configuration

# board_devices.yaml
devices:
  - name: <device_name>       # Required: unique identifier
    type: <device_type>       # Required: type identifier
    chip: <chip_name>         # Conditional: device chip name (e.g. LCD chip, IO expander chip, etc.)
    sub_type: <sub_type>      # Conditional: some devices have sub-types (e.g. LCD has SPI, DSI, ParlIO)
    config:
      <configurations>        # Required: device-specific configuration
    peripherals:              # Conditional: some devices depend on specific peripherals such as I2C, SPI, GPIO, etc.
      - name: <periph_name>   # Conditional: if peripherals exists, fill in the corresponding peripheral name
    dependencies:             # Conditional: some devices require additional components (e.g. LCD, LCD Touch, etc.)
      <component_name>:
        require: <scope>
        version: <version_spec>
```

*When adapting to a new development board, pay special attention to all configuration items marked with `[IO]` and `[TO_BE_CONFIRMED]`—these are closely related to hardware pin assignments and correct operation. Make sure they match the actual hardware.*

- For detailed YAML rules, see: [device_and_peripheral_rules.md](device_and_peripheral_rules.md)
- For board creation workflow, custom code in board directories, etc., see: [how_to_customize_board.md](how_to_customize_board.md)

**Usage Conventions:**

- The snippets below should be pasted into the board directory's **`board_peripherals.yaml`** (inside the `peripherals:` list) and **`board_devices.yaml`** (inside the `devices:` list) respectively.
- The `name` referenced in a device's `peripherals:` section must **exactly match** the name in `board_peripherals.yaml`; peripheral names **must start with the type** (e.g. `i2c_master`, `i2s_audio_out`).

---

## Table of Contents

- [Audio Codec (`audio_codec`)](#audio-codec-audio_codec)
  - [Playback (DAC, `dac_enabled: true`)](#playback-dac-dac_enabled-true)
  - [Recording (ADC, `adc_enabled: true`)](#recording-adc-adc_enabled-true)
  - [Full-Duplex (Same Codec Chip)](#full-duplex-same-codec-chip)
  - [Recording and Playback without External Codec](#recording-and-playback-without-external-codec)
  - [I2S Advanced Configuration](#i2s-advanced-configuration)
- [LCD (`display_lcd`)](#lcd-display_lcd)
  - [DSI (`sub_type: dsi`)](#dsi-sub_type-dsi)
  - [SPI (`sub_type: spi`)](#spi-sub_type-spi)
  - [PARLIO (`sub_type: parlio`)](#parlio-sub_type-parlio)
- [LEDC Backlight (`ledc_ctrl`)](#ledc-backlight-ledc_ctrl)
- [LCD Touch (`lcd_touch_i2c`)](#lcd-touch-lcd_touch_i2c)
- [Camera (`camera`)](#camera-camera)
  - [DVP (`sub_type: dvp`)](#dvp-sub_type-dvp)
  - [CSI (`sub_type: csi`)](#csi-sub_type-csi)
  - [SPI Camera (`sub_type: spi`)](#spi-camera-sub_type-spi)
- [Button (`button`)](#button-button)
  - [GPIO (`sub_type: gpio`)](#gpio-sub_type-gpio)
  - [ADC (`sub_type: adc_single` / `adc_multi`)](#adc-sub_type-adc_single--adc_multi)

---

## Audio Codec (`audio_codec`)

- **Overview**: When using an external codec for playback (DAC) or recording (ADC), the peripheral side requires at least **one I2C bus** (control) and **one I2S bus** (data; playback typically uses `std-out`, recording typically uses `std-in`); a PA GPIO is optional.
- **Constraint**: Do **not** set both `adc_enabled` and `dac_enabled` to true in the same logical device instance. For full-duplex with a single physical codec chip, split it into two unidirectional devices in the board configuration (e.g. `audio_dac` + `audio_adc`); see "Full-Duplex".
- **Full Fields (YAML)**: [dev_audio_codec.yaml](../devices/dev_audio_codec/dev_audio_codec.yaml)
- **Reference Code**:

  Recording and playback using SD card: [record_to_sdcard.c](../examples/record_to_sdcard/main/record_to_sdcard.c), [play_sdcard_music.c](../examples/play_sdcard_music/main/play_sdcard_music.c);

  Playback without SD card: [play_embed_music.c](../examples/play_embed_music/main/play_embed_music.c);

  Recording and playback without SD card: [record_and_play.c](../examples/record_and_play/main/record_and_play.c);

### Playback (DAC, `dac_enabled: true`)

**`board_peripherals.yaml`**:

```yaml
  - name: i2c_master
    type: i2c
    role: master
    config:
      # I2C port number (default: -1 for auto selection)
      port: 0
      # I2C pins configuration
      pins:
        sda: 17                     # [IO] SDA pin
        scl: 18                     # [IO] SCL pin

  - name: i2s_audio_out
    type: i2s
    role: master
    format: std-out
    config:
      port: 0
      sample_rate_hz: 16000            # [TO_BE_CONFIRMED] Sample rate in Hz
      data_bit_width: 16               # [TO_BE_CONFIRMED] Data bit width
      slot_mode: I2S_SLOT_MODE_STEREO  # [TO_BE_CONFIRMED] Slot mode
      # Valid values:
      # - I2S_SLOT_MODE_MONO
      # - I2S_SLOT_MODE_STEREO
      pins:
        bclk: 9                     # [IO] BCLK pin
        ws: 45                      # [IO] WS pin
        dout: 8                     # [IO] Data output pin

  - name: gpio_pa_control
    type: gpio
    role: io
    config:
      pin: 46                   # [IO] GPIO pin number
      mode: GPIO_MODE_OUTPUT    # [TO_BE_CONFIRMED] GPIO mode
```

**`board_devices.yaml`**:

```yaml
  - name: audio_dac
    chip: es8311                        # [TO_BE_CONFIRMED] Codec chip type (es8311, es7210, etc.)
    type: audio_codec
    config:
      adc_enabled: false                # [TO_BE_CONFIRMED] Enable ADC functionality (default: false)
      dac_enabled: true                 # [TO_BE_CONFIRMED] Enable DAC functionality (default: false)

    peripherals:
      - name: gpio_pa_control
        active_level: 1                 # Active level (0-low, 1-high) (default: 0)
      - name: i2s_audio_out             # [TO_BE_CONFIRMED] I2S peripheral for audio data interface
      - name: i2c_master                # [TO_BE_CONFIRMED] I2C peripheral for codec control
        address: 0x30                   # [TO_BE_CONFIRMED] I2C device address, include the read/write bit (hex format) (default: 0x30)
        frequency: 400000               # I2C clock frequency in Hz (default: 400000)
```

If the board has no PA control pin, there is no need to add the `gpio_pa_control` dependency or peripheral configuration.

### Recording (ADC, `adc_enabled: true`)

**`board_peripherals.yaml`** (can share the same `i2c_master` with playback; I2S recording typically uses a separate `std-in` peripheral name):

```yaml
  - name: i2c_master
    type: i2c
    role: master
    config:
      port: 0
      pins:
        sda: 17                     # [IO] SDA pin
        scl: 18                     # [IO] SCL pin

  - name: i2s_audio_in
    type: i2s
    role: master
    format: std-in
    config:
      port: 0
      sample_rate_hz: 48000            # [TO_BE_CONFIRMED] Sample rate in Hz
      data_bit_width: 16               # [TO_BE_CONFIRMED] Data bit width
      slot_mode: I2S_SLOT_MODE_STEREO  # [TO_BE_CONFIRMED] Slot mode
      # Valid values:
      # - I2S_SLOT_MODE_MONO
      # - I2S_SLOT_MODE_STEREO
      pins:
        bclk: 9                     # [IO] BCLK pin
        ws: 45                      # [IO] WS pin
        din: 10                     # [IO] Data input pin
```

**`board_devices.yaml`**:

```yaml
  - name: audio_adc
    chip: es8311                        # [TO_BE_CONFIRMED] Codec chip type (es8311, es7210, etc.)
    type: audio_codec
    config:
      adc_enabled: true                 # [TO_BE_CONFIRMED] Enable ADC functionality (default: false)
      dac_enabled: false                # [TO_BE_CONFIRMED] Enable DAC functionality (default: false)

    peripherals:
      - name: i2s_audio_in              # [TO_BE_CONFIRMED] I2S peripheral for audio data interface
      - name: i2c_master                  # [TO_BE_CONFIRMED] I2C peripheral for codec control
        address: 0x30                     # [TO_BE_CONFIRMED] I2C device address, include the read/write bit (hex format) (default: 0x30)
        frequency: 400000                 # I2C clock frequency in Hz (default: 400000)
```

### Full-Duplex (Same Codec Chip)

Configure **two** `type: audio_codec` devices in `board_devices.yaml` (e.g. `audio_dac` + `audio_adc`), enabling only DAC or only ADC respectively. **I2C control** can point to the same `i2c_master` (you may use YAML `&` / `*` anchors to avoid duplicating `address` / `frequency`).
**Board Reference**: [esp32_s3_korvo2l/board_devices.yaml](../boards/esp32_s3_korvo2l/board_devices.yaml).

### Recording and Playback without External Codec

The two examples above focus on **external Codec + I2S**. If no codec chip is needed and you want to use **internal ADC for audio capture** or **PDM speaker** mode, refer to the **ADC Mic Usage Mode** in [dev_audio_codec.yaml](../devices/dev_audio_codec/dev_audio_codec.yaml);

**Board Reference**: [esp32_c3_lyra/board_devices.yaml](../boards/esp32_c3_lyra/board_devices.yaml), [esp32_c3_lyra/board_peripherals.yaml](../boards/esp32_c3_lyra/board_peripherals.yaml) (using `adc` peripheral or `adc_local_cfg`).

### I2S Advanced Configuration

The templates above only show **I2S STD mode**, recommended for single-microphone single-speaker setups. For multi-microphone or loopback scenarios, **TDM** mode is recommended. Refer to:

- **Board Reference**: [esp32_s3_korvo2_v3/board_peripherals.yaml](../boards/esp32_s3_korvo2_v3/board_peripherals.yaml) (TDM mode I2S)
- **All I2S Modes**: [periph_i2s.yml](../peripherals/periph_i2s/periph_i2s.yml)

---

## LCD (`display_lcd`)

- **Overview**: LCD can be divided into three `sub_type` values based on the interface type: **`dsi`** / **`spi`** / **`parlio`** (modes currently supported by Board Manager). Fill in `chip` and `dependencies` according to the actual hardware, and register the panel factory function in the board directory's **`setup_device.c`**.
- **Factory Function Signatures**:
  - DSI: **`lcd_dsi_panel_factory_entry_t(esp_lcd_dsi_bus_handle_t dsi_handle, dev_display_lcd_config_t *lcd_cfg, dev_display_lcd_handles_t *lcd_handles)`**
  - SPI / PARLIO: **`lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)`**
- **Notes**: For factory functions and board customization workflow, see [how_to_customize_board.md](how_to_customize_board.md) section 6; examples in [esp32_p4_function_ev/setup_device.c](../boards/esp32_p4_function_ev/setup_device.c), [esp32_s3_korvo2_v3/setup_device.c](../boards/esp32_s3_korvo2_v3/setup_device.c), etc.
- **Components**: Screen drivers and dependency versions can be found on [Component Registry](https://components.espressif.com/).
- **Full Fields (YAML)**: [dev_display_lcd.yaml](../devices/dev_display_lcd/dev_display_lcd.yaml)
- **Reference Code**: [test_dev_lcd_init.c](../test_apps/main/test_dev_lcd_init.c) (`display_lcd`), [test_dev_lcd_lvgl.c](../test_apps/main/test_dev_lcd_lvgl.c) (LVGL example)

### DSI (`sub_type: dsi`)

DSI (MIPI) LCD requires an LDO peripheral for power supply and a DSI data interface configuration.

**`board_peripherals.yaml`**

```yaml
  - name: ldo_mipi
    type: ldo
    config:
      chan_id: 3                      # [TO_BE_CONFIRMED] LDO channel ID
      voltage_mv: 2500                # [TO_BE_CONFIRMED] Output voltage in millivolts
      # Whether voltage is adjustable (default: 1 for true)
      # Valid values: 0 (fixed), 1 (adjustable)
      adjustable: 1
      # Hardware ownership flag (default: 0 for software control)
      # Valid values: 0 (software-controlled), 1 (hardware-controlled)
      owned_by_hw: 0

  - name: dsi_display
    type: dsi
    config:
      # DSI bus identifier (default: 0 for primary bus)
      # Valid values depending on available DSI controllers, esp32p4 supports only 1 MIPI DSI bus
      bus_id: 0
      # Number of data lanes (default: 2 for dual-lane mode)
      # esp32p4 supports up to 2 data lanes
      data_lanes: 2
      # Physical layer clock source (default: MIPI_DSI_PHY_CLK_SRC_DEFAULT)
      phy_clk_src: 0
      # Valid values:
      # - 0 (Set it to 0 and then let the driver configure it)
      # - MIPI_DSI_PHY_CLK_SRC_DEFAULT
      # - MIPI_DSI_PHY_CLK_SRC_PLL_F20M
      # - MIPI_DSI_PHY_CLK_SRC_PLL_F25M
      # - MIPI_DSI_PHY_CLK_SRC_RC_FAST
      lane_bit_rate_mbps: 1000          # [TO_BE_CONFIRMED] Bit rate per data lane in Mbps
```

**`board_devices.yaml`**

```yaml
  - name: display_lcd
    type: display_lcd
    sub_type: dsi
    chip: ek79007                          # [TO_BE_CONFIRMED] LCD chip type (generic)
    dependencies:
      espressif/esp_lcd_ek79007: "^1.0.0"  # [TO_BE_CONFIRMED] Component dependency for the LCD chip
    config:
      reset_gpio_num: 27                        # [IO] GPIO pin for reset signal (use -1 if RST not from SoC)
      rgb_ele_order: LCD_RGB_ELEMENT_ORDER_RGB  # [TO_BE_CONFIRMED] RGB element order (default: RGB)
      data_endian: LCD_RGB_DATA_ENDIAN_BIG      # [TO_BE_CONFIRMED] Data endianness (default: BIG)
      bits_per_pixel: 24                        # [TO_BE_CONFIRMED] Bits per pixel (24bpp, RGB888)
      dpi_config:
        dpi_clock_freq_mhz: 48                       # [TO_BE_CONFIRMED] DPI pixel clock MHz from panel budget
        pixel_format: LCD_COLOR_PIXEL_FORMAT_RGB888  # [TO_BE_CONFIRMED] Pixel format that used by the MIPI LCD device (default: RGB565)
        in_color_format: LCD_COLOR_FMT_RGB888        # [TO_BE_CONFIRMED] Input color format (default: RGB565)
        out_color_format: LCD_COLOR_FMT_RGB888       # [TO_BE_CONFIRMED] Output color format (default: RGB565)
        video_timing:
          h_size: 1024            # [TO_BE_CONFIRMED] Horizontal resolution
          v_size: 600             # [TO_BE_CONFIRMED] Vertical resolution
          hsync_back_porch: 120   # [TO_BE_CONFIRMED] Horizontal back porch
          hsync_pulse_width: 10   # [TO_BE_CONFIRMED] Horizontal sync pulse width
          hsync_front_porch: 120  # [TO_BE_CONFIRMED] Horizontal front porch
          vsync_back_porch: 20    # [TO_BE_CONFIRMED] Vertical back porch
          vsync_pulse_width: 1    # [TO_BE_CONFIRMED] Vertical sync pulse width
          vsync_front_porch: 10   # [TO_BE_CONFIRMED] Vertical front porch

    peripherals:
      - name: ldo_mipi          # [TO_BE_CONFIRMED] LDO peripheral for dsi power management
      - name: dsi_display       # [TO_BE_CONFIRMED] DSI peripheral instance used for this display
```

**Board Reference**: [esp32_p4_function_ev/board_devices.yaml](../boards/esp32_p4_function_ev/board_devices.yaml), [esp32_p4_function_ev/board_peripherals.yaml](../boards/esp32_p4_function_ev/board_peripherals.yaml), [esp32_p4_function_ev/setup_device.c](../boards/esp32_p4_function_ev/setup_device.c).

### SPI (`sub_type: spi`)

SPI LCD requires an SPI interface configuration.

**`board_peripherals.yaml`**

```yaml
  - name: spi_master
    type: spi
    config:
      spi_bus_config:
        # Standard SPI pins (union fields - use either dataX_io_num or traditional names)
        spi_port: SPI3_HOST             # [TO_BE_CONFIRMED] SPI port
        mosi_io_num: 37                 # [IO] MOSI pin
        miso_io_num: 35                 # [IO] MISO pin
        sclk_io_num: 36                 # [IO] SCLK pin
        quadwp_io_num: -1               # [IO] Quad Write Protect pin
        quadhd_io_num: -1               # [IO] Quad Hold pin
        # Maximum transfer size (default: 4092)
        max_transfer_sz: 32000
```

**`board_devices.yaml`**

```yaml
  - name: display_lcd
    type: display_lcd
    sub_type: spi
    chip: ili9341                          # [TO_BE_CONFIRMED] LCD chip type (e.g., st77916, st7789, ili9341, gc9a01)
    dependencies:
      espressif/esp_lcd_ili9341: "*"       # [TO_BE_CONFIRMED] Component dependency for the LCD chip
    config:
      x_max: 320                          # [TO_BE_CONFIRMED] Horizontal resolution
      y_max: 240                          # [TO_BE_CONFIRMED] Vertical resolution
      io_spi_config:
        cs_gpio_num: 3                    # [IO] GPIO pin for Chip Select (default: -1)
        dc_gpio_num: 35                   # [IO] GPIO pin for Data/Command (default: -1)
        pclk_hz: 40000000                 # [TO_BE_CONFIRMED] Frequency of pixel clock (default: 40MHz)
      lcd_panel_config:
        reset_gpio_num: -1                # Reset GPIO pin (default: -1); set to SoC pin [IO] when RST is on GPIO
        rgb_ele_order: LCD_RGB_ELEMENT_ORDER_BGR  # [TO_BE_CONFIRMED] RGB element order (default: RGB)
        data_endian: LCD_RGB_DATA_ENDIAN_BIG      # [TO_BE_CONFIRMED] Data endianness (default: BIG)
        bits_per_pixel: 16                        # [TO_BE_CONFIRMED] Bits per pixel (default: 16)

    peripherals:
      - name: spi_master                  # [TO_BE_CONFIRMED] SPI peripheral for LCD communication
```

**Board Reference**: [esp32_s3_korvo2_v3/board_devices.yaml](../boards/esp32_s3_korvo2_v3/board_devices.yaml), [esp32_s3_korvo2_v3/board_peripherals.yaml](../boards/esp32_s3_korvo2_v3/board_peripherals.yaml), [esp32_s3_korvo2_v3/setup_device.c](../boards/esp32_s3_korvo2_v3/setup_device.c).

### PARLIO (`sub_type: parlio`)

PARLIO LCD does not require explicit peripheral dependencies.

**`board_devices.yaml`**

```yaml
  - name: display_lcd
    type: display_lcd
    sub_type: parlio
    chip: ili9341                           # [TO_BE_CONFIRMED] must match lcd_panel_factory_entry_t + driver
    dependencies:
      espressif/esp_lcd_ili9341: "*"        # [TO_BE_CONFIRMED]
    config:
      x_max: 284                            # [TO_BE_CONFIRMED] Horizontal resolution
      y_max: 240                            # [TO_BE_CONFIRMED] Vertical resolution
      io_parl_config:
        dc_gpio_num: 7                      # [IO] GPIO used for D/C line
        clk_gpio_num: 1                     # [IO] GPIO used for CLK line
        cs_gpio_num: 6                      # [IO] GPIO used for CS line
        # GPIOs used for data lines (ESP_PARLIO_LCD_WIDTH_MAX == 8); 1-line SPI uses index 0 only, rest -1
        data_gpio_nums: [0, -1, -1, -1, -1, -1, -1, -1]  # [IO] GPIOs used for data lines
        data_width: 1                       # [TO_BE_CONFIRMED] 1 (SPI) or 8 (I80); must match wiring
        pclk_hz: 40000000                   # [TO_BE_CONFIRMED] Frequency of pixel clock
        max_transfer_bytes: 136320          # [TO_BE_CONFIRMED] >= largest color transfer; too small breaks DMA path
      lcd_panel_config:
        reset_gpio_num: -1                        # Reset GPIO pin (default: -1); else pin [IO]
        rgb_ele_order: LCD_RGB_ELEMENT_ORDER_RGB  # [TO_BE_CONFIRMED] RGB element order (default: RGB)
        data_endian: LCD_RGB_DATA_ENDIAN_LITTLE   # [TO_BE_CONFIRMED] Data endianness (default: LITTLE)
        bits_per_pixel: 16                        # [TO_BE_CONFIRMED] Bits per pixel (default: 16)
```

---

## LEDC Backlight (`ledc_ctrl`)

PWM backlight is provided by **`periph_ledc`** (peripheral, `type: ledc`) which manages the hardware timer and GPIO output. The **`ledc_ctrl`** device (`type: ledc_ctrl`) binds to this peripheral and exposes logical configuration such as brightness percentage.

- **Full Fields (Device YAML)**: [dev_ledc_ctrl.yaml](../devices/dev_ledc_ctrl/dev_ledc_ctrl.yaml)
- **Full Fields (Peripheral YAML)**: [periph_ledc.yml](../peripherals/periph_ledc/periph_ledc.yml)
- **Reference Code**: [test_dev_ledc.c](../test_apps/main/test_dev_ledc.c); for LCD integration, also see the backlight-related path in [test_dev_lcd_init.c](../test_apps/main/test_dev_lcd_init.c) (requires `CONFIG_ESP_BOARD_DEV_LEDC_CTRL_SUPPORT`)
- **Board Reference**: Full **`ledc_ctrl` + `ledc` peripheral** combination in [esp32_p4_function_ev/board_peripherals.yaml](../boards/esp32_p4_function_ev/board_peripherals.yaml), [esp32_p4_function_ev/board_devices.yaml](../boards/esp32_p4_function_ev/board_devices.yaml).

**`board_peripherals.yaml`**

```yaml
  - name: ledc_backlight
    type: ledc
    role: none
    config:
      gpio_num: 47                     # [IO] GPIO pin number
      channel: "LEDC_CHANNEL_0"        # [TO_BE_CONFIRMED] LEDC channel
      timer_sel: "LEDC_TIMER_0"        # [TO_BE_CONFIRMED] LEDC timer
      freq_hz: 4000                    # [TO_BE_CONFIRMED] PWM frequency in Hz
      duty_resolution: "LEDC_TIMER_13_BIT"  # [TO_BE_CONFIRMED] Duty resolution in bits
```

**`board_devices.yaml`**

```yaml
  - name: lcd_brightness
    type: ledc_ctrl
    config:
      default_percent: 100              # [TO_BE_CONFIRMED] Default brightness percentage (0-100)
    peripherals:
      - name: ledc_backlight            # LEDC peripheral name (must reference an LEDC peripheral)
```

---

## LCD Touch (`lcd_touch_i2c`)

- **Purpose**: I2C touch; `type` is **`lcd_touch_i2c`**. Fill in `chip` and `dependencies` at the board level, and register the touch factory function in **`setup_device.c`**.
- **Factory Function Signature**: **`lcd_touch_factory_entry_t(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *touch_dev_config, esp_lcd_touch_handle_t *ret_touch)`**
- **Notes**: Same as LCD, see [how_to_customize_board.md](how_to_customize_board.md) section 6 and the board `setup_device.c` examples above.
- **Components**: Touch drivers and dependencies can be found on [Component Registry](https://components.espressif.com/).
- **Full Fields (YAML)**: [dev_lcd_touch_i2c.yaml](../devices/dev_lcd_touch_i2c/dev_lcd_touch_i2c.yaml)
- **Reference Code**: [test_dev_lcd_init.c](../test_apps/main/test_dev_lcd_init.c) (`test_dev_lcd_touch_init`)
- **Board Reference**: [esp32_s3_korvo2_v3/board_devices.yaml](../boards/esp32_s3_korvo2_v3/board_devices.yaml) (`lcd_touch` + TT21100), [esp32_p4_function_ev/board_devices.yaml](../boards/esp32_p4_function_ev/board_devices.yaml) (`lcd_touch` + GT911); see the respective `setup_device.c` for factory registration.

**`board_peripherals.yaml`**

If the touch shares I2C with an existing device, just ensure a matching **`i2c_master`** already exists at the board level (full configuration not repeated here, see [periph_i2c.yml](../peripherals/periph_i2c/periph_i2c.yml)).

**`board_devices.yaml`**

```yaml
  - name: lcd_touch
    chip: cst816s                           # [TO_BE_CONFIRMED] Touch chip type (e.g., cst816s, ft5x06, gt911, st1633)
    type: lcd_touch_i2c
    dependencies:
      espressif/esp_lcd_touch_cst816s: "*"  # [TO_BE_CONFIRMED] Component dependency for the touch chip
    config:
      io_i2c_config:
        dev_addr: 0x15                    # [TO_BE_CONFIRMED] I2C device address (default: 0x5a)
        lcd_cmd_bits: 8                   # [TO_BE_CONFIRMED] Bit-width of LCD command (default: 8)
        flags:
          disable_control_phase: true     # Disable control phase for touch (default: true)
        peripherals:
          - name: i2c_master              # I2C peripheral for touch communication

      touch_config:
        x_max: 284                        # [TO_BE_CONFIRMED] Maximum X coordinate (default: 320)
        y_max: 240                        # [TO_BE_CONFIRMED] Maximum Y coordinate (default: 240)
        rst_gpio_num: -1                  # [IO] Reset GPIO (default: -1, GPIO_NUM_NC)
        int_gpio_num: 3                   # [IO] Interrupt GPIO (default: -1, GPIO_NUM_NC)
        flags:
          swap_xy: true                   # Swap X and Y coordinates (default: false)
          mirror_x: false                 # Mirror X coordinates (default: false)
          mirror_y: true                  # Mirror Y coordinates (default: false)
```

---

## Camera (`camera`)

- **Purpose**: `type` is fixed as **`camera`**, with `sub_type` being **`dvp`** / **`csi`** / **`spi`**. The device's `peripherals` must include an **`i2c*`** bus for sensor SCCB (except USB-UVC, etc.). When `dont_init_ldo: true` for CSI, a separate **`ldo*`** peripheral also needs to be declared.
- **Full Fields (YAML)**: [dev_camera.yaml](../devices/dev_camera/dev_camera.yaml)
- **Reference Code**: [test_dev_camera.c](../test_apps/main/test_dev_camera.c)

### DVP (`sub_type: dvp`)

**`board_peripherals.yaml`**

Requires an existing **`i2c_master`** dependency; full configuration not repeated here, see [periph_i2c.yml](../peripherals/periph_i2c/periph_i2c.yml).

**`board_devices.yaml`**

```yaml
  - name: camera
    type: camera
    sub_type: dvp
    config:
      dvp_config:
        reset_io: -1        # [IO] GPIO pin for reset signal (default: -1, not connected)
        pwdn_io: -1         # [IO] GPIO pin for power down signal (default: -1, not connected)
        vsync_io: 21        # [IO] GPIO pin for vertical sync signal (default: depends on implementation)
        data_width: "CAM_CTLR_DATA_WIDTH_8"  # [TO_BE_CONFIRMED] Data width configuration:
                                             # CAM_CTLR_DATA_WIDTH_8
                                             # CAM_CTLR_DATA_WIDTH_16
                                             # CAM_CTLR_DATA_WIDTH_12
                                             # CAM_CTLR_DATA_WIDTH_10
        de_io: 38            # [TO_BE_CONFIRMED] GPIO pin for data enable signal (default: depends on implementation)
        pclk_io: 11          # [TO_BE_CONFIRMED] GPIO pin for pixel clock signal (default: depends on implementation)
        xclk_io: 40          # [TO_BE_CONFIRMED] GPIO pin for external clock output (default: depends on implementation)
        xclk_freq: 10000000  # [TO_BE_CONFIRMED] External clock frequency in Hz (default: 10MHz)
        data_io:
          data_io_0: 13      # [IO] GPIO pin for data bit 0 (default: depends on implementation)
          data_io_1: 47      # [IO] GPIO pin for data bit 1 (default: depends on implementation)
          data_io_2: 14      # [IO] GPIO pin for data bit 2 (default: depends on implementation)
          data_io_3: 3       # [IO] GPIO pin for data bit 3 (default: depends on implementation)
          data_io_4: 12      # [IO] GPIO pin for data bit 4 (default: depends on implementation)
          data_io_5: 42      # [IO] GPIO pin for data bit 5 (default: depends on implementation)
          data_io_6: 41      # [IO] GPIO pin for data bit 6 (default: depends on implementation)
          data_io_7: 39      # [IO] GPIO pin for data bit 7 (default: depends on implementation)
    peripherals:
      - name: i2c_master     # [TO_BE_CONFIRMED] I2C bus name for camera control (default: depends on implementation)
        frequency: 100000    # I2C frequency in Hz (default: 400kHz)
```

**Board Reference**: [esp32_s3_korvo2_v3/board_devices.yaml](../boards/esp32_s3_korvo2_v3/board_devices.yaml), [esp32_s3_korvo2_v3/board_peripherals.yaml](../boards/esp32_s3_korvo2_v3/board_peripherals.yaml).

### CSI (`sub_type: csi`)

**`board_peripherals.yaml`**

Requires an existing **`i2c_master`** dependency; full configuration not repeated here, see [periph_i2c.yml](../peripherals/periph_i2c/periph_i2c.yml).

In addition to **`i2c_master`**, if **`dont_init_ldo: true`** (LDO is powered by Board Manager's **`ldo`** peripheral instead of esp_video's built-in LDO initialization), an **`ldo_*`** dependency must also be added (see [periph_ldo.yml](../peripherals/periph_ldo/periph_ldo.yml) for configuration), or it can share the same **`ldo`** peripheral with **`DSI LCD`**.

**`board_devices.yaml`**

```yaml
  - name: camera
    type: camera
    sub_type: "csi"
    config:
      csi_config:
        reset_io: 26           # [IO] GPIO pin for reset signal (example value)
        pwdn_io: -1            # [IO] GPIO pin for power down signal (example value)
        dont_init_ldo: false   # [TO_BE_CONFIRMED] If true, MIPI-CSI video device will not initialize the LDO (default: true)
        # If using the periph_ldo in esp_board_manager to manage the ldo peripheral, it needs to be set to true
        xclk_config:
          xclk_pin: 11            # [IO] GPIO pin for camera XCLK signal (default: -1, not connected)
          xclk_freq_hz: 24000000  # [TO_BE_CONFIRMED] XCLK frequency in Hz (default: 20MHz)
    peripherals:
      - name: i2c_master      # [TO_BE_CONFIRMED] I2C bus name for camera control
        frequency: 100000     # I2C frequency in Hz
      # - name: ldo_mipi        # [TO_BE_CONFIRMED] LDO peripheral for csi power management
```

For the pattern where **`dont_init_ldo: true`** and `camera` uses **`ldo_mipi`** side by side, see **Board Reference**: [esp32_p4_function_ev/board_devices.yaml](../boards/esp32_p4_function_ev/board_devices.yaml), [esp32_p4_function_ev/board_peripherals.yaml](../boards/esp32_p4_function_ev/board_peripherals.yaml).

### SPI Camera (`sub_type: spi`)

**`board_peripherals.yaml`**

Requires an existing **`i2c_master`** dependency; full configuration not repeated here, see [periph_i2c.yml](../peripherals/periph_i2c/periph_i2c.yml).

**`board_devices.yaml`**

```yaml
  - name: camera
    type: camera
    sub_type: spi
    config:
      spi_config:
        # esp_video_init_spi_config_t (esp_video_init.h), excluding sccb_config (filled at init from periph_i2c).
        # SPI CAM interface type: ESP_CAM_CTLR_SPI_CAM_INTF_SPI or ESP_CAM_CTLR_SPI_CAM_INTF_PARLIO
        intf: ESP_CAM_CTLR_SPI_CAM_INTF_SPI
        # SPI CAM data I/O mode (SPI interface only supports 1-bit): ESP_CAM_CTLR_SPI_CAM_IO_MODE_1BIT / _2BIT
        io_mode: ESP_CAM_CTLR_SPI_CAM_IO_MODE_1BIT
        spi_port: 1                         # [TO_BE_CONFIRMED] SPI port
        spi_cs_pin: 27                      # [IO] SPI CS pin
        spi_sclk_pin: 25                    # [IO] SPI SCLK pin
        spi_data0_io_pin: 23                # [IO] SPI data0 I/O pin
        # SPI data1 I/O pin (only required when io_mode is ESP_CAM_CTLR_SPI_CAM_IO_MODE_2BIT, set to -1 if not used)
        spi_data1_io_pin: -1                # [IO] SPI data1 I/O pin
        # SPI interface camera sensor reset pin, if hardware has no reset pin, set reset_pin to be -1
        reset_pin: -1                       # [IO] SPI interface camera sensor reset pin
        # SPI interface camera sensor power down pin, if hardware has no power down pin, set pwdn_pin to be -1
        pwdn_pin: -1                        # [IO] SPI interface camera sensor power down pin
        # Output clock resource / frequency / pin for SPI interface camera sensor
        xclk_source: ESP_CAM_SENSOR_XCLK_LEDC
        xclk_freq: 24000000                # [TO_BE_CONFIRMED] XCLK frequency in Hz (default: 20MHz)
        xclk_pin: 24                       # [IO] XCLK pin
        # Used when xclk_source is ESP_CAM_SENSOR_XCLK_LEDC (ledc_timer_t / ledc_clk_cfg_t / ledc_channel_t)
        xclk_ledc_cfg:
          timer: 0                          # [TO_BE_CONFIRMED] The timer source of channel
          clk_cfg: LEDC_AUTO_CLK            # [TO_BE_CONFIRMED] LEDC source clock from ledc_clk_cfg_t
          channel: 0                        # [TO_BE_CONFIRMED] LEDC channel used for XCLK
    peripherals:
      - name: i2c_master                    # Must match a periph_i2c name; provides SCCB i2c_handle to esp_video
        frequency: 100000                   # SCCB I2C frequency (Hz)
```

---

## Button (`button`)

- **Purpose**: `type` is **`button`**; `sub_type` can be **`gpio`** (single GPIO button), **`adc_single`** (single ADC button), or **`adc_multi`** (multi-button resistor network on the same ADC channel).
- **Full Fields (YAML)**: [dev_button.yaml](../devices/dev_button/dev_button.yaml) (including `events_cfg`, etc.; do not use the deprecated `events` field).
- **Reference Code**: [test_dev_button.c](../test_apps/main/test_dev_button.c)

### GPIO (`sub_type: gpio`)

**`board_peripherals.yaml`**

```yaml
  - name: gpio_button_io_0
    type: gpio
    role: io
    config:
      pin: 0                   # [IO] GPIO pin number
      mode: "GPIO_MODE_INPUT"  # [TO_BE_CONFIRMED] GPIO mode
```

**`board_devices.yaml`**

```yaml
  - name: gpio_button_0            # The name of the device, must be unique
    type: button                   # The type of the device
    sub_type: gpio                 # Button sub type: "gpio", "adc_single", or "adc_multi"
    config:
      active_level: 0              # [TO_BE_CONFIRMED] Active level (0-low, 1-high) when button is pressed
    peripherals:
      - name: gpio_button_io_0     # [TO_BE_CONFIRMED] GPIO peripheral name
```

### ADC (`sub_type: adc_single` / `adc_multi`)

**`board_peripherals.yaml`**

```yaml
  - name: adc_oneshot
    type: adc
    role: oneshot
    config:
      # ADC unit, optional values are ADC_UNIT_1 and ADC_UNIT_2, should be less than 'SOC_ADC_PERIPH_NUM'
      # please check the 'SOC_ADC_PERIPH_NUM' in 'soc/soc_caps.h' for the valid values
      unit_id: ADC_UNIT_1  # [TO_BE_CONFIRMED] ADC unit ID
      # ADC channel to be used.
      # Use adc_oneshot_io_to_channel() and adc_oneshot_channel_to_io() to get the corresponding relationship between ADC channels and ADC IO.
      channel_id: 4                      # [TO_BE_CONFIRMED] ADC channel ID
```

**ADC Peripheral Full Fields (YAML)**: [periph_adc.yml](../peripherals/periph_adc/periph_adc.yml)

**`board_devices.yaml` (`adc_single`)**

```yaml
  - name: adc_button_0     # The name of the device, must be unique
    type: button           # The type of the device
    sub_type: adc_single   # Button sub type: "gpio", "adc_single", or "adc_multi"
    config:
      button_index: 0      # [TO_BE_CONFIRMED] Button index on the channel
      min_voltage: 0       # [TO_BE_CONFIRMED] Minimum voltage in mV for button press
      max_voltage: 500     # [TO_BE_CONFIRMED] Maximum voltage in mV for button press
    peripherals:
      - name: adc_oneshot  # [TO_BE_CONFIRMED] ADC peripheral name
```

**`board_devices.yaml` (`adc_multi`)**

```yaml
  - name: adc_button_group         # The name of the device, must be unique
    type: button                   # The type of the device
    sub_type: adc_multi            # Button sub type: "gpio", "adc_single", or "adc_multi"
    config:
      button_num: 6                                      # [TO_BE_CONFIRMED] Number of buttons in the group, must not be greater than CONFIG_ADC_BUTTON_MAX_BUTTON_PER_CHANNEL (default: 8)
      voltage_range: [380, 820, 1110, 1650, 1980, 2410]  # [TO_BE_CONFIRMED] Nominal voltage for each button in mV
      # This voltage_range configuration is only applicable to the Korvo2 V3 development board.
      # Please refer to the schematic of your development board to determine the correct configuration.
      button_labels: ["VOLUME_UP", "VOLUME_DOWN", "SET", "PLAY", "MUTE", "REC"]  # Labels for each button
      # Optional but recommended labels for each button in the group.
      # When esp_board_device_callback_register(name, cb, NULL) is used, these labels are passed as callback user_data by default.
      # If omitted, ADC multi-button callbacks still work, but the default user_data is NULL so the callback cannot distinguish buttons by label.
      max_voltage: 3000                                  # Maximum voltage in mV for this ADC channel (default: 3000)
    peripherals:
      - name: adc_oneshot          # [TO_BE_CONFIRMED] ADC peripheral name
```

**`voltage_range`** and **`button_num`** must match the hardware voltage divider.
**Board Reference**: [esp32_s3_korvo2_v3/board_devices.yaml](../boards/esp32_s3_korvo2_v3/board_devices.yaml) (`adc_multi` with Korvo2 V3 voltage divider network); for a different voltage divider, see [lyrat_mini_v1_1/board_devices.yaml](../boards/lyrat_mini_v1_1/board_devices.yaml). **Do not blindly copy values when switching boards**.
