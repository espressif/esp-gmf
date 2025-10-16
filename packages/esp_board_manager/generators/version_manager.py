# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
Version management for ESP Board Manager
"""

import os
import importlib.util
from pathlib import Path
from typing import Dict, List, Tuple, Optional, Any
from .utils.logger import LoggerMixin

# Constants for component prefixes
DEV_PREFIX = 'dev_'
PERIPH_PREFIX = 'periph_'
DEV_PREFIX_LEN = len(DEV_PREFIX)
PERIPH_PREFIX_LEN = len(PERIPH_PREFIX)


class VersionManager(LoggerMixin):
    """Manages different versions of devices and peripherals with scanning and comparison"""

    def __init__(self, script_dir: Path):
        super().__init__()
        self.script_dir = script_dir

        # Determine the root directory for all ESP Board Manager resources
        # Priority: 1. IDF_EXTRA_ACTIONS_PATH, 2. script_dir
        idf_extra_actions_path = os.environ.get('IDF_EXTRA_ACTIONS_PATH')
        if idf_extra_actions_path:
            self.root_dir = Path(idf_extra_actions_path)
        else:
            self.root_dir = script_dir

        # All paths are now relative to root_dir
        self.devices_dir = self.root_dir / 'devices'
        self.peripherals_dir = self.root_dir / 'peripherals'

        self.device_versions = {}
        self.peripheral_versions = {}

    def scan_versions(self) -> None:
        """Scan all available versions for devices and peripherals"""
        self._scan_device_versions()
        self._scan_peripheral_versions()

    def _scan_device_versions(self) -> None:
        """Scan device versions from device folders"""
        if not self.devices_dir.exists():
            return

        for device_folder in self.devices_dir.iterdir():
            if not device_folder.is_dir() or not device_folder.name.startswith(DEV_PREFIX):
                continue

            device_type = device_folder.name[DEV_PREFIX_LEN:]  # Remove 'dev_' prefix
            self.device_versions[device_type] = {}

            # Look for version-specific files
            for file_path in device_folder.iterdir():
                if file_path.is_file():
                    version = self._extract_version_from_file(file_path)
                    if version:
                        self.device_versions[device_type][version] = {
                            'parser_file': file_path,
                            'config_files': self._find_config_files(device_folder, version),
                            'dependencies': self._extract_dependencies(file_path)
                        }

            self.logger.info(f'Found {len(self.device_versions[device_type])} versions for device {device_type}')

    def _scan_peripheral_versions(self) -> None:
        """Scan peripheral versions from peripheral files and folders"""
        if not self.peripherals_dir.exists():
            return

        # Helper function to extract version info from a peripheral file
        def _extract_periph_version_info(periph_file: Path, periph_type: str) -> None:
            """Extract version information from a peripheral file and add to versions dict"""
            if periph_type not in self.peripheral_versions:
                self.peripheral_versions[periph_type] = {}

            version = self._extract_version_from_python_file(periph_file)
            if version:
                self.peripheral_versions[periph_type][version] = {
                    'parser_file': periph_file,
                    'dependencies': self._extract_dependencies(periph_file)
                }
            self.logger.info(f'Found {len(self.peripheral_versions[periph_type])} versions for peripheral {periph_type}')

        # First, scan subfolders (new structure: periph_xxx/periph_xxx.py)
        for periph_folder in self.peripherals_dir.iterdir():
            if not periph_folder.is_dir():
                continue

            # Check if folder name starts with 'periph_'
            if not periph_folder.name.startswith(PERIPH_PREFIX):
                continue

            periph_type = periph_folder.name[PERIPH_PREFIX_LEN:]  # Remove 'periph_' prefix

            # Look for Python parser file - folder name should match file name
            periph_file = periph_folder / f'{periph_folder.name}.py'
            if periph_file.exists():
                _extract_periph_version_info(periph_file, periph_type)

        # Then, scan direct files in peripherals directory (old structure: periph_xxx.py)
        for periph_file in self.peripherals_dir.glob(f'{PERIPH_PREFIX}*.py'):
            # Skip if it's inside a subfolder (already handled above)
            if periph_file.parent != self.peripherals_dir:
                continue

            periph_type = periph_file.stem[PERIPH_PREFIX_LEN:]  # Remove 'periph_' prefix
            _extract_periph_version_info(periph_file, periph_type)

    def _extract_version_from_file(self, file_path: Path) -> Optional[str]:
        """Extract version from file name or content"""
        # Try to extract from filename first
        filename = file_path.stem
        if '_v' in filename:
            version_part = filename.split('_v')[-1]
            if self._is_valid_version(version_part):
                return version_part

        # Try to extract from file content
        if file_path.suffix == '.py':
            return self._extract_version_from_python_file(file_path)
        elif file_path.suffix == '.yaml':
            return self._extract_version_from_yaml_file(file_path)

        return None

    def _extract_version_from_python_file(self, file_path: Path) -> Optional[str]:
        """Extract version from Python file"""
        try:
            spec = importlib.util.spec_from_file_location('module', str(file_path))
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)

            if hasattr(module, 'VERSION'):
                return module.VERSION

        except Exception as e:
            self.logger.warning(f'⚠️  Failed to extract version from {file_path}: {e}')

        return None

    def _extract_version_from_yaml_file(self, file_path: Path) -> Optional[str]:
        """Extract version from YAML file"""
        try:
            import yaml
            with open(file_path, 'r') as f:
                yaml_data = yaml.safe_load(f)
                if yaml_data and 'version' in yaml_data:
                    return yaml_data['version']

        except Exception as e:
            self.logger.warning(f'⚠️  Failed to extract version from {file_path}: {e}')

        return None

    def _is_valid_version(self, version: str) -> bool:
        """Check if version string is valid"""
        import re
        # Check for semantic versioning pattern (x.y.z)
        pattern = r'^\d+\.\d+(\.\d+)?$'
        return bool(re.match(pattern, version))

    def _find_config_files(self, folder: Path, version: str) -> List[Path]:
        """Find configuration files for specific version"""
        config_files = []

        # Look for version-specific config files
        for file_path in folder.glob(f'*{version}*.yaml'):
            config_files.append(file_path)

        # Look for default config files
        for file_path in folder.glob('*.yaml'):
            if version not in file_path.name and 'default' in file_path.name:
                config_files.append(file_path)

        return config_files

    def _extract_dependencies(self, file_path: Path) -> List[str]:
        """Extract dependencies from file"""
        dependencies = []

        try:
            if file_path.suffix == '.py':
                spec = importlib.util.spec_from_file_location('module', str(file_path))
                module = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(module)

                if hasattr(module, 'DEPENDENCIES'):
                    dependencies = module.DEPENDENCIES

        except Exception as e:
            self.logger.warning(f'⚠️  Failed to extract dependencies from {file_path}: {e}')

        return dependencies

    def get_available_versions(self, component_type: str, component_name: str) -> List[str]:
        """Get available versions for a component"""
        if component_type == 'device':
            return list(self.device_versions.get(component_name, {}).keys())
        elif component_type == 'peripheral':
            return list(self.peripheral_versions.get(component_name, {}).keys())
        return []

    def get_component_info(self, component_type: str, component_name: str, version: str) -> Optional[Dict[str, Any]]:
        """Get component information for specific version"""
        if component_type == 'device':
            return self.device_versions.get(component_name, {}).get(version)
        elif component_type == 'peripheral':
            return self.peripheral_versions.get(component_name, {}).get(version)
        return None

    def load_parser(self, component_type: str, component_name: str, version: str) -> Optional[Any]:
        """Load parser for specific component version"""
        component_info = self.get_component_info(component_type, component_name, version)
        if not component_info:
            return None

        parser_file = component_info['parser_file']

        try:
            spec = importlib.util.spec_from_file_location(f'{component_name}_{version}', str(parser_file))
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            return module

        except Exception as e:
            self.logger.error(f'Failed to load parser for {component_type} {component_name} v{version}: {e}')
            return None

    def validate_version_compatibility(self, component_type: str, component_name: str, version: str, target_chip: str) -> bool:
        """Validate if component version is compatible with target chip"""
        component_info = self.get_component_info(component_type, component_name, version)
        if not component_info:
            return False

        # Check dependencies
        dependencies = component_info.get('dependencies', [])
        for dep in dependencies:
            if not self._check_dependency_availability(dep, target_chip):
                self.logger.warning(f'⚠️  Dependency {dep} not available for chip {target_chip}')
                return False

        return True

    def _check_dependency_availability(self, dependency: str, target_chip: str) -> bool:
        """Check if dependency is available for target chip"""
        # This would need to be implemented based on ESP-IDF component availability
        # For now, return True as a placeholder
        return True

    def generate_version_summary(self) -> str:
        """Generate a summary of all available versions"""
        summary = []
        summary.append('ESP Board Manager Version Summary')
        summary.append('=' * 40)
        summary.append('')

        # Device versions
        summary.append('Device Versions:')
        for device_type, versions in self.device_versions.items():
            summary.append(f'  {device_type}:')
            for version in versions.keys():
                summary.append(f'    - {version}')
        summary.append('')

        # Peripheral versions
        summary.append('Peripheral Versions:')
        for periph_type, versions in self.peripheral_versions.items():
            summary.append(f'  {periph_type}:')
            for version in versions.keys():
                summary.append(f'    - {version}')

        return '\n'.join(summary)
