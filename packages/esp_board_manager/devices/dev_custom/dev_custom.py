# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# Custom device config parser
VERSION = 'v1.0.0'

import sys
import re
from typing import Dict, Any, List, Tuple

def get_includes():
    """Get required header includes for custom device"""
    return ['dev_custom.h']

def _get_c_type_from_value(value: Any) -> str:
    """Determine C type from Python value"""
    if isinstance(value, bool):
        return 'bool'
    elif isinstance(value, int):
        if -128 <= value <= 127:
            return 'int8_t'
        elif 0 <= value <= 255:
            return 'uint8_t'
        elif -32768 <= value <= 32767:
            return 'int16_t'
        elif 0 <= value <= 65535:
            return 'uint16_t'
        elif -2147483648 <= value <= 2147483647:
            return 'int32_t'
        else:
            return 'uint32_t'
    elif isinstance(value, float):
        return 'float'
    elif isinstance(value, str):
        return 'const char *'
    else:
        return 'void *'

def _get_c_value_from_python(value: Any) -> str:
    """Convert Python value to C literal"""
    if isinstance(value, bool):
        return 'true' if value else 'false'
    elif isinstance(value, int):
        return str(value)
    elif isinstance(value, float):
        return str(value)
    elif isinstance(value, str):
        return f'"{value}"'
    else:
        return 'NULL'

def _sanitize_field_name(name: str) -> str:
    """Convert YAML key to valid C field name"""
    # Replace invalid characters with underscores
    name = re.sub(r'[^a-zA-Z0-9_]', '_', name)
    # Ensure it starts with letter or underscore
    if name and not name[0].isalpha() and name[0] != '_':
        name = '_' + name
    # Ensure it's not empty
    if not name:
        name = 'field'
    return name

def _generate_struct_fields(config: Dict[str, Any]) -> Tuple[List[str], List[str]]:
    """Generate C struct field definitions and initialization values"""
    field_definitions = []
    init_values = []

    for key, value in config.items():
        if key == 'peripherals':  # Skip peripherals, handled separately
            continue

        field_name = _sanitize_field_name(key)
        c_type = _get_c_type_from_value(value)
        c_value = _get_c_value_from_python(value)

        # Generate field definition
        field_def = f'    {c_type:<12} {field_name};'
        field_definitions.append(field_def)

        # Generate initialization value
        init_values.append(f'        .{field_name} = {c_value},')

    return field_definitions, init_values

def _generate_peripheral_fields(peripherals_list: List[Any]) -> Tuple[List[str], List[str]]:
    """Generate peripheral-related struct fields"""
    field_definitions = []
    init_values = []

    if not peripherals_list:
        return field_definitions, init_values

    # Add peripheral count field
    field_definitions.append('    uint8_t     peripheral_count;')
    init_values.append(f'        .peripheral_count = {len(peripherals_list)},')

    # Add peripheral names array
    if len(peripherals_list) == 1:
        # Single peripheral - use simple field
        periph = peripherals_list[0]
        if isinstance(periph, dict) and 'name' in periph:
            periph_name = periph['name']
        else:
            periph_name = str(periph)

        field_definitions.append('    const char *peripheral_name;')
        init_values.append(f'        .peripheral_name = "{periph_name}",')
    else:
        # Multiple peripherals - use array
        field_definitions.append('    const char *peripheral_names[MAX_PERIPHERALS];')
        for i, periph in enumerate(peripherals_list):
            if isinstance(periph, dict) and 'name' in periph:
                periph_name = periph['name']
            else:
                periph_name = str(periph)
            init_values.append(f'        .peripheral_names[{i}] = "{periph_name}",')

    return field_definitions, init_values

def parse(name, config, peripherals_dict=None):
    """
    Parse custom device configuration from YAML to C structure

    Args:
        name (str): Device name
        config (dict): Device configuration dictionary
        peripherals_dict (dict): Dictionary of available peripherals for validation

    Returns:
        dict: Parsed configuration with 'struct_type', 'struct_init', and 'struct_definition' keys
    """
    # Extract configuration parameters
    device_config = config.get('config', {})
    peripherals_list = config.get('peripherals', [])

    # Validate peripherals if peripherals_dict is provided
    if peripherals_dict is not None:
        for periph in peripherals_list:
            if isinstance(periph, dict) and 'name' in periph:
                periph_name = periph['name']
            else:
                periph_name = str(periph)
            # Check if peripheral exists in peripherals_dict
            if periph_name not in peripherals_dict:
                raise ValueError(f"Custom device {name} references undefined peripheral '{periph_name}'")

    # Generate struct fields for custom config
    custom_fields, custom_inits = _generate_struct_fields(device_config)

    # Generate struct fields for peripherals
    periph_fields, periph_inits = _generate_peripheral_fields(peripherals_list)

    # Combine all fields
    all_fields = custom_fields + periph_fields
    all_inits = custom_inits + periph_inits

    # Generate struct definition
    struct_name = f'dev_custom_{_sanitize_field_name(name)}_config_t'
    struct_definition = f'''typedef struct {{
    const char *name;           /*!< Custom device name */
    const char *type;           /*!< Device type: "custom" */
    const char *chip;           /*!< Chip name */
{chr(10).join(all_fields)}
}} {struct_name};'''

    # Build structure initialization
    struct_init = {
        'name': name,  # Return raw name, let dict_to_c_initializer handle quoting
        'type': config.get('type', 'custom'),
        'chip': config.get('chip', 'unknown'),  # Return raw chip name, let dict_to_c_initializer handle quoting
    }

    # Add custom config fields to struct_init
    for key, value in device_config.items():
        if key != 'peripherals':
            field_name = _sanitize_field_name(key)
            struct_init[field_name] = value  # Return raw value, let dict_to_c_initializer handle conversion

    # Add peripheral fields to struct_init
    if peripherals_list:
        struct_init['peripheral_count'] = len(peripherals_list)
        if len(peripherals_list) == 1:
            periph = peripherals_list[0]
            if isinstance(periph, dict) and 'name' in periph:
                periph_name = periph['name']
            else:
                periph_name = str(periph)
            struct_init['peripheral_name'] = periph_name  # Return raw name
        else:
            for i, periph in enumerate(peripherals_list):
                if isinstance(periph, dict) and 'name' in periph:
                    periph_name = periph['name']
                else:
                    periph_name = str(periph)
                struct_init[f'peripheral_names[{i}]'] = periph_name  # Return raw name

    return {
        'struct_type': struct_name,
        'struct_init': struct_init,
        'struct_definition': struct_definition
    }
