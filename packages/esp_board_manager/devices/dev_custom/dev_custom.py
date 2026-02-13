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

def _get_struct_name(base_name: str, field_name: str) -> str:
    """Generate struct name for nested object"""
    return f'{base_name}_{_sanitize_field_name(field_name)}_t'

def _generate_struct_fields_recursive(name: str, config: Dict[str, Any], defined_structs: List[str]) -> Tuple[List[str], List[str]]:
    """
    Generate C struct field definitions and initialization values recursively

    Args:
        name: Name of the current struct/device (used for naming child structs)
        config: Configuration dictionary
        defined_structs: List to append new struct definitions to

    Returns:
        Tuple of (field_definitions, init_values)
    """
    field_definitions = []
    init_values = []

    for key, value in config.items():
        if key == 'peripherals':  # Skip peripherals, handled separately
            continue

        field_name = _sanitize_field_name(key)
        c_value = _get_c_value_from_python(value)

        # Handle nested dictionary
        if isinstance(value, dict):
            child_struct_name = _get_struct_name(name, key)

            # Recursively generate fields for this child struct
            child_fields, _ = _generate_struct_fields_recursive(child_struct_name.replace('_t', ''), value, defined_structs)

            # Define the child struct
            struct_def = f'typedef struct {{\n'
            for f in child_fields:
                struct_def += f'{f}\n'
            struct_def += f'}} {child_struct_name};'
            defined_structs.append(struct_def)

            c_type = child_struct_name
            # For init, the value is passed as is, generator handles recursive dicts

        # Handle list
        elif isinstance(value, list) and value:
            # Check if it's a list of dicts (objects)
            if isinstance(value[0], dict):
                child_struct_name = _get_struct_name(name, key)

                # Merge keys from ALL dicts in the list to form a complete struct definition
                merged_config = {}
                for item in value:
                    if isinstance(item, dict):
                        for k, v in item.items():
                            if k not in merged_config:
                                merged_config[k] = v
                            # Optional: Check for type consistency? For now allow overwrite/first-found.

                # Recursively generate fields using the merged config
                child_fields, _ = _generate_struct_fields_recursive(child_struct_name.replace('_t', ''), merged_config, defined_structs)

                # Define the child struct
                struct_def = f'typedef struct {{\n'
                for f in child_fields:
                    struct_def += f'{f}\n'
                struct_def += f'}} {child_struct_name};'
                defined_structs.append(struct_def)

                # For arrays, we don't name the field in definition again here because c_type already includes it,
                # but our pattern below is `{type} {name};`.
                c_type = child_struct_name
                field_name = f'{field_name}[{len(value)}]'

            # Check for list of lists (2D array)
            elif isinstance(value[0], list):
                 # Assume consistent inner type and size based on first element for now
                 # or try to find max size
                 if value[0]:
                    item_type = _get_c_type_from_value(value[0][0])
                    # Ensure it is a valid type
                    if item_type == 'void *':
                         item_type = 'int' # Fallback? Or keep void*?
                 else:
                    item_type = 'void *'

                 # Calculate dimensions
                 dim1 = len(value)
                 # Find max length of inner lists to be safe, or assume fixed if C array
                 max_dim2 = 0
                 for sublist in value:
                     if isinstance(sublist, list) and len(sublist) > max_dim2:
                         max_dim2 = len(sublist)

                 c_type = item_type
                 field_name = f'{field_name}[{dim1}][{max_dim2}]'

            else:
                # List of primitives
                item_type = _get_c_type_from_value(value[0])
                c_type = item_type
                field_name = f'{field_name}[{len(value)}]'

        # Handle primitives
        else:
            c_type = _get_c_type_from_value(value)

        # Generate field definition
        field_def = f'    {c_type:<12} {field_name};'
        field_definitions.append(field_def)

        # Generate initialization value
        # For complex types, _get_c_value_from_python returns NULL/string.
        # Since this return value seems unused for the final C generation (which uses the dict), roughly correct is fine.
        init_values.append(f'        .{field_name.split("[")[0]} = {c_value},')

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

    # List to hold any nested struct definitions
    defined_structs = []

    # Base struct name
    struct_name = f'dev_custom_{_sanitize_field_name(name)}_config_t'

    # Generate struct fields for custom config (recursive)
    # We pass struct_name as base for naming sub-structs
    custom_fields, _ = _generate_struct_fields_recursive(struct_name.replace('_config_t', ''), device_config, defined_structs)

    # Generate struct fields for peripherals
    periph_fields, _ = _generate_peripheral_fields(peripherals_list)

    # Combine all fields
    all_fields = custom_fields + periph_fields

    # Generate main struct definition
    main_struct_def = f'''typedef struct {{
    const char *name;           /*!< Custom device name */
    const char *type;           /*!< Device type: "custom" */
    const char *chip;           /*!< Chip name */
{chr(10).join(all_fields)}
}} {struct_name};'''

    # Combine all definitions: nested ones first, then main one
    full_definition = '\n\n'.join(defined_structs + [main_struct_def])

    # Build structure initialization
    struct_init = {
        'name': name,
        'type': config.get('type', 'custom'),
        'chip': config.get('chip', 'unknown'),
    }

    # Add custom config fields to struct_init
    for key, value in device_config.items():
        if key != 'peripherals':
            field_name = _sanitize_field_name(key)
            struct_init[field_name] = value  # Return raw value, let dict_to_c_initializer handle conversions

    # Add peripheral fields to struct_init
    if peripherals_list:
        struct_init['peripheral_count'] = len(peripherals_list)
        if len(peripherals_list) == 1:
            periph = peripherals_list[0]
            if isinstance(periph, dict) and 'name' in periph:
                periph_name = periph['name']
            else:
                periph_name = str(periph)
            struct_init['peripheral_name'] = periph_name
        else:
            for i, periph in enumerate(peripherals_list):
                if isinstance(periph, dict) and 'name' in periph:
                    periph_name = periph['name']
                else:
                    periph_name = str(periph)
                struct_init[f'peripheral_names[{i}]'] = periph_name

    return {
        'struct_type': struct_name,
        'struct_init': struct_init,
        'struct_definition': full_definition
    }
