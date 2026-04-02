"""
Tests for parser edge cases and generation behavior regressions.
"""

from pathlib import Path
import re
import sys

import pytest


def test_adc_continuous_patterns_reject_single_unit_conv_mode_for_mixed_units(bmgr_root):
    sys.path.insert(0, str(bmgr_root))
    from peripherals.periph_adc import periph_adc as mod

    with pytest.raises(ValueError, match='conv_mode cannot be single-unit'):
        mod.parse(
            'adc_audio_in',
            {
                'role': 'continuous',
                'config': {
                    'patterns': [
                        {
                            'unit': 'ADC_UNIT_1',
                            'channel': 4,
                            'atten': 'ADC_ATTEN_DB_0',
                            'bit_width': 'ADC_BITWIDTH_DEFAULT',
                        },
                        {
                            'unit': 'ADC_UNIT_2',
                            'channel': 0,
                            'atten': 'ADC_ATTEN_DB_0',
                            'bit_width': 'ADC_BITWIDTH_DEFAULT',
                        },
                    ],
                    'conv_mode': 'ADC_CONV_SINGLE_UNIT_1',
                },
            },
        )


def test_adc_oneshot_rejects_list_channel_id(bmgr_root):
    sys.path.insert(0, str(bmgr_root))
    from peripherals.periph_adc import periph_adc as mod

    with pytest.raises(ValueError, match='must be a single integer'):
        mod.parse(
            'adc_oneshot',
            {
                'role': 'oneshot',
                'config': {
                    'unit_id': 'ADC_UNIT_1',
                    'channel_id': [4],
                },
            },
        )


def test_adc_continuous_single_unit_accepts_matching_explicit_conv_mode(bmgr_root):
    sys.path.insert(0, str(bmgr_root))
    from peripherals.periph_adc import periph_adc as mod

    result = mod.parse(
        'adc_audio_in',
        {
            'role': 'continuous',
            'config': {
                'unit_id': 'ADC_UNIT_1',
                'atten': 'ADC_ATTEN_DB_0',
                'bit_width': 'ADC_BITWIDTH_DEFAULT',
                'channel_id': [4],
                'conv_mode': 'ADC_CONV_SINGLE_UNIT_1',
            },
        },
    )

    assert result['struct_init']['cfg']['continuous']['conv_mode'] == 'ADC_CONV_SINGLE_UNIT_1'


def test_i2c_rejects_lp_port_with_regular_clk_source(bmgr_root):
    sys.path.insert(0, str(bmgr_root))
    from peripherals.periph_i2c import periph_i2c as mod

    with pytest.raises(ValueError, match='incompatible with LP I2C port'):
        mod.parse(
            'i2c_master',
            {
                'config': {
                    'port': 'LP_I2C_NUM_0',
                    'clk_source': 'I2C_CLK_SRC_DEFAULT',
                    'pins': {
                        'sda': 1,
                        'scl': 2,
                    },
                },
            },
        )


def test_i2c_rejects_regular_port_with_lp_clk_source(bmgr_root):
    sys.path.insert(0, str(bmgr_root))
    from peripherals.periph_i2c import periph_i2c as mod

    with pytest.raises(ValueError, match='incompatible with regular I2C port'):
        mod.parse(
            'i2c_master',
            {
                'config': {
                    'port': 0,
                    'clk_source': 'LP_I2C_SCLK_DEFAULT',
                    'pins': {
                        'sda': 1,
                        'scl': 2,
                    },
                },
            },
        )


def test_i2c_basic_parse_returns_i2c_master_bus_config(bmgr_root):
    sys.path.insert(0, str(bmgr_root))
    from peripherals.periph_i2c import periph_i2c as mod

    result = mod.parse(
        'i2c_master',
        {
            'config': {
                'port': 0,
                'pins': {
                    'sda': 18,
                    'scl': 23,
                },
            },
        },
    )

    assert result['struct_type'] == 'i2c_master_bus_config_t'
    assert result['struct_init']['i2c_port'] == 'I2C_NUM_0'
    assert result['struct_init']['sda_io_num'] == 18
    assert result['struct_init']['scl_io_num'] == 23


