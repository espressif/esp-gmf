# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# SDM peripheral config parser
VERSION = 'v1.0.0'

import sys

import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'generators'))

def get_includes() -> list:
    """Return list of required include headers for SDM peripheral"""
    return [
        'periph_sdm.h',
    ]

def parse(name: str, config: dict) -> dict:
    """Parse SDM peripheral configuration from YAML to C structure

    Args:
        name: Peripheral name
        config: Configuration dictionary from YAML

    Returns:
        Dictionary containing SDM configuration structure
    """
    try:
        # Get the config dictionary
        cfg = config.get('config', {})

        # Get GPIO pin number with validation
        gpio_num = cfg.get('gpio_num', -1)
        if gpio_num < 0:
            raise ValueError(f'Invalid GPIO pin number: {gpio_num}. Must be >= 0.')

        # Get clock source with validation
        clk_src = cfg.get('clk_src', 'SDM_CLK_SRC_DEFAULT')

        # Get sample rate with validation
        sample_rate_hz = cfg.get('sample_rate_hz', 1000000)
        if sample_rate_hz <= 0:
            raise ValueError(f'Invalid sample rate: {sample_rate_hz}. Must be > 0.')

        # Get invert_out flag with validation
        invert_out = cfg.get('invert_out', False)
        if not isinstance(invert_out, bool):
            raise ValueError(f'Invalid invert_out value: {invert_out}. Must be boolean.')

        # Get io_loop_back flag with validation
        io_loop_back = cfg.get('io_loop_back', False)
        if not isinstance(io_loop_back, bool):
            raise ValueError(f'Invalid io_loop_back value: {io_loop_back}. Must be boolean.')

        # Create configuration structure
        result = {
            'struct_type': 'sdm_config_t',
            'struct_var': f'{name}_cfg',
            'struct_init': {
                'gpio_num': gpio_num,
                'clk_src': clk_src,
                'sample_rate_hz': sample_rate_hz,
                'flags': {
                    'invert_out': 1 if invert_out else 0,
                    'io_loop_back': 1 if io_loop_back else 0
                }
            }
        }

        return result

    except ValueError as e:
        # Re-raise ValueError with more context
        raise ValueError(f"YAML validation error in SDM peripheral '{name}': {e}")
    except Exception as e:
        # Catch any other exceptions and provide context
        raise ValueError(f"Error parsing SDM peripheral '{name}': {e}")
