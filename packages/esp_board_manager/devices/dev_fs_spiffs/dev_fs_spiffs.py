# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# SPIFFS filesystem device config parser
VERSION = 'v1.0.0'

import sys
import yaml
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

def load_default_config() -> dict:
    """Load default configuration from YAML file"""
    config_file = Path(__file__).parent / 'dev_fs_spiffs.yaml'
    try:
        with open(config_file, 'r', encoding='utf-8') as f:
            config = yaml.safe_load(f)
            # Extract config from the first fs_spiffs device in the list
            if isinstance(config, list):
                for device in config:
                    if device.get('type') == 'fs_spiffs':
                        return device.get('config', {})
            # If no fs_spiffs device found, return empty dict
            return {}
    except FileNotFoundError:
        print(f'Warning: Configuration file {config_file} not found, using hardcoded defaults')
        return {
            'base_path': '/spiffs',
            'partition_label': 'spiffs_data',
            'max_files': 5,
            'format_if_mount_failed': False
        }
    except yaml.YAMLError as e:
        print(f'Warning: Error parsing configuration file {config_file}: {e}, using hardcoded defaults')
        return {
            'base_path': '/spiffs',
            'partition_label': 'spiffs_data',
            'max_files': 5,
            'format_if_mount_failed': False
        }

def get_includes() -> list:
    """Return list of required include headers for SPIFFS filesystem device"""
    return [
        'dev_fs_spiffs.h'
    ]

def parse(name: str, config: dict, peripherals_dict=None) -> dict:
    # Parse the device name - use name directly for C naming
    c_name = name.replace('-', '_')  # Convert hyphens to underscores for C naming

    # Get the device config
    device_config = config.get('config', {})

    # Load default configuration from YAML file
    default_config = load_default_config()

    result = {
        'struct_type': 'dev_fs_spiffs_config_t',
        'struct_var': f'{c_name}_cfg',  # Use the correct C name format
        'struct_init': {
            'name': c_name,  # Use the correct C name
            'base_path': device_config.get('base_path', default_config.get('base_path', '/spiffs')),
            'partition_label': device_config.get('partition_label', default_config.get('partition_label', 'spiffs_data')),
            'max_files': int(device_config.get('max_files', default_config.get('max_files', 5))),
            'format_if_mount_failed': bool(device_config.get('format_if_mount_failed', default_config.get('format_if_mount_failed', False)))
        }
    }
    return result
