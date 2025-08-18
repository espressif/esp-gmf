"""
Path operation utilities for ESP Board Manager
"""

import os
from pathlib import Path
from typing import List, Optional


def find_components_boards(project_root: Path) -> List[Path]:
    """Search for 'boards' directories in all components"""
    boards_dirs = []
    components_dir = project_root / 'components'

    if not components_dir.exists():
        print(f'Warning: Components directory not found: {components_dir}')
        return boards_dirs

    print(f'Searching for boards directories in components: {components_dir}')

    for component in components_dir.iterdir():
        if component.is_dir():
            boards_path = component / 'boards'
            if boards_path.exists() and boards_path.is_dir():
                # Skip board_manager's own boards directory to avoid duplication
                if component.name != 'board_manager':
                    boards_dirs.append(boards_path)
                    print(f"Found boards directory in component '{component.name}': {boards_path}")

    return boards_dirs


def scan_board_directory(boards_dir: Path, dir_name: str) -> List[str]:
    """Scan a boards directory and return list of valid boards"""
    board_dirs = []

    if not boards_dir.exists():
        print(f'Warning: {dir_name} directory does not exist: {boards_dir}')
        return board_dirs

    print(f'Scanning {dir_name} directory: {boards_dir}')

    for board_dir in boards_dir.iterdir():
        if board_dir.is_dir():
            kconfig_path = board_dir / 'Kconfig'
            if kconfig_path.exists():
                board_dirs.append(board_dir.name)
                print(f'Found board in {dir_name}: {board_dir.name}')
            else:
                print(f"Warning: Directory '{board_dir.name}' in {dir_name} exists but no Kconfig file found")

    return sorted(board_dirs)


def get_relative_path(from_path: Path, to_path: Path) -> str:
    """Get relative path from one path to another"""
    try:
        return str(to_path.relative_to(from_path))
    except ValueError:
        # If paths are not relative, return the absolute path
        return str(to_path)


def ensure_directory_exists(path: Path) -> bool:
    """Ensure directory exists, create if necessary"""
    try:
        path.mkdir(parents=True, exist_ok=True)
        return True
    except Exception as e:
        print(f'Error creating directory {path}: {e}')
        return False
