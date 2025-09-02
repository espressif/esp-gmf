# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
Parser loader for ESP Board Manager
"""

import importlib.util
import os
from pathlib import Path
from typing import Dict, Tuple, Callable, List

def load_parsers(search_dirs: List[str], prefix: str, base_dir: str = None) -> Dict[str, Tuple[str, Callable, Callable]]:
    """
    prefix: 'parse_periph_' for peripherals, 'parse_dev_' for devices
    base_dir: Base directory to search for parser directories (defaults to current directory)
    Returns: Dict[type_name, (version, parse_func, get_includes_func)]
    """
    parsers: Dict[str, Tuple[str, Callable, Callable]] = {}
    version_map = {}

    # If search_dirs is empty but base_dir is provided, use base_dir directly
    if not search_dirs and base_dir:
        search_dirs = ['']  # Empty string will make os.path.join(base_dir, "") = base_dir

    # Special handling for devices directory with new folder structure
    # When prefix is "dev_" and base_dir points to devices directory, load from folder structure
    if prefix == 'dev_' and base_dir and os.path.basename(base_dir) == 'devices':
        parsers.update(load_device_parsers_from_folders(base_dir))
        return parsers

    for search_dir in search_dirs:
        # Construct absolute path if base_dir is provided
        if base_dir:
            search_path = os.path.join(base_dir, search_dir)
        else:
            search_path = search_dir

        if not os.path.isdir(search_path):
            print(f'Warning: Directory not found: {search_path}')
            continue

        # Special handling for devices directory with new folder structure
        if search_dir == 'devices' and prefix == 'dev_':
            parsers.update(load_device_parsers_from_folders(base_dir))
            continue

        # Original logic for other directories
        for fname in os.listdir(search_path):
            if fname.startswith(prefix) and fname.endswith('.py'):
                type_name = fname[len(prefix):-3]
                script_path = os.path.join(search_path, fname)
                spec = importlib.util.spec_from_file_location(type_name, script_path)
                mod = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(mod)
                version = getattr(mod, 'VERSION', None)
                parse_func = getattr(mod, 'parse', None)
                get_includes_func = getattr(mod, 'get_includes', None)
                if not version or not parse_func:
                    raise RuntimeError(f'{fname} missing VERSION or parse()')
                if (type_name, version) in version_map:
                    raise RuntimeError(f'Duplicate parser version: {type_name} v{version}')
                version_map[(type_name, version)] = script_path
                if type_name not in parsers:
                    parsers[type_name] = (version, parse_func, get_includes_func)
    return parsers

def load_device_parsers_from_folders(base_dir: str = None) -> Dict[str, Tuple[str, Callable, Callable]]:
    """
    Load device parsers from the new device folder structure.
    Each device type has its own folder under devices/ directory.

    Args:
        base_dir: Base directory to search for devices directory (defaults to current directory)

    Returns:
        dict: Dictionary of device parsers {device_type: (version, parse_func, get_includes_func)}
    """
    parsers = {}

    # Construct absolute path if base_dir is provided
    if base_dir:
        # Check if base_dir already points to devices directory
        if os.path.basename(base_dir) == 'devices':
            devices_dir = Path(base_dir)
        else:
            devices_dir = Path(base_dir) / 'devices'
    else:
        devices_dir = Path('devices')

    if not devices_dir.exists():
        print(f'Warning: Devices directory not found: {devices_dir}')
        return parsers

    for device_folder in devices_dir.iterdir():
        if not device_folder.is_dir():
            continue

        device_type = device_folder.name
        if not device_type.startswith('dev_'):
            continue

        # Extract the actual device type by removing 'dev_' prefix
        actual_device_type = device_type[4:]  # Remove 'dev_' prefix

        # Look for Python parser file
        parser_file = device_folder / f'{device_type}.py'
        if not parser_file.exists():
            print(f'Warning: No parser file found for device {device_type}: {parser_file}')
            continue

        try:
            # Import the parser module
            spec = importlib.util.spec_from_file_location(device_type, str(parser_file))
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)

            # Look for parse and get_includes functions
            version = getattr(mod, 'VERSION', '1.0.0')
            parse_func = getattr(mod, 'parse', None)  # Device parsers use 'parse' function
            get_includes_func = getattr(mod, 'get_includes', None)

            if parse_func:
                # Add the parser with both the folder name and the actual device type
                parsers[device_type] = (version, parse_func, get_includes_func)
                parsers[actual_device_type] = (version, parse_func, get_includes_func)

                # Use logger for debug output
                from .utils.logger import get_logger
                logger = get_logger('parser_loader')
                logger.debug(f'Loaded parser for {device_type} -> {actual_device_type} (version: {version})')
            else:
                print(f'Warning: No parse function found in {parser_file}')

        except Exception as e:
            print(f'Error loading parser for {device_type}: {e}')

    return parsers
