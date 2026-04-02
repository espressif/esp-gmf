# Board Manager 板级配置模板（最小模板）

本文介绍 `board_peripherals.yaml` 与 `board_devices.yaml` 的各项配置，以及**常见的最小配置模板**，便于理解和新建板子。
**注意：本文仅展示最小配置模板，若出现功能不正常，请参考完整配置进行调试！**

**YAML 规则：**

```yaml
# board_peripherals.yaml
peripherals:
  - name: <peripheral_name>    # 必填：唯一标识符
    type: <peripheral_type>    # 必填：类型标识符
    role: <role>               # 条件必填：工作模式（如 master/slave、tx/rx、continuous/oneshot 等，取决于外设类型）
    format: <format_string>    # 条件必填：数据格式（当前仅 I2S 使用，如 std-out、tdm-in、pdm-out）
    config: <configuration>    # 必填：外设专属配置

# board_devices.yaml
devices:
  - name: <device_name>       # 必填：唯一标识符
    type: <device_type>       # 必填：类型标识符
    chip: <chip_name>         # 条件必填：设备芯片名称（如 LCD 芯片、IO 扩展芯片等）
    sub_type: <sub_type>      # 条件必填：部分设备存在子类型（如 LCD 分为 SPI、DSI、ParlIO）
    config:
      <configurations>        # 必填：设备专属配置
    peripherals:              # 条件必填：部分设备运行依赖指定的外设，如I2C，SPI，GPIO 等
      - name: <periph_name>   # 条件必填：若 peripherals 存在则填写对应 peripheral 的 name
    dependencies:             # 条件必填：部分设备依赖额外的组件（如 LCD、LCD Touch 等）
      <component_name>:
        require: <scope>
        version: <version_spec>
```

*适配新开发板时，应重点检查所有带 `[IO]` 和 `[TO_BE_CONFIRMED]` 标记的配置项，这些配置项与硬件引脚分配及功能能否正常运转密切相关，请确保它们与实际硬件匹配。*

- 详细 YAML 规则请参考：[device_and_peripheral_rules_cn.md](device_and_peripheral_rules_cn.md)
- 建板流程、板目录自定义代码说明等请参考：[how_to_customize_board_cn.md](how_to_customize_board_cn.md)

**使用约定：**

- 下列片段需分别粘贴到板目录下的 **`board_peripherals.yaml`**（`peripherals:` 列表内）与 **`board_devices.yaml`**（`devices:` 列表内）。
- 设备里 `peripherals:` 所引用的 `name` 必须与 `board_peripherals.yaml` 中**完全一致**；外设命名**需要以类型开头**（如 `i2c_master`、`i2s_audio_out`）。

---

## 目录

