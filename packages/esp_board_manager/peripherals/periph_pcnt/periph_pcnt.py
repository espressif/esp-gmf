# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# PCNT peripheral config parser
VERSION = 'v1.0.0'

# Define valid PCNT configuration values
VALID_PCNT_CHANNEL_EDGE_ACTIONS = [
    'PCNT_CHANNEL_EDGE_ACTION_HOLD',
    'PCNT_CHANNEL_EDGE_ACTION_INCREASE',
    'PCNT_CHANNEL_EDGE_ACTION_DECREASE'
]

VALID_PCNT_CHANNEL_LEVEL_ACTIONS = [
    'PCNT_CHANNEL_LEVEL_ACTION_KEEP',
    'PCNT_CHANNEL_LEVEL_ACTION_INVERSE',
    'PCNT_CHANNEL_LEVEL_ACTION_HOLD'
]

# SOC constants (adjust based on actual SOC)
SOC_PCNT_CHANNELS_PER_UNIT = 2
SOC_PCNT_THRES_POINT_PER_UNIT = 5

def validate_enum_value(value: str, enum_type: str, valid_values: list) -> bool:
    """Validate if the enum value is within the valid range.

    Args:
        value: The enum value to validate
        enum_type: The type of enum for error messages
        valid_values: List of valid values

    Returns:
        True if valid, False otherwise
    """
    if value not in valid_values:
        print(f"❌ YAML VALIDATION ERROR: Invalid {enum_type} value '{value}'!")
        print(f'   File: board_peripherals.yaml')
        print(f'   📋 Peripheral: PCNT configuration')
        print(f"   ❌ Invalid value: '{value}'")
        print(f'   ✅ Valid values: {valid_values}')
        print(f'   ℹ️ Please use one of the valid enum values listed above')
        return False
    return True

def get_enum_value(value: str, default_value: str, enum_type: str, valid_values: list) -> str:
    """Helper function to get enum value with validation.

    Args:
        value: The value from YAML config
        default_value: The default value to use if value is not provided
        enum_type: The type of enum for validation
        valid_values: List of valid values

    Returns:
        The enum value to use
    """
    if not value:
        result = default_value
    else:
        result = value

    # Validate the enum value
    if not validate_enum_value(result, enum_type, valid_values):
        raise ValueError(f"YAML validation error in PCNT peripheral: Invalid {enum_type} value '{result}'. Valid values: {valid_values}")

    return result

def get_includes() -> list:
    """Return list of required include headers for PCNT"""
    return [
        'periph_pcnt.h',
    ]

def parse_pcnt_unit_config(unit_config: dict) -> dict:
    """Parse PCNT unit configuration"""
    unit_cfg = {
        'low_limit': unit_config.get('low_limit', -100),
        'high_limit': unit_config.get('high_limit', 100),
        'intr_priority': unit_config.get('intr_priority', 0)
    }

    # Parse flags
    flags_dict = unit_config.get('flags', {})
    flags_config = {
        'accum_count': int(flags_dict.get('accum_count', False))  # Convert bool to int
    }

    if flags_dict.get('en_step_notify_up') is not None:
        flags_config['en_step_notify_up'] = int(flags_dict.get('en_step_notify_up', False))

    if flags_dict.get('en_step_notify_down') is not None:
        flags_config['en_step_notify_down'] = int(flags_dict.get('en_step_notify_down', False))

    # Add SOC-specific flags if supported
    # Note: These would be conditionally included based on SOC capabilities
    unit_cfg['flags'] = flags_config

    return unit_cfg

def parse_pcnt_filter_config(filter_config: dict) -> dict:
    """Parse PCNT glitch filter configuration"""
    filter_cfg = {
        'max_glitch_ns': filter_config.get('max_glitch_ns', 1000)
    }

    return filter_cfg

def parse_pcnt_channel_list(channel_list: list) -> list:
    """Parse PCNT channel configuration list"""
    parsed_channels = []

    for channel_config in channel_list:
        # Parse channel configuration
        channel_cfg_raw = channel_config.get('channel_config', {})
        flags_raw = channel_cfg_raw.get('flags', {})

        edge_gpio = channel_cfg_raw.get('edge_gpio_num', -1)
        level_gpio = channel_cfg_raw.get('level_gpio_num', -1)

        invert_edge_input = int(flags_raw.get('invert_edge_input', False))
        invert_level_input = int(flags_raw.get('invert_level_input', False))
        virt_edge_io_level = int(flags_raw.get('virt_edge_io_level', 0))
        virt_level_io_level = int(flags_raw.get('virt_level_io_level', 0))
        io_loop_back = int(flags_raw.get('io_loop_back', False))

        pos_act = get_enum_value(
            channel_config.get('pos_act'),
            'PCNT_CHANNEL_EDGE_ACTION_INCREASE',
            'PCNT positive edge action',
            VALID_PCNT_CHANNEL_EDGE_ACTIONS
        )
        neg_act = get_enum_value(
            channel_config.get('neg_act'),
            'PCNT_CHANNEL_EDGE_ACTION_DECREASE',
            'PCNT negative edge action',
            VALID_PCNT_CHANNEL_EDGE_ACTIONS
        )
        high_act = get_enum_value(
            channel_config.get('high_act'),
            'PCNT_CHANNEL_LEVEL_ACTION_KEEP',
            'PCNT high level action',
            VALID_PCNT_CHANNEL_LEVEL_ACTIONS
        )
        low_act = get_enum_value(
            channel_config.get('low_act'),
            'PCNT_CHANNEL_LEVEL_ACTION_KEEP',
            'PCNT low level action',
            VALID_PCNT_CHANNEL_LEVEL_ACTIONS
        )

        parsed_channels.append({
            'channel_config': {
                'edge_gpio_num': edge_gpio,
                'level_gpio_num': level_gpio,
                'flags': {
                    'invert_edge_input': invert_edge_input,
                    'invert_level_input': invert_level_input,
                    'virt_edge_io_level': virt_edge_io_level,
                    'virt_level_io_level': virt_level_io_level,
                    'io_loop_back': io_loop_back,
                }
            },
            'pos_act': pos_act,
            'neg_act': neg_act,
            'high_act': high_act,
            'low_act': low_act
        })

    return parsed_channels

