# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
Configuration generators for ESP Board Manager
"""

# Delay imports to avoid circular dependency issues
def get_config_generator():
    from .config_generator import ConfigGenerator
    return ConfigGenerator

def get_sdkconfig_manager():
    from .sdkconfig_manager import SDKConfigManager
    return SDKConfigManager

def get_dependency_manager():
    from .dependency_manager import DependencyManager
    return DependencyManager

def get_source_scanner():
    from .source_scanner import SourceScanner
    return SourceScanner
