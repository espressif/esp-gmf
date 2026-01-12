#!/usr/bin/env python3
"""
# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.
"""

"""
Board Creation Script for ESP Board Manager

This script provides an interactive command-line interface to create a new board
configuration with automatic file generation and dependency validation.

Features:
- Cursor-based checkbox interface for chip, device, and peripheral selection
- Arrow key navigation and spacebar toggling (↑/↓/Space)
- 'q' key to cancel/return to previous step
- Cross-platform compatibility (Windows PowerShell/CMD, Linux/macOS terminals)
- Automatic dependency checking and resolution
- Graceful fallback to simple number-based menu when terminal control is unavailable

Platform Support:
- Linux/macOS: Full terminal control with arrow keys
- Windows PowerShell/CMD: Uses j/k keys for navigation (arrow keys may not work)
- All platforms: Fallback to SimpleTerminalMenu if terminal control fails

Usage Instructions:
1. Use arrow keys (or j/k) to navigate between options
2. Press Space to toggle selection (for multi-select)
3. Press Enter to confirm selection
4. Press 'q' to cancel/return to previous step
5. Press Ctrl+C to cancel the entire process at any time

Note: In single selection mode (chip selection), pressing Space will toggle the
      current item (select/deselect). In multiple selection mode, Space toggles
      individual items without affecting others.
"""

import os
import sys
import yaml
import re
from pathlib import Path
from typing import Dict, List, Set, Optional, Tuple, Any

# Add current directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from generators.utils.logger import setup_logger, get_logger, LoggerMixin


class EscKeyPressed(Exception):
    """Exception raised when ESC key is pressed to go back to previous step"""
    def __init__(self, step_name: str = ''):
        self.step_name = step_name
        super().__init__(f'ESC key pressed at step: {step_name}')