def validate_channel_count(channel_count: int, channel_list: list) -> bool:
    """Validate that channel count matches the number of channels in the list"""
    if channel_count != len(channel_list):
        print(f'❌ YAML VALIDATION ERROR: Channel count mismatch!')
        print(f'   File: board_peripherals.yaml')
        print(f'   📋 Peripheral: PCNT configuration')
        print(f'   ❌ channel_count: {channel_count}')
        print(f'   ❌ channel_list length: {len(channel_list)}')
        print(f'   ℹ️ channel_count must match the number of channels in channel_list')
        return False

    if channel_count > SOC_PCNT_CHANNELS_PER_UNIT:
        print(f'❌ YAML VALIDATION ERROR: Too many channels!')
        print(f'   File: board_peripherals.yaml')
        print(f'   📋 Peripheral: PCNT configuration')
        print(f'   ❌ Number of channels: {channel_count}')
        print(f'   ℹ️ Maximum channels per unit: {SOC_PCNT_CHANNELS_PER_UNIT}')
        return False

    return True

def validate_watch_point_count(watch_point_count: int, watch_point_list: list) -> bool:
    """Validate that watch point count matches the number of watch points in the list"""
    if watch_point_count != len(watch_point_list):
        print(f'❌ YAML VALIDATION ERROR: Watch point count mismatch!')
        print(f'   File: board_peripherals.yaml')
        print(f'   📋 Peripheral: PCNT configuration')
        print(f'   ❌ watch_point_count: {watch_point_count}')
        print(f'   ❌ watch_point_list length: {len(watch_point_list)}')
        print(f'   ℹ️ watch_point_count must match the number of watch points in watch_point_list')
        return False

    if watch_point_count > (SOC_PCNT_THRES_POINT_PER_UNIT + 3):
        print(f'❌ YAML VALIDATION ERROR: Too many watch points!')
        print(f'   File: board_peripherals.yaml')
        print(f'   📋 Peripheral: PCNT configuration')
        print(f'   ❌ Number of watch points: {watch_point_count}')
        print(f'   ℹ️ Maximum watch points per unit: {SOC_PCNT_THRES_POINT_PER_UNIT + 3}')
        return False

    return True

def parse(name: str, full_config: dict, peripherals_dict=None) -> dict:
    """Parse PCNT peripheral configuration from YAML"""
    # Get the actual config from the full_config
    config = full_config.get('config', {})

    # Parse unit configuration
    unit_config_raw = config.get('unit_config', {})
    unit_config = parse_pcnt_unit_config(unit_config_raw)

    # Parse filter configuration
    filter_config_raw = config.get('filter_config', {})
    filter_config = parse_pcnt_filter_config(filter_config_raw)

    # Parse channel configuration
    channel_count = config.get('channel_count', 0)
    channel_list_raw = config.get('channel_list', [])

    # Validate channel count
    if not validate_channel_count(channel_count, channel_list_raw):
        raise ValueError(f"Invalid channel configuration for PCNT peripheral '{name}'")

    # Parse channel list
    channel_list = parse_pcnt_channel_list(channel_list_raw)

    # Parse watch points
    watch_point_count = config.get('watch_point_count', 0)
    watch_point_list = config.get('watch_point_list', [])

    # Validate watch point count
    if not validate_watch_point_count(watch_point_count, watch_point_list):
        raise ValueError(f"Invalid watch point configuration for PCNT peripheral '{name}'")

    print(channel_list)

    # Build the main configuration structure
    struct_init = {
        'unit_config': unit_config,
        'filter_config': filter_config,
        'channel_count': channel_count,
        'channel_list': channel_list,
        'watch_point_count': watch_point_count,
        'watch_point_list': watch_point_list
    }

    # Build the result
    result = {
        'struct_type': 'periph_pcnt_config_t',
        'struct_var': f'{name}_cfg',
        'struct_init': struct_init,
        '_channel_count': channel_count,  # Store for code generation
        '_watch_point_count': watch_point_count  # Store for code generation
    }

    return result
