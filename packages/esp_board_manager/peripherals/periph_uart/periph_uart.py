# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# UART device config parser
VERSION = 'v1.0.0'

# Define valid enum values for UART configuration
VALID_UART_ENUMS = {
    'data_bits': [
        'UART_DATA_5_BITS',
        'UART_DATA_6_BITS',
        'UART_DATA_7_BITS',
        'UART_DATA_8_BITS'
    ],
    'parity': [
        'UART_PARITY_DISABLE',
        'UART_PARITY_EVEN',
        'UART_PARITY_ODD'
    ],
    'stop_bits': [
        'UART_STOP_BITS_1',
        'UART_STOP_BITS_1_5',
        'UART_STOP_BITS_2'
    ],
    'flow_ctrl': [
        'UART_HW_FLOWCTRL_DISABLE',
        'UART_HW_FLOWCTRL_RTS',
        'UART_HW_FLOWCTRL_CTS',
        'UART_HW_FLOWCTRL_CTS_RTS'
    ],
    'uart_mode': [
        'UART_MODE_UART',
        'UART_MODE_RS485_HALF_DUPLEX',
        'UART_MODE_IRDA',
        'UART_MODE_RS485_COLLISION_DETECT',
        'UART_MODE_RS485_APP_CTRL'
    ]
}

def validate_enum_value(value: str, enum_type: str) -> bool:
    """Validate if the enum value is within the valid range.

    Args:
        value: The enum value to validate
        enum_type: The type of enum (data_bits, parity, etc.)

    Returns:
        True if valid, False otherwise
    """
    if enum_type not in VALID_UART_ENUMS:
        print(f"⚠️  WARNING: Unknown enum type '{enum_type}' for UART")
        return True  # Allow unknown types to pass

    if value not in VALID_UART_ENUMS[enum_type]:
        print(f"❌ YAML VALIDATION ERROR: Invalid {enum_type} value '{value}'!")
        print(f'   File: board_peripherals.yaml')
        print(f'   📋 Peripheral: UART configuration')
        print(f"   ❌ Invalid value: '{value}'")
        print(f'   ✅ Valid values: {VALID_UART_ENUMS[enum_type]}')
        print(f'   ℹ️ Please use one of the valid enum values listed above')
        return False

    return True

def get_enum_value(value: str, default_value: str, enum_type: str = None) -> str:
    """Helper function to get enum value with validation.

    Args:
        value: The value from YAML config
        default_value: The default value to use if value is not provided
        enum_type: The type of enum for validation (optional)

    Returns:
        The enum value to use
    """
    if not value:
        result = default_value
    else:
        result = value

    # Validate the enum value if enum_type is provided
    if enum_type and not validate_enum_value(result, enum_type):
        raise ValueError(f"Invalid {enum_type} value '{value}'. Please use one of the valid enum values.")

    return result

def get_includes() -> list:
    """Return list of required include headers for UART"""
    return [
        'periph_uart.h'
    ]

def parse_uart_config(uart_config_dict: dict) -> dict:
    """Parse UART configuration structure from YAML"""

    uart_config = {
        'baud_rate': uart_config_dict.get('baud_rate', 115200),
        'data_bits': get_enum_value(uart_config_dict.get('data_bits'), 'UART_DATA_8_BITS', 'data_bits'),
        'parity': get_enum_value(uart_config_dict.get('parity'), 'UART_PARITY_DISABLE', 'parity'),
        'stop_bits': get_enum_value(uart_config_dict.get('stop_bits'), 'UART_STOP_BITS_1', 'stop_bits'),
        'flow_ctrl': get_enum_value(uart_config_dict.get('flow_ctrl'), 'UART_HW_FLOWCTRL_DISABLE', 'flow_ctrl'),  # Default disabled
        'rx_flow_ctrl_thresh': uart_config_dict.get('rx_flow_ctrl_thresh', 0),
    }

    # Handle union for source clock - ensure only one is specified
    source_clk = uart_config_dict.get('source_clk')
    lp_source_clk = uart_config_dict.get('lp_source_clk')

    if source_clk is not None and lp_source_clk is not None:
        raise ValueError("YAML validation error in UART device: Cannot specify both 'source_clk' and 'lp_source_clk' in the union")

    if source_clk is not None:
        # Use regular UART source clock
        uart_config['source_clk'] = source_clk
    elif lp_source_clk is not None:
        # Use LP_UART source clock
        uart_config['lp_source_clk'] = lp_source_clk
    else:
        # Default to regular UART source clock
        uart_config['source_clk'] = 'UART_SCLK_DEFAULT'

    # Handle nested flags structure with bitfields
    flags_dict = uart_config_dict.get('flags', {})
    flags_config = {
        'allow_pd': flags_dict.get('allow_pd', 0),  # Default to 0 (not allowed)
        'backup_before_sleep': flags_dict.get('backup_before_sleep', 0)  # Default to 0
    }
    uart_config['flags'] = flags_config

    return uart_config

def parse(name: str, full_config: dict, peripherals_dict=None) -> dict:
    """Parse UART device configuration from YAML"""
    # Get the actual config from the full_config
    config = full_config.get('config', {})

    # Get UART number
    uart_num = config.get('uart_num')

    # Parse UART configuration
    uart_config_raw = config.get('uart_config', {})
    uart_config = parse_uart_config(uart_config_raw)

    # Build the main configuration structure
    struct_init = {
        'uart_num': uart_num,
        'rx_buffer_size': config.get('rx_buffer_size', 256),
        'tx_buffer_size': config.get('tx_buffer_size', 256),
        'use_queue': config.get('use_queue', False),
        'queue_size': config.get('queue_size', 10),  # Default queue size
        'intr_alloc_flags': config.get('intr_alloc_flags', 0),
        'uart_config': uart_config,
        'tx_io_num': config.get('tx_io_num', -1),
        'rx_io_num': config.get('rx_io_num', -1),
        'rts_io_num': config.get('rts_io_num', -1),
        'cts_io_num': config.get('cts_io_num', -1),
        'uart_mode': get_enum_value(config.get('uart_mode'), 'UART_MODE_UART', 'uart_mode')
    }

    # Build the result
    result = {
        'struct_type': 'periph_uart_config_t',
        'struct_var': f'{name}_cfg',
        'struct_init': struct_init
    }

    return result
