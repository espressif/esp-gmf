# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# DAC peripheral config parser
VERSION = 'v1.0.0'

import sys

# Map string roles to enum values
DAC_ROLE_MAP = {
    'oneshot': 'ESP_BOARD_PERIPH_ROLE_ONESHOT',
    'continuous': 'ESP_BOARD_PERIPH_ROLE_CONTINUOUS',
    'cosine': 'ESP_BOARD_PERIPH_ROLE_COSINE'
}

def get_includes() -> list:
    """Return list of required include headers for DAC peripheral"""
    return [
        'periph_dac.h'
    ]

def validate_enum_value(value: str, enum_type: str) -> bool:
    """Validate if the enum value is within the valid range for ESP-IDF DAC driver.

    Args:
        value: The enum value to validate
        enum_type: The type of enum (dac_channel, dac_channel_mask, etc.)

    Returns:
        True if valid, False otherwise
    """
    # Define valid enum values based on ESP-IDF DAC driver
    valid_enums = {
        'dac_channel': [
            'DAC_CHAN_0',
            'DAC_CHAN_1'
        ],
        'dac_channel_mask': [
            'DAC_CHANNEL_MASK_CH0',
            'DAC_CHANNEL_MASK_CH1',
            'DAC_CHANNEL_MASK_ALL'
        ],
        'dac_continuous_chan_mode': [
            'DAC_CHANNEL_MODE_SIMUL',
            'DAC_CHANNEL_MODE_ALTER'
        ],
        'dac_cosine_atten': [
            'DAC_COSINE_ATTEN_DEFAULT',
            'DAC_COSINE_ATTEN_DB_0',
            'DAC_COSINE_ATTEN_DB_6',
            'DAC_COSINE_ATTEN_DB_12',
            'DAC_COSINE_ATTEN_DB_18'
        ],
        'dac_cosine_phase': [
            'DAC_COSINE_PHASE_0',
            'DAC_COSINE_PHASE_180'
        ]
    }

    if enum_type not in valid_enums:
        print(f"⚠️  WARNING: Unknown enum type '{enum_type}' for validation")
        return True  # Allow unknown types to pass

    if value not in valid_enums[enum_type]:
        print(f"❌ YAML VALIDATION ERROR: Invalid {enum_type} value '{value}'!")
        print(f'      File: board_peripherals.yaml')
        print(f'      Peripheral: DAC configuration')
        print(f"   ❌ Invalid value: '{value}'")
        print(f'   ✅ Valid values: {valid_enums[enum_type]}')
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

    Raises:
        ValueError: If validation fails and the value is not empty
    """
    if not value:
        result = full_enum
    else:
        result = value

    # Validate the enum value if enum_type is provided
    if enum_type and not validate_enum_value(result, enum_type):
        # If validation fails and we have a non-empty value, raise an exception
        if value:  # Only raise if user provided a value (not empty)
            raise ValueError(f"Invalid {enum_type} value '{value}'. Please use one of the valid enum values.")
        else:
            # If no value provided, use default and show warning
            print(f"⚠️  WARNING: Using default value '{full_enum}' due to validation failure")
            result = full_enum

    return result

def parse(name: str, config: dict) -> dict:
    """Parse DAC peripheral configuration.

    Args:
        name: Peripheral name (e.g. 'dac-0')
        config: Configuration dictionary containing:
            - role: DAC role type ('oneshot', 'continuous', or 'cosine')
            - config: Configuration specific to the role

    Returns:
        Dictionary containing DAC configuration structure
    """
    try:
        # Get role type
        print(config)
        role = config.get('role')
        if role is None:
            raise ValueError("Missing 'role' in DAC configuration")

        if role not in ['oneshot', 'continuous', 'cosine']:
            raise ValueError(f"Invalid DAC role: {role}. Must be 'oneshot', 'continuous', or 'cosine'")

        # Convert string role to enum value
        enum_role = DAC_ROLE_MAP.get(role)
        if enum_role is None:
            raise ValueError(f"Invalid DAC role '{role}' for DAC device '{name}'")

        # Get the actual config
        config = config.get('config', {})
        if role == 'oneshot':
            # Parse oneshot configuration
            channel = config.get('channel', 0)
            if channel not in [0, 1]:
                raise ValueError(f'Invalid DAC channel: {channel}. Must be 0 or 1')

            channel_enum = f'DAC_CHAN_{channel}'
            return {
                'struct_type': 'periph_dac_config_t',
                'struct_var': f'{name}_cfg',
                'struct_init': {
                    'role': enum_role,
                    'oneshot_cfg': {
                        'chan_id': channel_enum
                    }
                }
            }

        elif role == 'continuous':
            # Parse continuous configuration
            chan_mask = get_enum_value(config.get('chan_mask'), 'DAC_CHANNEL_MASK_CH0', 'dac_channel_mask')
            desc_num = config.get('desc_num', 8)
            buf_size = config.get('buf_size', 2048)
            freq_hz = config.get('freq_hz', 1000000)
            offset = config.get('offset', 0)
            clk_src = config.get('clk_src', 'DAC_DIGI_CLK_SRC_DEFAULT')
            chan_mode = get_enum_value(config.get('chan_mode'), 'DAC_CHANNEL_MODE_SIMUL', 'dac_continuous_chan_mode')

            return {
                'struct_type': 'periph_dac_config_t',
                'struct_var': f'{name}_cfg',
                'struct_init': {
                    'role': enum_role,
                    'continuous_cfg': {
                        'chan_mask': chan_mask,
                        'desc_num': desc_num,
                        'buf_size': buf_size,
                        'freq_hz': freq_hz,
                        'offset': offset,
                        'clk_src': clk_src,
                        'chan_mode': chan_mode
                    }
                }
            }

        else:  # cosine mode
            # Parse cosine configuration
            channel = config.get('channel', 0)
            if channel not in [0, 1]:
                raise ValueError(f'Invalid DAC channel: {channel}. Must be 0 or 1')

            channel_enum = f'DAC_CHAN_{channel}'
            freq_hz = config.get('freq_hz', 1000)
            clk_src = config.get('clk_src', 'DAC_COSINE_CLK_SRC_DEFAULT')

            atten = get_enum_value(config.get('atten'), 'DAC_COSINE_ATTEN_DEFAULT', 'dac_cosine_atten')
            phase = get_enum_value(config.get('phase'), 'DAC_COSINE_PHASE_0', 'dac_cosine_phase')
            offset = config.get('offset', 0)
            force_set_freq = config.get('force_set_freq', False)

            return {
                'struct_type': 'periph_dac_config_t',
                'struct_var': f'{name}_cfg',
                'struct_init': {
                    'role': enum_role,
                    'cosine_cfg': {
                        'chan_id': channel_enum,
                        'freq_hz': freq_hz,
                        'clk_src': clk_src,
                        'atten': atten,
                        'phase': phase,
                        'offset': offset,
                        'flags': {
                            'force_set_freq': force_set_freq
                        }
                    }
                }
            }

    except ValueError as e:
        # Re-raise ValueError with more context
        raise ValueError(f"YAML validation error in DAC peripheral '{name}': {e}")
    except Exception as e:
        # Catch any other exceptions and provide context
        raise ValueError(f"Error parsing DAC peripheral '{name}': {e}")
