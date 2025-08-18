# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
Logging utilities for ESP Board Manager
"""

import logging
import sys
from pathlib import Path
from typing import Optional

# Global log level control
_global_log_level = logging.INFO

def set_global_log_level(level: int):
    """Set global log level for all loggers"""
    global _global_log_level
    _global_log_level = level

    # Update all existing loggers
    for logger_name in logging.root.manager.loggerDict:
        logger = logging.getLogger(logger_name)
        if hasattr(logger, 'handlers') and logger.handlers:
            for handler in logger.handlers:
                handler.setLevel(level)
        logger.setLevel(level)

def get_global_log_level() -> int:
    """Get current global log level"""
    return _global_log_level

# Unified icon constants for consistent messaging
class Icons:
    """Unified icon constants for consistent messaging across the application"""

    # Essential status icons only
    SUCCESS = '✅'
    ERROR = '❌'
    WARNING = '⚠️'
    INFO = 'ℹ️'
    STEP = '⚙️'


class ColoredFormatter(logging.Formatter):
    """Custom formatter with colors for console output"""

    COLORS = {
        'DEBUG': '\033[36m',    # Cyan
        'INFO': '\033[32m',     # Green
        'WARNING': '\033[33m',  # Yellow
        'ERROR': '\033[31m',    # Red
        'CRITICAL': '\033[35m', # Magenta
        'RESET': '\033[0m'      # Reset
    }

    def format(self, record):
        # Add color to levelname
        if record.levelname in self.COLORS:
            record.levelname = f"{self.COLORS[record.levelname]}{record.levelname}{self.COLORS['RESET']}"

        return super().format(record)


def setup_logger(name: str = 'esp_board_manager',
                level: int = None,
                log_file: Optional[Path] = None,
                use_colors: bool = True) -> logging.Logger:
    """Setup logger with console and optional file output"""

    # Use global log level if not specified
    if level is None:
        level = _global_log_level

    logger = logging.getLogger(name)
    logger.setLevel(level)

    # Clear existing handlers
    logger.handlers.clear()

    # Console handler
    console_handler = logging.StreamHandler(sys.stdout)
    console_handler.setLevel(level)

    if use_colors:
        formatter = ColoredFormatter(
            '%(asctime)s - %(name)s - %(levelname)s - %(message)s',
            datefmt='%H:%M:%S'
        )
    else:
        formatter = logging.Formatter(
            '%(asctime)s - %(name)s - %(levelname)s - %(message)s',
            datefmt='%H:%M:%S'
        )

    console_handler.setFormatter(formatter)
    logger.addHandler(console_handler)

    # File handler (optional)
    if log_file:
        file_handler = logging.FileHandler(log_file)
        file_handler.setLevel(level)
        file_formatter = logging.Formatter(
            '%(asctime)s - %(name)s - %(levelname)s - %(message)s'
        )
        file_handler.setFormatter(file_formatter)
        logger.addHandler(file_handler)

    return logger


def get_logger(name: str = 'esp_board_manager') -> logging.Logger:
    """Get logger instance"""
    logger = logging.getLogger(name)

    # If logger has no handlers, configure it with default settings
    if not logger.handlers:
        logger.setLevel(_global_log_level)

        # Console handler
        console_handler = logging.StreamHandler(sys.stdout)
        console_handler.setLevel(_global_log_level)

        formatter = logging.Formatter(
            '%(message)s'  # Simplified format for better readability
        )
        console_handler.setFormatter(formatter)
        logger.addHandler(console_handler)
    else:
        # Update existing handlers with global log level
        logger.setLevel(_global_log_level)
        for handler in logger.handlers:
            handler.setLevel(_global_log_level)

    return logger


def get_debug_logger(name: str = 'esp_board_manager') -> logging.Logger:
    """Get debug logger instance for detailed output"""
    logger = logging.getLogger(f'{name}_debug')

    # If logger has no handlers, configure it with debug settings
    if not logger.handlers:
        logger.setLevel(logging.DEBUG)

        # Console handler
        console_handler = logging.StreamHandler(sys.stdout)
        console_handler.setLevel(logging.DEBUG)

        formatter = logging.Formatter(
            '%(message)s'  # Simplified format for better readability
        )
        console_handler.setFormatter(formatter)
        logger.addHandler(console_handler)

    return logger


class LoggerMixin:
    """Mixin class to add logging capabilities to any class"""

    def __init__(self):
        self._logger = None

    @property
    def logger(self):
        """Get logger for this class"""
        if self._logger is None:
            self._logger = get_logger(f'{self.__class__.__module__}.{self.__class__.__name__}')
        return self._logger


# Cached logger instance for print_* functions to avoid repeated get_logger() calls
_module_logger = None

def _get_module_logger():
    """Get cached module logger instance"""
    global _module_logger
    if _module_logger is None:
        _module_logger = get_logger()
    return _module_logger


# Convenience functions for consistent messaging (optimized)
def print_step(step_num: int, total_steps: int, message: str):
    """Print a formatted step message"""
    _get_module_logger().info(f'{Icons.STEP} Step {step_num}/{total_steps}: {message}')


def print_success(message: str):
    """Print a success message"""
    _get_module_logger().info(f'{Icons.SUCCESS} {message}')


def print_error(message: str):
    """Print an error message"""
    _get_module_logger().error(f'{Icons.ERROR} {message}')


def print_warning(message: str):
    """Print a warning message"""
    _get_module_logger().warning(f'{Icons.WARNING} {message}')


def print_info(message: str):
    """Print an info message"""
    _get_module_logger().info(f'{Icons.INFO} {message}')


def print_yaml_error(message: str, file_path: str = '', details: str = ''):
    """Print a YAML parsing error message"""
    logger = _get_module_logger()
    logger.error(f'{Icons.ERROR} YAML PARSING ERROR: {message}')
    if file_path:
        logger.error(f'   File: {file_path}')
    if details:
        logger.info(f'   {Icons.INFO} {details}')


def print_file_error(message: str, file_path: str = '', details: str = ''):
    """Print a file-related error message"""
    logger = _get_module_logger()
    logger.error(f'{Icons.ERROR} FILE ERROR: {message}')
    if file_path:
        logger.error(f'   File: {file_path}')
    if details:
        logger.info(f'   {Icons.INFO} {details}')


def print_process_start(message: str):
    """Print a process start message"""
    _get_module_logger().info(f'{Icons.STEP} {message}')


def print_process_complete(message: str):
    """Print a process completion message"""
    _get_module_logger().info(f'{Icons.SUCCESS} {message}')


def print_scan_result(message: str):
    """Print a scan result message"""
    _get_module_logger().info(f'{Icons.INFO} {message}')


def print_generated_file(file_path: str):
    """Print a file generation message"""
    _get_module_logger().info(f'{Icons.SUCCESS} Generated: {file_path}')


def print_found_item(item_type: str, item_name: str, location: str = ''):
    """Print a found item message"""
    location_info = f' (at: {location})' if location else ''
    _get_module_logger().info(f'{Icons.INFO} Found {item_type}: {item_name}{location_info}')
