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
from typing import Dict, Tuple, Callable, List, Optional

# Constants for component prefixes and directory names
DEV_PREFIX = 'dev_'
PERIPH_PREFIX = 'periph_'
DEV_PREFIX_LEN = len(DEV_PREFIX)
PERIPH_PREFIX_LEN = len(PERIPH_PREFIX)
DEVICES_DIR = 'devices'
PERIPHERALS_DIR = 'peripherals'

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
    if prefix == DEV_PREFIX and base_dir and os.path.basename(base_dir) == DEVICES_DIR:
        parsers.update(load_component_parsers_from_folders(base_dir, DEV_PREFIX, DEVICES_DIR, add_both_keys=True))
        return parsers

    # Special handling for peripherals directory with new folder structure
    # When prefix is "periph_" and base_dir points to peripherals directory, load from both folders and files
    if prefix == PERIPH_PREFIX and base_dir and os.path.basename(base_dir) == PERIPHERALS_DIR:
        parsers.update(load_component_parsers_from_folders(base_dir, PERIPH_PREFIX, PERIPHERALS_DIR, add_both_keys=False))
        # Continue to also load from direct files in the directory

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
        if search_dir == DEVICES_DIR and prefix == DEV_PREFIX:
            parsers.update(load_component_parsers_from_folders(base_dir, DEV_PREFIX, DEVICES_DIR, add_both_keys=True))
            continue

        # Original logic for other directories - load files directly in the directory
        for fname in os.listdir(search_path):
            # Skip if it's a directory (we already handled subfolders for peripherals)
            if os.path.isdir(os.path.join(search_path, fname)):
                continue
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

def load_component_parsers_from_folders(base_dir: Optional[str], prefix: str, dir_name: str, add_both_keys: bool = False) -> Dict[str, Tuple[str, Callable, Callable]]:
    """
    Generic function to load component parsers from folder structure.
    Each component type can have its own folder under the component directory.
    The folder name should match the file name (e.g., periph_spi/periph_spi.py or dev_xxx/dev_xxx.py).

    Args:
        base_dir: Base directory to search for component directory (defaults to current directory)
        prefix: Component prefix ('dev_' or 'periph_')
        dir_name: Component directory name ('devices' or 'peripherals')
        add_both_keys: If True, add parser with both folder name and type name (for devices).
                      If False, only add with type name (for peripherals).

    Returns:
        dict: Dictionary of component parsers {component_type: (version, parse_func, get_includes_func)}
    """
    parsers = {}
    prefix_len = len(prefix)

    # Construct absolute path if base_dir is provided
    if base_dir:
        # Check if base_dir already points to the component directory
        if os.path.basename(base_dir) == dir_name:
            component_dir = Path(base_dir)
        else:
            component_dir = Path(base_dir) / dir_name
    else:
        component_dir = Path(dir_name)

    if not component_dir.exists():
        print(f'Warning: {dir_name.capitalize()} directory not found: {component_dir}')
        return parsers

    for component_folder in component_dir.iterdir():
        if not component_folder.is_dir():
            continue

        # Check if folder name starts with the expected prefix
        if not component_folder.name.startswith(prefix):
            continue

        # Extract the actual component type by removing prefix
        component_type = component_folder.name[prefix_len:]

        # Look for Python parser file - folder name should match file name
        parser_file = component_folder / f'{component_folder.name}.py'
        if not parser_file.exists():
            print(f'Warning: No parser file found for {dir_name[:-1]} {component_folder.name}: {parser_file}')
            continue

        try:
            # Import the parser module
            spec = importlib.util.spec_from_file_location(component_folder.name, str(parser_file))
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)

            # Look for parse and get_includes functions
            version = getattr(mod, 'VERSION', '1.0.0')
            parse_func = getattr(mod, 'parse', None)
            get_includes_func = getattr(mod, 'get_includes', None)

            if parse_func:
                # Add the parser with the component type name
                parsers[component_type] = (version, parse_func, get_includes_func)

                # For devices, also add with the full folder name
                if add_both_keys:
                    parsers[component_folder.name] = (version, parse_func, get_includes_func)

                # Use logger for debug output
                from .utils.logger import get_logger
                logger = get_logger('parser_loader')
                logger.debug(f'Loaded parser for {component_folder.name} -> {component_type} (version: {version})')
            else:
                print(f'Warning: No parse function found in {parser_file}')

        except Exception as e:
            logger.error(f'Error loading parser for {component_folder.name}: {e}')

    return parsers

# Backward compatibility aliases
def load_device_parsers_from_folders(base_dir: str = None) -> Dict[str, Tuple[str, Callable, Callable]]:
    """Load device parsers from folder structure (backward compatibility)"""
    return load_component_parsers_from_folders(base_dir, DEV_PREFIX, DEVICES_DIR, add_both_keys=True)

def load_peripheral_parsers_from_folders(base_dir: str = None) -> Dict[str, Tuple[str, Callable, Callable]]:
    """Load peripheral parsers from folder structure (backward compatibility)"""
    return load_component_parsers_from_folders(base_dir, PERIPH_PREFIX, PERIPHERALS_DIR, add_both_keys=False)
