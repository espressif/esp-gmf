# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
Dependency management for ESP Board Manager
"""

import os
import yaml
from pathlib import Path
from typing import Dict, List, Set, Optional, Any

from .utils.logger import LoggerMixin
from .utils.yaml_utils import load_yaml_safe, save_yaml_safe


class DependencyManager(LoggerMixin):
    """Manages component dependencies and idf_component.yml updates"""

    def __init__(self, script_dir: Path):
        super().__init__()
        self.script_dir = script_dir

        # Determine the root directory for all ESP Board Manager resources
        # Priority: 1. IDF_EXTRA_ACTIONS_PATH, 2. script_dir
        import os
        idf_extra_actions_path = os.environ.get('IDF_EXTRA_ACTIONS_PATH')
        if idf_extra_actions_path:
            self.root_dir = Path(idf_extra_actions_path)
        else:
            self.root_dir = script_dir

    def extract_device_dependencies(self, dev_yaml_path: str) -> Dict[str, str]:
        """
        Extract dependencies from board_devices.yaml file.

        Args:
            dev_yaml_path: Path to board_devices.yaml file

        Returns:
            dict: Dictionary of component dependencies with versions
        """
        dependencies = {}
        try:
            data = load_yaml_safe(Path(dev_yaml_path))

            if not data or 'devices' not in data:
                self.logger.warning(f'No devices found in {dev_yaml_path}')
                return dependencies

            for device in data['devices']:
                # Only process explicit dependencies
                if 'dependencies' in device:
                    device_deps = device['dependencies']
                    if isinstance(device_deps, list):
                        for dep in device_deps:
                            if isinstance(dep, dict):
                                # Handle dictionary format: {"component": "version"}
                                for component, version in dep.items():
                                    dependencies[component] = version
                            elif isinstance(dep, str):
                                # Handle string format: "component:version"
                                if ':' in dep:
                                    component, version = dep.split(':', 1)
                                    dependencies[component.strip()] = version.strip()
                                else:
                                    # Default version if not specified
                                    dependencies[dep.strip()] = '*'
                    elif isinstance(device_deps, dict):
                        # Handle direct dictionary format
                        for component, version in device_deps.items():
                            dependencies[component] = version

            if dependencies:
                self.logger.debug(f'Extracted {len(dependencies)} dependencies from {dev_yaml_path}:')
                for component, version in dependencies.items():
                    self.logger.debug(f'  - {component}: {version}')
            else:
                self.logger.debug(f'No dependencies found in {dev_yaml_path}')

        except Exception as e:
            self.logger.error(f'Error extracting dependencies from {dev_yaml_path}: {e}')

        return dependencies

    def update_idf_component_dependencies(self, new_dependencies: Dict[str, str]) -> bool:
        """
        Update idf_component.yml with new dependencies.

        New Rules:
        1. idf_component.yml dependencies start empty each time to avoid accumulation when switching boards
        2. After checking device and peripheral yaml dependencies, compare with idf_component.yml dependencies,
           if equal, do not modify idf_component.yml, if not equal, update idf_component.yml.

        Args:
            new_dependencies: Dictionary of component dependencies with versions

        Returns:
            bool: True if update was successful
        """
        idf_component_file = self.root_dir / 'idf_component.yml'

        # Check if file exists
        if not idf_component_file.exists():
            self.logger.error(f'Error: {idf_component_file} not found')
            return False

        try:
            # Read current content
            current_data = load_yaml_safe(idf_component_file)
            if not current_data:
                self.logger.error(f'Failed to load {idf_component_file}')
                return False

            # Get current dependencies (empty dict if not present)
            current_dependencies = current_data.get('dependencies', {})

            # Create new data structure with empty dependencies
            new_data = current_data.copy()
            new_data['dependencies'] = {}

            # Add new dependencies to the empty dependencies dict
            for component, version in new_dependencies.items():
                new_data['dependencies'][component] = version
                self.logger.debug(f'Added dependency: {component}: {version}')

            # Rule 2: Compare dependencies only (not entire file content)
            if current_dependencies == new_data['dependencies']:
                self.logger.info('✅ Dependencies identical - no modification needed')
                return True

            # Dependencies are different, update the file
            self.logger.info(f'   Dependencies changed, updating {idf_component_file}')
            self.logger.debug(f'   Old dependencies: {current_dependencies}')
            self.logger.debug(f"   New dependencies: {new_data['dependencies']}")

            # Write updated file
            success = save_yaml_safe(new_data, idf_component_file)
            if success:
                self.logger.info(f"✅ Updated {idf_component_file} with {len(new_data['dependencies'])} dependencies")
                return True
            else:
                self.logger.error(f'Failed to write {idf_component_file}')
                return False

        except Exception as e:
            self.logger.error(f'Error updating {idf_component_file}: {e}')
            import traceback
            traceback.print_exc()
            return False

    def scan_extra_dev_files(self, board_path: str) -> tuple:
        """
        Scan board directory for extra_dev files and return their configurations.

        Args:
            board_path: Path to the board directory

        Returns:
            tuple: (extra_configs_dict, extra_includes_list)
        """
        extra_configs = {}
        extra_includes = set()

        if not os.path.exists(board_path):
            return extra_configs, extra_includes

        self.logger.info(f'   Scanning for extra_dev files in: {board_path}')

        for filename in os.listdir(board_path):
            if filename.startswith('extra_dev_') and filename.endswith('.py'):
                config_name = filename[10:-3]  # Remove "extra_dev_" prefix and ".py" suffix
                yaml_filename = f'extra_dev_{config_name}.yaml'
                yaml_path = os.path.join(board_path, yaml_filename)

                if not os.path.exists(yaml_path):
                    self.logger.warning(f'Found {filename} but no corresponding {yaml_filename}')
                    continue

                self.logger.info(f'Found extra_dev configuration: {config_name}')

                try:
                    # Import the Python module dynamically
                    import importlib.util
                    script_path = os.path.join(board_path, filename)
                    spec = importlib.util.spec_from_file_location(config_name, script_path)
                    module = importlib.util.module_from_spec(spec)
                    spec.loader.exec_module(module)

                    # Look for a function that returns the configuration
                    # Common patterns: get_<config_name>_config(), get_<config_name>_config_<suffix>(), or get_config()
                    config_func = None
                    possible_func_names = [
                        f'get_{config_name}_config',
                        f"get_{config_name}_config_{config_name.split('_')[-1]}",
                        f"get_{config_name.replace('_st77916', '')}_config_st77916",  # Handle special case for st77916
                        'get_config'
                    ]

                    for func_name in possible_func_names:
                        if hasattr(module, func_name):
                            config_func = getattr(module, func_name)
                            break

                    if config_func is None:
                        self.logger.warning(f'No configuration function found in {filename}')
                        continue

                    # Call the function to get the configuration
                    config_data = config_func()
                    if config_data:
                        extra_configs[config_name] = config_data
                        self.logger.info(f'Successfully loaded configuration for {config_name}')
                    else:
                        self.logger.warning(f'Configuration function returned empty data for {config_name}')

                    # Look for get_includes function and collect headers
                    if hasattr(module, 'get_includes'):
                        includes = module.get_includes()
                        if includes:
                            extra_includes.update(includes)
                            self.logger.info(f'Collected includes: {includes}')

                except Exception as e:
                    self.logger.error(f'Error loading {filename}: {e}')
                    continue

        return extra_configs, extra_includes

    def validate_extra_dev_usage(self, extra_configs: Dict[str, Any], devices: List) -> bool:
        """
        Validate that extra_dev configurations are actually used in device configurations.

        Args:
            extra_configs: Dictionary of extra device configurations
            devices: List of device objects

        Returns:
            bool: True if validation passes, False otherwise
        """
        if not extra_configs:
            return True

        self.logger.info('Validating extra_dev configuration usage...')

        def check_nested_dict(d, config_name, device_name, path=''):
            """Recursively check nested dictionary for config_name references"""
            if isinstance(d, dict):
                for key, value in d.items():
                    current_path = f'{path}.{key}' if path else key
                    if isinstance(value, str):
                        # Check for direct string references
                        if config_name in value:
                            return True, f'{device_name}.{current_path}'
                        # Check for string references with "&" prefix
                        if f'&{config_name}' in value:
                            return True, f'{device_name}.{current_path}'
                    elif isinstance(value, dict):
                        # Recursively check nested dictionaries
                        found, found_path = check_nested_dict(value, config_name, device_name, current_path)
                        if found:
                            return True, found_path
            return False, ''

        # Check if any extra_dev config names are referenced in device configs
        unused_configs = []
        used_configs = []

        for config_name in extra_configs.keys():
            # Check if config_name appears in any device configuration
            found = False
            for device in devices:
                if hasattr(device, 'config') and isinstance(device.config, dict):
                    found, path = check_nested_dict(device.config, config_name, device.name)
                    if found:
                        used_configs.append(config_name)
                        self.logger.info(f'✅ {config_name} is referenced in {path}')
                        break

            if not found:
                unused_configs.append(config_name)
                self.logger.warning(f'⚠ {config_name} is not referenced in any device configuration')

        if unused_configs:
            self.logger.warning(f'Warning: {len(unused_configs)} extra_dev configurations are not used: {unused_configs}')
            return False

        self.logger.info(f'✅ All {len(used_configs)} extra_dev configurations are properly referenced')
        return True

