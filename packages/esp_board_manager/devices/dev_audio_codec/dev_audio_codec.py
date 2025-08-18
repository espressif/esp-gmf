# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# Audio codec device config parser
VERSION = 'v1.0.0'

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

def get_includes() -> list:
    """Return list of required include headers for audio codec device"""
    return [
        'dev_audio_codec.h'
    ]

def parse(name: str, config: dict, peripherals_dict=None) -> dict:
    # Parse the device name - use name directly for C naming
    c_name = name.replace('-', '_')  # Convert hyphens to underscores for C naming

    # Get the device config and peripherals
    device_config = config.get('config', {})
    peripherals = config.get('peripherals', [])

    # Get chip name from device level
    chip_name = config.get('chip', 'unknown')

    # Initialize default configurations
    pa_cfg = None  # Only set if PA is actually configured
    i2c_cfg = {'name': '', 'port': 0, 'address': 0x30, 'frequency': 400000}  # Default I2C address in hex
    i2s_cfg = {'name': '', 'port': 0}

    # Process peripherals
    for periph in peripherals:
        periph_name = periph.get('name', '')
        if periph_name.startswith('gpio'):
            # Check if peripheral exists in peripherals_dict if provided
            if peripherals_dict is not None and periph_name not in peripherals_dict:
                raise ValueError(f"Audio codec device {name} references undefined peripheral '{periph_name}'")
            # This is PA configuration
            # Get GPIO pin number from peripheral config
            port = -1
            if peripherals_dict and periph_name in peripherals_dict:
                peripheral_config = peripherals_dict[periph_name]
                port = peripheral_config.config.get('pin', -1)
            pa_cfg = {
                'name': periph_name,  # Use original name from YAML
                'port': port,  # Use GPIO pin number as port
                'active_level': int(periph.get('active_level', 0)),
                'gain': float(periph.get('gain', 0.0))
            }
        elif periph_name.startswith('i2c'):
            # Check if peripheral exists in peripherals_dict if provided
            if peripherals_dict is not None and periph_name not in peripherals_dict:
                raise ValueError(f"Audio codec device {name} references undefined peripheral '{periph_name}'")
            # This is I2C configuration
            address = periph.get('address', 0x30)  # Keep hex format
            # Get port from peripheral config if available
            port = 0
            if peripherals_dict and periph_name in peripherals_dict:
                peripheral_config = peripherals_dict[periph_name]
                port = peripheral_config.config.get('port', 0)
            i2c_cfg = {
                'name': periph_name,  # Use original name from YAML
                'port': port,
                'address': address,  # No need to convert, keep as hex
                'frequency': int(periph.get('frequency', 400000))
            }
        elif periph_name.startswith('i2s'):
            # Check if peripheral exists in peripherals_dict if provided
            if peripherals_dict is not None and periph_name not in peripherals_dict:
                raise ValueError(f"Audio codec device {name} references undefined peripheral '{periph_name}'")
            # This is I2S configuration
            # Get port from peripheral config if available
            port = 0
            if peripherals_dict and periph_name in peripherals_dict:
                peripheral_config = peripherals_dict[periph_name]
                port = peripheral_config.config.get('port', 0)
            i2s_cfg = {
                'name': periph_name,  # Use original name from YAML
                'port': port
            }

    # Convert string masks to integers
    adc_channel_mask = device_config.get('adc_channel_mask', '0011')
    if isinstance(adc_channel_mask, str):
        # Convert binary string to hex (e.g., "1110" -> 0xE)
        adc_channel_mask = int(adc_channel_mask, 2)

    dac_channel_mask = device_config.get('dac_channel_mask', '0')
    if isinstance(dac_channel_mask, str):
        # Convert binary string to hex (e.g., "1110" -> 0xE)
        dac_channel_mask = int(dac_channel_mask, 2)

    # Parse metadata if provided
    metadata = device_config.get('metadata', None)
    metadata_size = len(metadata) if metadata else 0

    # Use default empty PA config if no PA is configured
    if pa_cfg is None:
        pa_cfg = {'name': 'none', 'port': -1, 'active_level': 0, 'gain': 0.0}

    result = {
        'struct_type': 'dev_audio_codec_config_t',
        'struct_var': f'{c_name}_cfg',  # Use the correct C name format
        'struct_init': {
            'name': c_name,  # Use the correct C name
            'chip': chip_name,  # Add chip field
            'type': str(config.get('type', 'audio_codec')),
            'adc_enabled': bool(device_config.get('adc_enabled', False)),
            'adc_max_channel': int(device_config.get('adc_max_channel', 0)),
            'adc_channel_mask': adc_channel_mask,
            'adc_init_gain': int(device_config.get('adc_init_gain', 0)),
            'dac_enabled': bool(device_config.get('dac_enabled', False)),
            'dac_max_channel': int(device_config.get('dac_max_channel', 0)),
            'dac_channel_mask': dac_channel_mask,
            'dac_init_gain': int(device_config.get('dac_init_gain', 0)),
            'pa_cfg': pa_cfg,
            'i2c_cfg': i2c_cfg,
            'i2s_cfg': i2s_cfg,
            'metadata': metadata,
            'metadata_size': metadata_size,
            'mclk_enabled': bool(device_config.get('mclk_enabled', False)),
            'aec_enabled': bool(device_config.get('aec', False)),
            'eq_enabled': bool(device_config.get('eq', False)),
            'alc_enabled': bool(device_config.get('alc', False))
        }
    }
    return result
