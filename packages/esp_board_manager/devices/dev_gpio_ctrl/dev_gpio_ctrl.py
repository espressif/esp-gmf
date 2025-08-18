# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# GPIO control device config parser
VERSION = 'v1.0.0'

import sys

def get_includes():
    """Get required header includes for GPIO control device"""
    return ['dev_gpio_ctrl.h']

def parse(name, config, peripherals_dict=None):
    """
    Parse GPIO control device configuration from YAML to C structure

    Args:
        name (str): Device name
        config (dict): Device configuration dictionary
        peripherals_dict (dict): Dictionary of available peripherals for validation

    Returns:
        dict: Parsed configuration with 'struct_type' and 'struct_init' keys
    """
    # Extract configuration parameters
    device_config = config.get('config', {})
    peripherals_list = config.get('peripherals', [])

    # GPIO control configuration
    active_level = device_config.get('active_level', 1)
    default_level = device_config.get('default_level', 0)

    # Extract GPIO peripheral name from peripherals list
    gpio_name = 'gpio-0'  # Default fallback
    if peripherals_list and len(peripherals_list) > 0:
        gpio_periph = peripherals_list[0]
        if isinstance(gpio_periph, dict) and 'name' in gpio_periph:
            gpio_periph_name = gpio_periph['name']
        else:
            gpio_periph_name = str(gpio_periph)

        if gpio_periph_name.startswith('gpio') or gpio_periph_name.startswith('gpio_'):
            # Check if peripheral exists in peripherals_dict if provided
            if peripherals_dict is not None and gpio_periph_name not in peripherals_dict:
                raise ValueError(f"GPIO device {name} references undefined peripheral '{gpio_periph_name}'")
            gpio_name = gpio_periph_name
        else:
            raise ValueError(f'GPIO device {name} should reference a GPIO peripheral, got: {gpio_periph_name}')
    else:
        raise ValueError(f'GPIO device {name} must have at least one GPIO peripheral defined')

    # Build structure initialization
    struct_init = {
        'name': f'"{name}"',
        'type': str(config.get('type', 'gpio_ctrl')),
        'gpio_name': gpio_name,
        'active_level': active_level,
        'default_level': default_level
    }

    return {
        'struct_type': 'dev_gpio_ctrl_config_t',
        'struct_init': struct_init
    }
