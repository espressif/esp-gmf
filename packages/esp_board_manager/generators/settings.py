# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
Settings and configuration for ESP Board Manager
"""

import os
from pathlib import Path
from typing import Dict, Any, Set


class BoardManagerConfig:
    """Configuration class for ESP Board Manager"""

    # File extensions
    SOURCE_EXTENSIONS = {'.c', '.cpp', '.cc', '.cxx', '.h', '.hpp'}
    YAML_EXTENSIONS = {'.yml', '.yaml'}
    PYTHON_EXTENSIONS = {'.py'}

    # Exclude patterns
    EXCLUDE_DIRS = {'__pycache__', '.git', 'build', 'cmake-build-*', '.vscode', '.idea'}
    EXCLUDE_FILES = {'.DS_Store', 'Thumbs.db', '*.swp', '*.tmp'}

    # C constant prefixes
    C_CONSTANT_PREFIXES = {
        'GPIO_', 'I2C_', 'I2S_', 'SDMMC_', 'SPI_', 'UART_', 'PWM_', 'LEDC_',
        'LCD_', 'SD_', 'ADC_', 'DAC_', 'RMT_', 'TWAI_', 'CAN_', 'USB_',
        'BIT64'
    }

    # Default values
    DEFAULT_BOARD = 'esp32s3_korvo_3'
    DEFAULT_VERSION = '1.0.0'
    DEFAULT_MANUFACTURER = 'ESPRESSIF'

    # Scan settings
    MAX_SCAN_DEPTH = 5

    # SDKConfig settings
    SDKCONFIG_BACKUP_SUFFIX = '.backup'

    @classmethod
    def get_source_extensions(cls) -> Set[str]:
        """Get source file extensions"""
        return cls.SOURCE_EXTENSIONS.copy()

    @classmethod
    def get_exclude_dirs(cls) -> Set[str]:
        """Get directories to exclude"""
        return cls.EXCLUDE_DIRS.copy()

    @classmethod
    def get_exclude_files(cls) -> Set[str]:
        """Get files to exclude"""
        return cls.EXCLUDE_FILES.copy()

    @classmethod
    def get_c_constant_prefixes(cls) -> Set[str]:
        """Get C constant prefixes"""
        return cls.C_CONSTANT_PREFIXES.copy()

    @classmethod
    def should_skip_directory(cls, dirname: str) -> bool:
        """Check if directory should be skipped"""
        if dirname.startswith('.'):
            return True

        for pattern in cls.EXCLUDE_DIRS:
            if pattern.endswith('*'):
                if dirname.startswith(pattern[:-1]):
                    return True
            elif dirname == pattern:
                return True

        return False

    @classmethod
    def should_skip_file(cls, filename: str) -> bool:
        """Check if file should be skipped"""
        for pattern in cls.EXCLUDE_FILES:
            if pattern.endswith('*'):
                if filename.endswith(pattern[1:]):
                    return True
            elif filename == pattern:
                return True

        return False


class EnvironmentConfig:
    """Environment-specific configuration"""

    @staticmethod
    def get_project_dir() -> str:
        """Get project directory from environment"""
        return os.environ.get('PROJECT_DIR', '')

    @staticmethod
    def get_idf_path() -> str:
        """Get ESP-IDF path from environment"""
        return os.environ.get('IDF_PATH', '')

    @staticmethod
    def get_board_customer_path() -> str:
        """Get board customer path from environment"""
        return os.environ.get('BOARD_CUSTOMER_PATH', '')

    @staticmethod
    def is_verbose_mode() -> bool:
        """Check if verbose mode is enabled"""
        return os.environ.get('GMF_VERBOSE', '').lower() in ('1', 'true', 'yes')

    @staticmethod
    def get_log_level() -> str:
        """Get log level from environment"""
        return os.environ.get('GMF_LOG_LEVEL', 'INFO').upper()