def test_camera_dvp_requires_i2c_peripheral(bmgr_root):
    sys.path.insert(0, str(bmgr_root))
    from devices.dev_camera import dev_camera as mod

    with pytest.raises(ValueError, match='requires an i2c peripheral'):
        mod.parse(
            'camera',
            {
                'type': 'camera',
                'sub_type': 'dvp',
                'config': {
                    'dvp_config': {},
                },
                'peripherals': [],
            },
        )


def test_button_gpio_rejects_legacy_gpio_name_and_events_keys(bmgr_root):
    sys.path.insert(0, str(bmgr_root))
    from devices.dev_button import dev_button as mod

    with pytest.raises(ValueError, match="Legacy 'events' is no longer supported"):
        mod.parse(
            'gpio_button_0',
            {
                'type': 'button',
                'sub_type': 'gpio',
                'config': {
                    'events': {
                        'press_down': True,
                    },
                    'gpio_name': 'gpio_button',
                },
                'peripherals': [
                    {'name': 'gpio_button'},
                ],
            },
        )


def test_button_gpio_requires_top_level_gpio_peripheral(bmgr_root):
    sys.path.insert(0, str(bmgr_root))
    from devices.dev_button import dev_button as mod

    with pytest.raises(ValueError, match="Legacy 'config.gpio_name' is no longer supported"):
        mod.parse(
            'gpio_button_0',
            {
                'type': 'button',
                'sub_type': 'gpio',
                'config': {
                    'events_cfg': {
                        'press_down': True,
                    },
                    'gpio_name': 'gpio_button',
                },
            },
        )


def test_button_adc_requires_top_level_adc_peripheral(bmgr_root):
    sys.path.insert(0, str(bmgr_root))
    from devices.dev_button import dev_button as mod

    with pytest.raises(ValueError, match="Legacy 'config.adc_name' is no longer supported"):
        mod.parse(
            'adc_button_0',
            {
                'type': 'button',
                'sub_type': 'adc_single',
                'config': {
                    'adc_name': 'adc_oneshot',
                    'button_index': 0,
                    'min_voltage': 0,
                    'max_voltage': 500,
                },
            },
        )


def test_camera_csi_allows_missing_ldo_when_dont_init_ldo_false(bmgr_root):
    sys.path.insert(0, str(bmgr_root))
    from devices.dev_camera import dev_camera as mod

    result = mod.parse(
        'camera',
        {
            'type': 'camera',
            'sub_type': 'csi',
            'config': {
                'csi_config': {
                    'dont_init_ldo': False,
                },
            },
            'peripherals': [
                {'name': 'i2c_master', 'frequency': 400000},
            ],
        },
    )

    assert result['struct_init']['sub_cfg']['csi']['ldo_name'] == ''


def test_camera_csi_requires_ldo_when_dont_init_ldo_true(bmgr_root):
    sys.path.insert(0, str(bmgr_root))
    from devices.dev_camera import dev_camera as mod

    with pytest.raises(ValueError, match='requires an ldo peripheral'):
        mod.parse(
            'camera',
            {
                'type': 'camera',
                'sub_type': 'csi',
                'config': {
                    'csi_config': {
                        'dont_init_ldo': True,
                    },
                },
                'peripherals': [
                    {'name': 'i2c_master', 'frequency': 400000},
                ],
            },
        )


