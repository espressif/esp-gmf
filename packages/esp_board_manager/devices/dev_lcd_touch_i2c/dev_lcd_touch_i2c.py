# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# LCD touch I2C device config parser
VERSION = 'v1.0.0'

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

def get_includes() -> list:
    """Return list of required include headers for LCD touch I2C device"""
    return [
        'dev_lcd_touch_i2c.h'
    ]

def parse(name: str, config: dict, peripherals_dict=None) -> dict:
    """Parse LCD touch I2C device configuration from YAML to C structure"""

    # Parse the device name - use name directly for C naming
    c_name = name.replace('-', '_')  # Convert hyphens to underscores for C naming

    # Get the device config and peripherals
    device_config = config.get('config', {})
    peripherals = config.get('peripherals', [])

    # Get chip name from device level
    chip_name = config.get('chip', 'unknown')

    # Get nested configurations
    io_i2c_config = device_config.get('io_i2c_config', {})
    touch_config = device_config.get('touch_config', {})

    # Initialize default configurations
    i2c_cfg = {'name': '', 'port': 0, 'address': 0x5D, 'frequency': 400000}

    # Process I2C peripheral from io_i2c_config.peripherals
    io_i2c_peripherals = io_i2c_config.get('peripherals', [])
    for periph in io_i2c_peripherals:
        periph_name = periph.get('name', '')
        if periph_name.startswith('i2c'):
            # Check if peripheral exists in peripherals_dict if provided
            if peripherals_dict is not None and periph_name not in peripherals_dict:
                raise ValueError(f"Device '{name}' references undefined peripheral '{periph_name}'")
            # This is I2C configuration
            address = periph.get('address', 0x5D)  # Keep hex format
            # Get port from peripheral config if available
            port = 0
            if peripherals_dict and periph_name in peripherals_dict:
                peripheral_config = peripherals_dict[periph_name]
                port = peripheral_config.config.get('port', 0)
            i2c_cfg = {
                'name': periph_name,  # Use original name from YAML
                'port': port,
                'address': address,  # No need to convert, keep as hex
                'frequency': int(periph.get('frequency', 400000))
            }

    # Parse I2C interface settings
    dev_addr_raw = io_i2c_config.get('dev_addr', 0x5a)

    # Handle dev_addr configuration: support both single value and array
    if isinstance(dev_addr_raw, list):
        # Array format: [primary_addr, secondary_addr]
        i2c_addr_primary = int(dev_addr_raw[0]) if len(dev_addr_raw) > 0 else 0x5a
        i2c_addr_secondary = int(dev_addr_raw[1]) if len(dev_addr_raw) > 1 else 0
    else:
        # Single value format: use as primary address, secondary = 0
        i2c_addr_primary = int(dev_addr_raw)
        i2c_addr_secondary = 0

    # Keep dev_addr for backward compatibility (use primary address)
    dev_addr = i2c_addr_primary

    control_phase_bytes = int(io_i2c_config.get('control_phase_bytes', 1))
    dc_bit_offset = int(io_i2c_config.get('dc_bit_offset', 0))
    lcd_cmd_bits = int(io_i2c_config.get('lcd_cmd_bits', 8))
    lcd_param_bits = int(io_i2c_config.get('lcd_param_bits', 0))
    scl_speed_hz = int(io_i2c_config.get('scl_speed_hz', 100000))

    # Parse I2C flags from io_i2c_config.flags
    io_i2c_flags = io_i2c_config.get('flags', {})
    dc_low_on_data = bool(io_i2c_flags.get('dc_low_on_data', False))
    disable_control_phase = bool(io_i2c_flags.get('disable_control_phase', True))

    # Parse touch configuration
    x_max = int(touch_config.get('x_max', 320))
    y_max = int(touch_config.get('y_max', 240))
    rst_gpio_num = int(touch_config.get('rst_gpio_num', -1))
    int_gpio_num = int(touch_config.get('int_gpio_num', -1))

    # Parse touch levels
    levels = touch_config.get('levels', {})
    reset_level = int(levels.get('reset', 0))
    interrupt_level = int(levels.get('interrupt', 0))

    # Parse touch flags
    touch_flags = touch_config.get('flags', {})
    swap_xy = bool(touch_flags.get('swap_xy', False))
    mirror_x = bool(touch_flags.get('mirror_x', False))
    mirror_y = bool(touch_flags.get('mirror_y', False))

    result = {
        'struct_type': 'dev_lcd_touch_i2c_config_t',
        'struct_var': f'{c_name}_cfg',
        'struct_init': {
            'name': c_name,
            'chip': chip_name,
            'type': str(config.get('type', 'lcd_touch_i2c')),
            'i2c_name': i2c_cfg['name'],
            'i2c_addr': [f'0x{i2c_addr_primary:02x}', f'0x{i2c_addr_secondary:02x}'],

            # IO I2C Configuration (esp_lcd_panel_io_i2c_config_t)
            'io_i2c_config': {
                'dev_addr': dev_addr,
                'control_phase_bytes': control_phase_bytes,
                'dc_bit_offset': dc_bit_offset,
                'lcd_cmd_bits': lcd_cmd_bits,
                'lcd_param_bits': lcd_param_bits,
                'scl_speed_hz': scl_speed_hz,
                'flags': {
                    'dc_low_on_data': dc_low_on_data,
                    'disable_control_phase': disable_control_phase
                }
            },

            # Touch Configuration (esp_lcd_touch_config_t)
            'touch_config': {
                'x_max': x_max,
                'y_max': y_max,
                'rst_gpio_num': rst_gpio_num,
                'int_gpio_num': int_gpio_num,
                'levels': {
                    'reset': reset_level,
                    'interrupt': interrupt_level
                },
                'flags': {
                    'swap_xy': swap_xy,
                    'mirror_x': mirror_x,
                    'mirror_y': mirror_y
                },
                'process_coordinates': None,
                'interrupt_callback': None,
                'user_data': None,
                'driver_data': None
            }
        }
    }

    return result
