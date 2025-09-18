# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
Device parser for ESP Board Manager
Parses device configurations from YAML files
"""

from .utils.logger import LoggerMixin
from .utils.yaml_utils import load_yaml_safe
from .settings import BoardManagerConfig
from .parser_loader import load_parsers
from .peripheral_parser import PeripheralParser
from .name_validator import validate_component_name
from pathlib import Path
from typing import Dict, List, Any, Optional, Tuple
import os
import re

class DeviceParser(LoggerMixin):
    """Parser for device configurations"""

    def __init__(self, script_dir: Path):
        super().__init__()
        self.script_dir = script_dir
        self.peripheral_parser = PeripheralParser(script_dir)
        self.parser_loader = None  # Will be initialized when needed

    def _get_parser_loader(self):
        """Lazy initialization of parser_loader"""
        if self.parser_loader is None:
            # parser_loader.py only contains functions, not a class
            # We'll use the load_parsers function directly
            self.parser_loader = type('ParserLoader', (), {
                'get_parsers': lambda: load_parsers([], 'dev_', str(self.script_dir / 'devices'))
            })()
        return self.parser_loader

    def parse_devices_yaml(self, yaml_path: str, peripherals_dict: Dict[str, Any],
                          periph_name_map: Dict[str, str], board_path: str = '',
                          extra_configs: Dict[str, Any] = None,
                          extra_includes: set = None, stop_on_error: bool = True) -> List[str]:
        """
        Parse devices from YAML file and return list of device types found

        Args:
            yaml_path: Path to the YAML file
            peripherals_dict: Dictionary of available peripherals
            periph_name_map: Mapping of peripheral names
            board_path: Board path
            extra_configs: Extra configurations
            extra_includes: Extra includes
            stop_on_error: Whether to stop on critical errors (default: True)
        """
        if extra_configs is None:
            extra_configs = {}
        if extra_includes is None:
            extra_includes = set()

        # Load parsers
        parsers = self._get_parser_loader().get_parsers()

        # Try to load the YAML file
        try:
            data = load_yaml_safe(Path(yaml_path))
        except FileNotFoundError:
            self.logger.warning(f'Could not load {yaml_path}: File not found')
            return []
        except Exception as e:
            self.logger.warning(f'Could not load {yaml_path}: {e}')
            return []

        # Check if file exists
        if not Path(yaml_path).exists():
            self.logger.error(f'File not found! Path: {yaml_path}')
            self.logger.info('Please check if the file exists and the path is correct')
            return []

        # Try to read the file
        try:
            data = load_yaml_safe(Path(yaml_path))
        except PermissionError:
            self.logger.error(f'Cannot read file! Path: {yaml_path}')
            self.logger.info(f'Error: Permission denied')
            return []
        except Exception as e:
            self.logger.error(f'Cannot read file! Path: {yaml_path}')
            self.logger.info(f'Error: {e}')
            return []

        # Try to parse YAML
        try:
            data = load_yaml_safe(Path(yaml_path))
        except Exception as e:
            self.logger.error(f'Invalid YAML syntax! Path: {yaml_path}')
            self.logger.info(f'Please check the YAML syntax and indentation. Error: {e}')
            return []
        except Exception as e:
            self.logger.error(f'Unexpected error! Path: {yaml_path}')
            self.logger.info(f'Error: {e}')
            return []

        # Validate data structure
        if not data:
            self.logger.error(f'Empty or invalid YAML file! Path: {yaml_path}')
            self.logger.info('The file appears to be empty or contains no valid YAML content')
            return []

        if 'devices' not in data:
            self.logger.error(f'No devices found! Path: {yaml_path}')
            self.logger.info("The file should contain a 'devices' section with at least one device")
            return []

        devices = data['devices']
        if not devices:
            self.logger.error(f'No devices found! Path: {yaml_path}')
            self.logger.info("The file should contain a 'devices' section with at least one device")
            return []

        # Process devices
        result_devices = []
        device_types = []

        for i, dev in enumerate(devices):
            try:
                # Validate device structure
                if 'name' not in dev:
                    self.logger.error(f'Device missing name! Path: {yaml_path}')
                    self.logger.info(f'Device #{i+1}: {dev}. Missing field: name. Each device must have a name field')
                    continue

                # Validate peripheral references
                if 'peripherals' in dev:
                    for periph in dev['peripherals']:
                        if isinstance(periph, dict):
                            if 'name' not in periph:
                                self.logger.error(f'Peripheral missing name! Path: {yaml_path}')
                                self.logger.info(f"Device #{i+1}: {dev['name']}. Peripheral: {periph}. Missing field: name")
                                continue
                            periph_name = periph['name']
                        else:
                            periph_name = str(periph)

                        # Check if peripheral name is valid
                        if not periph_name or not isinstance(periph_name, str):
                            self.logger.error(f'Invalid peripheral name! Path: {yaml_path}')
                            self.logger.info(f"Device #{i+1}: {dev['name']}. Invalid peripheral name: {periph_name}")
                            continue

                        # Check if peripheral exists in peripherals_dict
                        if periph_name not in peripherals_dict:
                            error_msg = f"Undefined peripheral reference! Path: {yaml_path}\nDevice '{dev['name']}'. Peripheral '{periph_name}' not found in peripherals configuration"
                            self.logger.error(error_msg)
                            if stop_on_error:
                                raise ValueError(error_msg)
                            continue

                # Validate peripheral references in config
                if 'config' in dev and 'peripherals' in dev['config']:
                    for periph_name in dev['config']['peripherals']:
                        if not isinstance(periph_name, str):
                            self.logger.error(f'Invalid peripheral name! Path: {yaml_path}')
                            self.logger.info(f"Device #{i+1}: {dev['name']}. Invalid peripheral name in config: {periph_name}")
                            continue

                        if periph_name not in peripherals_dict:
                            error_msg = f"Undefined peripheral reference! Path: {yaml_path}\nDevice '{dev['name']}'. Peripheral '{periph_name}' in config not found in peripherals configuration"
                            self.logger.error(error_msg)
                            if stop_on_error:
                                raise ValueError(error_msg)
                            continue

                        # Map peripheral name to actual peripheral type
                        if periph_name in periph_name_map:
                            dev['config']['peripherals'][periph_name_map[periph_name]] = dev['config']['peripherals'].pop(periph_name)

                # Validate peripheral reference format
                if 'peripheral' in dev:
                    periph_ref = dev['peripheral']
                    if not isinstance(periph_ref, str):
                        self.logger.error(f'Invalid peripheral reference! Path: {yaml_path}')
                        self.logger.info(f"Device #{i+1}: {dev['name']}. Invalid peripheral reference: {periph_ref}")
                        continue

                # Ensure init_skip field exists with default value
                if 'init_skip' not in dev:
                    dev['init_skip'] = False  # Default to False (do not skip initialization)

                result_devices.append(dev)
                device_types.append(dev['type'])

            except ValueError as e:
                # Re-raise ValueError to stop the generation process
                raise
            except Exception as e:
                self.logger.error(f'Invalid device configuration! Path: {yaml_path}')
                self.logger.info(f'Device #{i+1}: {dev}. Error: {e}')
                continue

        self.logger.info(f'   Loaded {len(result_devices)} devices from {yaml_path}')
        return device_types

    def parse_devices_yaml_legacy(self, yaml_path: str, peripherals_dict: Dict[str, Any], stop_on_error: bool = True) -> List[Any]:
        """
        Legacy method that returns a list of device objects for backward compatibility

        Args:
            yaml_path: Path to the YAML file
            peripherals_dict: Dictionary of available peripherals
            stop_on_error: Whether to stop on critical errors (default: True)
        """
        from dataclasses import dataclass

        @dataclass
        class Device:
            name: str
            type: str
            config: dict
            peripherals: list
            init_skip: bool = False  # Default to False (do not skip initialization)

        # Load YAML with includes
        try:
            data = self._load_yaml_with_includes(yaml_path)
        except Exception as e:
            self.logger.error(f'Failed to load YAML file! Path: {yaml_path}')
            self.logger.info(f'Error: {e}')
            return []

        if not data:
            self.logger.error(f'Empty or invalid YAML file! Path: {yaml_path}')
            self.logger.info('The file appears to be empty or contains no valid YAML content')
            return []

        devices = data.get('devices', [])
        if not devices:
            self.logger.error(f'No devices found! Path: {yaml_path}')
            self.logger.info("The file should contain a 'devices' section with at least one device")
            return []

        result_devices = []
        for i, dev in enumerate(devices):
            try:
                name = dev.get('name')
                if not name:
                    self.logger.error(f'Device missing name! Path: {yaml_path}')
                    self.logger.info(f'Device #{i+1}: {dev}. Missing field: name. Each device must have a name field')
                    continue

                # Process peripherals
                periph_list = []
                device_peripherals = dev.get('peripherals', [])

                for j, p in enumerate(self.peripheral_parser.flatten_peripherals(device_peripherals)):
                    try:
                        if isinstance(p, dict):
                            pname = p.get('name')
                            if not pname:
                                self.logger.error(f'Peripheral missing name! Path: {yaml_path}')
                                self.logger.info(f"Device '{name}', Peripheral #{j+1}: {p}. Missing field: name")
                                continue

                            # Validate peripheral name using unified rules
                            if not validate_component_name(pname):
                                self.logger.error(f'Invalid peripheral name! Path: {yaml_path}')
                                self.logger.info(f"Device '{name}', Peripheral: {pname}. Invalid peripheral name. Peripheral names must be lowercase, start with a letter, and contain only letters, numbers, and underscores")
                                continue

                            if pname not in peripherals_dict:
                                error_msg = f"Undefined peripheral reference! Path: {yaml_path}\nDevice '{name}', Peripheral: {pname}. Undefined peripheral: {pname}"
                                self.logger.error(error_msg)
                                if stop_on_error:
                                    raise ValueError(error_msg)
                                continue

                            p_copy = p.copy()
                            p_copy['name'] = pname
                            periph_list.append(p_copy)
                        else:
                            # Validate string peripheral references
                            if not validate_component_name(p):
                                self.logger.error(f'Invalid peripheral name! Path: {yaml_path}')
                                self.logger.info(f"Device '{name}', Peripheral: {p}. Invalid peripheral name. Peripheral names must be lowercase, start with a letter, and contain only letters, numbers, and underscores")
                                continue

                            if p not in peripherals_dict:
                                error_msg = f"Undefined peripheral reference! Path: {yaml_path}\nDevice '{name}', Peripheral: {p}. Undefined peripheral: {p}"
                                self.logger.error(error_msg)
                                if stop_on_error:
                                    raise ValueError(error_msg)
                                continue

                            periph_list.append({'name': p})

                    except ValueError as e:
                        # Re-raise ValueError to stop the generation process
                        raise
                    except Exception as e:
                        self.logger.error(f'Invalid peripheral reference! Path: {yaml_path}')
                        self.logger.info(f"Device '{name}', Peripheral #{j+1}: {p}. Error: {e}")
                        continue

                result_devices.append(Device(
                    name=name,
                    type=dev.get('type', ''),
                    config=dev.get('config', {}),
                    peripherals=periph_list,
                    init_skip=dev.get('init_skip', False)  # Default to False (do not skip initialization)
                ))

            except ValueError as e:
                # Re-raise ValueError to stop the generation process
                raise
            except Exception as e:
                self.logger.error(f'Invalid device configuration! Path: {yaml_path}')
                self.logger.info(f'Device #{i+1}: {dev}. Error: {e}')
                continue

        self.logger.info(f'   Loaded {len(result_devices)} devices from {yaml_path}')
        return result_devices

    def _load_yaml_with_includes(self, yaml_path: str) -> dict:
        """Load YAML file with support for cross-file references and includes"""
        return load_yaml_with_includes(yaml_path)


def load_yaml_with_includes(yaml_path: str) -> dict:
    """Load YAML file with support for cross-file references and includes"""
    import yaml
    import os

    # First, load all YAML files and collect their content
    yaml_dir = os.path.dirname(yaml_path)
    all_content = ''

    # Load additional YAML files first (to ensure anchors are defined before references)
    if yaml_dir:
        for filename in os.listdir(yaml_dir):
            if filename.endswith('.yaml') and filename != os.path.basename(yaml_path):
                additional_yaml_path = os.path.join(yaml_dir, filename)
                try:
                    with open(additional_yaml_path, 'r', encoding='utf-8') as f:
                        additional_content = f.read()
                        all_content += additional_content + '\n'
                except Exception as e:
                    print(f'Warning: Could not load {filename}: {e}')

    # Then load the main YAML file
    try:
        with open(yaml_path, 'r', encoding='utf-8') as f:
            main_content = f.read()
            all_content += main_content
    except FileNotFoundError:
        print(f'Error: File not found! Path: {yaml_path}')
        print('Please check if the file exists and the path is correct')
        raise
    except Exception as e:
        print(f'Error: Cannot read file! Path: {yaml_path}')
        print(f'Error: {e}')
        raise

    # Now parse the combined content
    try:
        return yaml.safe_load(all_content)
    except yaml.YAMLError as e:
        print(f'Error: Invalid YAML syntax! Path: {yaml_path}')
        print(f'Please check the YAML syntax and indentation. Error: {e}')
        raise
    except Exception as e:
        print(f'Error: Unexpected error! Path: {yaml_path}')
        print(f'Error: {e}')
        raise
