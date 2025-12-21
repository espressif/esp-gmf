# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# Camera device config parser
VERSION = 'v1.0.0'

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

# Define valid enum values based on ESP-IDF camera driver
VALID_ENUMS = {
    'data_width': [
        'CAM_CTLR_DATA_WIDTH_8',
        'CAM_CTLR_DATA_WIDTH_10',
        'CAM_CTLR_DATA_WIDTH_12',
        'CAM_CTLR_DATA_WIDTH_16'
    ]
}

def get_includes() -> list:
    """Return list of required include headers for camera device"""
    return [
        'dev_camera.h'
    ]

def validate_enum_value(value: str, enum_type: str) -> bool:
    """Validate if the enum value is within the valid range for ESP-IDF camera driver.

    Args:
        value: The enum value to validate
        enum_type: The type of enum (data_width, etc.)

    Returns:
        True if valid, False otherwise
    """
    if enum_type not in VALID_ENUMS:
        print(f"⚠️  WARNING: Unknown enum type '{enum_type}' for validation")
        return True  # Allow unknown types to pass

    if value not in VALID_ENUMS[enum_type]:
        print(f"❌ YAML VALIDATION ERROR: Invalid {enum_type} value '{value}'!")
        print(f'   File: board_devices.yaml')
        print(f'   📋 Device: camera configuration')
        print(f"   ❌ Invalid value: '{value}'")
        print(f'   ✅ Valid values: {VALID_ENUMS[enum_type]}')
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
    """
    if not value:
        result = full_enum
    else:
        result = value

    # Validate the enum value if enum_type is provided
    if enum_type and not validate_enum_value(result, enum_type):
        raise ValueError(f"YAML validation error in camera device: Invalid {enum_type} value '{result}'. Valid values: {VALID_ENUMS.get(enum_type, [])}")

    return result

def parse(name: str, config: dict, peripherals_dict=None) -> dict:
    """Parse camera device configuration from YAML to C structure"""

    # Parse the device name - use name directly for C naming
    c_name = name.replace('-', '_')  # Convert hyphens to underscores for C naming

    # Get the device config
    device_config = config.get('config', {})

    sub_type = config.get('sub_type')
    if sub_type is None:
        bus_type = device_config.get('bus_type')
        if bus_type is not None:
            raise ValueError(f"Please use 'sub_type' to distinguish camera sensor type in the 'board_devices.yaml', refer to 'dev_camera.yaml'")
        else:
            raise ValueError(f"YAML validation error in camera device: Missing 'sub_type' field")

    # Initialize bus config
    bus_config = None
    # Parse configuration based on bus type
    if sub_type == 'dvp':
        # Process peripherals to find I2C master
        i2c_name = ''
        i2c_freq = 0
        peripherals = config.get('peripherals', [])
        for periph in peripherals:
            periph_name = periph.get('name', '')
            if periph_name.startswith('i2c'):
                # Check if peripheral exists in peripherals_dict if provided
                if peripherals_dict is not None and periph_name not in peripherals_dict:
                    raise ValueError(f"IO Expander device {name} references undefined peripheral '{periph_name}'")
                i2c_name = periph_name
                i2c_freq = int(periph.get('frequency', 100000))
                break

        dvp_config_dict = device_config.get('dvp_config', {})

        # Parse DVP IO and settings
        reset_io = int(dvp_config_dict.get('reset_io', -1))
        pwdn_io = int(dvp_config_dict.get('pwdn_io', -1))
        de_io = int(dvp_config_dict.get('de_io', -1))
        pclk_io = int(dvp_config_dict.get('pclk_io', -1))
        xclk_io = int(dvp_config_dict.get('xclk_io', -1))
        vsync_io = int(dvp_config_dict.get('vsync_io', -1))
        xclk_freq = int(dvp_config_dict.get('xclk_freq', 10000000))
        data_width = dvp_config_dict.get('data_width', 'CAM_CTLR_DATA_WIDTH_8')
        data_width_enum = get_enum_value(data_width, 'CAM_CTLR_DATA_WIDTH_8', 'data_width')

        # Parse DVP data io configuration
        data_io_config = dvp_config_dict.get('data_io', {})

        # Build data_io array from data_io_0 to data_io_15
        data_io_array = []
        for i in range(16):
            io_key = f'data_io_{i}'
            io_value = int(data_io_config.get(io_key, -1))
            data_io_array.append(io_value)

        # Build dvp_io structure
        dvp_io = {
            'data_width': data_width_enum,
            'data_io': data_io_array,
            'vsync_io': vsync_io,
            'de_io': de_io,
            'pclk_io': pclk_io,
            'xclk_io': xclk_io
        }

        # Build full DVP config
        bus_config = {
            'i2c_name': i2c_name,
            'i2c_freq': i2c_freq,
            'reset_io': reset_io,
            'pwdn_io': pwdn_io,
            'dvp_io': dvp_io,
            'xclk_freq': xclk_freq
        }

    # CSI configuration parsing
    elif sub_type == 'csi':
        # Process peripherals to find I2C master
        i2c_name = ''
        i2c_freq = 0
        peripherals = config.get('peripherals', [])
        for periph in peripherals:
            periph_name = periph.get('name', '')
            if periph_name.startswith('i2c'):
                # Check if peripheral exists in peripherals_dict if provided
                if peripherals_dict is not None and periph_name not in peripherals_dict:
                    raise ValueError(f"Camera device {name} references undefined peripheral '{periph_name}'")
                i2c_name = periph_name
                i2c_freq = int(periph.get('frequency', 100000))
                break

        csi_config_dict = device_config.get('csi_config', {})

        # Parse CSI settings
        reset_io = int(csi_config_dict.get('reset_io', -1))
        pwdn_io = int(csi_config_dict.get('pwdn_io', -1))
        dont_init_ldo = bool(csi_config_dict.get('dont_init_ldo', True))

        # Build CSI config
        bus_config = {
            'i2c_name': i2c_name,
            'i2c_freq': i2c_freq,
            'reset_io': reset_io,
            'pwdn_io': pwdn_io,
            'dont_init_ldo': dont_init_ldo,
        }

    # Placeholder for other bus types (SPI, USB-UVC)
    # These can be implemented similarly when their configurations are defined
    elif sub_type == 'spi':
        # SPI configuration parsing would go here
        bus_config = {}  # Placeholder
    elif sub_type == 'usb_uvc':
        # USB-UVC configuration parsing would go here
        bus_config = {}  # Placeholder
    else:
        raise ValueError(f"Unsupported sub_type '{sub_type}' for camera device {name}")

    # Create the base result structure
    result = {
        'struct_type': 'dev_camera_config_t',
        'struct_var': f'{c_name}_cfg',
        'struct_init': {
            'name': c_name,
            'type': str(config.get('type', 'camera')),
            'sub_type': sub_type,
            'sub_cfg' : {}
        }
    }

    # Add the bus-specific configuration
    result['struct_init']['sub_cfg'][sub_type] = bus_config
    return result
