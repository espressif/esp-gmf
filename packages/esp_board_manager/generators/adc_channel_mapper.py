#!/usr/bin/env python3
"""
# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.
"""

"""ADC channel-to-GPIO mapping helpers."""

from functools import lru_cache
from pathlib import Path
from typing import Iterable, List, Optional

import os
import re


ADC_CHANNEL_MACRO_RE = re.compile(
    r'#define\s+ADC(?P<unit>\d)_CHANNEL_(?P<channel>\d+)_GPIO_NUM\s+(?P<gpio>-?\d+)'
)
ADC_UNIT_RE = re.compile(r'ADC_UNIT_(?P<unit>\d+)')


def _normalize_chip_name(chip_name: str) -> str:
    if not chip_name:
        raise ValueError('chip name is required for ADC metadata extraction')
    return str(chip_name).strip().lower().replace('-', '')


def _default_idf_root() -> Path:
    return Path(__file__).resolve().parents[4] / 'esp-idf'


def _resolve_idf_root(idf_root: Optional[str] = None) -> Path:
    candidates = []
    if idf_root:
        candidates.append(Path(idf_root))

    env_idf_path = os.environ.get('IDF_PATH')
    if env_idf_path:
        candidates.append(Path(env_idf_path))

    candidates.append(_default_idf_root())

    for candidate in candidates:
        if candidate and candidate.exists():
            return candidate

    raise FileNotFoundError('Unable to locate ESP-IDF repository for ADC metadata extraction')


@lru_cache(maxsize=None)
def _load_adc_channel_map(chip_name: str, idf_root: Optional[str] = None) -> dict:
    normalized_chip = _normalize_chip_name(chip_name)
    resolved_idf_root = _resolve_idf_root(idf_root)
    header_path = resolved_idf_root / 'components' / 'soc' / normalized_chip / 'include' / 'soc' / 'adc_channel.h'
    if not header_path.exists():
        raise FileNotFoundError(f'ADC channel header not found: {header_path}')

    mapping = {}
    for line in header_path.read_text(encoding='utf-8').splitlines():
        match = ADC_CHANNEL_MACRO_RE.search(line)
        if not match:
            continue

        unit = int(match.group('unit'))
        channel = int(match.group('channel'))
        gpio = int(match.group('gpio'))
        mapping.setdefault(unit, {})[channel] = gpio

    if not mapping:
        raise ValueError(f'No ADC channel mappings found in {header_path}')

    return mapping


def _normalize_unit(unit: object) -> int:
    if isinstance(unit, int):
        if unit <= 0:
            raise ValueError(f'Unsupported ADC unit value: {unit}')
        return unit

    if isinstance(unit, str):
        match = ADC_UNIT_RE.fullmatch(unit.strip())
        if match:
            parsed = int(match.group('unit'))
            if parsed > 0:
                return parsed
        if unit.strip().isdigit():
            parsed = int(unit.strip())
            if parsed > 0:
                return parsed

    raise ValueError(f'Unsupported ADC unit value: {unit}')


def adc_channel_to_gpio(chip_name: str, unit: object, channel: int, idf_root: Optional[str] = None) -> int:
    channel_map = _load_adc_channel_map(chip_name, idf_root)
    unit_num = _normalize_unit(unit)
    gpio = channel_map.get(unit_num, {}).get(int(channel))
    if gpio is None or gpio < 0:
        raise ValueError(
            f'Unable to map ADC unit {unit} channel {channel} to GPIO for chip {chip_name}'
        )
    return gpio


def _dedupe_preserve(values: Iterable[int]) -> List[int]:
    ordered = []
    seen = set()
    for value in values:
        if value in seen:
            continue
        seen.add(value)
        ordered.append(value)
    return ordered


def adc_channels_to_gpios(
    chip_name: str,
    unit: object,
    channels: Iterable[int],
    idf_root: Optional[str] = None,
) -> List[int]:
    gpios = []
    for channel in channels:
        gpios.append(adc_channel_to_gpio(chip_name, unit, int(channel), idf_root=idf_root))
    return _dedupe_preserve(gpios)


def adc_patterns_to_gpios(
    chip_name: str,
    patterns: Iterable[dict],
    idf_root: Optional[str] = None,
) -> List[int]:
    gpios = []
    for item in patterns:
        gpios.append(
            adc_channel_to_gpio(
                chip_name,
                item.get('unit', 'ADC_UNIT_1'),
                int(item.get('channel', -1)),
                idf_root=idf_root,
            )
        )
    return _dedupe_preserve(gpios)
