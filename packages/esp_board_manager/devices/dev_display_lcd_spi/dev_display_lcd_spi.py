# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# LCD display SPI device config parser
VERSION = 'v1.0.0'

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

# Define valid enum values based on ESP-IDF LCD driver
VALID_ENUMS = {
    'rgb_ele_order': [
        'LCD_RGB_ELEMENT_ORDER_RGB',
        'LCD_RGB_ELEMENT_ORDER_BGR'
    ],
    'data_endian': [
        'LCD_RGB_DATA_ENDIAN_BIG',
        'LCD_RGB_DATA_ENDIAN_LITTLE'
    ]
}

def get_includes() -> list:
    """Return list of required include headers for LCD display device"""
    return [
        'dev_display_lcd_spi.h'
    ]

def validate_enum_value(value: str, enum_type: str) -> bool:
    """Validate if the enum value is within the valid range for ESP-IDF LCD driver.

    Args:
        value: The enum value to validate
        enum_type: The type of enum (rgb_ele_order, data_endian, etc.)

    Returns:
        True if valid, False otherwise
    """
    if enum_type not in VALID_ENUMS:
        print(f"⚠️  WARNING: Unknown enum type '{enum_type}' for validation")
        return True  # Allow unknown types to pass

    if value not in VALID_ENUMS[enum_type]:
        print(f"❌ YAML VALIDATION ERROR: Invalid {enum_type} value '{value}'!")
        print(f'   File: board_devices.yaml')
        print(f'   📋 Device: LCD display configuration')
        print(f"   ❌ Invalid value: '{value}'")
        print(f'   ✅ Valid values: {VALID_ENUMS[enum_type]}')
        print(f'   ℹ️ Please use one of the valid enum values listed above')
        return False

    return True

def get_enum_value(value: str, full_enum: str, enum_type: str = None) -> str:
    """Helper function to get enum value with validation.

    Args:
        value: The value from YAML config
        full_enum: The complete ESP-IDF enum value to use if value is not provided
        enum_type: The type of enum for validation (optional)

    Returns:
        The enum value to use
    """
    if not value:
        result = full_enum
    else:
        result = value

    # Validate the enum value if enum_type is provided
    if enum_type and not validate_enum_value(result, enum_type):
        raise ValueError(f"YAML validation error in LCD display device: Invalid {enum_type} value '{result}'. Valid values: {VALID_ENUMS.get(enum_type, [])}")

    return result

