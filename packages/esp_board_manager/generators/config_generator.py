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
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent))

from .parser_loader import load_parsers
from .peripheral_parser import PeripheralParser
from .device_parser import DeviceParser
from .name_validator import parse_component_name


class ConfigGenerator(LoggerMixin):
    """Main configuration generator class for board scanning and configuration file discovery"""

    def __init__(self, script_dir: Path):
        super().__init__()
        self.script_dir = script_dir
        # Use IDF_EXTRA_ACTIONS_PATH environment variable for Default boards path if available
        # Otherwise fall back to script_dir/boards
        idf_extra_actions_path = os.environ.get('IDF_EXTRA_ACTIONS_PATH')
        if idf_extra_actions_path:
            self.boards_dir = Path(idf_extra_actions_path) / 'boards'
        else:
            self.boards_dir = script_dir / 'boards'

    def is_c_constant(self, value: Any) -> bool:
        """Check if a value is a C constant based on naming patterns and prefixes"""
        if not isinstance(value, str):
            return False
        if value.isupper() and '_' in value:
            return True
        return any(value.startswith(prefix) for prefix in BoardManagerConfig.get_c_constant_prefixes())

    def dict_to_c_initializer(self, d: Dict[str, Any], indent: int = 4) -> List[str]:
        """Convert dictionary to C initializer format with proper type handling and formatting"""
        lines = []
        for k, v in d.items():
            if isinstance(v, dict):
                lines.append(f'.{k} = {{')
                lines += [' ' * (indent + 4) + l for l in self.dict_to_c_initializer(v, indent)]
                lines.append(' ' * indent + '},')
            elif isinstance(v, bool):
                lines.append(f".{k} = {'true' if v else 'false'},")
            elif v is None:
                lines.append(f'.{k} = NULL,')
            elif isinstance(v, str):
                # Use improved C constant detection
                if self.is_c_constant(v):
                    lines.append(f'.{k} = {v},')
                else:
                    lines.append(f'.{k} = "{v}",')
            elif isinstance(v, int):
                # For large integers (GPIO bit masks), use hexadecimal format with ULL suffix
                if k == 'pin_bit_mask' and v > 0xFFFFFFFF:
                    lines.append(f'.{k} = 0x{v:X}ULL,')
                # For channel mask fields, use hexadecimal format
                elif k in ['adc_channel_mask', 'dac_channel_mask']:
                    lines.append(f'.{k} = 0x{v:X},')
                else:
                    lines.append(f'.{k} = {v},')
            elif isinstance(v, float):
                lines.append(f'.{k} = {v},')
            elif isinstance(v, list):
                # Handle arrays of structures properly
                if v and isinstance(v[0], dict):
                    # This is an array of structures, generate proper C initializer
                    lines.append(f'.{k} = {{')
                    for i, item in enumerate(v):
                        if isinstance(item, dict):
                            lines.append(' ' * (indent + 4) + '{')
                            lines += [' ' * (indent + 8) + l for l in self.dict_to_c_initializer(item, indent + 4)]
                            lines.append(' ' * (indent + 4) + '},')
                        else:
                            lines.append(' ' * (indent + 4) + f'{item},')
                    lines.append(' ' * indent + '},')
                else:
                    # Simple array of primitive types
                    arr = '{' + ', '.join(str(x) for x in v) + '}'
                    lines.append(f'.{k} = {arr},')
            else:
                lines.append(f'.{k} = 0, // unsupported type')
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
                kconfig_path = board_path / 'Kconfig'
                board_info_path = board_path / 'board_info.yaml'

                if board_path.is_dir() and kconfig_path.exists():
                    all_boards[d] = str(board_path)
                    self.logger.debug(f'Found board in main directory: {d}')
                    if board_info_path.exists():
                        self.logger.debug(f'  - Has board_info.yaml')
                    else:
                        self.logger.debug(f'  - No board_info.yaml (will use defaults)')

        # 2. Scan components boards directories
        project_root = os.environ.get('PROJECT_DIR')
        if not project_root:
            # Start searching from current working directory, not script directory
            project_root = find_project_root(Path(os.getcwd()))

        if project_root:
            components_boards = self._find_components_boards(project_root)
            for boards_dir in components_boards:
                self.logger.info(f'   Scanning component boards: {boards_dir}')
                for d in os.listdir(boards_dir):
                    board_path = os.path.join(boards_dir, d)
                    kconfig_path = os.path.join(board_path, 'Kconfig')
                    board_info_path = os.path.join(board_path, 'board_info.yaml')

                    if os.path.isdir(board_path) and os.path.isfile(kconfig_path):
                        all_boards[d] = board_path
                        self.logger.debug(f'Found board in component directory: {d}')
                        if os.path.exists(board_info_path):
                            self.logger.debug(f'  - Has board_info.yaml')
                        else:
                            self.logger.debug(f'  - No board_info.yaml (will use defaults)')

        # 3. Scan customer boards directory if provided
        if board_customer_path and board_customer_path != 'NONE':
            self.logger.info(f'Scanning customer boards: {board_customer_path}')
            if os.path.exists(board_customer_path):
                # Check if the path is a single board directory or a directory containing multiple boards
                if self._is_board_directory(board_customer_path):
                    # It's a single board directory
                    board_name = os.path.basename(board_customer_path)
                    all_boards[board_name] = board_customer_path
                    self.logger.debug(f'Found single board: {board_name}')
                else:
                    # It's a directory containing multiple boards (recursive scan)
                    customer_boards = self._scan_all_directories_for_boards(board_customer_path, 'customer')
                    all_boards.update(customer_boards)
            else:
                self.logger.warning(f'⚠️  Warning: Customer boards path does not exist: {board_customer_path}')
        else:
            self.logger.info(f'   No customer boards path specified')

        return all_boards

    def _find_components_boards(self, project_root: str) -> List[str]:
        """Find boards directories in all components, excluding board_manager to avoid duplication"""
        boards_dirs = []
        components_dir = os.path.join(project_root, 'components')

        if not os.path.exists(components_dir):
            self.logger.warning(f'⚠️  Components directory not found: {components_dir}')
            return boards_dirs

        for component in os.listdir(components_dir):
            component_path = os.path.join(components_dir, component)
            if os.path.isdir(component_path):
                boards_path = os.path.join(component_path, 'boards')
                if os.path.exists(boards_path) and os.path.isdir(boards_path):
                    # Skip board_manager's own boards directory to avoid duplication
                    if component != 'board_manager':
                        boards_dirs.append(boards_path)
                        self.logger.debug(f"   Found boards directory in component '{component}': {boards_path}")

        return boards_dirs

    def _scan_all_directories_for_boards(self, root_dir: str, dir_name: str) -> Dict[str, str]:
        """Recursively scan all subdirectories for boards with Kconfig files"""
        board_dirs = {}

        if not os.path.exists(root_dir):
            self.logger.warning(f'⚠️  {dir_name} directory does not exist: {root_dir}')
            return board_dirs

        self.logger.info(f'Scanning all directories in {dir_name} path: {root_dir}')

        for d in os.listdir(root_dir):
            board_path = os.path.join(root_dir, d)
            kconfig_path = os.path.join(board_path, 'Kconfig')
            board_info_path = os.path.join(board_path, 'board_info.yaml')

            if os.path.isdir(board_path) and os.path.isfile(kconfig_path):
                board_dirs[d] = board_path
                self.logger.debug(f'Found board in {dir_name}: {d}')
                if os.path.exists(board_info_path):
                    self.logger.debug(f'  - Has board_info.yaml')
                else:
                    self.logger.debug(f'  - No board_info.yaml (will use defaults)')
            elif os.path.isdir(board_path):
                # Recursively scan subdirectories
                sub_boards = self._scan_all_directories_for_boards(board_path, f'{dir_name}/{d}')
                board_dirs.update(sub_boards)

        return board_dirs

    def _is_board_directory(self, path: str) -> bool:
        """Check if a directory is a valid board directory by verifying Kconfig file exists"""
        kconfig_path = os.path.join(path, 'Kconfig')
        return os.path.isdir(path) and os.path.isfile(kconfig_path)

    def get_selected_board_from_sdkconfig(self) -> str:
        """Read sdkconfig file to determine which board is selected, fallback to default if not found"""
        # First try to use PROJECT_DIR environment variable if available
        project_root = os.environ.get('PROJECT_DIR')
        if project_root:
            self.logger.info(f'Using PROJECT_DIR from environment: {project_root}')
        else:
            # Look for sdkconfig file in project root
            project_root = find_project_root(Path(os.getcwd()))
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

        self.logger.info(f"   Using board configuration files for '{board_name}':")
        self.logger.info(f'   Peripherals: {periph_yaml_path}')
        self.logger.info(f'   Devices: {dev_yaml_path}')

        return periph_yaml_path, dev_yaml_path