def test_display_lcd_dsi_finds_dsi_and_ldo_without_order_dependency(bmgr_root):
    sys.path.insert(0, str(bmgr_root))
    from devices.dev_display_lcd import dev_display_lcd as mod

    peripherals_dict = {
        'ldo_panel': object(),
        'dsi_panel': object(),
    }

    result = mod.parse(
        'display_lcd',
        {
            'name': 'display_lcd',
            'type': 'display_lcd',
            'sub_type': 'dsi',
            'config': {
                'dbi_config': {},
                'dpi_config': {},
            },
            'peripherals': [
                {'name': 'ldo_panel'},
                {'name': 'dsi_panel'},
            ],
        },
        peripherals_dict=peripherals_dict,
    )

    dsi_cfg = result['struct_init']['sub_cfg']['dsi']
    assert dsi_cfg['ldo_name'] == 'ldo_panel'
    assert dsi_cfg['dsi_name'] == 'dsi_panel'


def test_display_lcd_spi_fallback_uses_generic_spi_prefix(bmgr_root):
    sys.path.insert(0, str(bmgr_root))
    from devices.dev_display_lcd import dev_display_lcd as mod

    peripherals_dict = {
        'spi_bus_custom': object(),
    }

    result = mod.parse(
        'display_lcd',
        {
            'name': 'display_lcd',
            'type': 'display_lcd',
            'sub_type': 'spi',
            'config': {
                'io_spi_config': {},
            },
        },
        peripherals_dict=peripherals_dict,
    )

    assert result['struct_init']['sub_cfg']['spi']['spi_name'] == 'spi_bus_custom'


def test_generate_kconfig_allows_external_only_boards(bmgr_root, tmp_path):
    sys.path.insert(0, str(bmgr_root))
    from gen_bmgr_config_codes import BoardConfigGenerator

    generator = BoardConfigGenerator(bmgr_root)
    generator.gen_codes_dir = tmp_path / 'gen_codes'

    external_board = tmp_path / 'custom_board'
    external_board.mkdir()

    result = generator.generate_kconfig({
        'custom_board': str(external_board),
    })

    assert result is True

    kconfig_content = (generator.gen_codes_dir / 'Kconfig.in').read_text(encoding='utf-8')
    assert 'menu "Board Selection"' not in kconfig_content
    assert 'config ESP_BOARD_NAME' not in kconfig_content
    assert 'config ESP_BOARD_CUSTOM_BOARD' not in kconfig_content
    assert 'menu "Peripheral Support"' in kconfig_content


@pytest.mark.parametrize('selected_board', ['esp_box_3', 'custom_board'])
def test_generate_selected_board_kconfig_projbuild_defines_current_board_only(bmgr_root, tmp_path, selected_board):
    sys.path.insert(0, str(bmgr_root))
    from gen_bmgr_config_codes import BoardConfigGenerator

    generator = BoardConfigGenerator(bmgr_root)

    result = generator.generate_selected_board_kconfig_projbuild(
        selected_board=selected_board,
        project_root=str(tmp_path),
    )

    assert result is True

    projbuild_path = tmp_path / 'components' / 'gen_bmgr_codes' / 'Kconfig.projbuild'
    projbuild_content = projbuild_path.read_text(encoding='utf-8')
    board_macro = f'ESP_BOARD_{selected_board.upper().replace("-", "_")}'

    assert f'config {board_macro}' in projbuild_content
    assert '    bool\n    default y' in projbuild_content
    assert 'config ESP_BOARD_NAME' in projbuild_content
    assert f'    default "{selected_board}"' in projbuild_content


