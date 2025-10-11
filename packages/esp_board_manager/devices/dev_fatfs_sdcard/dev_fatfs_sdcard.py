# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# FATFS SD card device config parser
VERSION = 'v1.0.0'

# Define valid enum values for SDMMC configuration
VALID_SDMMC_ENUMS = {
    'slot': [
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
        print(f'   📋 Device: SDCARD configuration')
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
        raise ValueError(f"YAML validation error in SDCARD device: Invalid {enum_type} value '{result}'. Valid values: {VALID_SDMMC_ENUMS.get(enum_type, [])}")

    return result

def get_includes() -> list:
    """Return list of required include headers for SDCARD device"""
    return [
        'dev_fatfs_sdcard.h',
        'driver/sdmmc_types.h',
        'driver/sdmmc_host.h'
    ]

def parse(name: str, full_config: dict, peripherals_dict=None) -> dict:
    """Parse SDCARD device configuration from YAML"""
    # Get the actual config from the full_config
    config = full_config.get('config', {})

    # Get basic configuration
    mount_point = config.get('mount_point', '/sdcard')

    # VFS configuration - support both nested vfs_config and direct config
    vfs_config = config.get('vfs_config', {})
    # Try to get from vfs_config first, then fall back to direct config
    format_if_mount_failed = vfs_config.get('format_if_mount_failed', config.get('format_if_mount_failed', False))
    max_files = vfs_config.get('max_files', config.get('max_files', 5))
    allocation_unit_size = vfs_config.get('allocation_unit_size', config.get('allocation_unit_size', 16384))

    """
    Defined by sd_protocol_types.h
    #define SDMMC_HOST_FLAG_1BIT    BIT(0)      /*!< host supports 1-line SD and MMC protocol */
    #define SDMMC_HOST_FLAG_4BIT    BIT(1)      /*!< host supports 4-line SD and MMC protocol */
    #define SDMMC_HOST_FLAG_8BIT    BIT(2)      /*!< host supports 8-line MMC protocol */
    #define SDMMC_HOST_FLAG_SPI     BIT(3)      /*!< host supports SPI protocol */
    #define SDMMC_HOST_FLAG_DDR     BIT(4)      /*!< host supports DDR mode for SD/MMC */
    #define SDMMC_HOST_FLAG_DEINIT_ARG BIT(5)   /*!< host `deinit` function called with the slot argument */
    #define SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF \
                                     BIT(6)      /*!< Allocate internal buffer of 512 bytes that meets DMA's requirements.
                                                     Currently this is only used by the SDIO driver. Set this flag when
                                                     using SDIO CMD53 byte mode, with user buffer that is behind the cache
                                                     or not aligned to 4 byte boundary. */
    """
    # Get slot configuration from YAML with validation
    slot_raw = config.get('slot', 'SDMMC_HOST_SLOT_1')
    slot = get_enum_value(slot_raw, 'SDMMC_HOST_SLOT_1', 'slot')

    # Get frequency configuration from YAML with validation
    frequency_raw = config.get('frequency', 'SDMMC_FREQ_HIGHSPEED')
    frequency = get_enum_value(frequency_raw, 'SDMMC_FREQ_HIGHSPEED', 'frequency')
    bus_width = config.get('bus_width', 1)  # Default to 1-bit mode
    slot_flags = config.get('slot_flags', 'SDMMC_SLOT_FLAG_INTERNAL_PULLUP')  # Default slot flags

    # Get GPIO pins with default values from board_devices.yaml
    pins = config.get('pins', {})
    pin_config = {
        'clk': pins.get('clk', 15),  # GPIO_NUM_15
        'cmd': pins.get('cmd', 7),   # GPIO_NUM_7
        'd0': pins.get('d0', 4),     # GPIO_NUM_4
        'd1': pins.get('d1', -1),    # Not used
        'd2': pins.get('d2', -1),    # Not used
        'd3': pins.get('d3', -1),    # Not used
        'd4': pins.get('d4', -1),    # Not used
        'd5': pins.get('d5', -1),    # Not used
        'd6': pins.get('d6', -1),    # Not used
        'd7': pins.get('d7', -1),    # Not used
        'cd': pins.get('cd', -1),    # Card detect pin, not used
        'wp': pins.get('wp', -1)     # Write protect pin, not used
    }

    # Get optional LDO channel ID for powering SDMMC IO (Optional)
    ldo_chan_id = config.get('ldo_chan_id', -1)  # Default to not used

    return {
        'struct_type': 'dev_fatfs_sdcard_config_t',
        'struct_var': f'{name}_cfg',
        'struct_init': {
            'name': name,
            'mount_point': mount_point,
            'vfs_config': {
                'format_if_mount_failed': format_if_mount_failed,
                'max_files': max_files,
                'allocation_unit_size': allocation_unit_size
            },
            'frequency': frequency,
            'slot': slot,         # Use the slot value directly from YAML
            'bus_width': bus_width,
            'slot_flags': slot_flags,
            'pins': pin_config,
            'ldo_chan_id': ldo_chan_id,
        }
    }
