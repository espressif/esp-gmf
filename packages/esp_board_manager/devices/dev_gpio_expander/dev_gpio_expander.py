# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# GPIO Expander device config parser
VERSION = 'v1.0.0'

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

DEV_IO_EXPANDER_MAX_ADDR_COUNT = 2

def get_includes() -> list:
    """Return list of required include headers for IO Expander device"""
    return [
        'dev_gpio_expander.h'
    ]

def parse_i2c_addresses(addr_input):
    """Parse I2C addresses from yaml"""
    if isinstance(addr_input, list):
        return [int(addr) if isinstance(addr, str) else addr for addr in addr_input]
    
    elif isinstance(addr_input, str):
        if ',' in addr_input:
            addresses = []
            for addr_str in addr_input.split(','):
                addr_str = addr_str.strip()
                if addr_str.startswith('0x'):
                    addresses.append(int(addr_str, 16))
                else:
                    addresses.append(int(addr_str))
            return addresses
        else:
            if addr_input.startswith('0x'):
                return [int(addr_input, 16)]
            else:
                return [int(addr_input)]
    
    elif isinstance(addr_input, int):
        return [addr_input]
    
    else:
        raise ValueError(f'❌ Unsupported i2c_addr format: {type(addr_input)}')

def parse(name: str, config: dict, peripherals_dict=None) -> dict:
    """Parse IO Expander device configuration from YAML to C structure"""
    # Parse the device name - use name directly for C naming
    c_name = name.replace('-', '_')  # Convert hyphens to underscores for C naming
    
    # Get the device config
    device_config = config.get('config', {})
    peripherals = config.get('peripherals', [])
    
    # Get chip and type from device level
    chip_name = config.get('chip', 'tca9554')
    device_type = config.get('type', 'tca9554')

    # Process peripherals to find I2C master
    i2c_name = ''
    i2c_addr = ''
    i2c_addr_count = 0
    for periph in peripherals:
        periph_name = periph.get('name', '')
        if periph_name.startswith('i2c'):
            # Check if peripheral exists in peripherals_dict if provided
            if peripherals_dict is not None and periph_name not in peripherals_dict:
                raise ValueError(f"❌ IO Expander device {name} references undefined peripheral '{periph_name}'")
            i2c_name = periph_name
            i2c_addr = periph.get('i2c_addr')
            if i2c_addr is None:
                raise ValueError(f"❌ IO Expander device {name} peripheral '{periph_name}' does not have 'i2c_addr' field")
            i2c_addr = parse_i2c_addresses(i2c_addr)
            i2c_addr_count = len(i2c_addr)
            if i2c_addr_count > DEV_IO_EXPANDER_MAX_ADDR_COUNT:
                raise ValueError(f'❌ Too many I2C addresses ({i2c_addr_count}), Maximum allowed is {DEV_IO_EXPANDER_MAX_ADDR_COUNT}')
            break

    # Get max pins
    max_pins = int(device_config.get('max_pins', 8))  # 8 is default
    
    # Process output IO mask
    output_io_mask = 0
    output_io_level_mask = 0
    output_io_mask_list = device_config.get('output_io_mask', [1, 2, 3])  # Default to pins 1, 2, 3
    level_mask_list = device_config.get('output_io_level_mask', [1, 1, 1])
    if len(output_io_mask_list) != len(level_mask_list):
        print(f'⚠️  WARNING: Output IO mask and level mask lists are not the same length')
    # Convert pin numbers to enum names
    for i, pin in enumerate(output_io_mask_list):
        if isinstance(pin, int) and 0 <= pin < max_pins:
            output_io_mask |= (1 << pin)
            if i < len(level_mask_list):
                if (int(level_mask_list[i]) == 0 or int(level_mask_list[i]) == 1):
                    output_io_level_mask |= (level_mask_list[i] << pin)
                else:
                    print(f'⚠️  WARNING: Invalid level mask value {level_mask_list[i]} for pin {pin}, ignoring.')
            else:
                print(f'⚠️  WARNING: Pin {pin} out of range (0-{max_pins-1}), ignoring.')
        else:
            print(f'⚠️  WARNING: Invalid output pin specification {pin}, ignoring.')
    
    # Process input IO mask
    input_io_mask = 0
    input_pins = device_config.get('input_io_mask', [])
    # Convert input pin numbers to enum names
    if input_pins:
        for pin in input_pins:
            if isinstance(pin, int) and 0 <= pin < max_pins:
                input_io_mask |= (1 << pin)
            else:
                print(f'⚠️  WARNING: Invalid input pin specification {pin}, ignoring.')
    
    result = {
        'struct_type': 'dev_io_expander_config_t',
        'struct_var': f'{c_name}_cfg',
        'struct_init': {
            'name': c_name,
            'type': device_type,
            'chip': chip_name,
            'i2c_name': i2c_name,
            'i2c_addr_count': i2c_addr_count,
            'i2c_addr': i2c_addr,
            'max_pins': max_pins,
            'output_io_mask': output_io_mask,
            'output_io_level_mask': output_io_level_mask,
            'input_io_mask': input_io_mask
        }
    }
    
    return result
