# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
SDKConfig management for ESP Board Manager
"""

import os
import re
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

from .utils.logger import LoggerMixin
from .utils.file_utils import find_project_root, safe_write_file
from .utils.yaml_utils import load_yaml_safe
import yaml


class SDKConfigManager(LoggerMixin):
    """Manages SDKConfig file operations and auto-enabling features"""

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
        self.gen_codes_dir = self.root_dir / 'gen_codes'
        self.devices_dir = self.root_dir / 'devices'
        self.peripherals_dir = self.root_dir / 'peripherals'

    def update_sdkconfig_from_board_types(self, device_types: Set[str], peripheral_types: Set[str],
                                         sdkconfig_path: Optional[str] = None,
                                         enable: bool = True, board_name: Optional[str] = None) -> Dict[str, List[str]]:
        """
        Update sdkconfig file based on board device and peripheral types.

        Args:
            device_types: Set of device types from board YAML
            peripheral_types: Set of peripheral types from board YAML
            sdkconfig_path: Path to sdkconfig file (auto-detect if None)
            enable: Whether to enable features (True) or just check (False)
            board_name: Optional board name to update board selection

        Returns:
            Dict with 'enabled' and 'checked' lists of config items
        """
        result = {'enabled': [], 'checked': []}

        # Auto-detect sdkconfig path if not provided
        if sdkconfig_path is None:
            sdkconfig_path = self._find_sdkconfig_path()

        if not sdkconfig_path or not os.path.exists(sdkconfig_path):
            self.logger.warning(f'sdkconfig file not found at {sdkconfig_path}')
            return result

        self.logger.debug(f'   Updating sdkconfig: {sdkconfig_path}')
        self.logger.debug(f'   Device types: {device_types}')
        self.logger.debug(f'   Peripheral types: {peripheral_types}')
        if board_name:
            self.logger.debug(f'   Board name: {board_name}')

        # Read current sdkconfig
        try:
            with open(sdkconfig_path, 'r', encoding='utf-8') as f:
                sdkconfig_content = f.read()
        except Exception as e:
            self.logger.error(f'Error reading sdkconfig: {e}')
            return result

        # Update board selection if board_name is provided
        if board_name and enable:
            sdkconfig_content, board_changes = self._apply_board_selection_updates(
                sdkconfig_content, board_name
            )
            result['enabled'].extend(board_changes)

        # Build mapping from YAML types to sdkconfig options, separately for devices and peripherals
        device_mapping, peripheral_mapping = self._build_type_to_config_mappings()
        self.logger.debug(f'Device mapping: {device_mapping}')
        self.logger.debug(f'Peripheral mapping: {peripheral_mapping}')

        # Filter types by validity rule
        valid_device_types = {t for t in device_types if self._is_valid_type(t)}
        invalid_device_types = set(device_types) - valid_device_types
        if invalid_device_types:
            self.logger.warning(f'Ignored invalid device types: {sorted(invalid_device_types)}')

        valid_peripheral_types = {t for t in peripheral_types if self._is_valid_type(t)}
        invalid_periph_types = set(peripheral_types) - valid_peripheral_types
        if invalid_periph_types:
            self.logger.warning(f'Ignored invalid peripheral types: {sorted(invalid_periph_types)}')

        # Update Peripheral Support section
        if enable:
            sdkconfig_content, periph_changes = self._apply_section_updates(
                sdkconfig_content,
                section_header='Peripheral Support',
                section_end='end of Peripheral Support',
                selected_types=valid_peripheral_types,
                type_to_config_mapping=peripheral_mapping,
            )
            result['enabled'].extend(periph_changes)
        else:
            # Check only
            _, periph_changes = self._apply_section_updates(
                sdkconfig_content,
                section_header='Peripheral Support',
                section_end='end of Peripheral Support',
                selected_types=valid_peripheral_types,
                type_to_config_mapping=peripheral_mapping,
                apply_changes=False,
            )
            result['checked'].extend(periph_changes)

        # Update Device Support section
        if enable:
            sdkconfig_content, dev_changes = self._apply_section_updates(
                sdkconfig_content,
                section_header='Device Support',
                section_end='end of Device Support',
                selected_types=valid_device_types,
                type_to_config_mapping=device_mapping,
            )
            result['enabled'].extend(dev_changes)
        else:
            # Check only
            _, dev_changes = self._apply_section_updates(
                sdkconfig_content,
                section_header='Device Support',
                section_end='end of Device Support',
                selected_types=valid_device_types,
                type_to_config_mapping=device_mapping,
                apply_changes=False,
            )
            result['checked'].extend(dev_changes)

        # Write updated content back to file if changes were made
        if enable and (result['enabled'] or board_name):
            try:
                with open(sdkconfig_path, 'w', encoding='utf-8') as f:
                    f.write(sdkconfig_content)
                self.logger.info(f"   Successfully updated sdkconfig with {len(result['enabled'])} changes")
            except Exception as e:
                self.logger.error(f'Error writing sdkconfig: {e}')
                return result

        return result

    def auto_enable_sdkconfig_features(self, dev_yaml_path: str, periph_yaml_path: str,
                                      sdkconfig_path: Optional[str] = None,
                                      enable: bool = True) -> Dict[str, List[str]]:
        """
        Automatically check and enable sdkconfig features based on YAML device and peripheral types.
        Uses the existing Kconfig structure from Kconfig.in file.

        Args:
            dev_yaml_path: Path to device YAML file
            periph_yaml_path: Path to peripheral YAML file
            sdkconfig_path: Path to sdkconfig file (auto-detect if None)
            enable: Whether to enable features (True) or just check (False)

        Returns:
            Dict with 'enabled' and 'checked' lists of config items
        """
        result = {'enabled': [], 'checked': []}

        # Auto-detect sdkconfig path if not provided
        if sdkconfig_path is None:
            sdkconfig_path = self._find_sdkconfig_path()

        if not sdkconfig_path or not os.path.exists(sdkconfig_path):
            self.logger.warning(f'sdkconfig file not found at {sdkconfig_path}')
            return result

        # Read YAML files
        try:
            dev_data = load_yaml_safe(Path(dev_yaml_path))
            periph_data = load_yaml_safe(Path(periph_yaml_path))

            if not dev_data or not periph_data:
                self.logger.error('Failed to load YAML files')
                return result
        except Exception as e:
            self.logger.error(f'Error reading YAML files: {e}')
            return result

        # Extract device and peripheral types
        device_types = set()
        peripheral_types = set()

        # Extract device types
        for device in dev_data.get('devices', []):
            device_type = device.get('type')
            if device_type:
                device_types.add(device_type)

        # Extract peripheral types
        for peripheral in periph_data.get('peripherals', []):
            peripheral_type = peripheral.get('type')
            if peripheral_type:
                peripheral_types.add(peripheral_type)

        # Use the new method to update sdkconfig
        return self.update_sdkconfig_from_board_types(
            device_types, peripheral_types, sdkconfig_path, enable
        )

    def auto_enable_from_board_dir(self, board_dir: str,
                                   sdkconfig_path: Optional[str] = None,
                                   enable: bool = True) -> Dict[str, List[str]]:
        """Convenience wrapper: pass a board directory containing board_devices.yaml and
        board_peripherals.yaml to update the sdkconfig accordingly.

        Args:
            board_dir: Path to a board directory (e.g. .../boards/esp_box)
            sdkconfig_path: Optional explicit sdkconfig path
            enable: Apply changes if True; check-only if False
        """
        board_path = Path(board_dir)
        dev_yaml = board_path / 'board_devices.yaml'
        periph_yaml = board_path / 'board_peripherals.yaml'
        if not dev_yaml.exists() or not periph_yaml.exists():
            self.logger.error(f'Board YAMLs not found under {board_dir}')
            return {'enabled': [], 'checked': []}
        return self.auto_enable_sdkconfig_features(str(dev_yaml), str(periph_yaml), sdkconfig_path, enable)



    def _apply_board_selection_updates(self, sdkconfig_content: str, board_name: str) -> Tuple[str, List[str]]:
        """
        Apply board selection updates to the sdkconfig content.

        Args:
            sdkconfig_content: Current sdkconfig content
            board_name: Name of the board to select

        Returns:
            Tuple of (updated_content, list_of_changes)
        """
        changes = []

        # Find Board Selection section
        section_start = '# Board Selection'
        section_end = '# end of Board Selection'

        start_idx = sdkconfig_content.find(section_start)
        if start_idx == -1:
            self.logger.warning('Board Selection section not found in sdkconfig')
            return sdkconfig_content, changes

        end_idx = sdkconfig_content.find(section_end, start_idx)
        if end_idx == -1:
            self.logger.warning('Board Selection section end marker not found in sdkconfig')
            return sdkconfig_content, changes

        # Extract the section content
        section_content = sdkconfig_content[start_idx:end_idx + len(section_end)]

        # Convert board name to config macro format
        board_config_macro = f'CONFIG_BOARD_{board_name.upper().replace("-", "_")}'

        # Find all board config options in the section
        board_configs = re.findall(r'CONFIG_BOARD_[A-Z0-9_]+', section_content)

        if not board_configs:
            self.logger.warning('No board config options found in Board Selection section')
            return sdkconfig_content, changes

        # Create updated section content
        updated_section = section_start + '\n'

        for config in board_configs:
            if config == board_config_macro:
                # Enable the selected board
                updated_section += f'{config}=y\n'
                changes.append(f'Enabled {config}')
            else:
                # Disable other boards
                updated_section += f'# {config} is not set\n'
                changes.append(f'Disabled {config}')

        updated_section += f"\nCONFIG_ESP_BOARD_NAME=\"{board_name}\"\n"
        updated_section += section_end

        # Replace the section in the original content
        updated_content = (
            sdkconfig_content[:start_idx] +
            updated_section +
            sdkconfig_content[end_idx + len(section_end):]
        )

        return updated_content, changes

    def _find_sdkconfig_path(self) -> Optional[str]:
        """Find sdkconfig file path"""
        # Prefer current working directory as the project directory
        current_dir = Path.cwd()
        cwd_sdkconfig = current_dir / 'sdkconfig'
        if cwd_sdkconfig.exists():
            return str(cwd_sdkconfig)

        # Use PROJECT_DIR if provided
        project_root = os.environ.get('PROJECT_DIR')
        if not project_root:
            # Start searching from current working directory, not script directory
            project_root = find_project_root(Path.cwd())

        if project_root:
            sdkconfig_path = os.path.join(project_root, 'sdkconfig')
            if os.path.exists(sdkconfig_path):
                return sdkconfig_path

        # Fallback: try parent directories of CWD
        for parent in [current_dir] + list(current_dir.parents):
            potential_sdkconfig = parent / 'sdkconfig'
            if potential_sdkconfig.exists():
                return str(potential_sdkconfig)

        return None

    def is_auto_config_disabled_in_sdkconfig(self, sdkconfig_path: Optional[str] = None) -> bool:
        """
        Check if auto-config is disabled via sdkconfig CONFIG_ESP_BOARD_MANAGER_AUTO_CONFIG_DEVICE_AND_PERIPHERAL

        Args:
            sdkconfig_path: Path to sdkconfig file (auto-detect if None)

        Returns:
            True if auto-config is disabled, False otherwise
        """
        # Auto-detect sdkconfig path if not provided
        if sdkconfig_path is None:
            sdkconfig_path = self._find_sdkconfig_path()

        if not sdkconfig_path or not os.path.exists(sdkconfig_path):
            self.logger.warning(f'sdkconfig file not found at {sdkconfig_path}')
            return False

        try:
            with open(sdkconfig_path, 'r', encoding='utf-8') as f:
                sdkconfig_content = f.read()
            # Check if CONFIG_ESP_BOARD_MANAGER_AUTO_CONFIG_DEVICE_AND_PERIPHERAL is disabled
            if 'CONFIG_ESP_BOARD_MANAGER_AUTO_CONFIG_DEVICE_AND_PERIPHERAL=n' in sdkconfig_content:
                self.logger.info('Auto-config disabled via sdkconfig: CONFIG_ESP_BOARD_MANAGER_AUTO_CONFIG_DEVICE_AND_PERIPHERAL=n')
                return True
            else:
                self.logger.info('   Auto-config enabled via sdkconfig (or not set)')
                return False
        except Exception as e:
            self.logger.warning(f'Could not read sdkconfig to check auto-config setting: {e}')
            return False

    def _build_type_to_config_mappings(self) -> Tuple[Dict[str, List[str]], Dict[str, List[str]]]:
        """Build mappings from YAML types to sdkconfig options.

        Priority:
        1) Parse gen_codes/Kconfig.in (authoritative)
        2) Fallback to devices/peripherals CMakeLists.txt (legacy)

        Returns:
            (device_mapping, peripheral_mapping)
        """
        device_mapping: Dict[str, List[str]] = {}
        peripheral_mapping: Dict[str, List[str]] = {}

        # 1) Prefer parsing the generated Kconfig.in
        kconfig_in_path = self.gen_codes_dir / 'Kconfig.in'
        if kconfig_in_path.exists():
            try:
                with open(kconfig_in_path, 'r', encoding='utf-8') as f:
                    kconfig_text = f.read()
                # Device entries: config ESP_BOARD_DEV_<TYPE>_SUPPORT
                for m in re.findall(r'^config\s+(ESP_BOARD_DEV_([A-Z0-9_]+)_SUPPORT)\b', kconfig_text, flags=re.M):
                    full, type_upper = m
                    type_lower = type_upper.lower()
                    device_mapping[type_lower] = [f'CONFIG_{full}']
                # Peripheral entries: config ESP_BOARD_PERIPH_<TYPE>_SUPPORT
                for m in re.findall(r'^config\s+(ESP_BOARD_PERIPH_([A-Z0-9_]+)_SUPPORT)\b', kconfig_text, flags=re.M):
                    full, type_upper = m
                    type_lower = type_upper.lower()
                    peripheral_mapping[type_lower] = [f'CONFIG_{full}']
                # If we found any via Kconfig.in, return early
                if device_mapping or peripheral_mapping:
                    return device_mapping, peripheral_mapping
            except Exception as e:
                self.logger.warning(f'Failed parsing Kconfig.in: {e}')

        # 2) Fallback: parse devices/peripherals CMakeLists.txt (legacy names without DEV_/PERIPH_ prefix)
        # Parse devices CMakeLists.txt
        devices_cmake_path = self.devices_dir / 'CMakeLists.txt'

        if devices_cmake_path.exists():
            with open(devices_cmake_path, 'r', encoding='utf-8') as f:
                cmake_content = f.read()

            # Find all CONFIG_ESP_BOARD_*_SUPPORT patterns
            for match in re.findall(r'CONFIG_ESP_BOARD_([A-Z][A-Z0-9_]*)_SUPPORT', cmake_content):
                # Legacy: derive type name directly
                type_name = match.lower()
                config_option = f'CONFIG_ESP_BOARD_{match}_SUPPORT'
                device_mapping[type_name] = [config_option]

        # Parse peripherals CMakeLists.txt
        peripherals_cmake_path = self.peripherals_dir / 'CMakeLists.txt'

        if peripherals_cmake_path.exists():
            with open(peripherals_cmake_path, 'r', encoding='utf-8') as f:
                cmake_content = f.read()

            # Find all CONFIG_ESP_BOARD_*_SUPPORT patterns
            for match in re.findall(r'CONFIG_ESP_BOARD_([A-Z][A-Z0-9_]*)_SUPPORT', cmake_content):
                # Legacy: derive type name directly
                type_name = match.lower()
                config_option = f'CONFIG_ESP_BOARD_{match}_SUPPORT'
                peripheral_mapping[type_name] = [config_option]

        return device_mapping, peripheral_mapping

    def _is_valid_type(self, type_name: str) -> bool:
        """Validate type naming: lowercase letters, digits, underscores; cannot be only digits."""
        if not isinstance(type_name, str):
            return False
        if not re.fullmatch(r'[a-z0-9_]+', type_name):
            return False
        if re.fullmatch(r'[0-9]+', type_name):
            return False
        return True

    def _apply_section_updates(
        self,
        sdkconfig_content: str,
        *,
        section_header: str,
        section_end: str,
        selected_types: Set[str],
        type_to_config_mapping: Dict[str, List[str]],
        apply_changes: bool = True,
    ) -> Tuple[str, List[str]]:
        """Update a section in sdkconfig based on selected types and mapping.

        Args:
            sdkconfig_content: Full sdkconfig text
            section_header: The text inside the header line, e.g. 'Peripheral Support'
            section_end: The text inside the end line, e.g. 'end of Peripheral Support'
            selected_types: Set of types from YAML
            type_to_config_mapping: Mapping from type to list of CONFIG_ options
            apply_changes: If False, only compute what would change

        Returns:
            A tuple of (possibly updated content, list of change descriptions)
        """
        # Find section start and end line indices
        lines = sdkconfig_content.splitlines()
        header_regex = re.compile(rf'^#\s*{re.escape(section_header)}\s*$', re.M)
        end_regex = re.compile(rf'^#\s*{re.escape(section_end)}\s*$', re.M)

        start_idx = None
        end_idx = None
        for idx, line in enumerate(lines):
            if start_idx is None and header_regex.match(line):
                start_idx = idx
            if start_idx is not None and end_regex.match(line):
                end_idx = idx
                break

        if start_idx is None or end_idx is None or end_idx <= start_idx:
            self.logger.warning(f"Section '{section_header}' not found in sdkconfig; skipping")
            return sdkconfig_content, []

        # The actual configurable lines are between (start_idx+2) and (end_idx-1) typically,
        # but we will operate on the full exclusive range (start_idx+1, end_idx)
        content_start = start_idx + 1
        content_end = end_idx  # exclusive

        # Build desired config map
        all_configs: List[str] = []
        for cfg_list in type_to_config_mapping.values():
            all_configs.extend(cfg_list)
        all_configs = sorted(set(all_configs))

        selected_configs: Set[str] = set()
        for t in selected_types:
            for cfg in type_to_config_mapping.get(t, []):
                selected_configs.add(cfg)

        # Scan existing lines in section and compute replacements
        changes: List[str] = []
        present_configs: Set[str] = set()
        config_line_regex = re.compile(r'^(#\s+)?(CONFIG_ESP_BOARD_[A-Z0-9_]+_SUPPORT)\s*(=\s*[yn])?(\s+is not set)?\s*$')

        idx = content_start
        while idx < content_end:
            line = lines[idx]
            m = config_line_regex.match(line)
            if not m:
                idx += 1
                continue
            config_name = m.group(2)
            if config_name not in all_configs:
                # Not managed by current mapping (legacy symbol) -> remove it
                if apply_changes:
                    del lines[idx]
                    content_end -= 1
                    changes.append(f'{config_name} (legacy) -> removed')
                    continue  # do not advance idx
                else:
                    changes.append(f'{config_name} (legacy) present')
                    idx += 1
                    continue
            present_configs.add(config_name)
            should_enable = config_name in selected_configs
            desired_line = f'{config_name}=y' if should_enable else f'# {config_name} is not set'
            if line.strip() != desired_line:
                if apply_changes:
                    lines[idx] = desired_line
                changes.append(f"{config_name} -> {'y' if should_enable else 'not set'}")
            idx += 1

        # Insert missing configs before end marker
        missing_configs = [c for c in all_configs if c not in present_configs]
        if missing_configs:
            insert_pos = content_end  # before end line
            new_lines: List[str] = []
            for cfg in missing_configs:
                should_enable = cfg in selected_configs
                desired_line = f'{cfg}=y' if should_enable else f'# {cfg} is not set'
                new_lines.append(desired_line)
                changes.append(f"{cfg} -> {'y' if should_enable else 'not set'} (added)")
            if apply_changes and new_lines:
                # Insert with a preceding blank line if not already
                if insert_pos > 0 and lines[insert_pos - 1].strip() != '':
                    new_lines = [''] + new_lines
                lines[insert_pos:insert_pos] = new_lines

        updated_content = '\n'.join(lines) if apply_changes else sdkconfig_content
        return updated_content, changes



