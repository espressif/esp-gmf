# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# FATFS spi sd card device config parser
VERSION = 'v1.0.0'

# Define valid frequency enum values
VALID_FREQUENCY_ENUMS = [
    'SDMMC_FREQ_DEFAULT',      # 20000
    'SDMMC_FREQ_HIGHSPEED',    # 40000
    'SDMMC_FREQ_PROBING',      # 400
    'SDMMC_FREQ_52M',          # 52000
    'SDMMC_FREQ_26M',          # 26000
    'SDMMC_FREQ_DDR50',        # 50000
    'SDMMC_FREQ_SDR50'         # 100000
]

def validate_enum_value(value: str, enum_type: str) -> bool:
    """Validate if the enum value is within the valid range.

    Args:
        value: The enum value to validate
        enum_type: The type of enum (frequency, slot, etc.)

    Returns:
        True if valid, False otherwise
    """
    if enum_type == 'frequency':
        valid_enums = VALID_FREQUENCY_ENUMS
    else:
        print(f"⚠️  WARNING: Unknown enum type '{enum_type}' for validation")
        return True  # Allow unknown types to pass

    if value not in valid_enums:
        print(f"❌ YAML VALIDATION ERROR: Invalid {enum_type} value '{value}'!")
        print(f'   File: board_devices.yaml')
        print(f'   📋 Device: SDCARD configuration')
        print(f"   ❌ Invalid value: '{value}'")
        print(f'   ✅ Valid values: {valid_enums}')
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
        if enum_type == 'frequency':
            valid_values = VALID_FREQUENCY_ENUMS
        else:
            valid_values = []
        raise ValueError(f"YAML validation error in SDCARD device: Invalid {enum_type} value '{result}'. Valid values: {valid_values}")

    return result

def get_includes() -> list:
    """Return list of required include headers based on bus type"""
    return [
            'dev_fatfs_sdcard_spi.h',
        ]

def parse(name: str, full_config: dict, peripherals_dict=None) -> dict:
    """Parse SDCARD device configuration from YAML, supporting both SDMMC and SPI bus types"""
    # Get the actual config from the full_config
    config = full_config.get('config', {})

    # Get basic configuration
    mount_point = config.get('mount_point', '/sdcard')

    # VFS configuration - support both nested vfs_config and direct config
    vfs_config = config.get('vfs_config', {})
    format_if_mount_failed = vfs_config.get('format_if_mount_failed', config.get('format_if_mount_failed', False))
    max_files = vfs_config.get('max_files', config.get('max_files', 5))
    allocation_unit_size = vfs_config.get('allocation_unit_size', config.get('allocation_unit_size', 16384))

    # Get frequency from YAML with validation
    frequency_raw = config.get('frequency', 'SDMMC_FREQ_HIGHSPEED')
    frequency = get_enum_value(frequency_raw, 'SDMMC_FREQ_HIGHSPEED', 'frequency')

    # Get SPI configuration
    cs_gpio_num = config.get('cs_gpio_num', -1)  # Default not used
    if cs_gpio_num == -1:
        raise ValueError(f"Missing required 'cs_gpio_num' for SDCARD device '{name}' using SPI bus")
    
    # Get SPI peripheral name
    peripherals = config.get('peripherals', [])
    spi_bus_name = ''
    for periph in peripherals:
        if isinstance(periph, dict):
            periph_name = periph.get('name', '')
            if periph_name.startswith('spi'):
                # Check if peripheral exists in peripherals_dict if provided
                if peripherals_dict is not None and periph_name not in peripherals_dict:
                    raise ValueError(f"SPI SD card device {name} references undefined peripheral '{periph_name}'")
                spi_bus_name = periph_name
                break
        elif isinstance(periph, str) and periph.startswith('spi'):
            # Check if peripheral exists in peripherals_dict if provided
            if peripherals_dict is not None and periph not in peripherals_dict:
                raise ValueError(f"SPI SD card device {name} references undefined peripheral '{periph}'")
            spi_bus_name = periph
            break
    
    if not spi_bus_name:
        raise ValueError(f"Missing required SPI peripheral for SDCARD device '{name}' using SPI bus. Please specify in 'peripherals'.")
    
    # Build the result
    result = {
        'struct_type': 'dev_fatfs_sdcard_spi_config_t',
        'struct_var': f'{name}_cfg',
        'struct_init': {
            'name': name,
            'mount_point': mount_point,
            'vfs_config': {
                'format_if_mount_failed': format_if_mount_failed,
                'max_files': max_files,
                'allocation_unit_size': allocation_unit_size,
            },
            'frequency': frequency,
            'spi_bus_name': spi_bus_name,
            'cs_gpio_num': cs_gpio_num,
        }
    }
    
    return result