def test_lyrat_mini_peripheral_generation_keeps_structs_aligned(bmgr_root, tmp_path):
    sys.path.insert(0, str(bmgr_root))
    from gen_bmgr_config_codes import BoardConfigGenerator

    generator = BoardConfigGenerator(bmgr_root)
    generator.project_root = str(tmp_path)

    board_yaml = bmgr_root / 'boards' / 'lyrat_mini_v1_1' / 'board_peripherals.yaml'
    generator.process_peripherals(str(board_yaml))

    generated = tmp_path / 'components' / 'gen_bmgr_codes' / 'gen_board_periph_config.c'
    content = generated.read_text(encoding='utf-8')

    assert 'const static i2c_master_bus_config_t esp_bmgr_i2c_master_cfg' in content
    assert 'const static periph_i2s_config_t esp_bmgr_i2s_audio_out_cfg' in content
    assert 'const static periph_i2s_config_t esp_bmgr_i2s_audio_in_cfg' in content
    assert 'const static periph_adc_config_t esp_bmgr_adc_button_cfg' in content

    descriptor_block = content.split('// Peripheral descriptor array', 1)[1]
    descriptor_pairs = re.findall(
        r'\.name = "([^"]+)",.*?\.cfg = &(esp_bmgr_[^,]+),',
        descriptor_block,
        re.S,
    )
    assert descriptor_pairs == [
        ('i2c_master', 'esp_bmgr_i2c_master_cfg'),
        ('i2s_audio_out', 'esp_bmgr_i2s_audio_out_cfg'),
        ('i2s_audio_in', 'esp_bmgr_i2s_audio_in_cfg'),
        ('gpio_sd_power', 'esp_bmgr_gpio_sd_power_cfg'),
        ('gpio_headphone_detection', 'esp_bmgr_gpio_headphone_detection_cfg'),
        ('gpio_power_amp', 'esp_bmgr_gpio_power_amp_cfg'),
        ('gpio_green_led', 'esp_bmgr_gpio_green_led_cfg'),
        ('gpio_blue_led', 'esp_bmgr_gpio_blue_led_cfg'),
        ('gpio_sd_detect', 'esp_bmgr_gpio_sd_detect_cfg'),
        ('adc_button', 'esp_bmgr_adc_button_cfg'),
    ]


def test_process_peripherals_fails_fast_when_parser_is_missing(bmgr_root, tmp_path, monkeypatch):
    sys.path.insert(0, str(bmgr_root))
    import gen_bmgr_config_codes as bmgr_module

    generator = bmgr_module.BoardConfigGenerator(bmgr_root)
    generator.project_root = str(tmp_path)

    real_load_parsers = bmgr_module.load_parsers

    def _load_parsers_without_i2c(*args, **kwargs):
        parsers = real_load_parsers(*args, **kwargs)
        parsers.pop('i2c', None)
        return parsers

    monkeypatch.setattr(bmgr_module, 'load_parsers', _load_parsers_without_i2c)

    board_yaml = bmgr_root / 'boards' / 'lyrat_mini_v1_1' / 'board_peripherals.yaml'
    with pytest.raises(RuntimeError, match=r"i2c_master'.*type: i2c"):
        generator.process_peripherals(str(board_yaml))

    generated = tmp_path / 'components' / 'gen_bmgr_codes' / 'gen_board_periph_config.c'
    assert not generated.exists()


def test_parser_loader_raises_on_import_failure(bmgr_root, tmp_path):
    sys.path.insert(0, str(bmgr_root))
    from generators.parser_loader import load_parsers

    periph_dir = tmp_path / 'peripherals'
    parser_dir = periph_dir / 'periph_bad'
    parser_dir.mkdir(parents=True)
    (parser_dir / 'periph_bad.py').write_text(
        'raise RuntimeError("boom during import")\n',
        encoding='utf-8',
    )

    with pytest.raises(RuntimeError, match='periph_bad'):
        load_parsers([], prefix='periph_', base_dir=str(periph_dir))


def test_audio_codec_rejects_simultaneous_adc_and_dac_enablement(bmgr_root):
    sys.path.insert(0, str(bmgr_root))
    from devices.dev_audio_codec import dev_audio_codec as mod

    with pytest.raises(ValueError, match='cannot enable both adc_enabled and dac_enabled'):
        mod.parse(
            'audio_codec',
            {
                'type': 'audio_codec',
                'chip': 'es8311',
                'config': {
                    'adc_enabled': True,
                    'dac_enabled': True,
                },
                'peripherals': [
                    {'name': 'i2s_audio_out'},
                    {'name': 'i2c_master', 'address': 0x30},
                ],
            },
        )
