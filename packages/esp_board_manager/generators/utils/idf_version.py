# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
IDF version detection utility for ESP Board Manager.
Parses ESP-IDF version from esp_idf_version.h to enable version-aware
peripheral parser routing and compatibility handling.
"""

import os
import re

_idf_version = None


def get_idf_version():
    """
    Parse the current ESP-IDF version from esp_idf_version.h.

    Returns:
        tuple: (major, minor, patch), e.g. (5, 5, 3).
               Returns (0, 0, 0) if IDF_PATH is not set or version cannot be determined.
    """
    global _idf_version
    if _idf_version is not None:
        return _idf_version

    idf_path = os.environ.get('IDF_PATH')
    if not idf_path:
        _idf_version = (0, 0, 0)
        return _idf_version

    version_h = os.path.join(idf_path, 'components', 'esp_common', 'include', 'esp_idf_version.h')

    major, minor, patch = 0, 0, 0
    try:
        with open(version_h, 'r') as f:
            content = f.read()
            major_match = re.search(r'#define\s+ESP_IDF_VERSION_MAJOR\s+(\d+)', content)
            minor_match = re.search(r'#define\s+ESP_IDF_VERSION_MINOR\s+(\d+)', content)
            patch_match = re.search(r'#define\s+ESP_IDF_VERSION_PATCH\s+(\d+)', content)

            if major_match:
                major = int(major_match.group(1))
            if minor_match:
                minor = int(minor_match.group(1))
            if patch_match:
                patch = int(patch_match.group(1))
    except FileNotFoundError:
        pass

    _idf_version = (major, minor, patch)
    return _idf_version


def get_idf_major_version():
    """Return only the major version number."""
    return get_idf_version()[0]


def get_periph_version_dir():
    """
    Return the peripheral version directory name based on the IDF major version.

    Returns:
        str: 'v2' for IDF v6.0+, 'v1' otherwise.
    """
    major, _, _ = get_idf_version()
    if major >= 6:
        return 'v2'
    return 'v1'
