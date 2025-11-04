# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-MIT
#
# See LICENSE file for details.

"""
Board utilities for ESP Board Manager
"""

import os
import yaml
from typing import Optional, Dict, Any
from pathlib import Path


# Global board information cache
_board_info_cache: Optional[Dict[str, Any]] = None
_board_path: Optional[str] = None


def set_board_path(board_path: str) -> None:
    """Set the board path and clear the cache

    Args:
        board_path: Path to board directory containing board_info.yaml
    """
    global _board_path, _board_info_cache
    _board_path = board_path
    _board_info_cache = None  # Clear cache when board path changes


def get_board_path() -> Optional[str]:
    """Get the current board path

    Returns:
        Current board path or None if not set
    """
    return _board_path


def _load_board_info() -> Optional[Dict[str, Any]]:
    """Load board information from board_info.yaml file (internal function)

    Returns:
        Dictionary containing board information or None if not found
    """
    global _board_info_cache, _board_path

    if _board_info_cache is not None:
        return _board_info_cache

    if not _board_path:
        return None

    board_info_path = os.path.join(_board_path, 'board_info.yaml')

    if not os.path.exists(board_info_path):
        return None

    try:
        with open(board_info_path, 'r', encoding='utf-8') as f:
            board_yml = yaml.safe_load(f)
            _board_info_cache = board_yml
            return board_yml
    except Exception:
        return None


def get_chip_name() -> Optional[str]:
    """Get chip name from cached board information
    Returns:
        Chip name (e.g., 'esp32', 'esp32s3') or None if not found
    """
    board_info = _load_board_info()
    if not board_info:
        return None
    chip_name_lower = board_info.get('chip').lower()
    return chip_name_lower


def get_board_name() -> Optional[str]:
    """Get board name from cached board information
    Returns:
        Board name or None if not found
    """
    board_info = _load_board_info()
    if not board_info:
        return None
    return board_info.get('board')


def get_board_version() -> Optional[str]:
    """Get board version from cached board information

    Returns:
        Board version or None if not found
    """
    board_info = _load_board_info()
    if not board_info:
        return None

    return board_info.get('version')


def get_board_description() -> Optional[str]:
    """Get board description from cached board information

    Returns:
        Board description or None if not found
    """
    board_info = _load_board_info()
    if not board_info:
        return None

    return board_info.get('description')


def get_board_manufacturer() -> Optional[str]:
    """Get board manufacturer from cached board information

    Returns:
        Board manufacturer or None if not found
    """
    board_info = _load_board_info()
    if not board_info:
        return None

    return board_info.get('manufacturer')


def get_board_info() -> Optional[Dict[str, Any]]:
    """Get complete board information from cache

    Returns:
        Dictionary containing board information or None if not found
    """
    return _load_board_info()


# Legacy functions for backward compatibility
def get_chip_name_from_board_path(board_path: str) -> Optional[str]:
    """Extract chip name from board_info.yaml file (legacy function)

    Args:
        board_path: Path to board directory containing board_info.yaml

    Returns:
        Chip name (e.g., 'esp32', 'esp32s3') or None if not found
    """
    board_info_path = os.path.join(board_path, 'board_info.yaml')

    if not os.path.exists(board_info_path):
        return None

    try:
        with open(board_info_path, 'r', encoding='utf-8') as f:
            board_yml = yaml.safe_load(f)
            chip = board_yml.get('chip')
            return chip
    except Exception:
        return None


def get_board_info_from_path(board_path: str) -> Optional[Dict[str, Any]]:
    """Extract complete board information from board_info.yaml file (legacy function)

    Args:
        board_path: Path to board directory containing board_info.yaml

    Returns:
        Dictionary containing board information or None if not found
    """
    board_info_path = os.path.join(board_path, 'board_info.yaml')

    if not os.path.exists(board_info_path):
        return None

    try:
        with open(board_info_path, 'r', encoding='utf-8') as f:
            board_yml = yaml.safe_load(f)
            return board_yml
    except Exception:
        return None
