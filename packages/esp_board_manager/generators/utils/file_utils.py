# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
File utilities for ESP Board Manager
"""

import os
import re
from pathlib import Path
from typing import List, Set, Optional
from functools import lru_cache


def should_skip_directory(dirname: str) -> bool:
    """Check if directory should be skipped during scanning."""
    exclude_dirs = {'__pycache__', '.git', 'build', 'cmake-build-*'}
    return (dirname.startswith('.') or
            dirname in exclude_dirs or
            any(pattern in dirname for pattern in ['build', 'cmake-build']))


def should_skip_file(filename: str) -> bool:
    """Check if file should be skipped during scanning."""
    exclude_files = {'.DS_Store', 'Thumbs.db'}
    return filename in exclude_files


def _has_valid_project_declaration(cmake_file: Path) -> bool:
    """Check if CMakeLists.txt contains a valid project() declaration

    Args:
        cmake_file: Path to CMakeLists.txt file

    Returns:
        True if file contains valid project() declaration, False otherwise
    """
    try:
        with open(cmake_file, 'r', encoding='utf-8') as f:
            content = f.read()

        # Check for project() declaration - handle both single line and multi-line
        # Pattern: project(...) where ... can span multiple lines
        if 'project(' not in content:
            return False

        # Try to find complete project() declaration
        # Look for project( followed by closing parenthesis
        lines = content.split('\n')
        in_project_decl = False
        paren_count = 0

        for line in lines:
            stripped = line.strip()
            # Skip comments
            if stripped.startswith('#'):
                continue

            # Check for project( on a line
            if 'project(' in stripped:
                in_project_decl = True
                # Count opening parentheses
                paren_count = stripped.count('(') - stripped.count(')')
                # Check if it's a single-line declaration
                if ')' in stripped and paren_count == 0:
                    return True
            elif in_project_decl:
                # Continue counting parentheses
                paren_count += stripped.count('(') - stripped.count(')')
                if paren_count == 0 and ')' in stripped:
                    return True

        return False
    except (IOError, UnicodeDecodeError):
        return False


def find_project_root(start_dir: Path, max_depth: int = 5) -> Optional[Path]:
    """Find project root by looking for CMakeLists.txt with valid project() declaration

    This function searches upward from start_dir to find the ESP-IDF project root.
    It validates that CMakeLists.txt contains a valid project() declaration.
    The caller should check for components/ directory if needed.

    Args:
        start_dir: Starting directory to search from
        max_depth: Maximum number of parent directories to search (default: 5)
                   This prevents finding unrelated projects in parent directories.
                   Typical ESP-IDF projects need 2-3 levels, but deeper structures
                   (e.g., components/my_component/src/) may need up to 4-5 levels.

    Returns:
        Path to project root if found, None otherwise
    """
    current_dir = Path(start_dir).resolve()  # Resolve to absolute path
    depth = 0

    while current_dir != Path('/') and current_dir != Path('') and depth < max_depth:
        cmake_file = current_dir / 'CMakeLists.txt'

        # Check if CMakeLists.txt exists and has valid project() declaration
        if cmake_file.exists() and _has_valid_project_declaration(cmake_file):
            # Found valid project root
            return current_dir

        parent_dir = current_dir.parent
        if parent_dir == current_dir:  # Avoid infinite loop
            break
        current_dir = parent_dir
        depth += 1

    return None


def backup_file(file_path: Path) -> Path:
    """Create a backup of a file"""
    backup_path = file_path.with_suffix(file_path.suffix + '.backup')
    if file_path.exists():
        import shutil
        shutil.copy2(file_path, backup_path)
    return backup_path


def safe_write_file(file_path: Path, content: str, encoding: str = 'utf-8') -> bool:
    """Safely write content to file without backup"""
    try:
        # Write new content directly
        with open(file_path, 'w', encoding=encoding) as f:
            f.write(content)

        print(f'✅ Updated {file_path}')
        return True
    except Exception as e:
        print(f'❌ Error writing {file_path}: {e}')
        return False
