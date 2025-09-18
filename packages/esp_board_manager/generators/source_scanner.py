# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
Source code scanner for ESP Board Manager
"""

import os
import re
from pathlib import Path
from typing import List, Tuple

from .utils.logger import LoggerMixin
from .utils.file_utils import safe_write_file
from .settings import BoardManagerConfig


class SourceScanner(LoggerMixin):
    """Scans board directories for source files and manages CMakeLists.txt updates with filtering"""

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

    def scan_board_source_files(self, board_path: str) -> List[str]:
        """
        Scan board directory for source files and return their paths.

        Optimized version with better performance and configurability.

        Args:
            board_path: Path to the board directory

        Returns:
            list: List of source file paths relative to board_manager directory
        """
        source_files = []

        if not os.path.exists(board_path):
            self.logger.warning(f'⚠️  Board path does not exist: {board_path}')
            return source_files

        self.logger.info(f'   Scanning for source files in board directory: {board_path}')

        # Calculate board_manager directory once
        board_manager_dir = os.path.dirname(os.path.dirname(board_path))

        # Use pathlib for better path handling
        board_path_obj = Path(board_path)
        board_manager_dir_obj = Path(board_manager_dir)

        def scan_recursive(current_path: Path, depth: int = 0):
            """Recursively scan directory for source files."""
            if depth > BoardManagerConfig.MAX_SCAN_DEPTH:
                return

            try:
                for item in current_path.iterdir():
                    if item.is_file():
                        if BoardManagerConfig.should_skip_file(item.name):
                            continue

                        if item.suffix.lower() in BoardManagerConfig.get_source_extensions():
                            # Calculate relative path more efficiently
                            relative_path = str(item.relative_to(board_manager_dir_obj))
                            source_files.append(relative_path)
                            self.logger.debug(f'   Found source file: {relative_path}')

                    elif item.is_dir() and not BoardManagerConfig.should_skip_directory(item.name):
                        scan_recursive(item, depth + 1)

            except PermissionError:
                self.logger.warning(f'⚠️  Permission denied accessing {current_path}')
            except Exception as e:
                self.logger.warning(f'⚠️  Error scanning {current_path}: {e}')

        # Start recursive scanning
        scan_recursive(board_path_obj)

        if source_files:
            self.logger.debug(f'   Total source files found: {len(source_files)}')
        else:
            self.logger.debug('   No source files found')

        return source_files

    def parse_cmake_board_sources(self, content: str) -> Tuple[List[str], str]:
        """
        Parse existing BOARD_SETUP_SRCS from CMakeLists.txt content.

        Returns:
            tuple: (list of source files, the matched pattern for replacement)
        """
        board_srcs_pattern = r'set\(BOARD_SETUP_SRCS\s*\n(.*?)\)'
        match = re.search(board_srcs_pattern, content, re.DOTALL)

        if not match:
            return [], ''

        existing_sources = []
        for line in match.group(1).split('\n'):
            line = line.strip()
            if line.startswith('"') and line.endswith('"'):
                # Extract the source file path from quotes
                source_path = line[1:-1]
                existing_sources.append(source_path)

        return existing_sources, match.group(0)

    def create_board_sources_content(self, source_files: List[str]) -> str:
        """Create BOARD_SETUP_SRCS content for CMakeLists.txt."""
        content = 'set(BOARD_SETUP_SRCS\n'
        if source_files:
            for source_file in sorted(source_files):
                content += f'    "{source_file}"\n'
        content += ')\n\n'
        return content

    def find_insertion_point_for_board_sources(self, content: str) -> int:
        """Find the best insertion point for BOARD_SETUP_SRCS in CMakeLists.txt."""
        # Try to insert before idf_component_register
        idf_register_pattern = r'(idf_component_register\s*\()'
        idf_match = re.search(idf_register_pattern, content)

        if idf_match:
            return idf_match.start()

        # Fallback: insert at the beginning of the file
        return 0

    def ensure_board_sources_in_srcs_section(self, content: str) -> str:
        """Ensure BOARD_SETUP_SRCS is included in idf_component_register SRCS section."""
        if '${BOARD_SETUP_SRCS}' in content:
            return content

        # Find the SRCS section in idf_component_register
        srcs_pattern = r'(idf_component_register\s*\(\s*SRCS\s*)'
        match = re.search(srcs_pattern, content, re.MULTILINE | re.DOTALL)

        if not match:
            self.logger.warning('⚠️  Could not find SRCS section in CMakeLists.txt')
            return content

        # Insert BOARD_SRCS reference in SRCS section
        board_sources_var = '        ${BOARD_SRCS}  # Add board-specific sources\n'
        insert_pos = match.end()

        # Find the best insertion point within SRCS section
        # Try to insert before INCLUDE_DIRS first
        include_dirs_pattern = r'\s+INCLUDE_DIRS'
        include_match = re.search(include_dirs_pattern, content[insert_pos:])

        if include_match:
            insert_pos += include_match.start()
        else:
            # If no INCLUDE_DIRS found, insert before the closing parenthesis
            closing_pattern = r'\s+\)'
            closing_match = re.search(closing_pattern, content[insert_pos:])
            if closing_match:
                insert_pos += closing_match.start()
            else:
                self.logger.warning('⚠️  Could not find proper insertion point in CMakeLists.txt')
                return content

        # Insert the board sources reference
        board_sources_var = '        ${BOARD_SETUP_SRCS}  # Add board-specific sources\n'
        return content[:insert_pos] + board_sources_var + content[insert_pos:]

    def update_cmakelists_with_board_sources(self, board_source_files: List[str]) -> bool:
        """
        Update the main CMakeLists.txt file to include board-specific source files.

        Optimized version with better error handling and performance.

        Args:
            board_source_files: List of source file paths relative to board_manager directory

        Returns:
            bool: True if update was successful
        """
        cmakelists_path = self.root_dir / 'CMakeLists.txt'

        if not cmakelists_path.exists():
            self.logger.error(f'CMakeLists.txt not found at {cmakelists_path}')
            return False

        try:
            # Read current CMakeLists.txt content
            with open(cmakelists_path, 'r', encoding='utf-8') as f:
                content = f.read()

            # Sort board_source_files for consistent comparison
            board_source_files = sorted(board_source_files)

            # Check if BOARD_SETUP_SRCS already exists
            if 'BOARD_SETUP_SRCS' in content:
                existing_sources, matched_pattern = self.parse_cmake_board_sources(content)

                if matched_pattern:
                    # Sort existing sources for comparison
                    existing_sources = sorted(existing_sources)

                    # Check if content is identical
                    if existing_sources == board_source_files:
                        self.logger.info('   Board source files unchanged - no modification needed')
                        return True
                    else:
                        self.logger.debug(f'Board source files changed - updating CMakeLists.txt')
                        self.logger.debug(f'  Previous: {existing_sources}')
                        self.logger.debug(f'  New: {board_source_files}')
                else:
                    self.logger.warning('⚠️  BOARD_SETUP_SRCS found but could not parse existing sources')
                    return False
            else:
                self.logger.info('BOARD_SETUP_SRCS not found - creating new section')

            # Create new BOARD_SRCS content
            new_board_srcs = self.create_board_sources_content(board_source_files)

            # Update or create BOARD_SETUP_SRCS section
            if 'BOARD_SETUP_SRCS' in content:
                # Replace existing BOARD_SETUP_SRCS section including trailing empty lines
                board_srcs_pattern = r'set\(BOARD_SETUP_SRCS\s*\n.*?\)\s*\n*'
                content = re.sub(board_srcs_pattern, new_board_srcs, content, flags=re.DOTALL)
            else:
                # Insert BOARD_SETUP_SRCS before idf_component_register
                insert_pos = self.find_insertion_point_for_board_sources(content)
                content = content[:insert_pos] + new_board_srcs + content[insert_pos:]

            # Ensure BOARD_SETUP_SRCS is included in idf_component_register SRCS section
            content = self.ensure_board_sources_in_srcs_section(content)

            # Write the updated content
            success = safe_write_file(cmakelists_path, content)
            if success:
                if board_source_files:
                    self.logger.info(f'✅ Updated CMakeLists.txt with {len(board_source_files)} board source files:')
                    for source_file in board_source_files:
                        self.logger.info(f'   - {source_file}')
                else:
                    self.logger.info('✅ Updated CMakeLists.txt with empty BOARD_SRCS')
                return True
            else:
                self.logger.error('Failed to write CMakeLists.txt')
                return False

        except Exception as e:
            self.logger.error(f'Error updating CMakeLists.txt: {e}')
            import traceback
            traceback.print_exc()
            return False
