"""
Tests for sdkconfig consistency guard and auto-fix behavior.
"""

from pathlib import Path
import sys
import re
import tempfile


def _read(path: Path) -> str:
    return path.read_text(encoding='utf-8')


def _pick_existing_board_macro(bmgr_root: Path) -> str:
    kconfig_path = bmgr_root / 'gen_codes' / 'Kconfig.in'
    content = kconfig_path.read_text(encoding='utf-8')
    for m in re.finditer(r'^config\s+((?:ESP_)?BOARD_[A-Z0-9_]+)\s*$', content, flags=re.M):
        macro = m.group(1)
        return macro
    raise AssertionError('No board macro found in gen_codes/Kconfig.in')


def test_sdkconfig_consistency_auto_fix_only_updates_bmgr_symbols(bmgr_root, tmp_path):
    sys.path.insert(0, str(bmgr_root))
    from generators.sdkconfig_manager import SDKConfigManager

    manager = SDKConfigManager(bmgr_root)
    board_cfg_macro = _pick_existing_board_macro(bmgr_root)
    selected_board = re.sub(r'^(?:ESP_)?BOARD_', '', board_cfg_macro).lower()
    sdkconfig = tmp_path / 'sdkconfig'
    sdkconfig.write_text(
        '\n'.join([
            'CONFIG_FREERTOS_HZ=1000',
            'CONFIG_ESP_BOARD_PERIPH_I2C_SUPPORT=y',
            f'# CONFIG_{board_cfg_macro} is not set',
            '',
        ]),
        encoding='utf-8',
    )

    result = manager.ensure_sdkconfig_consistency(
        sdkconfig_path=str(sdkconfig),
        selected_board=selected_board,
        device_types={'audio_codec'},
        peripheral_types={'i2s'},
        device_subtypes={},
        auto_fix=True,
    )

    assert result['ok'] is True
    assert result['fixed'] is True

    content = _read(sdkconfig)
    # Unrelated sdkconfig options should remain untouched
    assert 'CONFIG_FREERTOS_HZ=1000' in content
    # Board and selected type symbols should be corrected/enabled
    assert f'CONFIG_{board_cfg_macro}=y' in content
    assert 'CONFIG_ESP_BOARD_DEV_AUDIO_CODEC_SUPPORT=y' in content
    assert 'CONFIG_ESP_BOARD_PERIPH_I2S_SUPPORT=y' in content
    # Non-selected but managed symbols should be forced to not set
    assert '# CONFIG_ESP_BOARD_PERIPH_I2C_SUPPORT is not set' in content


def test_sdkconfig_consistency_check_only_reports_without_modifying(bmgr_root, tmp_path):
    sys.path.insert(0, str(bmgr_root))
    from generators.sdkconfig_manager import SDKConfigManager

    manager = SDKConfigManager(bmgr_root)
    board_cfg_macro = _pick_existing_board_macro(bmgr_root)
    selected_board = re.sub(r'^(?:ESP_)?BOARD_', '', board_cfg_macro).lower()
    sdkconfig = tmp_path / 'sdkconfig'
    sdkconfig.write_text(
        '\n'.join([
            'CONFIG_FREERTOS_HZ=1000',
            'CONFIG_ESP_BOARD_PERIPH_I2C_SUPPORT=y',
            '',
        ]),
        encoding='utf-8',
    )
    before = _read(sdkconfig)

    result = manager.ensure_sdkconfig_consistency(
        sdkconfig_path=str(sdkconfig),
        selected_board=selected_board,
        device_types={'audio_codec'},
        peripheral_types={'i2s'},
        device_subtypes={},
        auto_fix=False,
    )

    assert result['ok'] is False
    assert result['fixed'] is False
    assert result['issues']
    assert _read(sdkconfig) == before


def test_board_manager_defaults_contains_device_peripheral_symbols(bmgr_root, tmp_path):
    sys.path.insert(0, str(bmgr_root))
    from generators.sdkconfig_manager import SDKConfigManager

    manager = SDKConfigManager(bmgr_root)
    board_path = bmgr_root / 'boards' / 'esp32_s3_korvo2_v3'
    output_file = tmp_path / 'board_manager.defaults'

    result = manager.generate_board_manager_defaults(
        board_path=str(board_path),
        project_path=str(tmp_path),
        board_name='esp32_s3_korvo2_v3',
        chip_name='esp32s3',
        output_file=str(output_file),
        device_types={'audio_codec', 'display_lcd'},
        peripheral_types={'i2c', 'i2s'},
        device_subtypes={'display_lcd': {'spi'}},
    )

    assert result['added']
    content = _read(output_file)
    assert 'CONFIG_ESP_BOARD_ESP32_S3_KORVO2_V3=y' in content
    assert 'CONFIG_ESP_BOARD_PERIPH_I2C_SUPPORT=y' in content
    assert 'CONFIG_ESP_BOARD_PERIPH_I2S_SUPPORT=y' in content
    assert 'CONFIG_ESP_BOARD_DEV_AUDIO_CODEC_SUPPORT=y' in content
    assert 'CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT=y' in content
    assert 'CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_SPI_SUPPORT=y' in content


def test_get_selected_board_from_sdkconfig(bmgr_root, monkeypatch):
    sys.path.insert(0, str(bmgr_root))
    from generators.config_generator import ConfigGenerator

    with tempfile.TemporaryDirectory() as project_dir:
        sdkconfig = Path(project_dir) / 'sdkconfig'
        sdkconfig.write_text(
            '\n'.join([
                'CONFIG_FREERTOS_HZ=1000',
                'CONFIG_ESP_BOARD_LYRAT_MINI_V1_1=y',
                '',
            ]),
            encoding='utf-8',
        )

        cfg = ConfigGenerator(bmgr_root)
        monkeypatch.setattr(cfg, 'get_project_root', lambda: Path(project_dir))

        assert cfg.get_selected_board_from_sdkconfig() == 'lyrat_mini_v1_1'