def parse(name: str, config: dict, peripherals_dict=None) -> dict:
    """Parse LCD display device configuration from YAML to C structure"""

    # Parse the device name - use name directly for C naming
    c_name = name.replace('-', '_')  # Convert hyphens to underscores for C naming

    # Get the device config and peripherals
    device_config = config.get('config', {})
    peripherals = config.get('peripherals', [])

    # Get chip name from device level
    chip_name = config.get('chip', 'unknown')

    # Initialize default configurations
    spi_cfg = {'name': '', 'port': 0}

    # Process peripherals
    for periph in peripherals:
        periph_name = periph.get('name', '')
        if periph_name.startswith('spi'):
            # Check if peripheral exists in peripherals_dict if provided
            if peripherals_dict is not None and periph_name not in peripherals_dict:
                raise ValueError(f"LCD display device {name} references undefined peripheral '{periph_name}'")
            # This is SPI configuration
            # Get port from peripheral config if available
            port = 0
            if peripherals_dict and periph_name in peripherals_dict:
                peripheral_config = peripherals_dict[periph_name]
                spi_bus_config = peripheral_config.get('config', {}).get('spi_bus_config', {})
                port = spi_bus_config.get('spi_port', 0)
            spi_cfg = {
                'name': periph_name,  # Use original name from YAML
                'port': port
            }

    # Get nested configurations
    io_spi_config = device_config.get('io_spi_config', {})
    lcd_panel_config = device_config.get('lcd_panel_config', {})

    # Get peripherals from device level or nested io_spi_config
    if not peripherals and 'peripherals' in io_spi_config:
        # Try to get peripherals from nested io_spi_config
        peripherals = io_spi_config.get('peripherals', [])

    # Extract SPI peripheral name from peripherals
    spi_name = ''
    for periph in peripherals:
        if isinstance(periph, dict):
            periph_name = periph.get('name', '')
            if periph_name.startswith('spi'):
                # Check if peripheral exists in peripherals_dict if provided
                if peripherals_dict is not None and periph_name not in peripherals_dict:
                    raise ValueError(f"LCD display device {name} references undefined peripheral '{periph_name}'")
                spi_name = periph_name
                break
        elif isinstance(periph, str) and periph.startswith('spi'):
            # Check if peripheral exists in peripherals_dict if provided
            if peripherals_dict is not None and periph not in peripherals_dict:
                raise ValueError(f"LCD display device {name} references undefined peripheral '{periph}'")
            spi_name = periph
            break

    # Parse SPI interface settings
    lcd_cmd_bits = int(io_spi_config.get('lcd_cmd_bits', 8))
    lcd_param_bits = int(io_spi_config.get('lcd_param_bits', 8))
    pclk_hz = int(io_spi_config.get('pclk_hz', 40000000))
    trans_queue_depth = int(io_spi_config.get('trans_queue_depth', 2))

    # Parse SPI flags from io_spi_config.flags
    io_spi_flags = io_spi_config.get('flags', {})
    dc_high_on_cmd = bool(io_spi_flags.get('dc_high_on_cmd', False))
    dc_low_on_data = bool(io_spi_flags.get('dc_low_on_data', False))
    dc_low_on_param = bool(io_spi_flags.get('dc_low_on_param', False))
    octal_mode = bool(io_spi_flags.get('octal_mode', False))
    quad_mode = bool(io_spi_flags.get('quad_mode', False))
    sio_mode = bool(io_spi_flags.get('sio_mode', False))
    lsb_first = bool(io_spi_flags.get('lsb_first', False))
    cs_high_active = bool(io_spi_flags.get('cs_high_active', False))

    # Parse CS timing configuration
    cs_ena_pretrans = int(io_spi_config.get('cs_ena_pretrans', 0))
    cs_ena_posttrans = int(io_spi_config.get('cs_ena_posttrans', 0))

    # Parse vendor configuration
    vendor_config = lcd_panel_config.get('vendor_config', '')

    # Parse GPIO configurations
    reset_gpio_num = int(lcd_panel_config.get('reset_gpio_num', -1))
    reset_active_high = bool(lcd_panel_config.get('flags', {}).get('reset_active_high', False))

    # Parse RGB element order with validation
    rgb_ele_order = lcd_panel_config.get('rgb_ele_order', 'LCD_RGB_ELEMENT_ORDER_RGB')
    rgb_ele_order_enum = get_enum_value(rgb_ele_order, 'LCD_RGB_ELEMENT_ORDER_RGB', 'rgb_ele_order')

    # Parse data endian with validation
    data_endian = lcd_panel_config.get('data_endian', 'LCD_RGB_DATA_ENDIAN_BIG')
    data_endian_enum = get_enum_value(data_endian, 'LCD_RGB_DATA_ENDIAN_BIG', 'data_endian')

    # Parse bits per pixel
    bits_per_pixel = int(lcd_panel_config.get('bits_per_pixel', 16))

    # Parse display orientation and size parameters
    # These parameters can be configured at both config level and lcd_panel_config level
    # Priority: config level > lcd_panel_config level > default values
    swap_xy = bool(device_config.get('swap_xy', lcd_panel_config.get('swap_xy', False)))
    mirror_x = bool(device_config.get('mirror_x', lcd_panel_config.get('mirror_x', False)))
    mirror_y = bool(device_config.get('mirror_y', lcd_panel_config.get('mirror_y', False)))
    x_max = int(device_config.get('x_max', lcd_panel_config.get('x_max', 320)))
    y_max = int(device_config.get('y_max', lcd_panel_config.get('y_max', 240)))
    invert_color = bool(device_config.get('invert_color', False))

    result = {
        'struct_type': 'dev_display_lcd_spi_config_t',
        'struct_var': f'{c_name}_cfg',
        'struct_init': {
            'name': c_name,
            'chip': chip_name,
            'type': str(config.get('type', 'display_lcd_spi')),
            'spi_name': spi_name,

            # IO SPI Configuration (esp_lcd_panel_io_spi_config_t)
            'io_spi_config': {
                'cs_gpio_num': int(io_spi_config.get('cs_gpio_num', -1)),
                'dc_gpio_num': int(io_spi_config.get('dc_gpio_num', -1)),
                'spi_mode': int(io_spi_config.get('spi_mode', 0)),
                'pclk_hz': pclk_hz,
                'trans_queue_depth': trans_queue_depth,
                'on_color_trans_done': None,
                'user_ctx': None,
                'lcd_cmd_bits': lcd_cmd_bits,
                'lcd_param_bits': lcd_param_bits,
                'cs_ena_pretrans': cs_ena_pretrans,
                'cs_ena_posttrans': cs_ena_posttrans,
                'flags': {
                    'dc_high_on_cmd': dc_high_on_cmd,
                    'dc_low_on_data': dc_low_on_data,
                    'dc_low_on_param': dc_low_on_param,
                    'octal_mode': octal_mode,
                    'quad_mode': quad_mode,
                    'sio_mode': sio_mode,
                    'lsb_first': lsb_first,
                    'cs_high_active': cs_high_active
                }
            },

            # Panel Device Configuration (esp_lcd_panel_dev_config_t)
            'panel_config': {
                'reset_gpio_num': reset_gpio_num,
                'rgb_ele_order': rgb_ele_order_enum,
                'data_endian': data_endian_enum,
                'bits_per_pixel': bits_per_pixel,
                'flags': {
                    'reset_active_high': reset_active_high
                },
                'vendor_config': vendor_config
            },

            # Display orientation and size parameters
            'swap_xy': swap_xy,
            'mirror_x': mirror_x,
            'mirror_y': mirror_y,
            'x_max': x_max,
            'y_max': y_max,
            'invert_color': invert_color
        }
    }

    return result
