# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
Peripheral parser for ESP Board Manager
Parses peripheral configurations from YAML files
"""

from .utils.logger import LoggerMixin
from .utils.yaml_utils import load_board_yaml_file, BoardConfigYamlError
from .utils.board_schema_version import warn_if_invalid_board_yaml_schema_version
from .settings import BoardManagerConfig
from .name_validator import validate_component_name
from pathlib import Path
from typing import Dict, List, Any, Optional, Tuple
import os
import re


class PeripheralParser(LoggerMixin):
    """Parser for peripheral configurations with validation and nested list flattening"""

    def __init__(self, root_dir: Path):
        super().__init__()
        self.root_dir = root_dir

    def parse_peripherals_yaml(self, yaml_path: str) -> Tuple[Dict[str, Any], Dict[str, str], List[str]]:
        """
        Parse peripherals from YAML file and return peripherals dict, name map, and types list.

        An empty file, missing ``peripherals`` key, or ``peripherals: []`` is valid and yields no peripherals.
        Undefined peripheral references for devices are validated in each device parser, not here.
        """
        if not Path(yaml_path).exists():
            self.logger.error(f'File not found! Path: {yaml_path}')
            self.logger.info('Please check if the file exists and the path is correct')
            return {}, {}, []

        data = load_board_yaml_file(Path(yaml_path))

        peripherals = data.get('peripherals', [])
        if not isinstance(peripherals, list):
            raise BoardConfigYamlError(
                yaml_path,
                BoardConfigYamlError.REASON_NOT_A_MAPPING,
                f'peripherals must be a YAML list, got {type(peripherals).__name__}',
            )

        if isinstance(data, dict) and data.get('version') is not None:
            warn_if_invalid_board_yaml_schema_version(
                self.logger, data.get('version'), f'{yaml_path} (file root)'
            )

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

                if 'version' in obj:
                    warn_if_invalid_board_yaml_schema_version(
                        self.logger,
                        obj.get('version'),
                        f'{yaml_path} peripheral #{i + 1} ({name})',
                    )

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

        self.logger.debug(f'   Successfully parsed {len(out)} peripherals from {yaml_path}')
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
