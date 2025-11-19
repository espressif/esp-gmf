"""
Path operation utilities for ESP Board Manager
"""

import os
from pathlib import Path
from typing import List, Optional


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
