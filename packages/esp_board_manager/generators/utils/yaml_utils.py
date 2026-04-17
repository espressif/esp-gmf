# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
YAML utilities for ESP Board Manager
"""

import yaml
from pathlib import Path
from typing import Any, Dict, Optional, Union
from .logger import LoggerMixin


class BoardConfigYamlError(Exception):
    """Board YAML must be readable and valid; empty file and syntax errors are distinct fatal cases."""

    REASON_EMPTY_FILE = 'empty_file'
    REASON_YAML_SYNTAX = 'yaml_syntax'
    REASON_NO_DOCUMENT = 'no_yaml_document'
    REASON_NOT_A_MAPPING = 'not_a_mapping'
    REASON_MISSING_KEY = 'missing_key'
    REASON_EMPTY_LIST = 'empty_list'

    def __init__(self, file_path: Union[str, Path], reason: str, detail: str = ''):
        self.file_path = str(file_path)
        self.reason = reason
        self.detail = detail or ''
        if reason == self.REASON_EMPTY_FILE:
            msg = f'Board YAML file is empty or contains only whitespace ({self.file_path}).'
        elif reason == self.REASON_YAML_SYNTAX:
            msg = f'Board YAML syntax error ({self.file_path}): {self.detail}'
        elif reason == self.REASON_NO_DOCUMENT:
            msg = (
                f'Board YAML has no document or resolves to null ({self.file_path}). '
                'Add a mapping root (e.g. peripherals:/devices:) or fix comment-only content.'
            )
        elif reason == self.REASON_NOT_A_MAPPING:
            msg = f'Board YAML structure/type error ({self.file_path}): {self.detail}'
        elif reason == self.REASON_MISSING_KEY:
            msg = f'Board YAML missing required key {self.detail!r} ({self.file_path}).'
        elif reason == self.REASON_EMPTY_LIST:
            msg = f'Board YAML required list {self.detail!r} is empty ({self.file_path}).'
        else:
            msg = f'Board YAML error ({self.file_path}): {reason} {self.detail}'
        super().__init__(msg)


def load_board_yaml_file(file_path: Union[str, Path]) -> Dict[str, Any]:
    """
    Load a single board YAML file (e.g. board_peripherals.yaml).

    Semantics:
        - File exists but is only whitespace, or parses to null (e.g. comments-only): returns {}.
        - YAML syntax error: raises BoardConfigYamlError (yaml_syntax).
        - Parsed root is not a mapping: raises BoardConfigYamlError (not_a_mapping).

    Raises:
        FileNotFoundError: path is not an existing file.
        BoardConfigYamlError: YAML syntax error or non-dict document root.
    """
    p = Path(file_path)
    if not p.is_file():
        raise FileNotFoundError(str(p))
    text = p.read_text(encoding='utf-8')
    if not text.strip():
        return {}
    try:
        data = yaml.safe_load(text)
    except yaml.YAMLError as e:
        raise BoardConfigYamlError(p, BoardConfigYamlError.REASON_YAML_SYNTAX, str(e)) from e
    if data is None:
        return {}
    if not isinstance(data, dict):
        raise BoardConfigYamlError(p, BoardConfigYamlError.REASON_NOT_A_MAPPING, type(data).__name__)
    return data


def load_board_yaml_strict(file_path: Union[str, Path]) -> Dict[str, Any]:
    """Deprecated alias for :func:`load_board_yaml_file` (kept for backward compatibility)."""
    return load_board_yaml_file(file_path)


class YamlUtils(LoggerMixin):
    """YAML utilities with logging support and error handling"""

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
