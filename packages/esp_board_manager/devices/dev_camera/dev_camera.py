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
    ],
    'spi_cam_intf': [
        'ESP_CAM_CTLR_SPI_CAM_INTF_SPI',
        'ESP_CAM_CTLR_SPI_CAM_INTF_PARLIO',
    ],
    'spi_cam_io_mode': [
        'ESP_CAM_CTLR_SPI_CAM_IO_MODE_1BIT',
        'ESP_CAM_CTLR_SPI_CAM_IO_MODE_2BIT',
    ],
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
                    raise ValueError(f"Camera device {name} references undefined peripheral '{periph_name}'")
                i2c_name = periph_name
                i2c_freq = int(periph.get('frequency', 100000))
                break

        if not i2c_name:
            raise ValueError(f"Camera device {name} (dvp) requires an i2c peripheral in 'peripherals'")

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
        # Process peripherals to find I2C master and LDO
        i2c_name = ''
        i2c_freq = 0
        ldo_name = ''
        csi_config_dict = device_config.get('csi_config', {})
        dont_init_ldo = bool(csi_config_dict.get('dont_init_ldo', True))
        peripherals = config.get('peripherals', [])
        for periph in peripherals:
            periph_name = periph.get('name', '')
            if periph_name.startswith('i2c'):
                # Check if peripheral exists in peripherals_dict if provided
                if peripherals_dict is not None and periph_name not in peripherals_dict:
                    raise ValueError(f"Camera device {name} references undefined peripheral '{periph_name}'")
                i2c_name = periph_name
                i2c_freq = int(periph.get('frequency', 100000))
            elif periph_name.startswith('ldo'):
                # Check if peripheral exists in peripherals_dict if provided
                if peripherals_dict is not None and periph_name not in peripherals_dict:
                    raise ValueError(f"Camera device {name} references undefined peripheral '{periph_name}'")
                ldo_name = periph_name

        if not i2c_name:
            raise ValueError(f"Camera device {name} (csi) requires an i2c peripheral in 'peripherals'")
        if dont_init_ldo and not ldo_name:
            raise ValueError(
                f'Camera device {name} (csi) requires an ldo peripheral when dont_init_ldo is true'
            )

        # Parse CSI settings
        reset_io = int(csi_config_dict.get('reset_io', -1))
        pwdn_io = int(csi_config_dict.get('pwdn_io', -1))

        # Parse XCLK configuration
        xclk_config_dict = csi_config_dict.get('xclk_config', {})
        xclk_pin = int(xclk_config_dict.get('xclk_pin', -1))
        xclk_freq_hz = int(xclk_config_dict.get('xclk_freq_hz', 20000000))
        if xclk_pin == -1 and 'esp_clock_router_cfg' in xclk_config_dict:
            xclk_config_dict = xclk_config_dict.get('esp_clock_router_cfg', {})
            xclk_pin = int(xclk_config_dict.get('xclk_pin', -1))
            xclk_freq_hz = int(xclk_config_dict.get('xclk_freq_hz', 20000000))
        xclk_config_struct = {}
        if xclk_pin != -1:
             # If user defined simple xclk_pin/freq, wrap it in esp_clock_router_cfg for C struct
             xclk_config_struct = {
                 'esp_clock_router_cfg': {
                     'xclk_pin': xclk_pin,
                     'xclk_freq_hz': xclk_freq_hz
                 }
             }

        # Build CSI config
        bus_config = {
            'i2c_name': i2c_name,
            'i2c_freq': i2c_freq,
            'ldo_name': ldo_name,
            'reset_io': reset_io,
            'pwdn_io': pwdn_io,
            'dont_init_ldo': dont_init_ldo,
            'xclk_config': xclk_config_struct
        }

    elif sub_type == 'spi':
        i2c_name = ''
        i2c_freq = 0
        peripherals = config.get('peripherals', [])
        for periph in peripherals:
            periph_name = periph.get('name', '')
            if periph_name.startswith('i2c'):
                if peripherals_dict is not None and periph_name not in peripherals_dict:
                    raise ValueError(f"Camera device {name} references undefined peripheral '{periph_name}'")
                i2c_name = periph_name
                i2c_freq = int(periph.get('frequency', 100000))
                break

        if not i2c_name:
            raise ValueError(f"Camera device {name} (spi) requires an i2c peripheral in 'peripherals'")

        sc = device_config.get('spi_config', {})
        intf = get_enum_value(sc.get('intf'), 'ESP_CAM_CTLR_SPI_CAM_INTF_SPI', 'spi_cam_intf')
        # io_mode / spi_data1_io_pin: newer esp_video (esp_video_init.h) has these fields; older 1.4.x
        # layout does not — emitting them breaks static init alignment vs released components.
        # Defaults are applied in dev_camera_sub_spi.c (1-bit, data1 NC) when the struct includes them.

        esp_video_spi = {
            'intf': intf,
            'spi_port': int(sc.get('spi_port', 0)),
            'spi_cs_pin': int(sc.get('spi_cs_pin', -1)),
            'spi_sclk_pin': int(sc.get('spi_sclk_pin', -1)),
            'spi_data0_io_pin': int(sc.get('spi_data0_io_pin', -1)),
            'reset_pin': int(sc.get('reset_pin', -1)),
            'pwdn_pin': int(sc.get('pwdn_pin', -1)),
            'xclk_source': sc.get('xclk_source', 'ESP_CAM_SENSOR_XCLK_LEDC'),
            'xclk_freq': int(sc.get('xclk_freq', 20000000)),
            'xclk_pin': int(sc.get('xclk_pin', -1)),
        }
        # Optional: only for esp_video whose esp_video_init_spi_config_t includes these members
        # (see esp_video_init.h). Omitted by default for compatibility with esp_video 1.4.x.
        if sc.get('io_mode') is not None and sc.get('io_mode') != '':
            esp_video_spi['io_mode'] = get_enum_value(sc.get('io_mode'), 'ESP_CAM_CTLR_SPI_CAM_IO_MODE_1BIT', 'spi_cam_io_mode')
        if sc.get('spi_data1_io_pin') is not None:
            esp_video_spi['spi_data1_io_pin'] = int(sc.get('spi_data1_io_pin'))
        ledc = sc.get('xclk_ledc_cfg')
        if ledc:
            esp_video_spi['xclk_ledc_cfg'] = {
                'timer': ledc.get('timer', 0),
                'clk_cfg': ledc.get('clk_cfg', 'LEDC_AUTO_CLK'),
                'channel': ledc.get('channel', 0),
            }
        bus_config = {
            'i2c_name': i2c_name,
            'i2c_freq': i2c_freq,
            'esp_video_spi': esp_video_spi,
        }
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
