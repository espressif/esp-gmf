# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# I2C peripheral config parser
VERSION = 'v1.0.0'

import sys

def get_includes() -> list:
    """Return list of required include headers for I2C peripheral"""
    return [
        'driver/i2c_master.h',
        'driver/i2c_types.h',
        'hal/gpio_types.h'
    ]

def parse(name: str, config: dict) -> dict:
    """Parse I2C peripheral configuration.

    Args:
        name: Peripheral name (e.g. 'i2c-0')
        config: Configuration dictionary containing:
            - port: I2C port number
            - pins: SDA and SCL pin numbers
            - enable_internal_pullup: Enable internal pullup resistors
            - glitch_count: Glitch filter count
            - intr_priority: Interrupt priority

    Returns:
        Dictionary containing I2C configuration structure
    """
    try:
        # Get the actual config
        config = config.get('config', {})

        # Get port number
        i2c_port = config.get('port', -1)  # -1 means auto selecting if not specified
        if i2c_port >= 0:
            i2c_port = f'I2C_NUM_{i2c_port}'  # Convert to enum format

        # Get pins from the config
        pins = config.get('pins', {})
        sda_io = int(pins.get('sda', -1))  # Convert to int, -1 means not set
        scl_io = int(pins.get('scl', -1))  # Convert to int, -1 means not set



        # Get pullup setting
        enable_internal_pullup = config.get('enable_internal_pullup', True)

        # Get glitch filter setting
        glitch_ignore_cnt = config.get('glitch_count', 7)  # Default to 7 if not set

        # Get interrupt priority setting
        intr_priority = config.get('intr_priority', 1)  # Default to 1 if not set
        if intr_priority is None or intr_priority == '':
            intr_priority = 1  # Set default if empty or None

        return {
            'struct_type': 'i2c_master_bus_config_t',
            'struct_var': f'{name}_cfg',
            'struct_init': {
                'i2c_port': i2c_port if i2c_port != -1 else 'I2C_NUM_AUTO',
                'sda_io_num': sda_io,
                'scl_io_num': scl_io,
                'clk_source': 'I2C_CLK_SRC_DEFAULT',  # Default to APB clock
                'glitch_ignore_cnt': glitch_ignore_cnt,
                'intr_priority': intr_priority,
                'trans_queue_depth': 0,  # Default queue depth
                'flags': {
                    'enable_internal_pullup': enable_internal_pullup,
                }
            }
        }

    except ValueError as e:
        # Re-raise ValueError with more context
        raise ValueError(f"YAML validation error in I2C peripheral '{name}': {e}")
    except Exception as e:
        # Catch any other exceptions and provide context
        raise ValueError(f"Error parsing I2C peripheral '{name}': {e}")