- [Audio codec（`audio_codec`）](#audio-codecaudio_codec)
  - [播放（DAC，`dac_enabled: true`）](#播放dacdac_enabled-true)
  - [录音（ADC，`adc_enabled: true`）](#录音adcadc_enabled-true)
  - [全双工（同一颗 Codec 芯片）](#全双工同一颗-codec-芯片)
  - [无外部 Codec 的录音和播放](#无外部-codec-的录音和播放)
  - [I2S 更多详细配置](#i2s-更多详细配置)
- [LCD（`display_lcd`）](#lcddisplay_lcd)
  - [DSI（`sub_type: dsi`）](#dsisub_type-dsi)
  - [SPI（`sub_type: spi`）](#spisub_type-spi)
  - [PARLIO（`sub_type: parlio`）](#parliosub_type-parlio)
- [LEDC 背光（`ledc_ctrl`）](#ledc-背光ledc_ctrl)
- [LCD Touch（`lcd_touch_i2c`）](#lcd-touchlcd_touch_i2c)
- [Camera（`camera`）](#cameracamera)
  - [DVP（`sub_type: dvp`）](#dvpsub_type-dvp)
  - [CSI（`sub_type: csi`）](#csisub_type-csi)
  - [SPI 相机（`sub_type: spi`）](#spi-相机sub_type-spi)
- [Button（`button`）](#buttonbutton)
  - [GPIO（`sub_type: gpio`）](#gpiosub_type-gpio)
  - [ADC（`sub_type: adc_single` / `adc_multi`）](#adcsub_type-adc_single--adc_multi)

---

## Audio codec（`audio_codec`）

- **介绍**：外接 Codec 做播放（DAC）或采集（ADC）时，外设侧至少需要 **一路 I2C**（控制）与 **一路 I2S**（数据；播放常用 `std-out`，录音常用 `std-in`）；功放 GPIO 为可选。
- **约束**：同一逻辑设备实例**不要同时**将 `adc_enabled` 与 `dac_enabled` 置为 true；一颗物理 Codec 全双工时，在板级配置中拆成两个单向设备（例如 `audio_dac` + `audio_adc`），见「全双工」。
- **完整字段（YAML）**：[dev_audio_codec.yaml](../devices/dev_audio_codec/dev_audio_codec.yaml)
- **参考代码**：

  使用 SD 卡单独录制及播放：[record_to_sdcard.c](../examples/record_to_sdcard/main/record_to_sdcard.c)、[play_sdcard_music.c](../examples/play_sdcard_music/main/play_sdcard_music.c)；

  不使用 SD 卡单独播放：[play_embed_music.c](../examples/play_embed_music/main/play_embed_music.c)；

  不使用 SD 卡边录制边播放：[record_and_play.c](../examples/record_and_play/main/record_and_play.c)；

### 播放（DAC，`dac_enabled: true`）

**`board_peripherals.yaml`**：

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

**`board_devices.yaml`**：

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

若板子无 PA 控制引脚，则无需添加 `gpio_pa_control` 依赖以及外设配置。

### 录音（ADC，`adc_enabled: true`）

**`board_peripherals.yaml`**（可与播放共用同一 `i2c_master`；I2S 录音一般用独立 `std-in` 外设名）：

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

**`board_devices.yaml`**：

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

### 全双工（同一颗 Codec 芯片）

在 `board_devices.yaml` 中配置**两个** `type: audio_codec` 设备（例如 `audio_dac` + `audio_adc`），分别只开 DAC / 只开 ADC；**I2C 控制**可指向同一个 `i2c_master`（可用 YAML `&` / `*` 锚点避免重复写 `address` / `frequency`）。
**板级参考**：[esp32_s3_korvo2l/board_devices.yaml](../boards/esp32_s3_korvo2l/board_devices.yaml)。

### 无外部 Codec 的录音和播放

上述两个示例以**外接 Codec + I2S** 为主。若无需使用 Codec 芯片，直接使用 **内部模数转换（ADC）采集音频** 或 **PDM 直驱扬声器** 等模式，设备字段以 [dev_audio_codec.yaml](../devices/dev_audio_codec/dev_audio_codec.yaml) 中 **ADC Mic Usage Mode** 为准；

**板级参考**：[esp32_c3_lyra/board_devices.yaml](../boards/esp32_c3_lyra/board_devices.yaml)、[esp32_c3_lyra/board_peripherals.yaml](../boards/esp32_c3_lyra/board_peripherals.yaml)（复用 `adc` 外设或 `adc_local_cfg`）。

### I2S 更多详细配置

上述模板仅展示 **I2S STD 模式**，推荐单麦克风单扬声器时使用。若存在多路麦克风以及回采数据，推荐使用 **TDM** 模式，可参考：

- **板级参考**：[esp32_s3_korvo2_v3/board_peripherals.yaml](../boards/esp32_s3_korvo2_v3/board_peripherals.yaml)（TDM 模式 I2S）
- **所有 I2S 模式**：[periph_i2s.yml](../peripherals/periph_i2s/periph_i2s.yml)

---

## LCD（`display_lcd`）

- **介绍**：LCD 根据接口类型可以分为 **`dsi`** / **`spi`** / **`parlio`** 三种 `sub_type`（目前 board manager 支持的模式）；需要根据实际情况填写 `chip`、`dependencies`，并在板目录 **`setup_device.c`** 中注册面板工厂函数。
- **工厂函数签名**：
  - DSI：**`lcd_dsi_panel_factory_entry_t(esp_lcd_dsi_bus_handle_t dsi_handle, dev_display_lcd_config_t *lcd_cfg, dev_display_lcd_handles_t *lcd_handles)`**
  - SPI / PARLIO：**`lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)`**
- **说明**：工厂函数与板级自定义流程见 [how_to_customize_board_cn.md](how_to_customize_board_cn.md) 第 6 节；示例见 [esp32_p4_function_ev/setup_device.c](../boards/esp32_p4_function_ev/setup_device.c)、[esp32_s3_korvo2_v3/setup_device.c](../boards/esp32_s3_korvo2_v3/setup_device.c) 等。
- **组件**：屏幕驱动与依赖版本见 [Component Registry](https://components.espressif.com/)。
- **完整字段（YAML）**：[dev_display_lcd.yaml](../devices/dev_display_lcd/dev_display_lcd.yaml)
- **参考代码**：[test_dev_lcd_init.c](../test_apps/main/test_dev_lcd_init.c)（`display_lcd`）、[test_dev_lcd_lvgl.c](../test_apps/main/test_dev_lcd_lvgl.c)（LVGL 示例）

### DSI（`sub_type: dsi`）

使用 DSI（MIPI）LCD 需要配置 LDO 外设进行供电，并配置 DSI 数据接口。

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

**板级参考**：[esp32_p4_function_ev/board_devices.yaml](../boards/esp32_p4_function_ev/board_devices.yaml)、[esp32_p4_function_ev/board_peripherals.yaml](../boards/esp32_p4_function_ev/board_peripherals.yaml)、[esp32_p4_function_ev/setup_device.c](../boards/esp32_p4_function_ev/setup_device.c)。

### SPI（`sub_type: spi`）

SPI LCD 需要配置 SPI 接口。

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

**板级参考**：[esp32_s3_korvo2_v3/board_devices.yaml](../boards/esp32_s3_korvo2_v3/board_devices.yaml)、[esp32_s3_korvo2_v3/board_peripherals.yaml](../boards/esp32_s3_korvo2_v3/board_peripherals.yaml)、[esp32_s3_korvo2_v3/setup_device.c](../boards/esp32_s3_korvo2_v3/setup_device.c)。

### PARLIO（`sub_type: parlio`）

PARLIO LCD 不需要显式依赖外设。

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

## LEDC 背光（`ledc_ctrl`）

PWM 背光由 **`periph_ledc`**（外设，`type: ledc`）提供硬件定时器与 GPIO 输出，再由 **`ledc_ctrl`** 设备（`type: ledc_ctrl`）绑定该外设并暴露亮度百分比等逻辑配置。

- **完整字段（设备 YAML）**：[dev_ledc_ctrl.yaml](../devices/dev_ledc_ctrl/dev_ledc_ctrl.yaml)
- **完整字段（外设 YAML）**：[periph_ledc.yml](../peripherals/periph_ledc/periph_ledc.yml)
- **参考代码**：[test_dev_ledc.c](../test_apps/main/test_dev_ledc.c)；与 LCD 联调时亦见 [test_dev_lcd_init.c](../test_apps/main/test_dev_lcd_init.c) 中背光相关路径（需 `CONFIG_ESP_BOARD_DEV_LEDC_CTRL_SUPPORT`）
- **板级参考**：**`ledc_ctrl` + `ledc` 外设** 的完整组合见 [esp32_p4_function_ev/board_peripherals.yaml](../boards/esp32_p4_function_ev/board_peripherals.yaml)、[esp32_p4_function_ev/board_devices.yaml](../boards/esp32_p4_function_ev/board_devices.yaml)。

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

## LCD Touch（`lcd_touch_i2c`）

- **用途**：I2C 触摸；`type` 为 **`lcd_touch_i2c`**。板级填写 `chip`、`dependencies`，并在 **`setup_device.c`** 中注册触摸工厂函数。
- **工厂函数签名**：**`lcd_touch_factory_entry_t(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *touch_dev_config, esp_lcd_touch_handle_t *ret_touch)`**
- **说明**：与 LCD 相同，见 [how_to_customize_board_cn.md](how_to_customize_board_cn.md) 第 6 节及上述板级 `setup_device.c` 示例。
- **组件**：触摸驱动与依赖见 [Component Registry](https://components.espressif.com/)。
- **完整字段（YAML）**：[dev_lcd_touch_i2c.yaml](../devices/dev_lcd_touch_i2c/dev_lcd_touch_i2c.yaml)
- **参考代码**：[test_dev_lcd_init.c](../test_apps/main/test_dev_lcd_init.c)（`test_dev_lcd_touch_init`）
- **板级参考**：[esp32_s3_korvo2_v3/board_devices.yaml](../boards/esp32_s3_korvo2_v3/board_devices.yaml)（`lcd_touch` + TT21100）、[esp32_p4_function_ev/board_devices.yaml](../boards/esp32_p4_function_ev/board_devices.yaml)（`lcd_touch` + GT911）；工厂注册见各自 `setup_device.c`。

**`board_peripherals.yaml`**

若触摸与已有设备共用 I2C，只需保证板级已存在匹配的 **`i2c_master`**（本节不重复展示完整配置，见 [periph_i2c.yml](../peripherals/periph_i2c/periph_i2c.yml)）。

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

## Camera（`camera`）

- **用途**：`type` 固定为 **`camera`**，`sub_type` 为 **`dvp`** / **`csi`** / **`spi`**；设备 `peripherals` 中须包含 **`i2c*`** 供传感器 SCCB（USB-UVC 等除外）。CSI 在设置 `dont_init_ldo: true` 时，还需声明单独 **`ldo*`** 外设。
- **完整字段（YAML）**：[dev_camera.yaml](../devices/dev_camera/dev_camera.yaml)
- **参考代码**：[test_dev_camera.c](../test_apps/main/test_dev_camera.c)

### DVP（`sub_type: dvp`）

**`board_peripherals.yaml`**

需依赖已存在的 **`i2c_master`**；本节不重复展示完整配置，见 [periph_i2c.yml](../peripherals/periph_i2c/periph_i2c.yml)。

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

**板级参考**：[esp32_s3_korvo2_v3/board_devices.yaml](../boards/esp32_s3_korvo2_v3/board_devices.yaml)、[esp32_s3_korvo2_v3/board_peripherals.yaml](../boards/esp32_s3_korvo2_v3/board_peripherals.yaml)。

### CSI（`sub_type: csi`）

**`board_peripherals.yaml`**

需依赖已存在的 **`i2c_master`**；本节不重复展示完整配置，见 [periph_i2c.yml](../peripherals/periph_i2c/periph_i2c.yml)。

除 **`i2c_master`** 外，若 **`dont_init_ldo: true`**（由 Board Manager 的 **`ldo`** 外设供电、不由 esp_video 内建初始化 LDO），须增加 **`ldo_*`** 依赖（配置见 [periph_ldo.yml](../peripherals/periph_ldo/periph_ldo.yml)），或与 **`DSI LCD`** 共享同一个 **`ldo`** 外设。

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

**`dont_init_ldo: true`** 时 `camera` 与 **`ldo_mipi`** 的并列写法见 **板级参考**：[esp32_p4_function_ev/board_devices.yaml](../boards/esp32_p4_function_ev/board_devices.yaml)、[esp32_p4_function_ev/board_peripherals.yaml](../boards/esp32_p4_function_ev/board_peripherals.yaml)。

### SPI 相机（`sub_type: spi`）

**`board_peripherals.yaml`**

需依赖已存在的 **`i2c_master`**；本节不重复展示完整配置，见 [periph_i2c.yml](../peripherals/periph_i2c/periph_i2c.yml)。

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

## Button（`button`）

- **用途**：`type` 为 **`button`**；**`sub_type`** 为 **`gpio`**（单 GPIO 按键）、**`adc_single`**（单 ADC 按键）或 **`adc_multi`**（同一 ADC 通道上的多键电阻网络）。
- **完整字段（YAML）**：[dev_button.yaml](../devices/dev_button/dev_button.yaml)（含 `events_cfg` 等；勿使用已废弃的 `events` 字段）。
- **参考代码**：[test_dev_button.c](../test_apps/main/test_dev_button.c)

### GPIO（`sub_type: gpio`）

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

### ADC（`sub_type: adc_single` / `adc_multi`）

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

**ADC 外设完整字段（YAML）**：[periph_adc.yml](../peripherals/periph_adc/periph_adc.yml)

**`board_devices.yaml`（`adc_single`）**

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

**`board_devices.yaml`（`adc_multi`）**

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

**`voltage_range`** 与 **`button_num`** 必须与硬件分压一致。
**板级参考**：[esp32_s3_korvo2_v3/board_devices.yaml](../boards/esp32_s3_korvo2_v3/board_devices.yaml)（`adc_multi` 与 Korvo2 V3 分压网）；对比另一套分压可参见 [lyrat_mini_v1_1/board_devices.yaml](../boards/lyrat_mini_v1_1/board_devices.yaml)，**换板后不可照搬数值**。
