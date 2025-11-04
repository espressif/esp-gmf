# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# LDO peripheral config parser
VERSION = 'v1.0.0'

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'generators'))

def get_includes() -> list:
    """Return list of required include headers for LDO peripheral"""
    return [
        'esp_ldo_regulator.h',
    ]

def parse(name: str, config: dict) -> dict:
    """Parse LDO peripheral configuration from YAML to C structure

    Args:
        name: Peripheral name
        config: Configuration dictionary from YAML

    Returns:
        Dictionary containing LDO configuration structure for esp_ldo_channel_config_t
    """
    try:
        # Get the config dictionary
        ldo_cfg = config.get('config', {})

        # Get ldo_cfg with validation
        chan_id = ldo_cfg.get('chan_id', 3)
        if not isinstance(chan_id, int) or chan_id < 0:
            raise ValueError(f'Invalid chan_id: {chan_id}. Must be an integer >= 0.')
        voltage_mv = ldo_cfg.get('voltage_mv', 2500)
        if not isinstance(voltage_mv, int) or voltage_mv < 0:
            raise ValueError(f'Invalid voltage_mv: {voltage_mv}. Must be an integer >= 0.')
        adjustable = ldo_cfg.get('adjustable', 1)
        if adjustable not in [0, 1]:
            raise ValueError(f'Invalid adjustable: {adjustable}. Must be 0 or 1.')
        owned_by_hw = ldo_cfg.get('owned_by_hw', 0)
        if owned_by_hw not in [0, 1]:
            raise ValueError(f'Invalid owned_by_hw: {owned_by_hw}. Must be 0 or 1.')
        
        # Create base config
        result = {
            'struct_type': 'esp_ldo_channel_config_t',
            'struct_var': f'{name}_cfg',
            'struct_init': {
                'chan_id': chan_id,
                'voltage_mv': voltage_mv,
                'flags': {
                    'adjustable': adjustable,
                    'owned_by_hw': owned_by_hw,
                }
            }
        }

        return result

    except ValueError as e:
        # Re-raise ValueError with more context
        raise ValueError(f"YAML validation error in LDO peripheral '{name}': {e}")
    except Exception as e:
        # Catch any other exceptions and provide context
        raise ValueError(f"Error parsing LDO peripheral '{name}': {e}")
