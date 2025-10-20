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
    """Manages SDKConfig file operations, auto-enabling features, and board-specific configurations"""

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
                                         enable: bool = True, board_name: Optional[str] = None,
                                         chip_name: str = None) -> Dict[str, List[str]]:
        """
        Update sdkconfig file based on board device and peripheral types.

        Args:
            device_types: Set of device types from board YAML
            peripheral_types: Set of peripheral types from board YAML
            sdkconfig_path: Path to sdkconfig file (auto-detect if None)
            enable: Whether to enable features (True) or just check (False)
            board_name: Optional board name to update board selection
            chip_name: Chip name to set CONFIG_IDF_TARGET

        Returns:
            Dict with 'enabled' and 'checked' lists of config items
        """
        result = {'enabled': [], 'checked': []}

        # Auto-detect sdkconfig path if not provided
        if sdkconfig_path is None:
            sdkconfig_path = self._find_sdkconfig_path()

        if not sdkconfig_path:
            self.logger.warning(f'    sdkconfig path not found')
            return result

        # Check if sdkconfig file exists, create if not
        if not os.path.exists(sdkconfig_path):
            self.logger.info(f'   sdkconfig file not found at {sdkconfig_path}, creating new one')
            # Create directory if it doesn't exist
            os.makedirs(os.path.dirname(sdkconfig_path), exist_ok=True)
            # Create empty sdkconfig file
            sdkconfig_content = ''
        else:
            self.logger.debug(f'   Updating existing sdkconfig: {sdkconfig_path}')
            # Read current sdkconfig
            try:
                with open(sdkconfig_path, 'r', encoding='utf-8') as f:
                    sdkconfig_content = f.read()
            except Exception as e:
                self.logger.error(f'Error reading sdkconfig: {e}')
                return result

        self.logger.debug(f'   Device types: {device_types}')
        self.logger.debug(f'   Peripheral types: {peripheral_types}')
        if board_name:
            self.logger.debug(f'   Board name: {board_name}')
        if chip_name:
            self.logger.debug(f'   Chip name: {chip_name}')

        # Update board selection if board_name is provided
        if board_name and enable:
            sdkconfig_content, board_changes = self._apply_board_selection_updates(
                sdkconfig_content, board_name
            )
            result['enabled'].extend(board_changes)

            # Update CONFIG_IDF_TARGET with chip_name
            if chip_name:
                sdkconfig_content, target_changes = self._apply_target_updates(
                    sdkconfig_content, chip_name
                )
                result['enabled'].extend(target_changes)

        # Build mapping from YAML types to sdkconfig options, separately for devices and peripherals
        device_mapping, peripheral_mapping = self._build_type_to_config_mappings()
        self.logger.debug(f'Device mapping: {device_mapping}')
        self.logger.debug(f'Peripheral mapping: {peripheral_mapping}')

        # Filter types by validity rule
        valid_device_types = {t for t in device_types if self._is_valid_type(t)}
        invalid_device_types = set(device_types) - valid_device_types
        if invalid_device_types:
            self.logger.warning(f'⚠️  Ignored invalid device types: {sorted(invalid_device_types)}')

        valid_peripheral_types = {t for t in peripheral_types if self._is_valid_type(t)}
        invalid_periph_types = set(peripheral_types) - valid_peripheral_types
        if invalid_periph_types:
            self.logger.warning(f'⚠️  Ignored invalid peripheral types: {sorted(invalid_periph_types)}')

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

        # Write updated content back to file if changes were made or if we need to create/update CONFIG_IDF_TARGET
        if enable and (result['enabled'] or board_name or chip_name):
            try:
                with open(sdkconfig_path, 'w', encoding='utf-8') as f:
                    f.write(sdkconfig_content)
                if result['enabled']:
                    self.logger.info(f"   Successfully updated sdkconfig with {len(result['enabled'])} changes")
                else:
                    self.logger.info(f'   Successfully created/updated sdkconfig file')
            except Exception as e:
                self.logger.error(f'Error writing sdkconfig: {e}')
                return result

        return result

    def _apply_board_selection_updates(self, sdkconfig_content: str, board_name: str) -> Tuple[str, List[str]]:
        """
        Apply board selection updates to the sdkconfig content.
        First sets all existing CONFIG_BOARD_*** to n, then adds the new CONFIG_BOARD_***.

        Args:
            sdkconfig_content: Current sdkconfig content
            board_name: Name of the board to select

        Returns:
            Tuple of (updated_content, list_of_changes)
        """
        changes = []

        # Convert board name to config macro format
        board_config_macro = f'CONFIG_BOARD_{board_name.upper().replace("-", "_")}'

        # First, find and disable all existing CONFIG_BOARD_*** options in the entire sdkconfig
        # This handles cases where board configs might be outside the Board Selection section
        # Note: Don't modify CONFIG_BOARD_NAME as it's not a board selection config
        board_config_pattern = r'^CONFIG_BOARD_[A-Z0-9_]+=y$'
        lines = sdkconfig_content.split('\n')
        updated_lines = []

        for line in lines:
            if re.match(board_config_pattern, line.strip()) and not line.strip().startswith('CONFIG_BOARD_NAME='):
                # Set existing board configs to n (but not CONFIG_BOARD_NAME)
                config_name = line.split('=')[0]
                updated_lines.append(f'{config_name}=n')
                changes.append(f'Set {config_name}=n')
            else:
                updated_lines.append(line)

        # Update the content with disabled board configs
        sdkconfig_content = '\n'.join(updated_lines)

        # Find Board Selection section - it's between Board Manager Setting and Peripheral Support
        board_manager_end = '# end of Board Manager Setting'
        peripheral_support_start = '# Peripheral Support'

        # Find the end of Board Manager Setting section
        board_manager_end_idx = sdkconfig_content.find(board_manager_end)
        if board_manager_end_idx == -1:
            self.logger.info('   Board Manager Setting section not found in sdkconfig')
            # If no Board Manager Setting section, add the new board config at the end
            sdkconfig_content += f'\n# default:\n'
            sdkconfig_content += f'{board_config_macro}=y\n'
            sdkconfig_content += f'# default:\n'
            sdkconfig_content += f'CONFIG_BOARD_NAME="{board_name}"\n'
            changes.append(f'Added {board_config_macro}=y')
            return sdkconfig_content, changes

        # Find the start of Peripheral Support section
        peripheral_start_idx = sdkconfig_content.find(peripheral_support_start, board_manager_end_idx)
        if peripheral_start_idx == -1:
            self.logger.warning('⚠️  Peripheral Support section not found in sdkconfig')
            # If no Peripheral Support section, add the new board config after Board Manager Setting
            insertion_point = board_manager_end_idx + len(board_manager_end)
            sdkconfig_content = (sdkconfig_content[:insertion_point] +
                               f'\n# default:\n' +
                               f'{board_config_macro}=y\n' +
                               f'# default:\n' +
                               f'CONFIG_ESP_BOARD_NAME="{board_name}"\n' +
                               sdkconfig_content[insertion_point:])
            changes.append(f'Added {board_config_macro}=y')
            return sdkconfig_content, changes

        # Extract the Board Selection section content (between Board Manager Setting and Peripheral Support)
        section_start_idx = board_manager_end_idx + len(board_manager_end)
        section_content = sdkconfig_content[section_start_idx:peripheral_start_idx]

        # Create updated Board Selection section content - only keep the new board selection
        updated_section = f'\n\n# default:\n'
        updated_section += f'{board_config_macro}=y\n'
        updated_section += f'# default:\n'
        updated_section += f'CONFIG_ESP_BOARD_NAME="{board_name}"\n\n'
        changes.append(f'Added {board_config_macro}=y')

        # Replace the Board Selection section in the original content
        updated_content = (
            sdkconfig_content[:section_start_idx] +
            updated_section +
            sdkconfig_content[peripheral_start_idx:]
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
            self.logger.warning(f'   sdkconfig file not found at {sdkconfig_path}')
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
            self.logger.warning(f'⚠️  Could not read sdkconfig to check auto-config setting: {e}')
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
                self.logger.warning(f'⚠️  Failed parsing Kconfig.in: {e}')

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
            self.logger.info(f"   Section '{section_header}' not found in sdkconfig; skipping")
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

    def _apply_target_updates(self, sdkconfig_content: str, chip_name: str) -> Tuple[str, List[str]]:
        """
        Apply CONFIG_IDF_TARGET updates to the sdkconfig content.

        Args:
            sdkconfig_content: Current sdkconfig content
            chip_name: Chip name to set for CONFIG_IDF_TARGET

        Returns:
            Tuple of (updated_content, list_of_changes)
        """
        changes = []

        # Check if CONFIG_IDF_TARGET already exists
        target_pattern = r'^CONFIG_IDF_TARGET="[^"]*"'
        target_match = re.search(target_pattern, sdkconfig_content, re.MULTILINE)

        if target_match:
            # Replace existing CONFIG_IDF_TARGET
            old_target = target_match.group(0)
            new_target = f'CONFIG_IDF_TARGET="{chip_name}"'
            if old_target != new_target:
                sdkconfig_content = sdkconfig_content.replace(old_target, new_target)
                changes.append(f'Updated CONFIG_IDF_TARGET from {old_target} to {new_target}')
            else:
                self.logger.debug(f'CONFIG_IDF_TARGET already set to {chip_name}')
        else:
            # Add CONFIG_IDF_TARGET at the beginning of the file
            sdkconfig_content = f'CONFIG_IDF_TARGET="{chip_name}"\n' + sdkconfig_content
            changes.append(f'Added CONFIG_IDF_TARGET="{chip_name}"')

        return sdkconfig_content, changes

