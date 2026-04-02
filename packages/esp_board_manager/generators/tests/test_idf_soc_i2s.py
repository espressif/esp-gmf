# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT

import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from generators.utils.idf_soc_i2s import soc_i2s_hw_layout_version_from_idf


@pytest.fixture
def fake_idf_with_soc(tmp_path, monkeypatch):
    idf = tmp_path / 'idf'
    caps = idf / 'components' / 'soc' / 'esp32foobar' / 'include' / 'soc' / 'soc_caps.h'
    caps.parent.mkdir(parents=True)
    monkeypatch.setenv('IDF_PATH', str(idf))
    return caps


def test_soc_caps_v2(fake_idf_with_soc: Path) -> None:
    fake_idf_with_soc.write_text('#define SOC_I2S_HW_VERSION_2        (1)\n', encoding='utf-8')
    assert soc_i2s_hw_layout_version_from_idf('esp32foobar') == 2


def test_soc_caps_v1(fake_idf_with_soc: Path) -> None:
    fake_idf_with_soc.write_text('#define SOC_I2S_HW_VERSION_1        (1)\n', encoding='utf-8')
    assert soc_i2s_hw_layout_version_from_idf('esp32foobar') == 1


def test_soc_caps_v2_wins_when_both(fake_idf_with_soc: Path) -> None:
    fake_idf_with_soc.write_text(
        '#define SOC_I2S_HW_VERSION_1 0\n#define SOC_I2S_HW_VERSION_2 (1)\n',
        encoding='utf-8',
    )
    assert soc_i2s_hw_layout_version_from_idf('esp32foobar') == 2


def test_no_idf_path(monkeypatch) -> None:
    monkeypatch.delenv('IDF_PATH', raising=False)
    assert soc_i2s_hw_layout_version_from_idf('esp32c5') is None
