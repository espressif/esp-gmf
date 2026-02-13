# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
Configuration generator for ESP Board Manager
"""

import os
import re
import sys
from pathlib import Path
from typing import Dict, List, Tuple, Optional, Any

from .utils.logger import LoggerMixin
from .utils.file_utils import find_project_root, safe_write_file
from .utils.yaml_utils import load_yaml_safe
from .settings import BoardManagerConfig
sys.path.insert(0, str(Path(__file__).parent.parent))

from .parser_loader import load_parsers
from .peripheral_parser import PeripheralParser
from .device_parser import DeviceParser
from .name_validator import parse_component_name


class ConfigGenerator(LoggerMixin):
    """Main configuration generator class for board scanning and configuration file discovery"""

    def __init__(self, root_dir: Path):
        super().__init__()
        self.root_dir = root_dir
        # Use validated root_dir passed from main script
        self.boards_dir = self.root_dir / 'boards'
        # Cache project root to avoid repeated lookups
        self._project_root: Optional[str] = None

    def get_project_root(self) -> Optional[str]:
        """Get project root directory, with caching

        Returns:
            Project root path as string, or None if not found
        """
        # Return cached value if available
        if self._project_root is not None:
            return self._project_root

        # Try environment variable first
        project_root = os.environ.get('PROJECT_DIR')
        if project_root:
            self._project_root = project_root
            return project_root

        # Search for project root
        project_root_path = find_project_root(Path(os.getcwd()))
        if project_root_path:
            self._project_root = str(project_root_path)
            return self._project_root

        return None

    def is_c_constant(self, value: Any) -> bool:
        """Check if a value is a C constant based on naming patterns and prefixes"""
        if not isinstance(value, str):
            return False
        if value.isupper() and '_' in value:
            return True
        return any(value.startswith(prefix) for prefix in BoardManagerConfig.get_c_constant_prefixes())

    def _format_value_to_c(self, v: Any) -> str:
        """Helper to format a single value into C syntax recursively"""
        if isinstance(v, bool):
            return 'true' if v else 'false'
        elif v is None:
            return 'NULL'
        elif isinstance(v, str):
            # Don't quote if it's:
            # 1. A C constant (e.g. GPIO_NUM_1)
            # 2. A hex number (e.g. 0x12)
            # 3. Already quoted string (e.g. "value")
            x_stripped = v.strip()
            if self.is_c_constant(v) or x_stripped.lower().startswith('0x'):
                return str(v)
            elif (x_stripped.startswith('"') and x_stripped.endswith('"')) or \
                 (x_stripped.startswith("'") and x_stripped.endswith("'")):
                return v
            else:
                return f'"{v}"'
        elif isinstance(v, int):
            return str(v)
        elif isinstance(v, float):
            return str(v)
        elif isinstance(v, list):
            # Recursive formatting for lists -> C arrays { ... }
            elements = [self._format_value_to_c(item) for item in v]
            return '{' + ', '.join(elements) + '}'
        elif isinstance(v, dict):
            # Recursive formatting for dicts -> C structs { .k = v, ... }
            # Note: Inline structs usually don't need newlines/indentation for simple cases,
            # but for complex ones it might be long. Keeping it compact for now.
            fields = []
            for sub_k, sub_v in v.items():
                fields.append(f'.{sub_k} = {self._format_value_to_c(sub_v)}')
            return '{' + ', '.join(fields) + '}'
        else:
            return str(v)

    def dict_to_c_initializer(self, d: Dict[str, Any], indent: int = 4) -> List[str]:
        """Convert dictionary to C initializer format with proper type handling and formatting"""
        lines = []
        if isinstance(d, list):
            # Special case for top-level list (unlikely for struct init, but kept for compatibility)
            arr = ', '.join(self._format_value_to_c(x) for x in d)
            lines.append(f'{arr}')
            return lines

        for k, v in d.items():
            # Special handling for large integers or specific fields that need hex
            if isinstance(v, int):
                if k == 'pin_bit_mask' and v > 0xFFFFFFFF:
                    lines.append(f'.{k} = 0x{v:X}ULL,')
                    continue
                elif k in ['adc_channel_mask', 'dac_channel_mask']:
                    lines.append(f'.{k} = 0x{v:X},')
                    continue

            # Standard handling using the helper
            # We handle dicts specially here to preserve the nice multiline formatting
            if isinstance(v, dict):
                lines.append(f'.{k} = {{')
                lines += [' ' * (indent + 4) + l for l in self.dict_to_c_initializer(v, indent + 4)]
                lines.append(' ' * indent + '},')
            elif isinstance(v, list) and v and isinstance(v[0], dict):
                # Array of structs - special multiline formatting
                lines.append(f'.{k} = {{')
                for item in v:
                    lines.append(' ' * (indent + 4) + '{')
                    lines += [' ' * (indent + 8) + l for l in self.dict_to_c_initializer(item, indent + 4)]
                    lines.append(' ' * (indent + 4) + '},')
                lines.append(' ' * indent + '},')
            else:
                # Everything else (primitives, arrays of primitives, arrays of arrays)
                val_str = self._format_value_to_c(v)
                lines.append(f'.{k} = {val_str},')

        return lines

    def extract_id_from_name(self, name: str) -> int:
        """Extract numeric ID from component name using regex pattern matching"""
        import re
        m = re.search(r'(\d+)$', name)
        return int(m.group(1)) if m else -1

    def scan_board_directories(self, board_customer_path: Optional[str] = None) -> Dict[str, str]:
        """Scan all board directories including main, component, and customer boards"""
        all_boards = {}

        # 1. Scan main boards directory
        if self.boards_dir.exists():
            self.logger.info(f'   Scanning main boards: {self.boards_dir}')
            for d in os.listdir(self.boards_dir):
                board_path = self.boards_dir / d

                if not board_path.is_dir():
                    continue

                # Check if this is a valid board (has all 3 required YAML files)
                if self._is_board_directory(str(board_path)):
                    if self.is_valid_board_name(d):
                        all_boards[d] = str(board_path)
                        self.logger.debug(f'Found valid board in main directory: {d}')
                    else:
                        self.logger.warning(
                            f'⚠️  Skipping board with invalid name "{d}". Board names must contain only letters, numbers, and underscores.'
                        )
                else:
                    # Check if it has any of the YAML files but missing others (invalid board)
                    # We check for ANY of the 3 files to decide if it's an incomplete board
                    yaml_files = ['board_info.yaml', 'board_peripherals.yaml', 'board_devices.yaml']
                    found_files = [f for f in yaml_files if (board_path / f).exists()]

                    if found_files:
                        missing_files = [f for f in yaml_files if f not in found_files]
                        self.logger.warning(
                            f'⚠️  Skipping invalid board "{d}" in main directory - missing required files: {", ".join(missing_files)}'
                        )
                # Check for board name consistency
                # Moved to post-selection phase
                # self.check_board_name_consistency(str(board_path), d)

        # 2. Scan components boards directories (recursive scan)
        # Get project root first, then check if components exists
        project_root = self.get_project_root()

        if project_root:
            components_dir = os.path.join(project_root, 'components')
            # Get absolute path of Default boards to exclude
            default_boards_path = str(self.boards_dir.resolve()) if hasattr(self.boards_dir, 'resolve') else str(self.boards_dir)

            if os.path.exists(components_dir):
                self.logger.info(f'   Scanning component boards (recursive): {components_dir}')
                component_boards = self._scan_all_directories_for_boards(
                    components_dir,
                    'component',
                    exclude_path=default_boards_path
                )
                all_boards.update(component_boards)

            # 2.1 Scan managed_components boards directories
            # Scan directories under managed_components whose name contains 'boards'
            managed_components_dir = os.path.join(project_root, 'managed_components')
            if os.path.exists(managed_components_dir):
                self.logger.info(f'   Scanning managed components boards: {managed_components_dir}')
                try:
                    for d in os.listdir(managed_components_dir):
                        subdir_path = os.path.join(managed_components_dir, d)
                        if os.path.isdir(subdir_path) and 'boards' in d:
                            self.logger.info(f'     Found potential board component in managed_components: {d}')

                            # Check if the component itself is a board directory
                            if self._is_board_directory(subdir_path):
                                board_name = d
                                if self.is_valid_board_name(board_name):
                                    all_boards[board_name] = subdir_path
                                    self.logger.debug(f'     Found single board in managed_components: {board_name}')
                                else:
                                    self.logger.warning(
                                        f'⚠️  Skipping board with invalid name "{board_name}". Board names must contain only letters, numbers, and underscores.'
                                    )
                            else:
                                # Recursively scan for boards within the component
                                managed_boards = self._scan_all_directories_for_boards(
                                    subdir_path,
                                    f'component (managed: {d})',
                                    exclude_path=default_boards_path,
                                    max_depth=3
                                )
                                all_boards.update(managed_boards)
                except Exception as e:
                    self.logger.debug(f'Error scanning managed_components: {e}')

        # 3. Scan customer boards directory if provided
        if board_customer_path and board_customer_path != 'NONE':
            self.logger.info(f'Scanning customer boards: {board_customer_path}')
            if os.path.exists(board_customer_path):
                # Check if the path is a single board directory or a directory containing multiple boards
                if self._is_board_directory(board_customer_path):
                    # It's a single board directory
                    board_name = os.path.basename(board_customer_path)
                    if self.is_valid_board_name(board_name):
                        all_boards[board_name] = board_customer_path
                        self.logger.debug(f'Found single board: {board_name}')
                    else:
                        self.logger.warning(
                            f'⚠️  Skipping board with invalid name "{board_name}". Board names must contain only letters, numbers, and underscores.'
                        )
                    # Moved to post-selection phase
                    # self.check_board_name_consistency(board_customer_path, board_name)
                else:
                    # It's a directory containing multiple boards (recursive scan)
                    customer_boards = self._scan_all_directories_for_boards(board_customer_path, 'customer')
                    all_boards.update(customer_boards)
            else:
                self.logger.warning(f'⚠️  Warning: Customer boards path does not exist: {board_customer_path}')
        else:
            self.logger.debug(f'   No customer boards path specified')

        return all_boards

    def _scan_all_directories_for_boards(
        self,
        root_dir: str,
        dir_name: str,
        exclude_path: Optional[str] = None,
        depth: int = 0,
        max_depth: int = 10
    ) -> Dict[str, str]:
        """
        Recursively scan all subdirectories for valid boards.

        A valid board must have all 3 required files:
        - board_info.yaml
        - board_peripherals.yaml
        - board_devices.yaml

        Args:
            root_dir: Root directory to scan
            dir_name: Name for logging purposes
            exclude_path: Optional path to exclude (e.g., Default boards directory)
            depth: Current recursion depth
            max_depth: Maximum recursion depth to prevent infinite loops

        Returns:
            Dictionary of board_name -> board_path
        """
        board_dirs = {}

        if not os.path.exists(root_dir):
            self.logger.warning(f'⚠️  {dir_name} directory does not exist: {root_dir}')
            return board_dirs

        # Limit recursion depth for safety
        if depth >= max_depth:
            self.logger.debug(f'Max recursion depth ({max_depth}) reached at: {root_dir}')
            return board_dirs

        if depth == 0:
            self.logger.info(f'Scanning all directories in {dir_name} path: {root_dir}')

        try:
            for d in os.listdir(root_dir):
                board_path = os.path.join(root_dir, d)

                if not os.path.isdir(board_path):
                    continue

                # Check if this is a valid board directory (has all 3 required files)
                if self._is_board_directory(board_path):
                    # Check if this path should be excluded
                    if exclude_path:
                        board_abs_path = os.path.abspath(board_path)
                        parent_abs_path = os.path.abspath(os.path.dirname(board_path))
                        exclude_abs_path = os.path.abspath(exclude_path)

                        if parent_abs_path == exclude_abs_path:
                            self.logger.debug(f'Skipping excluded board: {d}')
                            continue

                    # Found a valid board
                    if self.is_valid_board_name(d):
                        board_dirs[d] = board_path
                        self.logger.debug(f'Found valid board in {dir_name}: {d}')
                    else:
                        self.logger.warning(
                            f'⚠️  Skipping board with invalid name "{d}". Board names must contain only letters, numbers, and underscores.'
                        )
                    # Moved to post-selection phase
                    # self.check_board_name_consistency(board_path, d)
                else:
                    # Check if it has any YAML files but missing others (invalid board)
                    # We check for ANY of the 3 files to decide if it's an incomplete board
                    yaml_files = ['board_info.yaml', 'board_peripherals.yaml', 'board_devices.yaml']
                    found_files = [f for f in yaml_files if os.path.isfile(os.path.join(board_path, f))]

                    if found_files:
                        # This looks like it was intended to be a board but is incomplete
                        missing_files = [f for f in yaml_files if f not in found_files]
                        self.logger.warning(
                            f'⚠️  Skipping invalid board "{d}" - missing required files: {", ".join(missing_files)}'
                        )
                    else:
                        # Not a board directory (no YAML files), continue scanning subdirectories
                        sub_boards = self._scan_all_directories_for_boards(
                            board_path,
                            f'{dir_name}/{d}',
                            exclude_path=exclude_path,
                            depth=depth + 1,
                            max_depth=max_depth
                        )
                        board_dirs.update(sub_boards)

        except PermissionError:
            self.logger.warning(f'⚠️  Permission denied accessing {root_dir}')
        except Exception as e:
            self.logger.debug(f'Error scanning {root_dir}: {e}')

        return board_dirs

    def _is_board_directory(self, path: str) -> bool:
        """
        Check if a directory is a valid board directory by verifying all required files exist.

        A valid board must have:
        1. board_info.yaml
        2. board_peripherals.yaml
        3. board_devices.yaml
        """
        if not os.path.isdir(path):
            return False

        required_files = [
            'board_info.yaml',
            'board_peripherals.yaml',
            'board_devices.yaml'
        ]

        for filename in required_files:
            file_path = os.path.join(path, filename)
            if not os.path.isfile(file_path):
                return False

        return True

    def is_valid_board_name(self, name: str) -> bool:
        """
        Check if board name is valid.
        Valid names can contain letters (uppercase/lowercase), numbers, and underscores.
        Must not contain hyphens or other special characters.
        """
        return bool(re.match(r'^[a-zA-Z0-9_]+$', name))

    def check_board_name_consistency(self, board_path: str, dir_name: str) -> None:
        """Check if board name in board_info.yaml matches directory name"""
        try:
            board_info_path = os.path.join(board_path, 'board_info.yaml')
            if os.path.exists(board_info_path):
                # Use simple parsing to avoid full YAML load overhead if possible,
                # but load_yaml_safe is safer
                board_info = load_yaml_safe(board_info_path)
                if board_info and 'board' in board_info:
                    board_name_in_yaml = board_info['board']
                    if board_name_in_yaml != dir_name:
                        self.logger.warning(
                            f'⚠️  Board name mismatch in "{dir_name}": '
                            f'directory name is "{dir_name}", but board_info.yaml says "{board_name_in_yaml}". '
                            f'The directory name will be used as the board ID.'
                        )
        except Exception as e:
            self.logger.debug(f'Error checking board name consistency for {dir_name}: {e}')

    def get_selected_board_from_sdkconfig(self) -> str:
        """Read sdkconfig file to determine which board is selected, fallback to default if not found"""
        # Get project root using cached method
        project_root = self.get_project_root()
        if project_root:
            self.logger.info(f'Using project root: {project_root}')
            if not project_root:
                self.logger.warning('⚠️  Could not find project root, using default board')
                return BoardManagerConfig.DEFAULT_BOARD

        sdkconfig_path = Path(project_root) / 'sdkconfig'
        if not sdkconfig_path.exists():
            self.logger.warning('⚠️  sdkconfig file not found, using default board')
            return BoardManagerConfig.DEFAULT_BOARD

        self.logger.info(f'Reading sdkconfig from: {sdkconfig_path}')

        # Read sdkconfig and look for BOARD_XX configs
        selected_board = None
        with open(sdkconfig_path, 'r') as f:
            for line in f:
                line = line.strip()
                if line.startswith('CONFIG_BOARD_') and '=y' in line:
                    # Extract board name from CONFIG_BOARD_ESP32S3_KORVO_3=y
                    config_name = line.split('=')[0]
                    board_name = config_name.replace('CONFIG_BOARD_', '').lower()
                    selected_board = board_name
                    self.logger.info(f'Found selected board in sdkconfig: {board_name}')
                    break

        if not selected_board:
            self.logger.warning('⚠️  No board selected in sdkconfig, using default board')
            return BoardManagerConfig.DEFAULT_BOARD

        return selected_board

    def find_board_config_files(self, board_name: str, all_boards: Dict[str, str]) -> Tuple[Optional[str], Optional[str]]:
        """Find board_peripherals.yaml and board_devices.yaml for the selected board"""
        if board_name not in all_boards:
            self.logger.error(f"Board '{board_name}' not found in any boards directory")
            self.logger.error(f'Available boards: {list(all_boards.keys())}')
            return None, None

        board_path = all_boards[board_name]
        periph_yaml_path = os.path.join(board_path, 'board_peripherals.yaml')
        dev_yaml_path = os.path.join(board_path, 'board_devices.yaml')

        if not os.path.exists(periph_yaml_path):
            self.logger.error(f"board_peripherals.yaml not found for board '{board_name}' at {periph_yaml_path}")
            return None, None

        if not os.path.exists(dev_yaml_path):
            self.logger.error(f"board_devices.yaml not found for board '{board_name}' at {dev_yaml_path}")
            return None, None

        self.logger.debug(f"   Using board configuration files for '{board_name}':")
        self.logger.info(f'   Peripherals: {periph_yaml_path}')
        self.logger.info(f'   Devices: {dev_yaml_path}')

        return periph_yaml_path, dev_yaml_path
