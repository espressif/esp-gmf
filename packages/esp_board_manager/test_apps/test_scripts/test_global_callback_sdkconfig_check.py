"""
Tests for sdkconfig consistency checking in board_manager_global_callback.
"""

import os
import sys
from pathlib import Path



class _Task:
    def __init__(self, name, aliases=None):
        self.name = name
        self.aliases = aliases or []


def _make_project(tmp_path: Path, with_sdkconfig: bool = True) -> Path:
    proj = tmp_path / 'proj'
    gen_dir = proj / 'components' / 'gen_bmgr_codes'
    gen_dir.mkdir(parents=True, exist_ok=True)
    (gen_dir / 'board_manager.defaults').write_text(
        '\n'.join(
            [
                'CONFIG_ESP_BOARD_TEST_BOARD=y',
                'CONFIG_ESP_BOARD_NAME="test_board"',
                'CONFIG_ESP_BOARD_PERIPH_I2C_SUPPORT=y',
                'CONFIG_ESP_BOARD_DEV_AUDIO_CODEC_SUPPORT=y',
                'CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_SPI_SUPPORT=y',
            ]
        )
        + '\n',
        encoding='utf-8',
    )
    if with_sdkconfig:
        (proj / 'sdkconfig').write_text('CONFIG_FREERTOS_HZ=1000\n', encoding='utf-8')
    return proj


def _load_callback(bmgr_root: Path, project_dir: Path):
    sys.path.insert(0, str(bmgr_root))
    import idf_ext  # noqa: F401

    ext = idf_ext.action_extensions({}, str(project_dir))
    return ext['global_action_callbacks'][0], idf_ext


def test_parse_bmgr_defaults_symbols(bmgr_root, tmp_path):
    _, idf_ext = _load_callback(bmgr_root, tmp_path)
    defaults = tmp_path / 'board_manager.defaults'
    defaults.write_text(
        '\n'.join(
            [
                'CONFIG_ESP_BOARD_TEST_BOARD=y',
                'CONFIG_ESP_BOARD_PERIPH_I2C_SUPPORT=y',
                'CONFIG_ESP_BOARD_DEV_AUDIO_CODEC_SUPPORT=y',
                'CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_SPI_SUPPORT=y',
            ]
        )
        + '\n',
        encoding='utf-8',
    )
    board, devs, periphs, subtypes = idf_ext._parse_bmgr_defaults_symbols(str(defaults))
    assert board == 'test_board'
    assert devs == {'audio_codec', 'display_lcd'}
    assert periphs == {'i2c'}
    assert subtypes == {'display_lcd': {'spi'}}


def test_callback_warns_on_inconsistent_sdkconfig(bmgr_root, tmp_path, monkeypatch, capsys):
    project_dir = _make_project(tmp_path, with_sdkconfig=True)
    callback, idf_ext = _load_callback(bmgr_root, project_dir)

    def _fake_check(self, **kwargs):
        return {
            'ok': False,
            'issues': ['CONFIG_ESP_BOARD_DEV_AUDIO_CODEC_SUPPORT missing, expected y'],
        }

    monkeypatch.setattr(
        idf_ext.SDKConfigManager,
        'ensure_sdkconfig_consistency',
        _fake_check,
        raising=True,
    )
    callback(
        None,
        {'project_dir': str(project_dir), 'define_cache_entry': []},
        [_Task('build')],
    )
    out = capsys.readouterr().out
    assert '[Board Manager] Checking sdkconfig consistency before action execution...' in out
    assert '[Board Manager] Detected 1 sdkconfig inconsistency issue(s):' in out
    assert 'Please run: idf.py gen-bmgr-config -b test_board' in out


def test_callback_skip_switch(bmgr_root, tmp_path, monkeypatch, capsys):
    project_dir = _make_project(tmp_path, with_sdkconfig=True)
    callback, idf_ext = _load_callback(bmgr_root, project_dir)

    def _should_not_be_called(self, **kwargs):
        raise AssertionError('ensure_sdkconfig_consistency should not be called when skip switch is enabled')

    monkeypatch.setattr(
        idf_ext.SDKConfigManager,
        'ensure_sdkconfig_consistency',
        _should_not_be_called,
        raising=True,
    )
    monkeypatch.setenv('ESP_BOARD_MANAGER_SKIP_SDKCONFIG_CHECK', '1')
    callback(
        None,
        {'project_dir': str(project_dir), 'define_cache_entry': []},
        [_Task('build')],
    )
    out = capsys.readouterr().out
    assert 'sdkconfig consistency check skipped by ESP_BOARD_MANAGER_SKIP_SDKCONFIG_CHECK' in out


def test_callback_injects_defaults_when_sdkconfig_missing(bmgr_root, tmp_path, monkeypatch):
    project_dir = _make_project(tmp_path, with_sdkconfig=False)
    callback, _ = _load_callback(bmgr_root, project_dir)
    monkeypatch.delenv('SDKCONFIG_DEFAULTS', raising=False)
    callback(
        None,
        {'project_dir': str(project_dir), 'define_cache_entry': []},
        [_Task('build')],
    )
    sdkdefaults = os.environ.get('SDKCONFIG_DEFAULTS', '')
    assert 'board_manager.defaults' in sdkdefaults
