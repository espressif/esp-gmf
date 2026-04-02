# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT

"""Tests for main/idf_component.yml override board path heuristics."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

import pytest

from generators.utils.main_idf_override import (
    collect_main_override_board_paths,
    dependency_name_suggests_main_override_board_scan,
)


@pytest.mark.parametrize(
    'dep,expected',
    [
        ('espressif/esp_board_manager', False),
        ('vendor/foo_board_manager', False),
        ('acme/my_boards_pack', True),
        ('acme/foo_boards_bar', True),
        ('acme/foo_board_bar', True),
        ('acme/custom_board', True),
        ('espressif/gmf_core', False),
        ('espressif/esp_capture', False),
    ],
)
def test_dependency_name_suggests_main_override_board_scan(dep: str, expected: bool) -> None:
    assert dependency_name_suggests_main_override_board_scan(dep) is expected


def test_collect_main_override_board_paths_resolves_relative(tmp_path: Path) -> None:
    board_root = tmp_path / 'ext' / 'my_boards'
    board_root.mkdir(parents=True)
    (board_root / 'board_info.yaml').write_text('chip: esp32\n', encoding='utf-8')
    (board_root / 'board_peripherals.yaml').write_text('peripherals: []\n', encoding='utf-8')
    (board_root / 'board_devices.yaml').write_text('devices: []\n', encoding='utf-8')

    main_dir = tmp_path / 'main'
    main_dir.mkdir()
    rel = Path('..') / 'ext' / 'my_boards'
    (main_dir / 'idf_component.yml').write_text(
        f'dependencies:\n  acme/ws_boards_vendor:\n    override_path: {rel.as_posix()}\n',
        encoding='utf-8',
    )

    found, missing = collect_main_override_board_paths(tmp_path)
    assert missing == []
    assert len(found) == 1
    assert found[0][0] == 'acme/ws_boards_vendor'
    assert found[0][1] == board_root.resolve()


def test_collect_accepts_absolute_override_path(tmp_path: Path) -> None:
    board_root = tmp_path / 'abs_boards_root'
    board_root.mkdir(parents=True)
    (board_root / 'board_info.yaml').write_text('chip: esp32\n', encoding='utf-8')
    (board_root / 'board_peripherals.yaml').write_text('peripherals: []\n', encoding='utf-8')
    (board_root / 'board_devices.yaml').write_text('devices: []\n', encoding='utf-8')

    main_dir = tmp_path / 'main'
    main_dir.mkdir()
    abs_literal = str(board_root.resolve())
    (main_dir / 'idf_component.yml').write_text(
        f'dependencies:\n  acme/pkg_boards_abs:\n    override_path: {abs_literal}\n',
        encoding='utf-8',
    )

    found, missing = collect_main_override_board_paths(tmp_path)
    assert missing == []
    assert len(found) == 1
    assert found[0][1] == board_root.resolve()


def test_collect_reports_missing_override(tmp_path: Path) -> None:
    main_dir = tmp_path / 'main'
    main_dir.mkdir()
    (main_dir / 'idf_component.yml').write_text(
        'dependencies:\n  acme/foo_boards_x:\n    override_path: ../nope\n',
        encoding='utf-8',
    )
    found, missing = collect_main_override_board_paths(tmp_path)
    assert found == []
    assert len(missing) == 1
    assert missing[0][0] == 'acme/foo_boards_x'
    assert missing[0][1] == '../nope'
