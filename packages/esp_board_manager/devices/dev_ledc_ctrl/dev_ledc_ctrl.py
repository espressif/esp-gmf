# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# LEDC control device config parser
VERSION = 'v1.0.0'

import sys

def get_includes():
    """Get required header includes for LEDC control device"""
    return ['dev_ledc_ctrl.h']

def parse(name, config, peripherals_dict=None):
    """
    Parse LEDC control device configuration from YAML to C structure

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

    # LEDC control configuration with defaults
    default_percent = device_config.get('default_percent', 100)  # Default 100% brightness

    # Extract LEDC peripheral name from peripherals list
    # ledc_name = "ledc-0"  # Default fallback
    if peripherals_list and len(peripherals_list) > 0:
        ledc_periph = peripherals_list[0]
        if isinstance(ledc_periph, dict) and 'name' in ledc_periph:
            ledc_periph_name = ledc_periph['name']
        else:
            ledc_periph_name = str(ledc_periph)

        if ledc_periph_name.startswith('ledc') or ledc_periph_name.startswith('ledc_'):
            # Check if peripheral exists in peripherals_dict if provided
            if peripherals_dict is not None and ledc_periph_name not in peripherals_dict:
                raise ValueError(f"LEDC device {name} references undefined peripheral '{ledc_periph_name}'")
            ledc_name = ledc_periph_name
        else:
            raise ValueError(f'LEDC device {name} should reference an LEDC peripheral, got: {ledc_periph_name}')
    else:
        raise ValueError(f'LEDC device {name} must have at least one LEDC peripheral defined')

    # Build structure initialization
    struct_init = {
        'name': f'"{name}"',
        'type': str(config.get('type', 'ledc_ctrl')),
        'ledc_name': ledc_name,
        'default_percent': default_percent
    }

    return {
        'struct_type': 'dev_ledc_ctrl_config_t',
        'struct_init': struct_init
    }
