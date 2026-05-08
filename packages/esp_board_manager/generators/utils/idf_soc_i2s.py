# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""Read ESP-IDF SoC I2S capability flags from soc_caps.h (matches driver headers)."""

import os
import re
from pathlib import Path
from typing import Optional


def _soc_caps_text_from_idf(chip_name: Optional[str]) -> Optional[str]:
    if not chip_name:
        return None
    idf_path = os.environ.get('IDF_PATH')
    if not idf_path:
        return None

    chip_dir = chip_name.strip().lower().replace('-', '')
    roots = Path(idf_path) / 'components' / 'soc' / chip_dir / 'include'
    candidates = [
        roots / 'soc' / 'soc_caps.h',
        roots / 'soc_caps.h',
    ]

    for caps_path in candidates:
        if not caps_path.is_file():
            continue
        try:
            return caps_path.read_text(encoding='utf-8', errors='ignore')
        except OSError:
            continue
    return None


def soc_i2s_cap_from_idf(chip_name: Optional[str], cap_name: str) -> Optional[bool]:
    """Return whether an I2S SoC cap is enabled in the active IDF.

    Reads ``$IDF_PATH/components/soc/<chip>/include/soc/soc_caps.h`` (or the
    legacy ``include/soc_caps.h`` path). Returns ``None`` if IDF data is not
    available so callers can fall back to stable chip heuristics.
    """
    if not cap_name:
        return None

    text = _soc_caps_text_from_idf(chip_name)
    if text is None:
        return None

    define_re = re.compile(
        r'#\s*define\s+'
        + re.escape(cap_name)
        + r'(?:\s+\(?\s*(\d+)\s*\)?)?'
    )
    match = define_re.search(text)
    if not match:
        return False
    value = match.group(1)
    return value is None or int(value) != 0


def soc_i2s_hw_layout_version_from_idf(chip_name: Optional[str]) -> Optional[int]:
    """Return ``1`` or ``2`` for ``SOC_I2S_HW_VERSION_1`` vs v2 IP layout.

    This mirrors ``i2s_std.h`` / ``i2s_tdm.h``: slot and clock structs differ when
    ``SOC_I2S_HW_VERSION_1`` is set vs ``SOC_I2S_HW_VERSION_2``.

    Reads ``$IDF_PATH/components/soc/<chip>/include/soc/soc_caps.h`` (or the legacy
    ``include/soc_caps.h`` path). Returns ``None`` if the file or macros are missing
    so callers can fall back to heuristics.

    Args:
        chip_name: Normalized chip id, e.g. ``esp32c5``, ``esp32``, ``esp32s2``.
    """
    text = _soc_caps_text_from_idf(chip_name)
    if text is None:
        return None

    define_re = re.compile(
        r'#\s*define\s+SOC_I2S_HW_VERSION_([12])\s+\(?\s*(\d+)\s*\)?'
    )

    v1_val: Optional[int] = None
    v2_val: Optional[int] = None
    for m in define_re.finditer(text):
        which, val = m.group(1), int(m.group(2))
        if which == '1':
            v1_val = val
        else:
            v2_val = val
    if v2_val == 1:
        return 2
    if v1_val == 1:
        return 1
    return None
