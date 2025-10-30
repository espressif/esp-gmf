# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# Analog Comparator peripheral config parser
VERSION = 'v1.0.0'

import sys

def get_includes() -> list:
    """Return list of required include headers for Analog Comparator peripheral"""
    return [
        'periph_anacmpr.h'
    ]

def validate_enum_value(value: str, enum_type: str) -> bool:
    """Validate if the enum value is within the valid range for ESP-IDF Analog Comparator driver.

    Args:
        value: The enum value to validate
        enum_type: The type of enum (ana_cmpr_ref_src, etc.)

    Returns:
        True if valid, False otherwise
    """
    # Define valid enum values based on ESP-IDF Analog Comparator driver
    valid_enums = {
        'ana_cmpr_ref_src': [
            'ANA_CMPR_REF_SRC_INTERNAL',
            'ANA_CMPR_REF_SRC_EXTERNAL'
        ],
        'ana_cmpr_cross_type': [
            'ANA_CMPR_CROSS_POS',
            'ANA_CMPR_CROSS_NEG',
            'ANA_CMPR_CROSS_ANY'
        ],
        'ana_cmpr_clk_src': [
            'ANA_CMPR_CLK_SRC_DEFAULT'
        ]
    }

    if enum_type not in valid_enums:
        print(f"⚠️  WARNING: Unknown enum type '{enum_type}' for validation")
        return True  # Allow unknown types to pass

    if value not in valid_enums[enum_type]:
        print(f"❌ YAML VALIDATION ERROR: Invalid {enum_type} value '{value}'!")
        print(f'      File: board_peripherals.yaml')
        print(f'      Peripheral: Analog Comparator configuration')
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
    """Parse Analog Comparator peripheral configuration.

    Args:
        name: Peripheral name (e.g. 'anacmpr-0')
        config: Configuration dictionary containing:
            - unit: Analog comparator unit number
            - ref_src: Reference source type
            - cross_type: Cross type for interrupt triggering
            - clk_src: Clock source
            - intr_priority: Interrupt priority
            - flags: Driver flags configuration

    Returns:
        Dictionary containing Analog Comparator configuration structure
    """
    try:
        # Get the actual config
        config = config.get('config', {})

        # Parse analog comparator configuration
        unit = config.get('unit', 0)
        if unit not in [0]:
            raise ValueError(f'Invalid Analog Comparator unit: {unit}. Currently only unit 0 is supported')

        ref_src = get_enum_value(config.get('ref_src'), 'ANA_CMPR_REF_SRC_INTERNAL', 'ana_cmpr_ref_src')
        cross_type = get_enum_value(config.get('cross_type'), 'ANA_CMPR_CROSS_ANY', 'ana_cmpr_cross_type')
        clk_src = get_enum_value(config.get('clk_src'), 'ANA_CMPR_CLK_SRC_DEFAULT', 'ana_cmpr_clk_src')
        intr_priority = config.get('intr_priority', 0)

        return {
            'struct_type': 'ana_cmpr_config_t',
            'struct_var': f'{name}_cfg',
            'struct_init': {
                'unit': unit,
                'ref_src': ref_src,
                'cross_type': cross_type,
                'clk_src': clk_src,
                'intr_priority': intr_priority,
            }
        }

    except ValueError as e:
        # Re-raise ValueError with more context
        raise ValueError(f"YAML validation error in Analog Comparator peripheral '{name}': {e}")
    except Exception as e:
        # Catch any other exceptions and provide context
        raise ValueError(f"Error parsing Analog Comparator peripheral '{name}': {e}")