class TerminalColors:
    """ANSI color codes for terminal output"""
    # Reset
    RESET = '\033[0m'

    # Regular colors
    BLACK = '\033[30m'
    RED = '\033[31m'
    GREEN = '\033[32m'
    YELLOW = '\033[33m'
    BLUE = '\033[34m'
    MAGENTA = '\033[35m'
    CYAN = '\033[36m'
    WHITE = '\033[37m'

    # Bright colors
    BRIGHT_BLACK = '\033[90m'
    BRIGHT_RED = '\033[91m'
    BRIGHT_GREEN = '\033[92m'
    BRIGHT_YELLOW = '\033[93m'
    BRIGHT_BLUE = '\033[94m'
    BRIGHT_MAGENTA = '\033[95m'
    BRIGHT_CYAN = '\033[96m'
    BRIGHT_WHITE = '\033[97m'

    # Background colors
    BG_BLACK = '\033[40m'
    BG_RED = '\033[41m'
    BG_GREEN = '\033[42m'
    BG_YELLOW = '\033[43m'
    BG_BLUE = '\033[44m'
    BG_MAGENTA = '\033[45m'
    BG_CYAN = '\033[46m'
    BG_WHITE = '\033[47m'

    # Styles
    BOLD = '\033[1m'
    DIM = '\033[2m'
    ITALIC = '\033[3m'
    UNDERLINE = '\033[4m'
    BLINK = '\033[5m'
    REVERSE = '\033[7m'
    HIDDEN = '\033[8m'

    @staticmethod
    def colorize(text: str, color_code: str) -> str:
        """Apply color to text"""
        return f'{color_code}{text}{TerminalColors.RESET}'

    @staticmethod
    def supports_color() -> bool:
        """Check if terminal supports color"""
        # Check if we're in a terminal that supports color
        if os.name == 'nt':
            # Windows - check for ANSI support (Windows 10+)
            try:
                import ctypes
                kernel32 = ctypes.windll.kernel32
                # Check if console supports virtual terminal sequences
                ENABLE_VIRTUAL_TERMINAL_PROCESSING = 0x0004
                handle = kernel32.GetStdHandle(-11)  # STD_OUTPUT_HANDLE
                mode = ctypes.c_uint32()
                if kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
                    return (mode.value & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0
            except:
                pass
            return False
        else:
            # Unix-like systems - check if stdout is a tty
            import sys
            return sys.stdout.isatty()


class TerminalCheckboxSelector:
    """Advanced terminal checkbox selector with arrow key navigation and spacebar toggling"""

    def __init__(self, title: str, options: List[str], multi_select: bool = True,
                 current_selection_status: Optional[str] = None,
                 preselected: Optional[List[str]] = None):
        self.title = title
        self.options = options
        self.multi_select = multi_select
        self.selected = [False] * len(options)
        self.cursor_pos = 0
        self.use_raw_input = self._check_terminal_support()
        self.current_selection_status = current_selection_status

        # Apply preselected items if provided
        if preselected:
            for i, option in enumerate(self.options):
                if option in preselected:
                    self.selected[i] = True

            # For single select mode, ensure only one item is selected
            if not self.multi_select and sum(self.selected) > 1:
                # Keep only the first preselected item
                first_selected_index = next(i for i, s in enumerate(self.selected) if s)
                for i in range(len(self.selected)):
                    self.selected[i] = (i == first_selected_index)

    def _check_terminal_support(self):
        """Check if terminal supports raw input mode"""
        try:
            import termios
            import tty
            # Test if we can get terminal settings
            fd = sys.stdin.fileno()
            termios.tcgetattr(fd)
            return True
        except (ImportError, AttributeError, OSError):
            return False

    def display(self):
        """Display the checkbox menu with enhanced UI"""
        os.system('clear' if os.name == 'posix' else 'cls')

        # Check if terminal supports color
        use_color = TerminalColors.supports_color()

        # Define color functions
        def c(text, color_code):
            return TerminalColors.colorize(text, color_code) if use_color else text

        # Determine if this is a missing peripherals warning
        is_missing_warning = 'MISSING' in self.title.upper() or '⚠️' in self.title

        # Choose colors based on context
        if is_missing_warning:
            border_color = TerminalColors.BRIGHT_RED
            title_color = TerminalColors.BOLD + TerminalColors.RED
            instructions_color = TerminalColors.YELLOW + TerminalColors.BOLD
            warning_prefix = '⚠️  '
        else:
            border_color = TerminalColors.BRIGHT_BLUE
            title_color = TerminalColors.BOLD + TerminalColors.CYAN
            instructions_color = TerminalColors.GREEN + TerminalColors.BOLD
            warning_prefix = ''

        # Title with border
        title_width = min(70, max(60, len(self.title) + 10))
        border_top = '┌' + '─' * (title_width - 2) + '┐'
        border_bottom = '└' + '─' * (title_width - 2) + '┘'

        print(c('\n' + border_top, border_color))
        title_padding = (title_width - len(self.title) - 2) // 2
        title_line = '│' + ' ' * title_padding + c(self.title, title_color) + ' ' * (title_width - len(self.title) - title_padding - 2) + '│'
        print(title_line)
        print(c(border_bottom, border_color))

        # Show overall current selection status if provided
        if self.current_selection_status:
            print(c('\n📋 Preview', TerminalColors.YELLOW + TerminalColors.BOLD))
            for line in self.current_selection_status.split('\n'):
                print(f'   {line}')

        # Instructions section
        print(c('\n📖 Instructions:', instructions_color))

        if self.use_raw_input:
            print(f'   {c("↑/↓", TerminalColors.BOLD)} : Navigate')
            print(f'   {c("Space", TerminalColors.BOLD)} : Toggle selection')
            print(f'   {c("Enter", TerminalColors.BOLD)} : Confirm selection')
            print(f'   {c("q", TerminalColors.BOLD)} : Cancel/Go back')
        else:
            print(f'   {c("j/k", TerminalColors.BOLD)} : Navigate up/down')
            print(f'   {c("Space", TerminalColors.BOLD)} : Toggle selection')
            print(f'   {c("Enter", TerminalColors.BOLD)} : Confirm selection')
            print(f'   {c("q", TerminalColors.BOLD)} : Cancel/Go back')

        print(f'   {c("Ctrl+C", TerminalColors.BOLD)} : Cancel')

        # Add selection mode hint
        if not self.multi_select:
            print(f'   {c("Please select one chip", TerminalColors.BOLD)}')
        else:
            print(f'   {c("Please select multiple items", TerminalColors.BOLD)}')

        # Add special warning for missing peripherals
        if is_missing_warning:
            print(f'\n{c("🚨 IMPORTANT", TerminalColors.BOLD + TerminalColors.BRIGHT_RED + TerminalColors.BLINK)}')
            print(f'   {c("Select the missing peripherals from the list below to satisfy device dependencies.", TerminalColors.RED)}')
            print(f'   {c("Devices require these peripherals to function properly.", TerminalColors.RED)}')

        # Options list
        print(c('\n📝 Options:', TerminalColors.MAGENTA + TerminalColors.BOLD))

        for i, option in enumerate(self.options):
            # Determine checkbox symbol
            if self.selected[i]:
                checkbox = c('✓', TerminalColors.GREEN)
                prefix = c(f'[{checkbox}]', TerminalColors.GREEN)
            else:
                checkbox = ' '
                prefix = c(f'[{checkbox}]', TerminalColors.BRIGHT_BLACK)

            # Determine cursor
            if i == self.cursor_pos:
                cursor = c('▶', TerminalColors.BRIGHT_CYAN)
                option_text = c(option, TerminalColors.BOLD)
            else:
                cursor = ' '
                option_text = option

            # Display line
            line_num = c(f'{i+1:2d}.', TerminalColors.BRIGHT_BLACK)
            print(f' {cursor} {line_num} {prefix} {option_text}')

        # Footer with selection count and specific selected items
        selected_count = sum(self.selected)
        if selected_count > 0:
            # Get selected items
            selected_items = [self.options[i] for i in range(len(self.options)) if self.selected[i]]
            selected_str = ', '.join(selected_items)
            print(c(f'\n   Selected item(s): {selected_str}', TerminalColors.GREEN))
        else:
            print(c(f'\n   No items selected', TerminalColors.YELLOW))

    def get_key(self):
        """Get a single keypress without waiting for Enter"""
        # First, try Windows-specific method
        if os.name == 'nt':
            try:
                import msvcrt
                ch = msvcrt.getch()
                # msvcrt.getch() returns bytes on Python 3
                if isinstance(ch, bytes):
                    try:
                        return ch.decode('utf-8', errors='ignore')
                    except UnicodeDecodeError:
                        return ''
                return ch
            except ImportError:
                pass

        # For Unix-like systems or if Windows method failed
        if self.use_raw_input:
            # Try raw terminal input (Unix)
            try:
                import termios
                import tty
                fd = sys.stdin.fileno()
                old_settings = termios.tcgetattr(fd)
                try:
                    tty.setraw(fd)
                    ch = sys.stdin.read(1)
                finally:
                    termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
                return ch
            except (ImportError, AttributeError, OSError):
                self.use_raw_input = False  # Disable raw input for future calls

        # Fallback method for systems without raw input support
        # This includes Windows without msvcrt or Unix without termios
        try:
            import select
            # Use select to check if input is available with timeout
            if select.select([sys.stdin], [], [], 0.1)[0]:
                ch = sys.stdin.read(1)
                return ch
            return ''
        except (ImportError, AttributeError):
            # Ultimate fallback: use input() with timeout (not ideal)
            # This will wait for Enter, so not ideal for arrow keys
            import time
            print("\n⚠️  Terminal doesn't support raw input. Using fallback mode.")
            print('   Press Enter after each key.')
            try:
                # Try to get a single character with timeout
                import threading
                result = []

                def get_input():
                    try:
                        # Read a single character (might need Enter)
                        line = input()
                        if line:
                            result.append(line[0])
                    except:
                        pass

                thread = threading.Thread(target=get_input)
                thread.daemon = True
                thread.start()
                thread.join(timeout=0.5)

                if result:
                    return result[0]
                return ''
            except:
                return ''

    def run(self):
        """Run the interactive selection"""
        while True:
            self.display()
            key = self.get_key()

            if not key:
                continue

            if key == '\x03':  # Ctrl+C
                raise KeyboardInterrupt()
            elif key == '\r' or key == '\n':  # Enter
                break
            elif key == 'q' or key == 'Q':  # 'q' key to go back
                # 'q' key - raise exception to go back
                # Clear screen and show going back message with enhanced UI
                os.system('clear' if os.name == 'posix' else 'cls')

                # Check if terminal supports color
                use_color = TerminalColors.supports_color()

                # Define color functions
                def c(text, color_code):
                    return TerminalColors.colorize(text, color_code) if use_color else text

                # Title with border
                title_width = min(70, max(60, len(self.title) + 10))
                border_top = '┌' + '─' * (title_width - 2) + '┐'
                border_bottom = '└' + '─' * (title_width - 2) + '┘'

                print(c('\n' + border_top, TerminalColors.YELLOW))
                title_padding = (title_width - len(self.title) - 2) // 2
                title_line = '│' + ' ' * title_padding + c(self.title, TerminalColors.BOLD + TerminalColors.YELLOW) + ' ' * (title_width - len(self.title) - title_padding - 2) + '│'
                print(title_line)
                print(c(border_bottom, TerminalColors.YELLOW))

                # Going back message
                print(c('\n⏪ Going back (q pressed)', TerminalColors.YELLOW))
                print(c('─' * title_width, TerminalColors.BRIGHT_BLACK))

                raise EscKeyPressed(self.title)
            elif key == '\x1b':  # Escape key - handle arrow keys only
                # For ESC key, we need to check if it's part of an arrow key sequence
                # Arrow keys typically send ESC + '[' + 'A'/'B'/'C'/'D'
                # We'll try to read the next character with a short timeout
                import time
                start_time = time.time()
                next_key = ''

                # Try to read next character with a very short timeout
                # Use a longer timeout to ensure we catch arrow key sequences
                while time.time() - start_time < 0.05:  # 50ms timeout (increased)
                    next_key = self.get_key()
                    if next_key:
                        break

                if next_key == '[':
                    # This is likely an arrow key sequence, read the arrow character
                    arrow_start = time.time()
                    arrow_key = ''
                    while time.time() - arrow_start < 0.05:  # 50ms timeout (increased)
                        arrow_key = self.get_key()
                        if arrow_key:
                            break

                    if arrow_key == 'A':  # Up arrow
                        self.cursor_pos = (self.cursor_pos - 1) % len(self.options)
                    elif arrow_key == 'B':  # Down arrow
                        self.cursor_pos = (self.cursor_pos + 1) % len(self.options)
                    # Ignore other arrow keys (left/right)
                    # Continue the loop to show updated display
                    continue
                # If ESC key is not part of arrow sequence, ignore it
                # (we use 'q' for going back now)
            elif key == ' ':
                if self.multi_select:
                    self.selected[self.cursor_pos] = not self.selected[self.cursor_pos]
                else:
                    # For single select, toggle current item
                    # Allow deselecting even if it's the only selected item
                    current_selected = self.selected[self.cursor_pos]

                    if current_selected:
                        # Deselect this item
                        self.selected[self.cursor_pos] = False
                    else:
                        # Currently not selected, select it and unselect others
                        for i in range(len(self.selected)):
                            self.selected[i] = (i == self.cursor_pos)
            elif key in ('k', 'K'):  # 'k' for up (vim-style)
                self.cursor_pos = (self.cursor_pos - 1) % len(self.options)
            elif key in ('j', 'J'):  # 'j' for down (vim-style)
                self.cursor_pos = (self.cursor_pos + 1) % len(self.options)

        # For single select, if nothing is selected, select the cursor position
        # This ensures at least one item is selected when user presses Enter
        if not self.multi_select:
            selected_count = sum(self.selected)
            if selected_count == 0:
                # No item selected, select the cursor position
                self.selected[self.cursor_pos] = True
            elif selected_count > 1:
                # More than one selected (shouldn't happen in single-select mode)
                # Keep only the first selected item
                first_selected = next(i for i, s in enumerate(self.selected) if s)
                for i in range(len(self.selected)):
                    self.selected[i] = (i == first_selected)

        selected_items = [self.options[i] for i in range(len(self.options)) if self.selected[i]]

        # Clear screen and show selection result with enhanced UI
        os.system('clear' if os.name == 'posix' else 'cls')

        return selected_items

class BoardCreator(LoggerMixin):
    """Interactive board creation tool"""

    def __init__(self, script_dir: Path):
        super().__init__()
        self.script_dir = script_dir

        # Determine root directory
        idf_extra_actions_path = os.environ.get('IDF_EXTRA_ACTIONS_PATH')
        if idf_extra_actions_path:
            self.root_dir = Path(idf_extra_actions_path)
        else:
            self.root_dir = script_dir

        self.boards_dir = self.root_dir / 'boards'
        self.peripherals_dir = self.root_dir / 'peripherals'
        self.devices_dir = self.root_dir / 'devices'

        # Initialize peripheral list and configuration cache
        self.peripheral_list = []  # List of peripheral type strings (e.g., ["i2c", "i2s_master_std-out"])
        self.peripheral_config_cache = {}  # Maps peripheral_type_str -> YAML block content

        # Initialize device list and configuration cache
        self.device_list = []  # List of device type strings (e.g., ["button_gpio", "button_adc_multi"])
        self.device_config_cache = {}  # Maps device_type_str -> YAML block content

        # Initialize device peripheral dependencies cache
        self.device_peripheral_deps = {}  # Maps device_type_str -> List[Dict] of peripheral dependencies

        # Initialize caps macro mapping cache
        self.soc_requirements_mapping = {'devices': {}, 'peripherals': {}}

        # Check terminal compatibility once at initialization
        try:
            self.terminal_supports_advanced = self._check_terminal_support()
        except Exception as e:
            self.logger.debug(f'Terminal check failed: {e}')
            self.terminal_supports_advanced = False

        self._load_peripheral_configurations()
        self._load_device_configurations()

        # Load caps macro mapping
        self._load_soc_requirements_mapping()

        # Analyze peripheral list to extract valid type, role, format combinations
        self._analyze_peripheral_list()
        self._extract_device_peripheral_dependencies()

    def _check_terminal_support(self):
        """Check if terminal supports raw input mode (same logic as TerminalCheckboxSelector)"""
        if os.environ.get('MCP_SERVICE') or not sys.stdin.isatty():
            return False
        try:
            import termios
            import tty
            # Test if we can get terminal settings
            fd = sys.stdin.fileno()
            termios.tcgetattr(fd)
            return True
        except (ImportError, AttributeError, OSError):
            return False

    def _load_device_configurations(self) -> None:
        """Load all device configurations from YAML files and cache them"""
        if not self.devices_dir.exists():
            self.logger.warning(f'Devices directory not found: {self.devices_dir}')
            return

        # Deprecated device types to exclude
        excluded_devices = {'dev_fatfs_sdcard_spi', 'dev_fatfs_sdcard', 'dev_display_lcd_spi'}

        # Use temporary sets to avoid duplicates
        device_set = set()
        temp_device_list = []
        temp_device_config_cache = {}

        for device_dir in sorted(self.devices_dir.iterdir()):
            if not device_dir.is_dir():
                continue
            if not device_dir.name.startswith('dev_'):
                continue

            # Skip excluded device types
            if device_dir.name in excluded_devices:
                self.logger.debug(f'Skipping excluded device type: {device_dir.name}')
                continue

            # Read device YAML file
            device_yaml_path = device_dir / f'{device_dir.name}.yaml'
            if not device_yaml_path.exists():
                continue

            try:
                with open(device_yaml_path, 'r', encoding='utf-8') as f:
                    content = f.read()
                    data = yaml.safe_load(content)
                    if not data:
                        continue

                    # Handle list format (most common)
                    if isinstance(data, list):
                        for entry in data:
                            if not isinstance(entry, dict):
                                continue

                            device_type = entry.get('type', '')
                            sub_type = entry.get('sub_type', '')

                            if device_type:
                                # Create device type string
                                if sub_type:
                                    device_type_str = f'{device_type}_{sub_type}'
                                else:
                                    device_type_str = device_type

                                # Check for duplicates
                                if device_type_str in device_set:
                                    self.logger.debug(f'Skipping duplicate device type: {device_type_str}')
                                    continue

                                device_set.add(device_type_str)

                                # Extract the YAML block for this device
                                extracted_block = self._extract_device_block_from_yaml(content, device_type, sub_type)
                                if extracted_block:
                                    temp_device_list.append(device_type_str)
                                    temp_device_config_cache[device_type_str] = extracted_block
                                    self.logger.debug(f'Loaded device configuration: {device_type_str}')
                    # Handle dict format (less common)
                    elif isinstance(data, dict):
                        device_type = data.get('type', '')
                        sub_type = data.get('sub_type', '')
                        if device_type:
                            if sub_type:
                                device_type_str = f'{device_type}_{sub_type}'
                            else:
                                device_type_str = device_type

                            # Check for duplicates
                            if device_type_str in device_set:
                                self.logger.debug(f'Skipping duplicate device type: {device_type_str}')
                                continue

                            device_set.add(device_type_str)

                            # For dict format, we need to convert it to YAML string
                            extracted_block = yaml.dump([data], default_flow_style=False, sort_keys=False)
                            temp_device_list.append(device_type_str)
                            temp_device_config_cache[device_type_str] = extracted_block
                            self.logger.debug(f'Loaded device configuration: {device_type_str}')
            except Exception as e:
                self.logger.debug(f'Error reading device YAML {device_yaml_path}: {e}')
                continue

        # Sort device list for consistent ordering
        temp_device_list.sort()
        self.device_list = temp_device_list
        self.device_config_cache = temp_device_config_cache
        self.logger.info(f'Loaded {len(self.device_list)} unique device configurations')

    def _parse_device_type(self, device_type_str: str) -> Tuple[str, Optional[str]]:
        """Parse device type string to extract type and sub_type

        Args:
            device_type_str: Device type string, either "type" or "type_sub-type"

        Returns:
            Tuple of (type, sub_type) where sub_type may be None

        Note: The folder format is dev_type, so we need to check if the folder exists.
        If device_type_str is "audio_codec", the folder should be "dev_audio_codec".
        If device_type_str is "fs_fat_sdmmc", it should be parsed as type="fs_fat", sub_type="sdmmc".
        """
        if '_' not in device_type_str:
            return device_type_str, None

        # Try to find the correct split point by checking folder existence
        # Strategy: Try splitting from the end, checking if folder exists
        parts = device_type_str.split('_')

        # Try all possible splits from the end
        # e.g., "fs_fat_sdmmc" -> try "fs_fat" + "sdmmc", "fs" + "fat_sdmmc", etc.
        for i in range(len(parts) - 1, 0, -1):
            potential_type = '_'.join(parts[:i])
            potential_sub_type = '_'.join(parts[i:])

            # Check if folder exists for this type
            potential_folder = self.devices_dir / f'dev_{potential_type}'
            if potential_folder.exists() and (potential_folder / f'dev_{potential_type}.yaml').exists():
                return potential_type, potential_sub_type if potential_sub_type else None

        # If no folder found, treat the entire string as type (no sub_type)
        # This handles cases where the type itself contains underscores (e.g., "audio_codec")
        return device_type_str, None

    def _extract_device_block_from_yaml(self, yaml_content: str, base_type: str, sub_type: Optional[str] = None) -> Optional[str]:
        """Extract matching device block from YAML content while preserving original formatting

        Args:
            yaml_content: Complete YAML file content as string
            base_type: Device base type (e.g., 'button')
            sub_type: Optional sub_type (e.g., 'adc_multi')

        Returns:
            Extracted YAML block as string, or None if not found
        """
        # Split content into lines
        lines = yaml_content.split('\n')

        # First, find the top-level indent (indent of first non-comment line starting with '-')
        top_level_indent = None
        for line in lines:
            stripped = line.lstrip()
            if stripped and not stripped.startswith('#') and stripped.startswith('-'):
                top_level_indent = len(line) - len(stripped)
                break

        if top_level_indent is None:
            # No top-level list found
            return None

        # Find all list entries (starting with '-' at top-level indent)
        entries = []
        current_entry = []
        in_entry = False
        entry_start = 0

        for i, line in enumerate(lines):
            stripped = line.lstrip()

            if not stripped:
                # Empty line - keep in current entry if we're in one
                if in_entry:
                    current_entry.append(line)
                continue

            current_indent = len(line) - len(stripped)

            # Check if this is a top-level list item
            is_top_level_item = (stripped.startswith('-') and current_indent == top_level_indent)

            if is_top_level_item:
                # Save previous entry if exists
                if current_entry and in_entry:
                    entries.append((entry_start, '\n'.join(current_entry)))

                # Start new entry
                in_entry = True
                entry_start = i
                current_entry = [line]
            elif in_entry:
                # This line belongs to current entry (could be nested with '-')
                current_entry.append(line)

        # Don't forget the last entry
        if current_entry and in_entry:
            entries.append((entry_start, '\n'.join(current_entry)))

        # Now search for matching entry
        for start_line, entry_content in entries:
            # Parse this entry to check type and sub_type
            try:
                entry_data = yaml.safe_load(entry_content)
                if not isinstance(entry_data, list):
                    entry_data = [entry_data]

                for entry in entry_data:
                    if not isinstance(entry, dict):
                        continue

                    entry_type = entry.get('type', '')
                    entry_sub_type = entry.get('sub_type', '')

                    # Check if this entry matches
                    if sub_type:
                        # Need to match both type and sub_type
                        if entry_type == base_type and entry_sub_type == sub_type:
                            # Filter out top-level comments before returning
                            return self._filter_top_level_comments(entry_content)
                    else:
                        # Match type only
                        if entry_type == base_type:
                            # Filter out top-level comments before returning
                            return self._filter_top_level_comments(entry_content)
            except yaml.YAMLError:
                # If YAML parsing fails, try regex matching
                type_pattern = rf'type:\s*{re.escape(base_type)}'
                if re.search(type_pattern, entry_content, re.IGNORECASE):
                    if sub_type:
                        sub_type_pattern = rf'sub_type:\s*{re.escape(sub_type)}'
                        if re.search(sub_type_pattern, entry_content, re.IGNORECASE):
                            # Filter out top-level comments before returning
                            return self._filter_top_level_comments(entry_content)
                    else:
                        # Check that there's no sub_type field (or it's empty)
                        sub_type_pattern = r'sub_type:\s*\S'
                        if not re.search(sub_type_pattern, entry_content, re.IGNORECASE):
                            # Filter out top-level comments before returning
                            return self._filter_top_level_comments(entry_content)

        return None

    def _filter_top_level_comments(self, content: str) -> str:
        """Filter out top-level comments (comments with zero indentation)

        Args:
            content: The content to filter

        Returns:
            Filtered content with top-level comments removed
        """
        lines = content.split('\n')
        filtered_lines = []

        for line in lines:
            stripped = line.lstrip()

            # Check if this is a top-level comment (starts with '#' and has zero indentation)
            if stripped.startswith('#') and line.startswith('#'):
                # This is a top-level comment - skip it
                continue

            # Keep all other lines
            filtered_lines.append(line)

        # Join filtered lines back
        filtered_content = '\n'.join(filtered_lines)

        # Remove leading empty lines
        filtered_lines = filtered_content.split('\n')
        while filtered_lines and not filtered_lines[0].strip():
            filtered_lines.pop(0)

        return '\n'.join(filtered_lines)

    def get_available_devices(self) -> List[str]:
        """Get list of available device types from cached device list"""
        return self.device_list.copy()

    def get_device_config(self, device_type_str: str) -> Optional[str]:
        """Get cached YAML configuration for a device type

        Args:
            device_type_str: Device type string (e.g., "button_gpio", "button_adc_multi")

        Returns:
            YAML block content or None if not found
        """
        return self.device_config_cache.get(device_type_str)

    def _load_soc_requirements_mapping(self) -> None:
        """Load and parse esp_board_soc_requirements.yml to create device/peripheral to macro mapping"""
        soc_requirements_path = self.script_dir / 'private_inc/esp_board_soc_requirements.yml'
        if not soc_requirements_path.exists():
            self.logger.warning(f'esp_board_soc_requirements.yml not found at {soc_requirements_path}')
            self.soc_requirements_mapping = {'devices': {}, 'peripherals': {}}
            return

        try:
            with open(soc_requirements_path, 'r', encoding='utf-8') as f:
                data = yaml.safe_load(f)
                if not data:
                    self.soc_requirements_mapping = {'devices': {}, 'peripherals': {}}
                    return

                # Initialize mapping dictionaries
                self.soc_requirements_mapping = {
                    'devices': {},
                    'peripherals': {}
                }

                # Parse devices section
                if 'devices' in data and isinstance(data['devices'], list):
                    for item in data['devices']:
                        if isinstance(item, dict):
                            for device_name, macro_str in item.items():
                                # Split comma-separated macros and strip whitespace
                                macros = [m.strip() for m in macro_str.split(',')]
                                self.soc_requirements_mapping['devices'][device_name] = macros
                                self.logger.debug(f'Device {device_name} requires macros: {macros}')

                # Parse peripherals section
                if 'peripherals' in data and isinstance(data['peripherals'], list):
                    for item in data['peripherals']:
                        if isinstance(item, dict):
                            for periph_name, macro_str in item.items():
                                # Split comma-separated macros and strip whitespace
                                macros = [m.strip() for m in macro_str.split(',')]
                                self.soc_requirements_mapping['peripherals'][periph_name] = macros
                                self.logger.debug(f'Peripheral {periph_name} requires macros: {macros}')

                self.logger.info(f'Loaded soc_requirements mapping: {len(self.soc_requirements_mapping["devices"])} devices, '
                               f'{len(self.soc_requirements_mapping["peripherals"])} peripherals')

        except Exception as e:
            self.logger.error(f'Error loading esp_board_soc_requirements.yml: {e}')
            self.soc_requirements_mapping = {'devices': {}, 'peripherals': {}}

    def _get_soc_caps(self, chip: str) -> Set[str]:
        """Get all SOC capability macros for a specific chip

        Args:
            chip: Chip name (e.g., 'esp32', 'esp32c3')

        Returns:
            Set of all defined SOC capability macros
        """
        # Get IDF_PATH environment variable
        idf_path = os.environ.get('IDF_PATH')
        if not idf_path:
            self.logger.warning('IDF_PATH environment variable not set, cannot check SOC capabilities')
            return set()

        # Build path to soc_caps.h file
        soc_caps_path = Path(idf_path) / 'components' / 'soc' / chip / 'include' / 'soc' / 'soc_caps.h'

        if not soc_caps_path.exists():
            # Try alternative path pattern (some chips might have different structure)
            alt_path = Path(idf_path) / 'components' / 'soc' / chip / 'include' / 'soc_caps.h'
            if alt_path.exists():
                soc_caps_path = alt_path
            else:
                self.logger.warning(f'soc_caps.h not found for chip {chip} at {soc_caps_path}')
                return set()

        macros = set()
        try:
            with open(soc_caps_path, 'r', encoding='utf-8') as f:
                content = f.read()

                # Parse C preprocessor macros using regex
                # Look for patterns like #define MACRO_NAME 1 or #define MACRO_NAME value
                # Also handle multi-line definitions and comments
                lines = content.split('\n')
                in_multiline_comment = False

                for line in lines:
                    # Handle multi-line comments
                    stripped_line = line.strip()

                    # Skip empty lines
                    if not stripped_line:
                        continue

                    # Handle C comments
                    if '/*' in stripped_line and '*/' in stripped_line:
                        # Single line comment, remove it
                        stripped_line = stripped_line.split('/*')[0].strip()
                    elif '/*' in stripped_line:
                        in_multiline_comment = True
                        stripped_line = stripped_line.split('/*')[0].strip()
                    elif '*/' in stripped_line:
                        in_multiline_comment = False
                        stripped_line = stripped_line.split('*/')[1].strip()
                    elif in_multiline_comment:
                        continue

                    # Remove inline comments
                    if '//' in stripped_line:
                        stripped_line = stripped_line.split('//')[0].strip()

                    # Check for #define statements
                    if stripped_line.startswith('#define '):
                        # Extract macro name (skip #define and get first token)
                        parts = stripped_line.split()
                        if len(parts) >= 2:
                            macro_name = parts[1]
                            # Skip function-like macros (those with parentheses)
                            if '(' not in macro_name:
                                macros.add(macro_name)
                                self.logger.debug(f'Found SOC macro: {macro_name}')

            self.logger.info(f'Loaded {len(macros)} SOC capability macros for chip {chip}')
            return macros

        except Exception as e:
            self.logger.error(f'Error reading soc_caps.h for chip {chip}: {e}')
            return set()

    def filter_by_chip_capability(self, chip: str, items: List[str], item_type: str = 'devices') -> List[str]:
        """Filter devices or peripherals based on chip capability

        Args:
            chip: Chip name (e.g., 'esp32', 'esp32c3')
            items: List of device or peripheral names to filter
            item_type: Type of items - "devices" or "peripherals"

        Returns:
            Filtered list of items that are supported by the chip
        """
        if not items:
            return []

        # Ensure soc_requirements_mapping is loaded
        if not hasattr(self, 'soc_requirements_mapping'):
            self._load_soc_requirements_mapping()

        # Get SOC capability macros for the chip
        soc_macros = self._get_soc_caps(chip)
        if not soc_macros:
            self.logger.warning(f'No SOC macros found for chip {chip}, returning all items')
            return items.copy()

        # Get the appropriate mapping based on item_type
        if item_type not in self.soc_requirements_mapping:
            self.logger.error(f'Invalid item_type: {item_type}. Must be "devices" or "peripherals"')
            return items.copy()

        mapping = self.soc_requirements_mapping[item_type]

        filtered_items = []
        for item in items:
            # Check if item has macro requirements in esp_board_soc_requirements.yml
            if item in mapping:
                required_macros = mapping[item]
                # Check if all required macros are defined in soc_caps.h
                supported = all(macro in soc_macros for macro in required_macros)
                if supported:
                    filtered_items.append(item)
                    self.logger.debug(f'Item {item} is supported by chip {chip}')
                else:
                    missing_macros = [macro for macro in required_macros if macro not in soc_macros]
                    self.logger.debug(f'Item {item} is NOT supported by chip {chip}. Missing macros: {missing_macros}')
            else:
                # If item is not in esp_board_soc_requirements.yml, assume it's supported by all chips
                filtered_items.append(item)
                self.logger.debug(f'Item {item} has no macro requirements, assuming supported by chip {chip}')

        self.logger.info(f'Filtered {len(items)} {item_type} to {len(filtered_items)} supported by chip {chip}')
        return filtered_items

    def _extract_device_peripheral_dependencies(self) -> None:
        """Extract peripheral dependencies from all loaded device configurations"""
        self.logger.info('Extracting peripheral dependencies from device configurations...')

        for device_type_str in self.device_list:
            if device_type_str.startswith('custom'):
                continue
            config = self.get_device_config(device_type_str)
            if not config:
                continue

            try:
                # Parse the YAML configuration
                data = yaml.safe_load(config)
                if not data:
                    continue
                # Handle both list and dict formats
                if isinstance(data, list):
                    for entry in data:
                        self._extract_peripheral_deps_from_entry(device_type_str, entry)
                elif isinstance(data, dict):
                    self._extract_peripheral_deps_from_entry(device_type_str, data)

            except Exception as e:
                self.logger.debug(f'Error extracting peripheral dependencies for {device_type_str}: {e}')
                continue

        self.logger.info(f'Extracted peripheral dependencies for {len(self.device_peripheral_deps)} devices')

    def _extract_peripheral_deps_from_entry(self, device_type_str: str, entry: Dict[str, Any]) -> None:
        """Extract peripheral dependencies from a single device configuration entry"""
        if not isinstance(entry, dict):
            return

        # Check if this entry has peripherals field
        peripherals = entry.get('peripherals')
        if not peripherals:
            return

        # Initialize dependencies list for this device type
        if device_type_str not in self.device_peripheral_deps:
            self.device_peripheral_deps[device_type_str] = []

        # Process each peripheral dependency
        if isinstance(peripherals, list):
            for periph_entry in peripherals:
                if isinstance(periph_entry, dict):
                    # Extract peripheral name and any extra parameters
                    periph_name = periph_entry.get('name', '')
                    if periph_name:
                        # Parse peripheral type, role, format, and instance from name
                        periph_type, role, format_str, instance = self._parse_peripheral_name(periph_name)

                        dep_entry = {
                            'periph_type': periph_type,
                            'role': role,
                            'format': format_str,
                            'instance': instance,
                        }

                        self.device_peripheral_deps[device_type_str].append(dep_entry)
                        self.logger.debug(f'Device {device_type_str} depends on peripheral: {periph_name} '
                                        f'(type: {periph_type}, role: {role}, format: {format_str}, instance: {instance})')

        elif isinstance(peripherals, dict):
            # Handle dict format (less common)
            periph_name = peripherals.get('name', '')
            if periph_name:
                # Parse peripheral type, role, format, and instance from name
                periph_type, role, format_str, instance = self._parse_peripheral_name(periph_name)

                dep_entry = {
                    'periph_type': periph_type,
                    'role': role,
                    'format': format_str,
                    'instance': instance,
                }

                self.device_peripheral_deps[device_type_str].append(dep_entry)
                self.logger.debug(f'Device {device_type_str} depends on peripheral: {periph_name} '
                                    f'(type: {periph_type}, role: {role}, format: {format_str}, instance: {instance})')

    def _analyze_peripheral_list(self) -> None:
        """Analyze peripheral list to extract valid type, role, format combinations

        This method analyzes the peripheral_list to understand the valid naming patterns
        and creates a mapping for better matching.
        """
        self.peripheral_patterns = {
            'types': set(),
            'roles': set(),
            'formats': set(),
        }

        for periph_str in self.peripheral_list:
            # Parse the peripheral string using existing method
            periph_type, role, format_str = self._parse_peripheral_type(periph_str)

            # Store the parsed components
            if periph_type:
                self.peripheral_patterns['types'].add(periph_type)

            if role:
                self.peripheral_patterns['roles'].add(role)

            if format_str:
                self.peripheral_patterns['formats'].add(format_str)

        self.logger.debug(f"Analyzed peripheral patterns: {len(self.peripheral_patterns['types'])} types, "
                         f"{len(self.peripheral_patterns['roles'])} roles, "
                         f"{len(self.peripheral_patterns['formats'])} formats")

    def _parse_peripheral_name(self, periph_str: str) -> Tuple[str, Optional[str], Optional[str]]:
        """Parse peripheral string using known patterns from peripheral_list

        Args:
            periph_str: Peripheral string without instance number

        Returns:
            Tuple of (type, role, format)
        """
        # If the peripheral string is in the list, use it
        periph_type = None
        role = None
        format_str = None
        instance = None

        if periph_str in self.peripheral_list:
            instance = periph_str

        str_parts = periph_str.split('_')
        if str_parts[0] in self.peripheral_patterns.get('types', set()):
            periph_type = str_parts[0]

        if len(str_parts) > 1 and str_parts[1] in self.peripheral_patterns.get('roles', set()):
            role = str_parts[1]

        if len(str_parts) > 2 and str_parts[2] in self.peripheral_patterns.get('formats', set()):
            format_str = str_parts[2]

        return periph_type, role, format_str, instance

    def get_device_peripheral_dependencies(self, device_type_str: str) -> List[Dict[str, Any]]:
        """Get peripheral dependencies for a specific device type

        Args:
            device_type_str: Device type string (e.g., "button_gpio")

        Returns:
            List of peripheral dependency dictionaries, or empty list if no dependencies
        """
        return self.device_peripheral_deps.get(device_type_str, []).copy()


    def _find_matching_peripheral(self, periph_type: str, role: Optional[str] = None,
                                 format_str: Optional[str] = None,
                                 instance: Optional[str] = None) -> Optional[str]:
        """Find a matching peripheral type in peripheral_list based on type, role, format, and instance

        Args:
            periph_type: Peripheral type to match (e.g., "gpio", "i2s")
            role: Optional role (e.g., "master", "slave")
            format_str: Optional format (e.g., "std-out", "oneshot")
            instance: Optional instance name (e.g., "gpio", "i2c", "adc_oneshot")

        Returns:
            Matched peripheral type string, or None if not found
        """

        # Build potential matches based on available information
        potential_matches = []

        # 1. Try exact match with all components
        if periph_type and role and format_str:
            exact_match = f'{periph_type}_{role}_{format_str}'
            if exact_match in self.peripheral_list:
                return exact_match
            potential_matches.append(exact_match)

        # 2. Try match with type and role
        if periph_type and role:
            type_role_match = f'{periph_type}_{role}'
            if type_role_match in self.peripheral_list:
                return type_role_match
            potential_matches.append(type_role_match)

        # 3. Try match with type and format (if format exists)
        if periph_type and format_str:
            type_format_match = f'{periph_type}_{format_str}'
            if type_format_match in self.peripheral_list:
                return type_format_match
            potential_matches.append(type_format_match)

        # 4. Try match with just type
        if periph_type in self.peripheral_list:
            return periph_type
        potential_matches.append(periph_type)

        # 5. Try to find the best match from peripheral_list
        # Score each peripheral based on how well it matches
        best_match = None
        best_score = -1

        for periph in self.peripheral_list:
            score = 0

            # Parse the peripheral from the list
            list_type, list_role, list_format = self._parse_peripheral_type(periph)

            # Score based on type match
            if list_type == periph_type:
                score += 10
            elif periph_type in list_type or list_type in periph_type:
                score += 5

            # Score based on role match
            if role and list_role == role:
                score += 5
            elif role and list_role and role in list_role:
                score += 2

            # Score based on format match
            if format_str and list_format == format_str:
                score += 3
            elif format_str and list_format and format_str in list_format:
                score += 1

            # Update best match if this one scores higher
            if score > best_score:
                best_score = score
                best_match = periph

        # Return the best match if it has a positive score
        if best_score > 0:
            return best_match

        # 6. Fallback: try to match by type prefix
        for periph in self.peripheral_list:
            if periph.startswith(periph_type + '_'):
                return periph

        # 7. Last resort: return any peripheral that contains the type
        for periph in self.peripheral_list:
            if periph_type in periph:
                return periph

        return None

    def _load_peripheral_configurations(self) -> None:
        """Load all peripheral configurations from YAML files and cache them

        Similar to device loading, but extracts type, role, and format from each configuration block.
        """
        if not self.peripherals_dir.exists():
            self.logger.warning(f'Peripherals directory not found: {self.peripherals_dir}')
            return

        # Use temporary sets to avoid duplicates
        peripheral_set = set()
        temp_peripheral_list = []
        temp_peripheral_config_cache = {}

        for periph_dir in sorted(self.peripherals_dir.iterdir()):
            if not periph_dir.is_dir():
                continue
            if not periph_dir.name.startswith('periph_'):
                continue

            periph_type = periph_dir.name[7:]  # Remove 'periph_' prefix
            periph_yaml_path = periph_dir / f'periph_{periph_type}.yml'

            if not periph_yaml_path.exists():
                # If no YAML file, add just the type with default role
                periph_type_str = f'{periph_type}_master'
                if periph_type_str not in peripheral_set:
                    peripheral_set.add(periph_type_str)
                    temp_peripheral_list.append(periph_type_str)
                    # Create minimal configuration
                    minimal_config = f'{periph_type}-0:\n  role: master\n  config: {{}}'
                    temp_peripheral_config_cache[periph_type_str] = minimal_config
                continue

            try:
                with open(periph_yaml_path, 'r', encoding='utf-8') as f:
                    content = f.read()
                    data = yaml.safe_load(content)
                    if not data:
                        continue

                    # Handle list format (most common in periph yml files)
                    if isinstance(data, list):
                        for entry in data:
                            if not isinstance(entry, dict):
                                continue

                            entry_type = entry.get('type', '')
                            role = entry.get('role', '')
                            format_str = entry.get('format', '')

                            # Use directory name as fallback type if not specified
                            if not entry_type:
                                entry_type = periph_type

                            # Build peripheral type string
                            parts = [entry_type]
                            if role:
                                parts.append(role)
                            if format_str:
                                parts.append(format_str)

                            # Join with underscores
                            if len(parts) == 1:
                                periph_type_str = parts[0]
                            elif len(parts) == 2:
                                periph_type_str = f'{parts[0]}_{parts[1]}'
                            else:
                                periph_type_str = f'{parts[0]}_{parts[1]}_{parts[2]}'

                            # Check for duplicates
                            if periph_type_str in peripheral_set:
                                self.logger.debug(f'Skipping duplicate peripheral type: {periph_type_str}')
                                continue

                            peripheral_set.add(periph_type_str)

                            # Extract the YAML block for this peripheral
                            extracted_block = self._extract_peripheral_block_from_yaml_content(content, entry_type, role, format_str)
                            if extracted_block:
                                temp_peripheral_list.append(periph_type_str)
                                temp_peripheral_config_cache[periph_type_str] = extracted_block
                                self.logger.debug(f'Loaded peripheral configuration: {periph_type_str}')
                    # Handle dict format (less common)
                    elif isinstance(data, dict):
                        entry_type = data.get('type', periph_type)
                        role = data.get('role', '')
                        format_str = data.get('format', '')

                        # Build peripheral type string
                        parts = [entry_type]
                        if role:
                            parts.append(role)
                        if format_str:
                            parts.append(format_str)

                        # Join with underscores
                        if len(parts) == 1:
                            periph_type_str = parts[0]
                        elif len(parts) == 2:
                            periph_type_str = f'{parts[0]}_{parts[1]}'
                        else:
                            periph_type_str = f'{parts[0]}_{parts[1]}_{parts[2]}'

                        # Check for duplicates
                        if periph_type_str in peripheral_set:
                            self.logger.debug(f'Skipping duplicate peripheral type: {periph_type_str}')
                            continue

                        peripheral_set.add(periph_type_str)

                        # For dict format, we need to convert it to YAML string
                        extracted_block = yaml.dump([data], default_flow_style=False, sort_keys=False)
                        temp_peripheral_list.append(periph_type_str)
                        temp_peripheral_config_cache[periph_type_str] = extracted_block
                        self.logger.debug(f'Loaded peripheral configuration: {periph_type_str}')
            except Exception as e:
                self.logger.debug(f'Error reading peripheral YAML {periph_yaml_path}: {e}')
                continue

        # If no configurations were loaded, add at least the basic type
        if not temp_peripheral_list:
            # Try to add basic peripheral types
            for periph_dir in sorted(self.peripherals_dir.iterdir()):
                if not periph_dir.is_dir():
                    continue
                if not periph_dir.name.startswith('periph_'):
                    continue

                periph_type = periph_dir.name[7:]
                periph_type_str = f'{periph_type}_master'

                if periph_type_str not in peripheral_set:
                    peripheral_set.add(periph_type_str)
                    temp_peripheral_list.append(periph_type_str)
                    minimal_config = f'{periph_type}-0:\n  role: master\n  config: {{}}'
                    temp_peripheral_config_cache[periph_type_str] = minimal_config

        # Sort peripheral list for consistent ordering
        temp_peripheral_list.sort()
        self.peripheral_list = temp_peripheral_list
        self.peripheral_config_cache = temp_peripheral_config_cache
        self.logger.info(f'Loaded {len(self.peripheral_list)} unique peripheral configurations')

    def _extract_peripheral_block_from_yaml_content(self, yaml_content: str, base_type: str, role: str = '', format_str: str = '') -> Optional[str]:
        """Extract matching peripheral block from YAML content while preserving original formatting

        Args:
            yaml_content: Complete YAML file content as string
            base_type: Peripheral base type (e.g., 'i2s')
            role: Peripheral role (e.g., 'master')
            format_str: Peripheral format (e.g., 'std-out')

        Returns:
            Extracted YAML block as string, or None if not found
        """
        # Split content into lines
        lines = yaml_content.split('\n')

        # First, find the top-level indent (indent of first non-comment line starting with '-')
        top_level_indent = None
        for line in lines:
            stripped = line.lstrip()
            if stripped and not stripped.startswith('#') and stripped.startswith('-'):
                top_level_indent = len(line) - len(stripped)
                break

        if top_level_indent is None:
            # No top-level list found
            return None

        # Find all list entries (starting with '-' at top-level indent)
        entries = []
        current_entry = []
        in_entry = False
        entry_start = 0

        for i, line in enumerate(lines):
            stripped = line.lstrip()

            if not stripped:
                # Empty line - keep in current entry if we're in one
                if in_entry:
                    current_entry.append(line)
                continue

            current_indent = len(line) - len(stripped)

            # Check if this is a top-level list item
            is_top_level_item = (stripped.startswith('-') and current_indent == top_level_indent)

            if is_top_level_item:
                # Save previous entry if exists
                if current_entry and in_entry:
                    entries.append((entry_start, '\n'.join(current_entry)))

                # Start new entry
                in_entry = True
                entry_start = i
                current_entry = [line]
            elif in_entry:
                # This line belongs to current entry (could be nested with '-')
                current_entry.append(line)

        # Don't forget the last entry
        if current_entry and in_entry:
            entries.append((entry_start, '\n'.join(current_entry)))

        # Now search for matching entry
        for start_line, entry_content in entries:
            # Parse this entry to check type, role, and format
            try:
                entry_data = yaml.safe_load(entry_content)
                if not isinstance(entry_data, list):
                    entry_data = [entry_data]

                for entry in entry_data:
                    if not isinstance(entry, dict):
                        continue

                    entry_type = entry.get('type', '')
                    entry_role = entry.get('role', '')
                    entry_format = entry.get('format', '')

                    # Check if this entry matches
                    # First check type
                    if entry_type != base_type:
                        continue

                    # Check role if specified
                    if role and entry_role != role:
                        continue

                    # Check format if specified
                    if format_str and entry_format != format_str:
                        continue

                    # If no role specified but entry has role, still match (any role)
                    # If no format specified but entry has format, still match (any format)

                    # Filter out top-level comments before returning
                    return self._filter_top_level_comments(entry_content)
            except yaml.YAMLError:
                # If YAML parsing fails, try regex matching
                type_pattern = rf'type:\s*{re.escape(base_type)}'
                if not re.search(type_pattern, entry_content, re.IGNORECASE):
                    continue

                # Check role if specified
                if role:
                    role_pattern = rf'role:\s*{re.escape(role)}'
                    if not re.search(role_pattern, entry_content, re.IGNORECASE):
                        continue

                # Check format if specified
                if format_str:
                    format_pattern = rf'format:\s*{re.escape(format_str)}'
                    if not re.search(format_pattern, entry_content, re.IGNORECASE):
                        continue

                # Filter out top-level comments before returning
                return self._filter_top_level_comments(entry_content)

        return None


    def _parse_peripheral_type(self, peripheral_str: str) -> Tuple[str, Optional[str], Optional[str]]:
        """Parse peripheral string in format type_role_format to extract type, role, and format

        Args:
            peripheral_str: Peripheral string in format "type", "type_role", or "type_role_format"

        Returns:
            Tuple of (type, role, format) where role and format may be None
        """
        if '_' not in peripheral_str:
            return peripheral_str, None, None

        parts = peripheral_str.split('_')

        # Simple parsing: first part is always type
        periph_type = parts[0]

        # Try to determine role and format
        role = None
        format_str = None

        if len(parts) >= 2:
            role = parts[1]

        if len(parts) >= 3:
            format_str = parts[2]

        return periph_type, role, format_str

    def get_available_peripherals(self) -> List[str]:
        """Get list of available peripheral types from cached peripheral list"""
        return self.peripheral_list.copy()

    def get_peripheral_config(self, peripheral_type_str: str) -> Optional[str]:
        """Get cached YAML configuration for a peripheral type

        Args:
            peripheral_type_str: Peripheral type string (e.g., "i2c", "i2s_master_std-out")

        Returns:
            YAML block content or None if not found
        """
        return self.peripheral_config_cache.get(peripheral_type_str)

    def _get_selection_status_string(self, chip: Optional[str], devices: List[str], peripherals: List[str], manufacturer: Optional[str] = None) -> str:
        """Generate a string showing current selection status"""
        lines = []
        if chip:
            lines.append(f'Chip: {chip}')
        else:
            lines.append('Chip: None')

        if manufacturer:
            lines.append(f'Manufacturer: {manufacturer}')
        else:
            lines.append('Manufacturer: None')

        if devices:
            lines.append(f'Devices: {", ".join(devices)}')
        else:
            lines.append('Devices: None')

        if peripherals:
            lines.append(f'Peripherals: {", ".join(peripherals)}')
        else:
            lines.append('Peripherals: None')

        return '\n'.join(lines)

    def create_board_directory(self, board_name: str, create_path: Optional[Path] = None) -> Optional[Path]:
        """Create board directory and return its path

        Args:
            board_name: Name of the board
            create_path: Path where to create the board directory. If None, uses current working directory.

        Returns:
            Path to the created board directory, or None if user chose not to overwrite
        """
        if create_path is None:
            # Use current working directory if no path specified
            create_path = Path.cwd()

        board_dir = create_path / board_name
        if board_dir.exists():
            # Prompt user for overwrite confirmation
            print(f'\n⚠️  Board directory already exists: {board_dir}')
            while True:
                try:
                    choice = input('Do you want to overwrite it? (y/n): ').strip().lower()
                    if choice == 'y':
                        # Remove existing directory and recreate
                        import shutil
                        shutil.rmtree(board_dir)
                        print(f'✅ Removed existing directory: {board_dir}')
                        break
                    elif choice == 'n':
                        print('❌ Operation cancelled by user')
                        return None
                    else:
                        print('❌ Invalid input. Please enter "y" to overwrite or "n" to cancel.')
                except KeyboardInterrupt:
                    print('\n❌ Operation cancelled')
                    return None

        board_dir.mkdir(parents=True, exist_ok=True)
        return board_dir

    def create_kconfig_file(self, board_dir: Path, board_name: str, chip: str) -> None:
        """Create Kconfig file for the board"""
        kconfig_path = board_dir / 'Kconfig'
        board_macro = board_name.upper().replace('-', '_')
        chip_macro = chip.upper().replace('-', '_')

        kconfig_content = f'config BOARD_{board_macro}\n'
        kconfig_content += f'    bool\n'
        kconfig_content += f'    depends on SOC_{chip_macro}\n'

        with open(kconfig_path, 'w', encoding='utf-8') as f:
            f.write(kconfig_content)

    def interactive_input_manufacturer(self, chip: Optional[str] = None,
                                      selected_devices: Optional[List[str]] = None,
                                      selected_peripherals: Optional[List[str]] = None,
                                      current_manufacturer: Optional[str] = None) -> str:
        """Interactive manufacturer input

        Args:
            chip: Selected chip (optional, for display purposes)
            selected_devices: Selected devices (optional, for display purposes)
            selected_peripherals: Selected peripherals (optional, for display purposes)
            current_manufacturer: Current manufacturer value to display in input prompt (optional)

        Returns:
            Manufacturer string, default is 'ESPRESSIF' if user doesn't input anything
        """
        # Prepare current selection status
        devices = selected_devices or []
        peripherals = selected_peripherals or []

        # Check if terminal supports color
        use_color = TerminalColors.supports_color()

        # Define color functions
        def c(text, color_code):
            return TerminalColors.colorize(text, color_code) if use_color else text

        # Title with border
        if self.terminal_supports_advanced:
            title = 'Manufacturer Information Input'
            title_width = min(70, max(60, len(title) + 10))
            border_top = '┌' + '─' * (title_width - 2) + '┐'
            border_bottom = '└' + '─' * (title_width - 2) + '┘'

            print(c('\n' + border_top, TerminalColors.BRIGHT_BLUE))
            title_padding = (title_width - len(title) - 2) // 2
            title_line = '│' + ' ' * title_padding + c(title, TerminalColors.BOLD + TerminalColors.CYAN) + ' ' * (title_width - len(title) - title_padding - 2) + '│'
            print(title_line)
            print(c(border_bottom, TerminalColors.BRIGHT_BLUE))

            current_selection_status = self._get_selection_status_string(chip, selected_devices, selected_peripherals, current_manufacturer)
            # Show overall current selection status if provided
            if current_selection_status:
                print(c('\n📋 Preview', TerminalColors.YELLOW + TerminalColors.BOLD))
                for line in current_selection_status.split('\n'):
                    print(f'   {line}')

            # Instructions
            print(c('\n📖 Instructions:', TerminalColors.GREEN + TerminalColors.BOLD))

        print(f'   Enter the manufacturer name for this board')
        print(f'   Press {c("Enter", TerminalColors.BOLD)} without input to use default: {c("ESPRESSIF", TerminalColors.BOLD)}')

        # Prepare input prompt with current manufacturer value if provided
        input_prompt = '\nEnter manufacturer (press Enter to confirm):\n'

        try:
            manufacturer = input(input_prompt).strip()

            # If user doesn't input anything, use default or keep current value
            if not manufacturer:
                if current_manufacturer:
                    # Keep current manufacturer value
                    manufacturer = current_manufacturer
                    print(c(f'✅ Keeping current manufacturer: {c(manufacturer, TerminalColors.BRIGHT_GREEN)}', TerminalColors.GREEN))
                else:
                    # No current manufacturer, use default
                    manufacturer = 'ESPRESSIF'
                    print(c(f'✅ Using default manufacturer: {c(manufacturer, TerminalColors.BRIGHT_GREEN)}', TerminalColors.GREEN))
            else:
                print(c(f'✅ Manufacturer set to: {c(manufacturer, TerminalColors.BRIGHT_GREEN)}', TerminalColors.GREEN))

            return manufacturer

        except KeyboardInterrupt:
            print(c('\n❌ Operation cancelled', TerminalColors.BRIGHT_RED))
            sys.exit(1)

    def create_board_info_yaml(self, board_dir: Path, board_name: str, chip: str, manufacturer: Optional[str] = None) -> None:
        """Create board_info.yaml file

        Args:
            board_dir: Board directory path
            board_name: Name of the board
            chip: Selected chip
            manufacturer: Manufacturer name (optional, defaults to 'ESPRESSIF  # To be confirmed')
        """
        board_info_path = board_dir / 'board_info.yaml'

        # Use provided manufacturer or default
        if manufacturer is None:
            manufacturer = 'ESPRESSIF  # To be confirmed'

        board_info = {
            'board': board_name,
            'chip': chip,
            'version': '1.0.0',
            'description': f'{board_name} board configuration',
            'manufacturer': manufacturer
        }

        with open(board_info_path, 'w', encoding='utf-8') as f:
            yaml.dump(board_info, f, default_flow_style=False, sort_keys=False)

    def get_available_chips(self) -> List[str]:
        """Get list of available chips by scanning ESP-IDF hal directory"""
        chips = set()
        # Try to get chips from ESP-IDF hal directory
        try:
            # Check IDF_PATH environment variable
            idf_path = os.environ.get('IDF_PATH')
            if not idf_path:
                self.logger.warning('IDF_PATH environment variable not set')
                # Fallback to common chips
                common_chips = ['esp32', 'esp32s2', 'esp32s3', 'esp32c3', 'esp32c6', 'esp32p4', 'esp32h2']
                for chip in common_chips:
                    chips.add(chip)
                return sorted(list(chips))

            soc_dir = Path(idf_path) / 'components' / 'soc'

            # Scan soc directory for chip-specific folders
            for item in soc_dir.iterdir():
                if not item.is_dir():
                    continue

                dir_name = item.name
                # Look for directories starting with "esp32" (ESP chips)
                if dir_name.startswith('esp32'):
                    chips.add(dir_name)

            # If no chips found, fallback to common chips
            if not chips:
                self.logger.warning('No chips found in HAL directory, using common chips')
                common_chips = ['esp32', 'esp32s2', 'esp32s3', 'esp32c3', 'esp32c6', 'esp32p4', 'esp32h2']
                for chip in common_chips:
                    chips.add(chip)

            return sorted(list(chips))

        except Exception as e:
            self.logger.error(f'Error getting chips from HAL directory: {e}')
            # Fallback to common chips
            common_chips = ['esp32', 'esp32s2', 'esp32s3', 'esp32c3', 'esp32c6', 'esp32p4', 'esp32h2']
            for chip in common_chips:
                chips.add(chip)
            return sorted(list(chips))

    def simple_interactive_select_chip(self) -> List[str]:
        """Simple interactive chip selection"""
        print('\n⚙️  Step 1: Select chip (single selection)')
        chips = self.get_available_chips()
        if not chips:
            raise ValueError('No chips available')

        print('Available chips:')
        for idx, chip in enumerate(chips, 1):
            print(f'  [{idx}] {chip}')

        while True:
            try:
                choice = input('\nEnter chip number: ').strip()
                idx = int(choice) - 1
                if 0 <= idx < len(chips):
                    selected_chip = chips[idx]
                    print(f'✅ Selected chip: {selected_chip}')
                    return selected_chip
                else:
                    print(f'❌ Invalid selection. Please enter a number between 1 and {len(chips)}')
            except ValueError:
                print('❌ Invalid input. Please enter a number.')
            except KeyboardInterrupt:
                print('\n❌ Operation cancelled')
                sys.exit(1)

    def interactive_select_chip(self, chip: Optional[str] = None,
                               selected_devices: Optional[List[str]] = None,
                               selected_peripherals: Optional[List[str]] = None,
                               manufacturer: Optional[str] = None) -> str:
        """Interactive chip selection using cursor-based checkbox interface"""
        chips = self.get_available_chips()
        if not chips:
            raise ValueError('No chips available')

        # Prepare current selection status
        devices = selected_devices or []
        peripherals = selected_peripherals or []
        selection_status = self._get_selection_status_string(chip, devices, peripherals, manufacturer)

        if self.terminal_supports_advanced:
            try:
                # Determine preselected chip
                preselected_chip = [chip] if chip else None
                selector = TerminalCheckboxSelector('Available chips (select one):', chips,
                                                   multi_select=False,
                                                   current_selection_status=selection_status,
                                                   preselected=preselected_chip)
                selected = selector.run()
                if selected:
                    return selected[0]
                else:
                    # User didn't select anything, raise error
                    raise ValueError('No chip selected')
            except (ImportError, AttributeError, OSError) as e:
                # Fallback to simple menu if terminal control fails
                self.logger.debug(f'Terminal control failed, using fallback: {e}')
                return self.simple_interactive_select_chip()
        else:
            # Terminal doesn't support advanced features, use simple menu
            self.logger.debug('Terminal does not support advanced features, using simple menu')
            return self.simple_interactive_select_chip()

    def simple_interactive_select_devices(self, chip: Optional[str] = None) -> List[str]:
        """Simple interactive device selection (multiple)"""
        print('\n⚙️  Step 2: Select devices (multiple selection)')
        devices = self.get_available_devices()
        if not devices:
            return []

        # Filter devices by chip capability if chip is selected
        if chip:
            filtered_devices = self.filter_by_chip_capability(chip, devices, 'devices')
            if len(filtered_devices) < len(devices):
                print(f'ℹ️  Note: Showing {len(filtered_devices)} out of {len(devices)} devices supported by chip {chip}')
                devices = filtered_devices
            else:
                print(f'ℹ️  All {len(devices)} devices are supported by chip {chip}')

        print('Available devices:')
        for idx, device in enumerate(devices, 1):
            print(f'  [{idx}] {device}')

        while True:
            try:
                choice = input('\nEnter device numbers (space-separated, press Enter to skip): ').strip()
                if not choice:
                    print('✅ No devices selected')
                    return []

                # Check if separator is space (not comma, semicolon, etc.)
                if ',' in choice or ';' in choice:
                    print('❌ Invalid separator detected.')
                    print('   Please use SPACE to separate numbers, e.g., "1 2 3"')
                    print('   Do not use commas, semicolons, or other separators.')
                    continue

                indices = [int(x) - 1 for x in choice.split()]
                selected = []
                for idx in indices:
                    if 0 <= idx < len(devices):
                        selected.append(devices[idx])
                    else:
                        print(f'❌ Invalid index: {idx + 1}')
                        break
                else:
                    if selected:
                        print(f'✅ Selected devices: {", ".join(selected)}')
                        return selected
                    else:
                        print('❌ No valid devices selected')
            except ValueError:
                print('❌ Invalid input. Please enter space-separated numbers.')
                print('   Example: "1 2 3" (use SPACE to separate, not commas)')
            except KeyboardInterrupt:
                print('\n❌ Operation cancelled')
                sys.exit(1)

    def interactive_select_devices(self, chip: Optional[str] = None,
                                  selected_devices: Optional[List[str]] = None,
                                  selected_peripherals: Optional[List[str]] = None,
                                  manufacturer: Optional[str] = None) -> List[str]:
        """Interactive device selection using cursor-based checkbox interface"""
        devices = self.get_available_devices()
        if not devices:
            return []

        # Filter devices by chip capability if chip is selected
        if chip:
            filtered_devices = self.filter_by_chip_capability(chip, devices, 'devices')
            if len(filtered_devices) < len(devices):
                self.logger.info(f'Filtered devices for chip {chip}: {len(filtered_devices)} out of {len(devices)} devices supported')
                devices = filtered_devices
            else:
                self.logger.debug(f'All {len(devices)} devices are supported by chip {chip}')

        # Prepare current selection status
        devices_list = selected_devices or []
        peripherals = selected_peripherals or []
        selection_status = self._get_selection_status_string(chip, devices_list, peripherals, manufacturer)

        if self.terminal_supports_advanced:
            try:
                selector = TerminalCheckboxSelector('Available devices (select multiple):', devices,
                                                   multi_select=True,
                                                   current_selection_status=selection_status,
                                                   preselected=selected_devices)
                return selector.run()
            except (ImportError, AttributeError, OSError) as e:
                # Fallback to simple menu if terminal control fails
                self.logger.debug(f'Terminal control failed, using fallback: {e}')
                return self.simple_interactive_select_devices(chip)
        else:
            # Terminal doesn't support advanced features, use simple menu
            self.logger.debug('Terminal does not support advanced features, using simple menu')
            return self.simple_interactive_select_devices(chip)

    def simple_interactive_select_peripherals(self, chip: Optional[str] = None) -> List[str]:
        """Simple interactive peripheral selection (multiple)

        Args:
            chip: Selected chip (optional). If provided, peripherals will be filtered by chip capability.
        """
        peripherals = self.get_available_peripherals()
        if not peripherals:
            return []

        # Filter peripherals by chip capability if chip is selected
        if chip:
            filtered_peripherals = self.filter_by_chip_capability(chip, peripherals, 'peripherals')
            if len(filtered_peripherals) < len(peripherals):
                self.logger.info(f'Filtered peripherals for chip {chip}: {len(filtered_peripherals)} out of {len(peripherals)} peripherals supported')
                peripherals = filtered_peripherals
            else:
                self.logger.debug(f'All {len(peripherals)} peripherals are supported by chip {chip}')

        print('\n⚙️  Step 3: Select peripherals (multiple selection, space-separated)')
        if chip:
            print(f'Available peripherals for chip {chip}:')
        else:
            print('Available peripherals:')
        for idx, periph in enumerate(peripherals, 1):
            print(f'  [{idx}] {periph}')

        while True:
            try:
                choice = input('\nEnter peripheral numbers (space-separated, press Enter to skip): ').strip()
                if not choice:
                    print('✅ No peripherals selected')
                    return []

                # Check if separator is space (not comma, semicolon, etc.)
                if ',' in choice or ';' in choice:
                    print('❌ Invalid separator detected.')
                    print('   Please use SPACE to separate numbers, e.g., "1 2 3"')
                    print('   Do not use commas, semicolons, or other separators.')
                    continue

                indices = [int(x) - 1 for x in choice.split()]
                selected = []
                for idx in indices:
                    if 0 <= idx < len(peripherals):
                        selected.append(peripherals[idx])
                    else:
                        print(f'❌ Invalid index: {idx + 1}')
                        break
                else:
                    if selected:
                        print(f'✅ Selected peripherals: {", ".join(selected)}')
                        return selected
                    else:
                        print('❌ No valid peripherals selected')
            except ValueError:
                print('❌ Invalid input. Please enter space-separated numbers.')
                print('   Example: "1 2 3" (use SPACE to separate, not commas)')
            except KeyboardInterrupt:
                print('\n❌ Operation cancelled')
                sys.exit(1)

    def interactive_select_peripherals(self, chip: Optional[str] = None,
                                      selected_devices: Optional[List[str]] = None,
                                      selected_peripherals: Optional[List[str]] = None,
                                      manufacturer: Optional[str] = None) -> List[str]:
        """Interactive peripheral selection using cursor-based checkbox interface"""
        peripherals = self.get_available_peripherals()
        if not peripherals:
            return []

        # Filter peripherals by chip capability if chip is selected
        if chip:
            filtered_peripherals = self.filter_by_chip_capability(chip, peripherals, 'peripherals')
            if len(filtered_peripherals) < len(peripherals):
                self.logger.info(f'Filtered peripherals for chip {chip}: {len(filtered_peripherals)} out of {len(peripherals)} peripherals supported')
                peripherals = filtered_peripherals
            else:
                self.logger.debug(f'All {len(peripherals)} peripherals are supported by chip {chip}')

        # Prepare current selection status
        devices = selected_devices or []
        peripherals_list = selected_peripherals or []
        selection_status = self._get_selection_status_string(chip, devices, peripherals_list, manufacturer)

        if self.terminal_supports_advanced:
            try:
                selector = TerminalCheckboxSelector('Available peripherals (select multiple):', peripherals,
                                                   multi_select=True,
                                                   current_selection_status=selection_status,
                                                   preselected=selected_peripherals)
                return selector.run()
            except (ImportError, AttributeError, OSError) as e:
                # Fallback to simple menu if terminal control fails
                self.logger.debug(f'Terminal control failed, using fallback: {e}')
                return self.simple_interactive_select_peripherals(chip)
        else:
            # Terminal doesn't support advanced features, use simple menu
            self.logger.debug('Terminal does not support advanced features, using simple menu')
            return self.simple_interactive_select_peripherals(chip)

    def check_dependencies(self, selected_devices: List[str], selected_peripherals: List[str]) -> Tuple[Set[Tuple[str, str]], bool]:
        """Check if all device dependencies are satisfied

        Args:
            selected_devices: List of selected device type strings
            selected_peripherals: List of selected peripheral type strings

        Returns:
            Tuple of (missing_dependencies_set, all_satisfied_bool)
            missing_dependencies_set contains tuples of (device_type, missing_peripheral_type)
        """
        missing = set()

        selected_peripherals_type = []
        for periph in selected_peripherals:
            # Parse the peripheral to compare
            periph_type_parsed, _, _ = self._parse_peripheral_type(periph)
            selected_peripherals_type.append(periph_type_parsed)

        for device_type_str in selected_devices:
            # Get peripheral dependencies for this device
            dependencies = self.get_device_peripheral_dependencies(device_type_str)
            if not dependencies:
                continue

            for dep in dependencies:
                periph_type = dep.get('periph_type', '')
                if not periph_type:
                    continue
                matched = False
                missing_periph = None
                instance = dep.get('instance', '')
                if instance and instance in selected_peripherals:
                    matched = True
                elif instance and instance not in selected_peripherals:
                    missing_periph = instance
                elif periph_type in selected_peripherals_type:
                    matched = True
                else:
                    missing_periph = periph_type
                if not matched:
                    missing.add((device_type_str, missing_periph))

        return missing, len(missing) == 0

    def simple_interactive_check_dependencies(self, selected_devices: List[str], selected_peripherals: List[str]) -> List[str]:
        """Simple interactive dependency checking and resolution"""
        print('\n⚙️  Step 4: Checking dependencies...')
        missing, all_satisfied = self.check_dependencies(selected_devices, selected_peripherals)

        if all_satisfied:
            print('\n✅ All dependencies satisfied')
            return selected_peripherals

        print('❌ Missing peripheral dependencies:')
        missing_periphs = set()
        for device_type, periph_type in missing:
            print(f'  - Device "{device_type}" requires peripheral "{periph_type}"')
            missing_periphs.add(periph_type)

        print('Please add the missing peripherals.')

        all_peripherals = self.get_available_peripherals()
        updated_peripherals = selected_peripherals.copy()

        while True:
            try:
                choice = input('\nEnter peripheral numbers: ').strip()
                if not choice:
                    print('❌ Please select peripherals')
                    continue

                # Check if separator is space (not comma, semicolon, etc.)
                if ',' in choice or ';' in choice:
                    print('❌ Invalid separator detected.')
                    print('   Please use SPACE to separate numbers, e.g., "1 2 3"')
                    print('   Do not use commas, semicolons, or other separators.')
                    continue

                indices = [int(x) - 1 for x in choice.split()]
                for idx in indices:
                    if 0 <= idx < len(all_peripherals):
                        updated_peripherals.append(all_peripherals[idx])
                    else:
                        print(f'❌ Invalid index: {idx + 1}')
                        break
                else:
                    # Check again
                    missing_after, satisfied = self.check_dependencies(selected_devices, updated_peripherals)
                    if satisfied:
                        print(f'\n✅ All dependencies satisfied.')
                        return updated_peripherals
                    else:
                        print('❌ Still missing dependencies. Please try again.')
                        missing_periphs_after = set()
                        for device_type, periph_type in missing_after:
                            missing_periphs_after.add(periph_type)
                        print(f'Missing: {", ".join(sorted(missing_periphs_after))}')
            except ValueError:
                print('❌ Invalid input. Please enter space-separated numbers.')
                print('   Example: "1 2 3" (use SPACE to separate, not commas)')
            except KeyboardInterrupt:
                print('\n❌ Operation cancelled')
                sys.exit(1)

    def interactive_check_dependencies(self, chip: Optional[str], selected_devices: List[str],
                                      selected_peripherals: List[str],
                                      manufacturer: Optional[str] = None) -> List[str]:
        """Interactive dependency checking and resolution using cursor-based checkbox interface"""
        missing, all_satisfied = self.check_dependencies(selected_devices, selected_peripherals)

        if all_satisfied:
            print('\n✅ All dependencies satisfied')
            return selected_peripherals

        # Get all available peripherals
        all_peripherals = self.get_available_peripherals()

        # Filter to show only missing peripherals (or all if missing not found)
        # Create a list of peripherals that match missing types
        missing_periphs = set()
        for device_type, periph_type in missing:
            missing_periphs.add(periph_type)

        suggested_peripherals = []
        for periph in all_peripherals:
            # Parse peripheral type
            periph_type, _, _ = self._parse_peripheral_type(periph)
            if periph_type in missing_periphs or periph in missing_periphs:
                suggested_peripherals.append(periph)

        # If no specific suggestions, show all peripherals
        if not suggested_peripherals:
            suggested_peripherals = all_peripherals

        # Prepare current selection status
        selection_status = self._get_selection_status_string(chip, selected_devices, selected_peripherals, manufacturer)

        if self.terminal_supports_advanced:
            # Use cursor-based checkbox interface to select missing peripherals
            # The title will be displayed with prominent colors in the TerminalCheckboxSelector
            try:
                selector = TerminalCheckboxSelector('MISSING PERIPHERALS - Select to add (multiple):',
                                                   suggested_peripherals,
                                                   multi_select=True,
                                                   current_selection_status=selection_status,
                                                   preselected=selected_peripherals)
                selected_to_add = selector.run()
            except (ImportError, AttributeError, OSError) as e:
                # Fallback to simple menu if terminal control fails
                self.logger.debug(f'Terminal control failed, using fallback: {e}')
                return self.simple_interactive_check_dependencies(selected_devices, selected_peripherals)
        else:
            # Terminal doesn't support advanced features, use simple menu
            self.logger.debug('Terminal does not support advanced features, using simple menu')
            return self.simple_interactive_check_dependencies(selected_devices, selected_peripherals)

        # Add selected peripherals to the list
        updated_peripherals = selected_peripherals.copy()
        for periph in selected_to_add:
            if periph not in updated_peripherals:
                updated_peripherals.append(periph)

        # Check again
        missing_after, satisfied = self.check_dependencies(selected_devices, updated_peripherals)
        if satisfied:
            print(f'\n✅ All dependencies satisfied.')
            return updated_peripherals
        else:
            # If still missing, show what's still missing and recursively call again
            print('❌ Still missing dependencies. Please add more peripherals.')
            missing_periphs_after = set()
            for device_type, periph_type in missing_after:
                missing_periphs_after.add(periph_type)
            print(f'Still missing: {", ".join(sorted(missing_periphs_after))}')

            # Recursively call to add more peripherals
            return self.interactive_check_dependencies(chip, selected_devices, updated_peripherals, manufacturer)


    def generate_peripheral_yaml(self, board_dir: Path, board_name: str, chip: str, selected_peripherals: List[str]) -> None:
        """Generate board_peripherals.yaml from cached peripheral configurations

        Uses the peripheral_config_cache to get pre-loaded YAML blocks for each peripheral type.
        Similar to generate_device_yaml, uses simple string concatenation with indentation.
        """
        periph_yaml_path = board_dir / 'board_peripherals.yaml'

        # Start with board header
        output_lines = [
            f'board: {board_name}',
            f'chip: {chip}',
            f'version: 1.0.0',
            'peripherals:'
        ]

        for i, periph_type_str in enumerate(selected_peripherals):
            # Get cached configuration for this peripheral type
            config_content = self.get_peripheral_config(periph_type_str)

            if config_content:
                # Add empty line before each peripheral except the first one
                if i > 0:
                    output_lines.append('')

                # Add the cached YAML block with proper indentation
                for line in config_content.split('\n'):
                    if line.strip():
                        output_lines.append('  ' + line)
                    else:
                        output_lines.append(line)
            else:
                # No cached configuration, create minimal entry
                self.logger.warning(f'No cached configuration found for peripheral {periph_type_str}, creating minimal entry')

                # Parse peripheral type to extract base type
                periph_type_parts = periph_type_str.split('_')
                base_type = periph_type_parts[0] if periph_type_parts else periph_type_str

                # Add empty line before each peripheral except the first one
                if i > 0:
                    output_lines.append('')

                # Create minimal peripheral entry
                output_lines.append(f'  - name: {base_type}-0')
                output_lines.append(f'    type: {base_type}')
                output_lines.append(f'    role: master')
                output_lines.append(f'    config: {{}}')

        # Write the output file
        with open(periph_yaml_path, 'w', encoding='utf-8') as f:
            f.write('\n'.join(output_lines))

    def generate_device_yaml(self, board_dir: Path, board_name: str, chip: str, selected_devices: List[str], selected_peripherals: List[str]) -> None:
        """Generate board_devices.yaml from cached device configurations

        Uses the device_config_cache to get pre-loaded YAML blocks for each device type.
        """
        dev_yaml_path = board_dir / 'board_devices.yaml'

        # Start with board header
        output_lines = [
            f'board: {board_name}',
            f'chip: {chip}',
            f'version: 1.0.0',
            'devices:'
        ]

        for i, device_type_str in enumerate(selected_devices):
            # Get cached configuration for this device type
            config_content = self.get_device_config(device_type_str)

            if config_content:
                # Parse the device type to get base_type and sub_type
                base_type, sub_type = self._parse_device_type(device_type_str)

                # Add empty line before each device except the first one
                if i > 0:
                    output_lines.append('')

                # Add the cached YAML block with proper indentation
                for line in config_content.split('\n'):
                    if line.strip():
                        output_lines.append('  ' + line)
                    else:
                        output_lines.append(line)
            else:
                # No cached configuration, create minimal entry
                self.logger.warning(f'No cached configuration found for device {device_type_str}, creating minimal entry')
                base_type, sub_type = self._parse_device_type(device_type_str)

                # Add empty line before each device except the first one
                if i > 0:
                    output_lines.append('')

                output_lines.append(f'  - name: {base_type}_0')
                output_lines.append(f'    type: {base_type}')
                if sub_type:
                    output_lines.append(f'    sub_type: {sub_type}')
                output_lines.append(f'    version: default')
                output_lines.append(f'    config: {{}}')

        # Write the output file
        with open(dev_yaml_path, 'w', encoding='utf-8') as f:
            f.write('\n'.join(output_lines))

    def run(self, board_name: str, create_path: Optional[Path] = None, input_args: Optional[str] = None) -> bool:
        """Run the interactive board creation process with ESC key support for going back

        Args:
            board_name: Name of the board to create
            create_path: Path where to create the board directory. If None, uses current working directory.
        """

        if input_args:
            input_path = Path(input_args)
            if input_path.parent == Path('.'):
                use_default_path = True
            else:
                use_default_path = False
            path_args = input_path.parent

        print('=' * 60)
        print('ESP Board Manager - Board Creation Tool')
        print('=' * 60)

        try:
            # Step 0: Create board directory and Kconfig
            print(f'\n⚙️  Creating board directory for "{board_name}"...')
            if create_path:
                print(f'   Creating in: {create_path}')
            else:
                print(f'   Creating in: {Path.cwd()} (current directory)')
            board_dir = self.create_board_directory(board_name, create_path)
            if board_dir is None:
                # User chose not to overwrite, exit
                return False
            print(f'✅ Created board directory: {board_dir}')

            # Initialize selection variables
            chip = None
            manufacturer = None
            selected_devices = []
            selected_peripherals = []

            # Define step constants
            STEP_CHIP = 'chip'
            STEP_MANUFACTURER = 'manufacturer'
            STEP_DEVICES = 'devices'
            STEP_PERIPHERALS = 'peripherals'
            STEP_DEPENDENCIES = 'dependencies'

            # Initialize current step
            current_step = STEP_CHIP

            # Main interactive loop with ESC key support
            while True:
                try:
                    if current_step == STEP_CHIP:
                        # Step 1: Select chip
                        chip = self.interactive_select_chip(chip, selected_devices, selected_peripherals, manufacturer)
                        current_step = STEP_MANUFACTURER
                        continue

                    elif current_step == STEP_MANUFACTURER:
                        # Step 2: Input manufacturer information
                        manufacturer = self.interactive_input_manufacturer(chip, selected_devices, selected_peripherals, manufacturer)

                        # Create Kconfig and board_info.yaml after manufacturer input
                        self.create_kconfig_file(board_dir, board_name, chip)
                        self.create_board_info_yaml(board_dir, board_name, chip, manufacturer)
                        print(f'✅ Created Kconfig and board_info.yaml')

                        current_step = STEP_DEVICES
                        continue

                    elif current_step == STEP_DEVICES:
                        # Step 3: Select devices
                        selected_devices = self.interactive_select_devices(chip, selected_devices, selected_peripherals, manufacturer)
                        current_step = STEP_PERIPHERALS
                        continue

                    elif current_step == STEP_PERIPHERALS:
                        # Step 4: Select peripherals
                        selected_peripherals = self.interactive_select_peripherals(chip, selected_devices, selected_peripherals, manufacturer)

                        # If devices selected, go to dependency check
                        if selected_devices:
                            current_step = STEP_DEPENDENCIES
                        else:
                            # No devices selected, skip dependency check and finish
                            break
                        continue

                    elif current_step == STEP_DEPENDENCIES:
                        # Step 5: Check dependencies
                        selected_peripherals = self.interactive_check_dependencies(chip, selected_devices, selected_peripherals, manufacturer)
                        # After dependency check, finish the process
                        break

                except EscKeyPressed as e:
                    # Handle ESC key based on current step
                    step_name = str(e.step_name).lower()

                    if current_step == STEP_CHIP:
                        # ESC at chip selection - exit the program
                        print('\n⏪ ESC pressed at chip selection - exiting program')
                        return False

                    elif current_step == STEP_MANUFACTURER:
                        # ESC at manufacturer input - go back to chip selection
                        print('\n⏪ ESC pressed at manufacturer input - going back to chip selection')
                        current_step = STEP_CHIP
                        # Keep chip selected, clear manufacturer
                        manufacturer = None
                        continue

                    elif current_step == STEP_DEVICES:
                        # ESC at device selection - go back to manufacturer input
                        print('\n⏪ ESC pressed at device selection - going back to manufacturer input')
                        current_step = STEP_CHIP
                        # Keep chip and manufacturer, clear devices and peripherals
                        selected_devices = []  # Clear selected devices
                        selected_peripherals = []  # Clear selected peripherals
                        continue

                    elif current_step == STEP_PERIPHERALS:
                        # ESC at peripheral selection - go back to device selection
                        print('\n⏪ ESC pressed at peripheral selection - going back to device selection')
                        current_step = STEP_DEVICES
                        # Keep chip, manufacturer, and devices, clear peripherals
                        selected_peripherals = []  # Clear selected peripherals
                        continue

                    elif current_step == STEP_DEPENDENCIES:
                        # ESC at dependency check - go back to peripheral selection
                        print('\n⏪ ESC pressed at dependency check - going back to peripheral selection')
                        current_step = STEP_PERIPHERALS
                        # Keep all selections (chip, manufacturer, devices, peripherals)
                        continue

                    else:
                        # Unknown step, default behavior: exit
                        print(f'\n⏪ ESC pressed at unknown step: {step_name} - exiting')
                        return False

            if chip:
                print(f'    Selected chip: {chip}')
            if manufacturer:
                print(f'    Selected manufacturer: {manufacturer}')
            if selected_devices:
                print(f'    Selected devices: {", ".join(selected_devices)}')
            if selected_peripherals:
                print(f'    Selected peripherals: {", ".join(selected_peripherals)}')

            # Step 6: Generate YAML files
            print('\n⚙️  Generating board configuration files...')

            # Generate peripheral YAML
            if selected_peripherals:
                self.generate_peripheral_yaml(board_dir, board_name, chip, selected_peripherals)
            else:
                print(f'⚠️  No peripherals selected, skipping board_peripherals.yaml generation')

            # Generate device YAML
            if selected_devices:
                self.generate_device_yaml(board_dir, board_name, chip, selected_devices, selected_peripherals)
            else:
                print(f'⚠️  No devices selected, skipping board_devices.yaml generation')

            # Success message
            print('\n✅ Board creation completed successfully!')
            print(f'\nBoard path: {board_dir}\n')
            print(f'Files created:')
            print(f'  - Kconfig')
            print(f'  - board_info.yaml')
            if selected_peripherals:
                print(f'  - board_peripherals.yaml')
            if selected_devices:
                print(f'  - board_devices.yaml')
            print((f'\nNext steps:'))
            print(f'⚠️  1. Review and customize the generated YAML files')
            print(f'      - Focus on checking each configuration item, especially those with [IO] and [TO_BE_CONFIRMED] tags')
            print(f'      - Devices find dependent peripherals by name, check the peripheral names in the device peripherals list')
            print(f'      - Peripherals name must start with the type')
            print(f'      - Make sure there are no peripherals or devices with the same name')
            if input_args:
                if use_default_path:
                    print(f'   2. Run: idf.py gen-bmgr-config -b {board_name}')
                else :
                    print(f'   2. Run: idf.py gen-bmgr-config -b {board_name} -c {path_args}')
            else:
                print(f'   2. Run: idf.py gen-bmgr-config -b {board_name} -c <path/to/your/board>')
            print('')
            return True

        except KeyboardInterrupt:
            print('\n❌ Operation cancelled by user (Ctrl+C)')
            return False
        except Exception as e:
            self.logger.error(f'❌ Error creating board: {e}')
            import traceback
            traceback.print_exc()
            return False


def main():
    """Main entry point"""
    import argparse

    parser = argparse.ArgumentParser(
        description='Create a new board configuration for ESP Board Manager',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    python create_board.py my_custom_board_v1_0
    python create_board.py my_custom_board_v1_0 -p /path/to/boards
    python create_board.py -p /path/to/boards my_custom_board_v1_0
    python create_board.py -p /path/to/boards/my_custom_board_v1_0  (board name extracted from path)
    python create_board.py my_custom_board_v1_0 --create-path /path/to/boards

Note: If -p path ends with a valid board name (e.g., path/board_name),
      the board name will be automatically extracted from the path.
        """
    )

    parser.add_argument(
        'board_name',
        nargs='?',  # Make it optional to allow extraction from path
        help='Name of the board to create. Can be extracted from -p path if path ends with board name.'
    )

    parser.add_argument(
        '-p', '--create-path',
        dest='create_path',
        type=str,
        help='Path where to create the board directory. If path ends with board name (e.g., path/board_name), board name will be extracted. If not specified, uses current working directory.'
    )

    args = parser.parse_args()

    # Parse create path and extract board name if needed
    create_path = None
    board_name = args.board_name

    if args.create_path:
        create_path_str = args.create_path
        # Check if path contains board name (path/board_name format)
        # Normalize path separators
        normalized_path = create_path_str.replace('\\', '/')
        # Remove trailing slashes
        normalized_path = normalized_path.rstrip('/')
        path_parts = [p for p in normalized_path.split('/') if p]  # Filter empty parts

        if len(path_parts) > 1:
            # Last part might be board name
            potential_board_name = path_parts[-1]
            potential_path = '/'.join(path_parts[:-1])

            # Validate potential board name format
            if re.match(r'^[a-z0-9_]+$', potential_board_name):
                # If board_name not provided, extract from path
                if not board_name:
                    board_name = potential_board_name
                    create_path = Path(potential_path).resolve() if potential_path else Path.cwd()
                else:
                    # Board name provided, use full path as create_path
                    create_path = Path(create_path_str).resolve()
            else:
                # Last part is not a valid board name, use full path
                create_path = Path(create_path_str).resolve()
        elif len(path_parts) == 1:
            # Single part, check if it's a valid board name
            potential_board_name = path_parts[0]
            if re.match(r'^[a-z0-9_]+$', potential_board_name) and not board_name:
                # Single part and looks like board name, use current directory as path
                board_name = potential_board_name
                create_path = Path.cwd()
            else:
                # Single part but board name already provided, or not a valid board name
                create_path = Path(create_path_str).resolve()
        else:
            # Empty path, use current directory
            create_path = Path.cwd()
    else:
        # No path specified, use current directory
        create_path = None

    # Check if board_name is still missing
    if not board_name:
        parser.error('board_name is required.\n\n'
                    'Usage: python create_board.py <board_name> [-p <path>]\n'
                    '       python create_board.py -p <path>/<board_name>\n'
                    'Example: python create_board.py my_board -p /path/to/boards\n'
                    '         python create_board.py -p /path/to/boards/my_board\n'
                    '         python create_board.py my_board  (creates in current directory)')

    # Validate board name
    if not re.match(r'^[a-z0-9_]+$', board_name):
        print(f'❌ Invalid board name: {board_name}')
        print('Board name must contain only lowercase letters, numbers, and underscores')
        sys.exit(1)

    # Validate and create path if needed
    if create_path:
        if not create_path.exists():
            # Auto-create the directory if it doesn't exist
            try:
                create_path.mkdir(parents=True, exist_ok=True)
                print(f'ℹ️  Created directory: {create_path}')
            except Exception as e:
                print(f'❌ Failed to create directory {create_path}: {e}')
                sys.exit(1)
        elif not create_path.is_dir():
            print(f'❌ Create path is not a directory: {create_path}')
            sys.exit(1)

    # Setup logging
    setup_logger('board_creator')
    logger = get_logger('main')

    # Create board creator and run
    script_dir = Path(__file__).parent
    creator = BoardCreator(script_dir)

    try:
        success = creator.run(board_name, create_path)
        if not success:
            sys.exit(1)
    except KeyboardInterrupt:
        print('\n❌ Operation cancelled by user')
        sys.exit(1)
    except Exception as e:
        logger.error(f'Unexpected error: {e}')
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == '__main__':
    main()
