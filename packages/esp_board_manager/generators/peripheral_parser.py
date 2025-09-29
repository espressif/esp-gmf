# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
Peripheral parser for ESP Board Manager
Parses peripheral configurations from YAML files
"""

from .utils.logger import LoggerMixin
from .utils.yaml_utils import load_yaml_safe
from .settings import BoardManagerConfig
from .name_validator import validate_component_name
from pathlib import Path
from typing import Dict, List, Any, Optional, Tuple
import os
import re


class PeripheralParser(LoggerMixin):
    """Parser for peripheral configurations with validation and nested list flattening"""

    def __init__(self, script_dir: Path):
        super().__init__()
        self.script_dir = script_dir

    def parse_peripherals_yaml(self, yaml_path: str) -> Tuple[Dict[str, Any], Dict[str, str], List[str]]:
        """
        Parse peripherals from YAML file and return peripherals dict, name map, and types list
        """
        # Check if file exists
        if not Path(yaml_path).exists():
            self.logger.error(f'File not found! Path: {yaml_path}')
            self.logger.info('Please check if the file exists and the path is correct')
            return {}, {}, []

        # Try to parse YAML
        try:
            data = load_yaml_safe(Path(yaml_path))
        except Exception as e:
            self.logger.error(f'Invalid YAML syntax! Path: {yaml_path}')
            self.logger.info(f'Please check the YAML syntax and indentation. Error: {e}')
            return {}, {}, []
        except Exception as e:
            self.logger.error(f'Unexpected error! Path: {yaml_path}')
            self.logger.info(f'Error: {e}')
            return {}, {}, []

        # Validate data structure
        if not data:
            self.logger.error(f'Empty or invalid YAML file! Path: {yaml_path}')
            self.logger.info('The file appears to be empty or contains no valid YAML content')
            return {}, {}, []

        if 'peripherals' not in data:
            self.logger.error(f'No peripherals found! Path: {yaml_path}')
            self.logger.info("The file should contain a 'peripherals' section with at least one peripheral")
            return {}, {}, []

        peripherals = data['peripherals']
        if not peripherals:
            self.logger.error(f'No peripherals found! Path: {yaml_path}')
            self.logger.info("The file should contain a 'peripherals' section with at least one peripheral")
            return {}, {}, []

        # Process peripherals
        out = {}
        periph_name_map = {}
        peripheral_types = []

        for i, obj in enumerate(peripherals):
            try:
                # Validate required fields
                if 'name' not in obj or 'type' not in obj:
                    self.logger.error(f'Missing required fields! Path: {yaml_path}')
                    self.logger.info(f'Peripheral #{i+1}: {obj}. Missing required fields: name and type')
                    continue

                name = obj['name']
                periph_type = obj['type']

                # Validate peripheral name using unified rules
                if not validate_component_name(name):
                    self.logger.error(f'Invalid peripheral name! Path: {yaml_path}')
                    self.logger.info(f"Peripheral #{i+1}: {obj}. Invalid name '{name}'. Peripheral names must be lowercase, start with a letter, and contain only letters, numbers, and underscores")
                    continue

                # Store peripheral data
                out[name] = obj
                periph_name_map[name] = name  # No mapping needed for device-style names
                peripheral_types.append(periph_type)

            except Exception as e:
                self.logger.error(f'Invalid peripheral configuration! Path: {yaml_path}')
                self.logger.info(f'Peripheral #{i+1}: {obj}. Error: {e}')
                continue

        self.logger.info(f'   Successfully parsed {len(out)} peripherals from {yaml_path}')
        return out, periph_name_map, peripheral_types

    def flatten_peripherals(self, periph):
        """Recursively expand anchor-referenced lists"""
        if isinstance(periph, list):
            result = []
            for p in periph:
                result.extend(self.flatten_peripherals(p))
            return result
        elif periph is None:
            return []
        else:
            return [periph]

    def parse_peripherals_yaml_legacy(self, yaml_path: str) -> List[Any]:
        """
        Legacy method that returns a list of peripheral objects for backward compatibility
        """
        peripherals_dict, periph_name_map, peripheral_types = self.parse_peripherals_yaml(yaml_path)

        # Convert to legacy format
        from dataclasses import dataclass

        @dataclass
        class PeripheralConfig:
            def __init__(self, name: str, type_: str, format_: str, config: Dict[str, Any], version: Optional[str] = None, role_: Optional[str] = None):
                self.name = name
                self.type = type_
                self.format = format_
                self.config = config
                self.version = version
                # Use role from YAML if provided, otherwise derive from format
                if role_ is not None:
                    self.role = role_
                elif type_ == 'i2s' or type_ == 'i2c' or type_ == 'spi':
                    self.role = 'master'
                else:
                    self.role = 'none'

        result = []
        for name, obj in peripherals_dict.items():
            format_ = obj.get('format')
            role_ = obj.get('role')
            config = obj.get('config', {})
            version = obj.get('version')

            result.append(PeripheralConfig(
                name=name,
                type_=obj['type'],
                format_=format_,
                config=config,
                version=version,
                role_=role_
            ))

        return result
