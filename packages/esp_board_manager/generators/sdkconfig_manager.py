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

    def __init__(self, root_dir: Path):
        super().__init__()
        self.root_dir = root_dir

        # All paths are now relative to root_dir
        self.gen_codes_dir = self.root_dir / 'gen_codes'
        self.devices_dir = self.root_dir / 'devices'
        self.peripherals_dir = self.root_dir / 'peripherals'

    def update_sdkconfig_from_board_types(self, device_types: Set[str], peripheral_types: Set[str],
                                         sdkconfig_path: Optional[str] = None,
                                         enable: bool = True) -> Dict[str, List[str]]:
        """
        Update sdkconfig file based on board device and peripheral types.

        This function only manages device/peripheral support configurations.
        Board selection (CONFIG_ESP_BOARD_XXX) and chip target (CONFIG_IDF_TARGET)
        are managed by generate_board_manager_defaults() in board_manager.defaults file.

        Args:
            device_types: Set of device types from board YAML
            peripheral_types: Set of peripheral types from board YAML
            sdkconfig_path: Path to sdkconfig file (auto-detect if None)
            enable: Whether to enable features (True) or just check (False)

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
            self.logger.info(f'   sdkconfig file is not exists at {sdkconfig_path}, creating new one')
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

        # Write updated content back to file if changes were made
        if enable and result['enabled']:
            try:
                with open(sdkconfig_path, 'w', encoding='utf-8') as f:
                    f.write(sdkconfig_content)
                self.logger.info(f"   Successfully updated sdkconfig with {len(result['enabled'])} changes")

                # Delete build/config/sdkconfig.json after updating sdkconfig
                self._delete_sdkconfig_json(sdkconfig_path)
            except Exception as e:
                self.logger.error(f'Error writing sdkconfig: {e}')
                return result

        return result

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
            project_root_path = find_project_root(Path.cwd())
            project_root = str(project_root_path) if project_root_path is not None else None

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

    def _delete_sdkconfig_json(self, sdkconfig_path: str) -> None:
        """
        Delete build/config/sdkconfig.json file after updating sdkconfig.

        Args:
            sdkconfig_path: Path to sdkconfig file
        """
        try:
            # Get project root directory from sdkconfig path
            sdkconfig_file = Path(sdkconfig_path)
            project_root = sdkconfig_file.parent
            # Build path to build/config/sdkconfig.json
            sdkconfig_json_path = project_root / 'build' / 'config' / 'sdkconfig.json'
            # Delete the file if it exists
            if sdkconfig_json_path.exists():
                sdkconfig_json_path.unlink()
                self.logger.debug(f'   Deleted {sdkconfig_json_path}')
            else:
                self.logger.debug(f'   {sdkconfig_json_path} does not exist, skipping deletion')
        except Exception as e:
            # Log warning but don't fail the operation
            self.logger.warning(f'⚠️  Failed to delete build/config/sdkconfig.json: {e}')

    def generate_board_manager_defaults(self, board_path: str, project_path: Optional[str] = None,
                                        board_name: Optional[str] = None, chip_name: Optional[str] = None,
                                        output_file: Optional[str] = None,
                                        device_types: Optional[Set[str]] = None,
                                        peripheral_types: Optional[Set[str]] = None,
                                        device_subtypes: Optional[Dict[str, Set[str]]] = None) -> Dict[str, List[str]]:
        """
        Generate board_manager.defaults file with board-specific configurations.

        This file will be automatically applied via SDKCONFIG_DEFAULTS environment variable
        during build/menuconfig/reconfigure by the global callback in idf_ext.py.

        The generated file includes:
        - CONFIG_IDF_TARGET: chip name
        - CONFIG_ESP_BOARD_XXX: board selection macro
        - CONFIG_ESP_BOARD_NAME: board name string
        - CONFIG_ESP_BOARD_PERIPH_XXX_SUPPORT: enabled peripheral types from board config
        - CONFIG_ESP_BOARD_DEV_XXX_SUPPORT: enabled device types from board config
        - CONFIG_ESP_BOARD_DEV_<DEV>_SUB_<SUBTYPE>_SUPPORT: enabled device subtypes
        - Content from board's sdkconfig.defaults.board file (if exists)

        Args:
            board_path: Path to the board directory
            project_path: Path to the project directory (auto-detect if None)
            board_name: Board name for CONFIG_ESP_BOARD_XXX selection (optional)
            chip_name: Chip name for CONFIG_IDF_TARGET (optional)
            output_file: Path to output file (board_manager.defaults)
            device_types: Enabled device types from board parsing
            peripheral_types: Enabled peripheral types from board parsing
            device_subtypes: Enabled device subtypes from board parsing

        Returns:
            Dict with 'added' list of config items count
        """
        result = {'updated': [], 'added': []}

        # Extract board name from path if not provided
        if board_name is None:
            board_name = Path(board_path).name

        # Build board-specific section content
        marker_start = '# --- Board-specific configuration (managed by esp_board_manager) ---'
        marker_end = '# --- End of board-specific configuration ---'

        board_section_content = f'{marker_start}\n'
        board_section_content += f'# Board: {board_name}\n'

        # Add chip target config if provided
        config_count = 0
        if chip_name:
            board_section_content += f'CONFIG_IDF_TARGET="{chip_name}"\n'
            config_count += 1

        # Add board selection configs
        board_config_macro = f'CONFIG_ESP_BOARD_{board_name.upper().replace("-", "_")}'
        board_section_content += f'{board_config_macro}=y\n'
        board_section_content += f'CONFIG_ESP_BOARD_NAME="{board_name}"\n'
        config_count += 2  # Board selection + board name

        # Add device/peripheral board-manager config symbols.
        # These are generated from parsed board YAML and should be treated as
        # source-of-truth defaults for the selected board.
        enabled_bmgr_symbols: List[str] = []
        for periph_type in sorted(peripheral_types or set()):
            enabled_bmgr_symbols.append(f'CONFIG_ESP_BOARD_PERIPH_{periph_type.upper()}_SUPPORT=y')
        for dev_type in sorted(device_types or set()):
            enabled_bmgr_symbols.append(f'CONFIG_ESP_BOARD_DEV_{dev_type.upper()}_SUPPORT=y')
        for dev_type in sorted((device_subtypes or {}).keys()):
            for subtype in sorted(device_subtypes.get(dev_type, set())):
                enabled_bmgr_symbols.append(
                    f'CONFIG_ESP_BOARD_DEV_{dev_type.upper()}_SUB_{subtype.upper()}_SUPPORT=y'
                )

        if enabled_bmgr_symbols:
            board_section_content += '\n'
            board_section_content += '\n'.join(enabled_bmgr_symbols) + '\n'
            config_count += len(enabled_bmgr_symbols)

        # Check if sdkconfig.defaults.board file exists and add its content
        board_defaults_path = Path(board_path) / 'sdkconfig.defaults.board'
        if board_defaults_path.exists():
            self.logger.info(f'   Found sdkconfig.defaults.board at {board_defaults_path}')
            try:
                with open(board_defaults_path, 'r', encoding='utf-8') as f:
                    board_defaults_content = f.read().strip()
                if board_defaults_content:
                    board_section_content += f'\n{board_defaults_content}\n'
                    config_count += len([line for line in board_defaults_content.splitlines()
                                       if line.strip() and not line.strip().startswith('#')])
            except Exception as e:
                self.logger.error(f'   Error reading sdkconfig.defaults.board: {e}')
        else:
            self.logger.debug(f'   No sdkconfig.defaults.board found at {board_defaults_path}')

        board_section_content += f'{marker_end}\n'

        # Write board_manager.defaults file
        if output_file is None:
            self.logger.error('   output_file parameter is required')
            return result

        try:
            output_path = Path(output_file)
            output_path.parent.mkdir(parents=True, exist_ok=True)
            with open(output_path, 'w', encoding='utf-8') as f:
                f.write(board_section_content)
            result['added'] = [f'{config_count} board-specific defaults']
            self.logger.debug(f'   Generated {config_count} board defaults to {output_file}')

            # Delete build cache files to ensure fresh configuration
            project_path = self._get_project_path(project_path)
            self._delete_sdkconfig_json(Path(project_path) / 'sdkconfig')

            return result
        except Exception as e:
            self.logger.error(f'   Error writing to output file {output_file}: {e}')
            return result

    def clear_board_manager_defaults(self, project_path: Optional[str] = None) -> bool:
        """
        Delete board_manager.defaults file.

        Args:
            project_path: Path to the project directory (auto-detect if None)

        Returns:
            True if successful or file doesn't exist, False on error
        """
        project_path = self._get_project_path(project_path)
        board_manager_defaults_path = Path(project_path) / 'components/gen_bmgr_codes/board_manager.defaults'

        if not board_manager_defaults_path.exists():
            self.logger.debug('   board_manager.defaults not found')
            return True

        try:
            board_manager_defaults_path.unlink()
            self.logger.info('   Deleted board_manager.defaults')
            return True
        except Exception as e:
            self.logger.warning(f'   ⚠️  Failed to delete board_manager.defaults: {e}')
            return False

    def _get_project_path(self, project_path: Optional[str]) -> str:
        """Get project path, auto-detect if None."""
        if project_path is None:
            return self.project_path if hasattr(self, 'project_path') else os.getcwd()
        return project_path

    def _parse_bmgr_symbols_from_kconfig(self, project_path: Optional[str] = None) -> Set[str]:
        """Parse board manager symbols from generated gen_codes/Kconfig.in
        and external boards' Kconfig.projbuild.

        Args:
            project_path: Project directory for locating Kconfig.projbuild.
                          Falls back to _get_project_path(None) if not provided.

        Returns:
            Set of symbol names without CONFIG_ prefix, e.g. ESP_BOARD_PERIPH_I2C_SUPPORT
        """
        symbols: Set[str] = set()

        # 1. Parse internal board symbols from Kconfig.in
        kconfig_in_path = self.gen_codes_dir / 'Kconfig.in'
        if kconfig_in_path.exists():
            try:
                with open(kconfig_in_path, 'r', encoding='utf-8') as f:
                    for line in f:
                        m = re.match(r'^\s*config\s+(ESP_BOARD_[A-Z0-9_]+)\s*$', line)
                        if m:
                            symbols.add(m.group(1))
            except Exception as e:
                self.logger.warning(f'⚠️  Failed to parse Kconfig.in for symbols: {e}')
        else:
            self.logger.warning(f'   Kconfig.in not found at {kconfig_in_path}, skip checking internal symbols')

        # 2. Parse external board symbols from Kconfig.projbuild
        resolved_project_path = project_path or self._get_project_path(None)
        projbuild_path = Path(resolved_project_path) / 'components' / 'gen_bmgr_codes' / 'Kconfig.projbuild'
        if projbuild_path.exists():
            try:
                with open(projbuild_path, 'r', encoding='utf-8') as f:
                    for line in f:
                        m = re.match(r'^\s*config\s+(ESP_BOARD_[A-Z0-9_]+)\s*$', line)
                        if m:
                            symbols.add(m.group(1))
            except Exception as e:
                self.logger.warning(f'⚠️  Failed to parse Kconfig.projbuild for symbols: {e}')

        return symbols

    def _build_expected_enabled_symbols(
        self,
        *,
        board_select_symbol: Optional[str],
        device_types: Set[str],
        peripheral_types: Set[str],
        device_subtypes: Optional[Dict[str, Set[str]]] = None,
    ) -> Set[str]:
        """Build expected enabled board manager symbols (without CONFIG_ prefix)."""
        expected: Set[str] = set()
        if board_select_symbol:
            expected.add(board_select_symbol)

        for periph_type in peripheral_types:
            expected.add(f'ESP_BOARD_PERIPH_{periph_type.upper()}_SUPPORT')

        for dev_type in device_types:
            expected.add(f'ESP_BOARD_DEV_{dev_type.upper()}_SUPPORT')

        for dev_type, subtypes in (device_subtypes or {}).items():
            dev_upper = dev_type.upper()
            for subtype in subtypes:
                expected.add(f'ESP_BOARD_DEV_{dev_upper}_SUB_{subtype.upper()}_SUPPORT')

        return expected

    def _detect_board_select_symbol(self, selected_board: str, project_path: Optional[str] = None) -> Optional[str]:
        """Detect board selection symbol from Kconfig.in or Kconfig.projbuild for current board name.

        Supports both historical naming styles:
        - BOARD_<BOARD_NAME>
        - ESP_BOARD_<BOARD_NAME>

        Args:
            selected_board: Currently selected board name
            project_path: Project directory for locating Kconfig.projbuild.
                          Falls back to _get_project_path(None) if not provided.
        """
        board_upper = selected_board.upper().replace('-', '_')
        candidates = [f'BOARD_{board_upper}', f'ESP_BOARD_{board_upper}']

        # Check internal board configs (Kconfig.in)
        kconfig_in_path = self.gen_codes_dir / 'Kconfig.in'
        if kconfig_in_path.exists():
            try:
                with open(kconfig_in_path, 'r', encoding='utf-8') as f:
                    text = f.read()
                for sym in candidates:
                    if re.search(rf'^\s*config\s+{re.escape(sym)}\s*$', text, flags=re.M):
                        return sym
            except Exception as e:
                self.logger.warning(f'⚠️  Failed to detect board selection symbol in Kconfig.in: {e}')

        # Check external board configs (Kconfig.projbuild)
        resolved_project_path = project_path or self._get_project_path(None)
        projbuild_path = Path(resolved_project_path) / 'components' / 'gen_bmgr_codes' / 'Kconfig.projbuild'
        if projbuild_path.exists():
            try:
                with open(projbuild_path, 'r', encoding='utf-8') as f:
                    text = f.read()
                for sym in candidates:
                    if re.search(rf'^\s*config\s+{re.escape(sym)}\s*$', text, flags=re.M):
                        return sym
            except Exception as e:
                self.logger.warning(f'⚠️  Failed to detect board selection symbol in Kconfig.projbuild: {e}')

        return None

    def _parse_sdkconfig_bmgr_state(self, sdkconfig_content: str) -> Dict[str, bool]:
        """Parse board manager bool symbols from sdkconfig text.

        Returns:
            Dict of symbol name (without CONFIG_) to bool enabled state.
        """
        states: Dict[str, bool] = {}
        line_set = re.compile(r'^CONFIG_(ESP_BOARD_[A-Z0-9_]+)=y\s*$')
        line_unset = re.compile(r'^#\s+CONFIG_(ESP_BOARD_[A-Z0-9_]+)\s+is\s+not\s+set\s*$')

        for line in sdkconfig_content.splitlines():
            m = line_set.match(line)
            if m:
                states[m.group(1)] = True
                continue
            m = line_unset.match(line)
            if m:
                states[m.group(1)] = False
        return states

    def ensure_sdkconfig_consistency(
        self,
        *,
        sdkconfig_path: str,
        selected_board: str,
        device_types: Set[str],
        peripheral_types: Set[str],
        device_subtypes: Optional[Dict[str, Set[str]]] = None,
        auto_fix: bool = True,
        project_path: Optional[str] = None,
    ) -> Dict[str, object]:
        """Ensure sdkconfig board-manager symbols match current generated board selection.

        This method only checks and updates board-manager related symbols (ESP_BOARD_*),
        without touching unrelated sdkconfig options.

        Args:
            project_path: Project directory for locating external board Kconfig.projbuild.
                          Falls back to os.getcwd() if not provided.
        """
        result: Dict[str, object] = {
            'ok': True,
            'fixed': False,
            'issues': [],
            'fixed_items': [],
        }

        if not os.path.exists(sdkconfig_path):
            self.logger.info(f'   sdkconfig not found at {sdkconfig_path}, skip consistency check')
            return result

        try:
            with open(sdkconfig_path, 'r', encoding='utf-8') as f:
                sdkconfig_content = f.read()
        except Exception as e:
            self.logger.error(f'❌ Failed to read sdkconfig for consistency check: {e}')
            result['ok'] = False
            return result

        managed_symbols = self._parse_bmgr_symbols_from_kconfig(project_path=project_path)
        if not managed_symbols:
            return result

        # Board selection macro style differs between versions (BOARD_* / ESP_BOARD_*).
        board_select_symbol = self._detect_board_select_symbol(selected_board, project_path=project_path)
        if board_select_symbol:
            managed_symbols.add(board_select_symbol)

        expected_enabled = self._build_expected_enabled_symbols(
            board_select_symbol=board_select_symbol,
            device_types=device_types,
            peripheral_types=peripheral_types,
            device_subtypes=device_subtypes,
        )
        expected_states = {sym: (sym in expected_enabled) for sym in managed_symbols}
        actual_states = self._parse_sdkconfig_bmgr_state(sdkconfig_content)

        issues: List[str] = []
        updates: Dict[str, bool] = {}

        for sym in sorted(managed_symbols):
            expected = expected_states[sym]
            actual = actual_states.get(sym, None)

            # Missing symbol in sdkconfig should be treated as "not set".
            # So only missing-but-expected-y is an inconsistency.
            if actual is None:
                if expected:
                    issues.append(f'CONFIG_{sym} missing, expected y')
                    updates[sym] = True
                continue

            if actual != expected:
                issues.append(f'CONFIG_{sym} is {"y" if actual else "not set"}, expected {"y" if expected else "not set"}')
                updates[sym] = expected

        result['issues'] = issues

        if not issues:
            self.logger.info('   sdkconfig board-manager symbols are consistent with current board generation')
            return result

        self.logger.warning(f'⚠️  Detected {len(issues)} board-manager sdkconfig inconsistency issue(s)')
        for issue in issues:
            self.logger.warning(f'   - {issue}')

        if not auto_fix:
            result['ok'] = False
            return result

        lines = sdkconfig_content.splitlines()
        line_set = re.compile(r'^CONFIG_(ESP_BOARD_[A-Z0-9_]+)=y\s*$')
        line_unset = re.compile(r'^#\s+CONFIG_(ESP_BOARD_[A-Z0-9_]+)\s+is\s+not\s+set\s*$')
        fixed_items: List[str] = []
        touched: Set[str] = set()

        for i, line in enumerate(lines):
            m = line_set.match(line) or line_unset.match(line)
            if not m:
                continue
            sym = m.group(1)
            if sym not in updates:
                continue
            desired = updates[sym]
            lines[i] = f'CONFIG_{sym}=y' if desired else f'# CONFIG_{sym} is not set'
            fixed_items.append(f'CONFIG_{sym} -> {"y" if desired else "not set"}')
            touched.add(sym)

        missing = [sym for sym in sorted(updates.keys()) if sym not in touched]
        if missing:
            lines.append('')
            lines.append('# --- Board Manager sdkconfig consistency fix ---')
            for sym in missing:
                desired = updates[sym]
                lines.append(f'CONFIG_{sym}=y' if desired else f'# CONFIG_{sym} is not set')
                fixed_items.append(f'CONFIG_{sym} -> {"y" if desired else "not set"} (added)')

        try:
            with open(sdkconfig_path, 'w', encoding='utf-8') as f:
                f.write('\n'.join(lines) + '\n')
            self._delete_sdkconfig_json(sdkconfig_path)
        except Exception as e:
            self.logger.error(f'❌ Failed to auto-fix sdkconfig consistency: {e}')
            result['ok'] = False
            return result

        result['fixed'] = True
        result['fixed_items'] = fixed_items
        self.logger.info(f'✅ Auto-fixed {len(fixed_items)} board-manager sdkconfig symbol(s)')
        return result
