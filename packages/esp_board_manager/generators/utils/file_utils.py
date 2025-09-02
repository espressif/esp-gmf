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


def find_project_root(start_dir: Path) -> Optional[Path]:
    """Find project root by looking for CMakeLists.txt and components directory"""
    current_dir = start_dir
    while current_dir != Path('/') and current_dir != Path(''):
        cmake_file = current_dir / 'CMakeLists.txt'
        components_dir = current_dir / 'components'

        if cmake_file.exists() and components_dir.exists():
            return current_dir

        parent_dir = current_dir.parent
        if parent_dir == current_dir:  # Avoid infinite loop
            break
        current_dir = parent_dir

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
