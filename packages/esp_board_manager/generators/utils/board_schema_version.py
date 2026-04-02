# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# Board-level YAML schema `version` (parsing contract): identifies which schema
# generation the file follows. This release implements generation **1.0.0**;
# omitting `version` means “use the current generation”. Future Board Manager
# releases may add new generations (e.g. after breaking YAML changes).

from __future__ import annotations

from typing import Any, FrozenSet, Optional

# Schema generation implemented by this Board Manager toolchain (not “the only value forever”).
CURRENT_BOARD_YAML_SCHEMA_VERSION = '1.0.0'

# All schema tags this release’s parser accepts. Extend when new generations ship.
SUPPORTED_BOARD_YAML_SCHEMA_VERSIONS: FrozenSet[str] = frozenset({CURRENT_BOARD_YAML_SCHEMA_VERSION})

# YAML may use this alias; it resolves to CURRENT_BOARD_YAML_SCHEMA_VERSION for checks and metadata.
_SCHEMA_DEFAULT_ALIAS = 'default'


def normalized_schema_version(value: Any) -> Optional[str]:
    if value is None:
        return None
    s = str(value).strip()
    if not s:
        return None
    # YAML parses unquoted 1.0 as float; normalize common numeric forms to semver
    if isinstance(value, (int, float)):
        parts = s.split('.')
        while len(parts) < 3:
            parts.append('0')
        s = '.'.join(parts)
    return s


def is_schema_version_default_alias(value: Any) -> bool:
    """True if value is the reserved word ``default`` (case-insensitive)."""
    n = normalized_schema_version(value)
    return n is not None and n.lower() == _SCHEMA_DEFAULT_ALIAS


def schema_version_is_recognized(value: Any) -> bool:
    """True if omitted/empty (implicit current generation), ``default`` alias, or a supported tag."""
    if value is None:
        return True
    if isinstance(value, str) and not value.strip():
        return True
    if is_schema_version_default_alias(value):
        return True
    n = normalized_schema_version(value)
    return n is not None and n in SUPPORTED_BOARD_YAML_SCHEMA_VERSIONS


def schema_version_is_allowed(value: Any) -> bool:
    """Alias for schema_version_is_recognized (backward compatible name)."""
    return schema_version_is_recognized(value)


def resolved_board_info_version_string(version_raw: Any) -> str:
    """
    Value for generated ``g_esp_board_info.version`` (C string).

    - Missing / empty → current generation tag.
    - ``default`` (any casing) → current generation tag.
    - Otherwise returns the stripped string from YAML (e.g. ``1.0.0``, or an unrecognized value).
    """
    if version_raw is None:
        return CURRENT_BOARD_YAML_SCHEMA_VERSION
    s = str(version_raw).strip()
    if not s:
        return CURRENT_BOARD_YAML_SCHEMA_VERSION
    if s.lower() == _SCHEMA_DEFAULT_ALIAS:
        return CURRENT_BOARD_YAML_SCHEMA_VERSION
    return s


def warn_if_invalid_board_yaml_schema_version(logger, value: Any, where: str) -> None:
    if schema_version_is_recognized(value):
        return
    logger.warning(
        '[Board Manager] Unrecognized board YAML schema `version` in %s: %r. '
        'This Board Manager release implements schema generation %r; omit `version`, '
        'use `version: default` (same as current), or set `version: %r`. '
        'Other values will be reserved for future generations after breaking changes.',
        where,
        value,
        CURRENT_BOARD_YAML_SCHEMA_VERSION,
        CURRENT_BOARD_YAML_SCHEMA_VERSION,
    )
