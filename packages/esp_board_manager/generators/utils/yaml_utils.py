# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
YAML utilities for ESP Board Manager
"""

import yaml
from pathlib import Path
from typing import Any, Dict, Optional
from .logger import LoggerMixin


class YamlUtils(LoggerMixin):
    """YAML utilities with logging support"""

    @staticmethod
    def load_yaml_safe(file_path: Path) -> Optional[Dict[str, Any]]:
        """Load YAML file with error handling and logging"""
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                return yaml.safe_load(f)
        except FileNotFoundError:
            YamlUtils._log_error('File not found!', str(file_path), 'Please check if the file exists and the path is correct')
            return None
        except yaml.YAMLError as e:
            YamlUtils._log_error('Invalid YAML syntax!', str(file_path), f'Please check the YAML syntax and indentation. Error: {e}')
            return None
        except Exception as e:
            YamlUtils._log_error('Unexpected error!', str(file_path), f'Error: {e}')
            return None

    @staticmethod
    def save_yaml_safe(data: Dict[str, Any], file_path: Path) -> bool:
        """Save YAML file with error handling and logging"""
        try:
            # Ensure directory exists
            file_path.parent.mkdir(parents=True, exist_ok=True)

            with open(file_path, 'w', encoding='utf-8') as f:
                yaml.dump(data, f, default_flow_style=False, sort_keys=False)
            return True
        except PermissionError:
            YamlUtils._log_file_error('Permission denied!', str(file_path), 'Please check file permissions and ensure the directory is writable')
            return False
        except Exception as e:
            YamlUtils._log_file_error('Failed to save file!', str(file_path), f'Error: {e}')
            return False

    @staticmethod
    def _log_error(message: str, file_path: str, details: str):
        """Log error message using module logger"""
        from .logger import _get_module_logger
        logger = _get_module_logger()
        logger.error(f'❌ {message}')
        logger.error(f'   File: {file_path}')
        logger.info(f'   ℹ️  {details}')

    @staticmethod
    def _log_file_error(message: str, file_path: str, details: str):
        """Log file error message using module logger"""
        from .logger import _get_module_logger
        logger = _get_module_logger()
        logger.error(f'❌ FILE ERROR: {message}')
        logger.error(f'   File: {file_path}')
        logger.info(f'   ℹ️  {details}')


# Convenience functions for backward compatibility
def load_yaml_safe(file_path: Path) -> Optional[Dict[str, Any]]:
    """Load YAML file with error handling"""
    return YamlUtils.load_yaml_safe(file_path)


def save_yaml_safe(data: Dict[str, Any], file_path: Path) -> bool:
    """Save YAML file with error handling"""
    return YamlUtils.save_yaml_safe(data, file_path)


def merge_yaml_data(base_data: Dict[str, Any], new_data: Dict[str, Any]) -> Dict[str, Any]:
    """Merge two YAML data structures, with new_data taking precedence"""
    result = base_data.copy()

    for key, value in new_data.items():
        if key in result:
            if isinstance(result[key], dict) and isinstance(value, dict):
                result[key] = merge_yaml_data(result[key], value)
            elif isinstance(result[key], list) and isinstance(value, list):
                # For lists, extend the base list with new items
                result[key].extend(value)
            else:
                result[key] = value
        else:
            result[key] = value

    return result


def extract_yaml_section(data: Dict[str, Any], section_path: str) -> Optional[Any]:
    """Extract a section from YAML data using dot notation"""
    keys = section_path.split('.')
    current = data

    for key in keys:
        if isinstance(current, dict) and key in current:
            current = current[key]
        else:
            return None

    return current
