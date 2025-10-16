# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# ADC device config parser
VERSION = 'v1.0.0'

# Define valid ADC configuration values
VALID_ADC_ROLES = ['continuous', 'oneshot']
VALID_ADC_UNITS = ['ADC_UNIT_1', 'ADC_UNIT_2']
VALID_ADC_ATTEN = [
    'ADC_ATTEN_DB_0',
    'ADC_ATTEN_DB_2_5',
    'ADC_ATTEN_DB_6',
    'ADC_ATTEN_DB_12',
    'ADC_ATTEN_DB_11'
]
VALID_ADC_BITWIDTH = [
    'ADC_BITWIDTH_9',
    'ADC_BITWIDTH_10',
    'ADC_BITWIDTH_11',
    'ADC_BITWIDTH_12',
    'ADC_BITWIDTH_13',
    'ADC_BITWIDTH_DEFAULT'
]
VALID_ADC_FORMATS = [
    'ADC_DIGI_OUTPUT_FORMAT_TYPE1',
    'ADC_DIGI_OUTPUT_FORMAT_TYPE2'
]
VALID_ADC_CONV_MODES = [
    'ADC_CONV_SINGLE_UNIT_1',
    'ADC_CONV_SINGLE_UNIT_2',
    'ADC_CONV_BOTH_UNIT',
    'ADC_CONV_ALTER_UNIT'
]
VALID_ADC_ULP_MODES = [
    'ADC_ULP_MODE_DISABLE',
    'ADC_ULP_MODE_FSM',
    'ADC_ULP_MODE_RISCV',
    'ADC_ULP_MODE_LP_CORE'
]

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
        print(f'   📋 Peripheral: ADC configuration')
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
        raise ValueError(f"Invalid {enum_type} value '{value}'. Please use one of the valid enum values.")

    return result

def get_includes() -> list:
    """Return list of required include headers for ADC"""
    return [
        'periph_adc.h',
    ]

def parse_adc_continuous_config(continuous_config: dict) -> dict:
    """Parse ADC continuous mode specific configuration"""

    pattern_num = continuous_config.get('pattern_num', 1)
    handle_cfg = {
        'max_store_buf_size': continuous_config.get('max_store_buf_size', 1024),
        'conv_frame_size': continuous_config.get('conv_frame_size', 256),
        'flags.flush_pool': continuous_config.get('flush_pool', 1),
    }

    continuous_cfg = {
        'handle_cfg': handle_cfg,
        'sample_freq_hz': continuous_config.get('sample_freq_hz', 20000),
        'format': get_enum_value(
            continuous_config.get('format'),
            'ADC_DIGI_OUTPUT_FORMAT_TYPE2',
            'ADC format',
            VALID_ADC_FORMATS
        ),
        'conv_mode': get_enum_value(
            continuous_config.get('conv_mode'),
            'ADC_CONV_SINGLE_UNIT_1',
            'ADC conversion mode',
            VALID_ADC_CONV_MODES
        ),
        'pattern_num': pattern_num
    }

    # Handle channel_id array
    channel_id = continuous_config.get('channel_id')
    if channel_id is None:
        raise ValueError(f'Missing channel ID for ADC continuous device')
    if isinstance(channel_id, list) and len(channel_id) <= 0:
        raise ValueError(f'Empty channel ID list for ADC continuous device')
    if not isinstance(channel_id, list):
        channel_id = [channel_id]

    channel_id_array = [0] * pattern_num
    for i, channel in enumerate(channel_id):
        if i < len(channel_id_array):
            channel_id_array[i] = channel

    continuous_cfg['channel_id'] = channel_id_array
    continuous_cfg['unit_id'] = get_enum_value(
        continuous_config.get('unit_id'),
        'ADC_UNIT_1',
        'ADC unit',
        VALID_ADC_UNITS
    )
    continuous_cfg['atten'] = get_enum_value(
        continuous_config.get('atten'),
        'ADC_ATTEN_DB_0',
        'ADC attenuation',
        VALID_ADC_ATTEN
    )
    continuous_cfg['bit_width'] = get_enum_value(
        continuous_config.get('bit_width'),
        'ADC_BITWIDTH_DEFAULT',
        'ADC bit width',
        VALID_ADC_BITWIDTH
    )

    return continuous_cfg

def parse_adc_oneshot_config(oneshot_config: dict) -> dict:
    """Parse ADC oneshot mode specific configuration"""
    unit_cfg = {
        'unit_id': get_enum_value(
            oneshot_config.get('unit_id'),
            'ADC_UNIT_1',
            'ADC unit',
            VALID_ADC_UNITS
        ),
        'clk_src': oneshot_config.get('clk_src'),
        'ulp_mode': get_enum_value(
            oneshot_config.get('ulp_mode'),
            'ADC_ULP_MODE_DISABLE',
            'ADC ULP mode',
            VALID_ADC_ULP_MODES
        )
    }

    channel_id = oneshot_config.get('channel_id')
    if channel_id is None:
        raise ValueError(f'Missing channel ID for ADC oneshot device')
    chan_cfg = {
        'atten': get_enum_value(
            oneshot_config.get('atten'),
            'ADC_ATTEN_DB_0',
            'ADC attenuation',
            VALID_ADC_ATTEN
        ),
        'bitwidth': get_enum_value(
            oneshot_config.get('bit_width'),
            'ADC_BITWIDTH_DEFAULT',
            'ADC bit width',
            VALID_ADC_BITWIDTH
        )
    }

    oneshot_cfg = {
        'channel_id': channel_id,
        'unit_cfg': unit_cfg,
        'chan_cfg': chan_cfg
    }

    return oneshot_cfg

def parse(name: str, full_config: dict, peripherals_dict=None) -> dict:
    """Parse ADC peripheral configuration from YAML to C structure

    Args:
        name: Peripheral name
        config: Configuration dictionary from YAML

    Returns:
        Dictionary containing ADC configuration structure
    """
    try:
        # Get the actual config from the full_config
        config = full_config.get('config', {})

        # Validate role
        role = full_config.get('role')
        if role is None:
            raise ValueError("Missing ADC role for ADC device '{}'".format(name))
        elif not validate_enum_value(role, 'ADC role', VALID_ADC_ROLES):
            raise ValueError(f"Invalid ADC role '{role}' for ADC device '{name}'")

        # Parse sub-type specific configuration
        if role == 'continuous':
            continuous_cfg = parse_adc_continuous_config(config)
            cfg_union = {'continuous': continuous_cfg}
        else:  # oneshot
            oneshot_cfg = parse_adc_oneshot_config(config)
            cfg_union = {'oneshot': oneshot_cfg}

        # Build the main configuration structure
        struct_init = {
            'role': role,
            'cfg': cfg_union
        }

        # Build the result
        result = {
            'struct_type': 'periph_adc_config_t',
            'struct_var': f'{name}_cfg',
            'struct_init': struct_init,
            '_role': role  # Store role for code generation
        }

        return result

    except ValueError as e:
        # Re-raise ValueError with more context
        raise ValueError(f"YAML validation error in ADC peripheral '{name}': {e}")
    except Exception as e:
        # Catch any other exceptions and provide context
        raise ValueError(f"Error parsing ADC peripheral '{name}': {e}")

