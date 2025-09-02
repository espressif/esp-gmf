# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
Unified name validator for ESP Board Manager
Provides consistent name validation for both devices and peripherals
"""

import re
from typing import Optional, Tuple

def validate_component_name(name: str) -> bool:
    """
    Validate component name using unified rules

    Rules:
    - Unique unambiguous strings
    - Lowercase characters
    - Can be combinations of numbers and letters
    - Cannot be numbers alone
    - Must start with a letter
    - Can contain underscores
    """
    if not name or not isinstance(name, str):
        return False

    # Must be lowercase
    if name != name.lower():
        return False

    # Cannot be numbers alone
    if name.isdigit():
        return False

    # Must contain only lowercase letters, numbers, and underscores
    if not re.match(r'^[a-z][a-z0-9_]*$', name):
        return False

    return True

def parse_legacy_name(name: str) -> Optional[Tuple[str, int, int]]:
    """
    Parse legacy name format: label[-index[-subindex]]

    Returns:
        Tuple of (label, index, subindex) or None if invalid
        index and subindex are -1 if not present
    """
    if not name or not isinstance(name, str):
        return None

    # Split the name into parts
    parts = name.split('-')
    if len(parts) > 3:  # Too many parts
        return None

    # Validate label
    label = parts[0]
    if not _is_valid_label(label):
        return None

    # Handle default values
    if len(parts) == 1:  # Just label
        return (label, -1, -1)

    # Validate index
    try:
        index_str = parts[1]
        if not _is_valid_number(index_str, 0, 99):
            return None
        index = int(index_str)

        # Handle subindex
        if len(parts) == 2:  # No subindex
            return (label, index, -1)

        # Validate subindex
        subindex_str = parts[2]
        if not _is_valid_number(subindex_str, 0, 99):
            return None
        subindex = int(subindex_str)

        return (label, index, subindex)

    except ValueError:
        return None

def _is_valid_label(label_str: str) -> bool:
    """Validate component label string"""
    if not label_str:
        return False

    # Must start with letter
    if not label_str[0].islower():
        return False

    # Must contain only lowercase letters, numbers, or underscores
    if not all(c.islower() or c.isdigit() or c == '_' for c in label_str):
        return False

    return True

def _is_valid_number(num_str: str, min_val: int, max_val: int) -> bool:
    """Validate a number string is in range and has no leading zeros"""
    if not num_str:
        return False

    # Check for leading zeros
    if len(num_str) > 1 and num_str[0] == '0':
        return False

    try:
        num = int(num_str)
        return min_val <= num <= max_val
    except ValueError:
        return False

# Backward compatibility functions
def parse_component_name(name: str):
    """
    Backward compatibility function for legacy code
    Returns a simple object with the parsed components
    """
    result = parse_legacy_name(name)
    if result is None:
        raise ValueError(f'Invalid component name format: {name}')

    label, index, subindex = result

    class LegacyParser:
        def __init__(self, label, index, subindex, raw_name):
            self.label = label
            self.index = index
            self.subindex = subindex
            self.raw_name = raw_name
            self.valid = True

        @property
        def name(self):
            """Return the original format name"""
            if self.index == -1 and self.subindex == -1:
                return self.label
            elif self.subindex == -1:
                return f'{self.label}-{self.index}'
            else:
                return f'{self.label}-{self.index}-{self.subindex}'

        @property
        def type(self):
            """Return the type (same as label for backward compatibility)"""
            return self.label

        @property
        def c_name(self):
            """Return the C-compatible name (with underscores)"""
            index = 0 if self.index == -1 else self.index
            subindex = 0 if self.subindex == -1 else self.subindex
            return f'{self.label}_{index}_{subindex}'

        def __str__(self):
            return f"ComponentName(label='{self.label}', index={self.index}, subindex={self.subindex})"

    return LegacyParser(label, index, subindex, name)
