# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# Power control device config parser
VERSION = 'v1.0.0'

import sys

def get_includes():
    """Get required header includes for power control device"""
    return ['dev_power_ctrl.h']

def parse(name, config, peripherals_dict=None):
    """
    Parse power control device configuration from YAML to C structure

    Args:
        name (str): Device name
        config (dict): Device configuration dictionary
        peripherals_dict (dict): Dictionary of available peripherals for validation

    Returns:
        dict: Parsed configuration with 'struct_type' and 'struct_init' keys
    """
    # Extract configuration parameters
    device_config = config.get('config', {})
    sub_type = config.get('sub_type', '')

    if not sub_type:
        raise ValueError(f"Power control device '{name}' missing required field 'sub_type'")

    # Build base structure initialization
    struct_init = {
        'name': name,
        'sub_type': sub_type,
    }

    # Handle different sub_types
    if sub_type == 'gpio':
        # Get peripherals from device_config
        peripherals = device_config.get('peripherals', [])
        if not peripherals:
            raise ValueError(f"GPIO power control device '{name}' missing required field 'peripherals'")

        # Find the GPIO peripheral
        gpio_peripheral = None
        for periph in peripherals:
            if isinstance(periph, dict) and 'name' in periph:
                # Check if this peripheral is a GPIO peripheral
                periph_name = periph.get('name', '')
                if peripherals_dict and periph_name in peripherals_dict:
                    periph_obj = peripherals_dict[periph_name]
                    if hasattr(periph_obj, 'type') and periph_obj.type == 'gpio':
                        gpio_peripheral = periph
                        break

        if not gpio_peripheral:
            raise ValueError(f"GPIO power control device '{name}' missing GPIO peripheral in peripherals list")

        # Get GPIO peripheral name
        gpio_periph_name = gpio_peripheral.get('name')
        # Get active_level from peripheral configuration
        active_level = gpio_peripheral.get('active_level', 1)  # Default to active high
        if active_level not in [0, 1]:
            raise ValueError(f"GPIO power control device '{name}' has invalid active_level: {active_level}. Must be 0 or 1")

        # Set the gpio member of the union
        sub_cfg_union = {
            'gpio': {
                'gpio_name': gpio_periph_name,
                'active_level': active_level
            }
        }
        struct_init['sub_cfg'] = sub_cfg_union

    else:
        raise ValueError(f"Unsupported power control sub_type '{sub_type}' for device '{name}'")

    return {
        'struct_type': 'dev_power_ctrl_config_t',
        'struct_var': f'{name}_cfg',
        'struct_init': struct_init
    }
