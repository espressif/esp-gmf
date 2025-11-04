# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# FS_FAT device config parser
VERSION = 'v1.0.0'

# Define valid enum values for SDMMC configuration
VALID_SDMMC_ENUMS = {
    'slot': [
        'SDMMC_HOST_SLOT_0',
        'SDMMC_HOST_SLOT_1',
        'SDMMC_HOST_SLOT_2'
    ],
    'frequency': [
        'SDMMC_FREQ_DEFAULT',      # 20000
        'SDMMC_FREQ_HIGHSPEED',    # 40000
        'SDMMC_FREQ_PROBING',      # 400
        'SDMMC_FREQ_52M',          # 52000
        'SDMMC_FREQ_26M',          # 26000
        'SDMMC_FREQ_DDR50',        # 50000
        'SDMMC_FREQ_SDR50'         # 100000
    ],
    'slot_flags': [
        'SDMMC_SLOT_FLAG_INTERNAL_PULLUP',
        'SDMMC_SLOT_FLAG_WP_ACTIVE_HIGH',
        'SDMMC_SLOT_FLAG_UHS1'
    ]
}

def validate_enum_value(value: str, enum_type: str) -> bool:
    """Validate if the enum value is within the valid range for ESP-IDF SDMMC driver.

    Args:
        value: The enum value to validate
        enum_type: The type of enum (slot, frequency, etc.)

    Returns:
        True if valid, False otherwise
    """
    if enum_type not in VALID_SDMMC_ENUMS:
        print(f"⚠️  WARNING: Unknown enum type '{enum_type}' for validation")
        return True  # Allow unknown types to pass

    if value not in VALID_SDMMC_ENUMS[enum_type]:
        print(f"❌ YAML VALIDATION ERROR: Invalid {enum_type} value '{value}'!")
        print(f'   File: board_devices.yaml')
        print(f'   📋 Device: FS_FAT configuration')
        print(f"   ❌ Invalid value: '{value}'")
        print(f'   ✅ Valid values: {VALID_SDMMC_ENUMS[enum_type]}')
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
        raise ValueError(f"YAML validation error in FS_FAT device: Invalid {enum_type} value '{result}'. Valid values: {VALID_SDMMC_ENUMS.get(enum_type, [])}")

    return result

def get_includes() -> list:
    """Return list of required include headers for FS_FAT device"""
    return [
        'dev_fs_fat.h',
        'driver/sdmmc_host.h',
        'sdmmc_cmd.h'
    ]

def parse_sdmmc_sub_config(sub_config: dict) -> dict:
    """Parse SDMMC sub configuration"""
    # Get slot configuration from YAML with validation
    slot_raw = sub_config.get('slot', 'SDMMC_HOST_SLOT_1')
    slot = get_enum_value(slot_raw, 'SDMMC_HOST_SLOT_1', 'slot')
    bus_width = sub_config.get('bus_width', 1)  # Default to 1-bit mode
    # Get slot flags with validation
    slot_flags_raw = sub_config.get('slot_flags', 'SDMMC_SLOT_FLAG_INTERNAL_PULLUP')
    slot_flags = get_enum_value(slot_flags_raw, 'SDMMC_SLOT_FLAG_INTERNAL_PULLUP', 'slot_flags')
    # Get GPIO pins with default values
    pins = sub_config.get('pins', {})
    pin_config = {
        'clk': pins.get('clk', -1),
        'cmd': pins.get('cmd', -1),
        'd0': pins.get('d0', -1),
        'd1': pins.get('d1', -1),
        'd2': pins.get('d2', -1),
        'd3': pins.get('d3', -1),
        'd4': pins.get('d4', -1),
        'd5': pins.get('d5', -1),
        'd6': pins.get('d6', -1),
        'd7': pins.get('d7', -1),
        'cd': pins.get('cd', -1),
        'wp': pins.get('wp', -1)
    }

    # Get optional LDO channel ID for powering SDMMC IO
    ldo_chan_id = sub_config.get('ldo_chan_id', -1)

    return {
        'slot': slot,
        'bus_width': bus_width,
        'slot_flags': slot_flags,
        'pins': pin_config,
        'ldo_chan_id': ldo_chan_id
    }

def parse_spi_sub_config(sub_config: dict, peripherals_dict=None) -> dict:
    """Parse SPI sub configuration"""
    cs_gpio_num = sub_config.get('cs_gpio_num', 15)

    # Get SPI bus name from peripherals
    spi_bus_name = None

    # First try to get peripherals from config (for backward compatibility)
    peripherals_list = []
    if sub_config:
        peripherals_list = sub_config.get('peripherals', [])

    # Look for spi_master peripheral in the peripherals list
    for peripheral in peripherals_list:
        if isinstance(peripheral, dict) and peripheral.get('name') == 'spi_master':
            spi_bus_name = peripheral.get('name', 'spi_master')
            break
        elif isinstance(peripheral, str) and peripheral == 'spi_master':
            spi_bus_name = 'spi_master'
            break

    # Fallback to peripherals_dict if not found in config
    if not spi_bus_name and peripherals_dict:
        # Look for spi_master peripheral
        for peripheral in peripherals_dict:
            if peripheral.get('name') == 'spi_master':
                spi_bus_name = peripheral.get('name', 'spi_master')
                break

    if not spi_bus_name:
        spi_bus_name = 'spi_master'  # Default fallback

    return {
        'cs_gpio_num': cs_gpio_num,
        'spi_bus_name': spi_bus_name
    }

def parse(name: str, full_config: dict, peripherals_dict=None) -> dict:
    """Parse FS_FAT device configuration from YAML"""
    config = full_config.get('config', {})
    sub_type = full_config.get('sub_type')
    # sub_type is mandatory
    if not sub_type:
        raise ValueError(f"FS_FAT device '{name}' is missing required 'sub_type' field. Must be 'sdmmc' or 'spi'")

    # Validate sub_type value
    if sub_type not in ['sdmmc', 'spi']:
        raise ValueError(f"FS_FAT device '{name}' has invalid 'sub_type' value '{sub_type}'. Must be 'sdmmc' or 'spi'")

    # Get basic configuration
    mount_point = config.get('mount_point', '/sdcard')

    # VFS configuration
    vfs_config = config.get('vfs_config', {})
    format_if_mount_failed = vfs_config.get('format_if_mount_failed', False)
    max_files = vfs_config.get('max_files', 5)
    allocation_unit_size = vfs_config.get('allocation_unit_size', 16384)

    # Parse sub configuration based on sub_type
    if sub_type == 'sdmmc':
        sub_cfg = parse_sdmmc_sub_config(config)
        # Set the sdmmc member of the union
        sub_cfg_union = {'sdmmc': sub_cfg}
    elif sub_type == 'spi':
        sub_cfg = parse_spi_sub_config(config, peripherals_dict)
        # Set the spi member of the union
        sub_cfg_union = {'spi': sub_cfg}
    else:
        raise ValueError(f'Unsupported sub_type: {sub_type}')

    return {
        'struct_type': 'dev_fs_fat_config_t',
        'struct_var': f'{name}_cfg',
        'struct_init': {
            'name': name,
            'mount_point': mount_point,
            'frequency': sub_cfg.get('frequency', 'SDMMC_FREQ_HIGHSPEED') if sub_type == 'sdmmc' else 'SDMMC_FREQ_DEFAULT',
            'vfs_config': {
                'format_if_mount_failed': format_if_mount_failed,
                'max_files': max_files,
                'allocation_unit_size': allocation_unit_size
            },
            'sub_type': sub_type,
            'sub_cfg': sub_cfg_union
        }
    }
