"""
Compatibility behavior tests for the I2S peripheral parser.
"""

import sys


def test_std_mode_omits_bclk_div_on_idf_5_4(bmgr_root, monkeypatch):
    sys.path.insert(0, str(bmgr_root))
    from generators.utils import idf_version as idf_version_mod
    from peripherals.periph_i2s import periph_i2s as mod

    monkeypatch.setattr(idf_version_mod, '_idf_version', (5, 4, 0))

    result = mod.parse(
        'i2s_audio_out',
        {
            'type': 'i2s',
            'role': 'master',
            'format': 'std-out',
            'config': {
                'sample_rate_hz': 16000,
                'bclk_div': 12,
                'pins': {
                    'bclk': 4,
                    'ws': 5,
                    'dout': 6,
                },
            },
        },
    )

    clk_cfg = result['struct_init']['i2s_cfg']['std']['clk_cfg']
    assert 'bclk_div' not in clk_cfg


def test_std_mode_keeps_bclk_div_on_idf_5_5_and_newer(bmgr_root, monkeypatch):
    sys.path.insert(0, str(bmgr_root))
    from generators.utils import idf_version as idf_version_mod
    from peripherals.periph_i2s import periph_i2s as mod

    monkeypatch.setattr(idf_version_mod, '_idf_version', (5, 5, 0))

    result = mod.parse(
        'i2s_audio_out',
        {
            'type': 'i2s',
            'role': 'master',
            'format': 'std-out',
            'config': {
                'sample_rate_hz': 16000,
                'bclk_div': 12,
                'pins': {
                    'bclk': 4,
                    'ws': 5,
                    'dout': 6,
                },
            },
        },
    )

    clk_cfg = result['struct_init']['i2s_cfg']['std']['clk_cfg']
    assert clk_cfg['bclk_div'] == 12


def test_unknown_chip_omits_pdm_tx_dout2_in_generated_gpio_cfg(bmgr_root, monkeypatch):
    sys.path.insert(0, str(bmgr_root))
    from peripherals.periph_i2s import periph_i2s as mod

    monkeypatch.setattr(mod, 'get_effective_chip_name', lambda: None)

    result = mod.parse(
        'i2s_audio_out',
        {
            'type': 'i2s',
            'role': 'master',
            'format': 'pdm-out',
            'config': {
                'sample_rate_hz': 16000,
                'pins': {
                    'clk': 4,
                    'dout': 5,
                    'dout2': 6,
                },
            },
        },
    )

    gpio_cfg = result['struct_init']['i2s_cfg']['pdm_tx']['gpio_cfg']
    assert 'dout2' not in gpio_cfg


def test_known_non_esp32_chip_keeps_pdm_tx_dout2_in_generated_gpio_cfg(bmgr_root, monkeypatch):
    sys.path.insert(0, str(bmgr_root))
    from peripherals.periph_i2s import periph_i2s as mod

    monkeypatch.setattr(mod, 'get_effective_chip_name', lambda: 'esp32s3')

    result = mod.parse(
        'i2s_audio_out',
        {
            'type': 'i2s',
            'role': 'master',
            'format': 'pdm-out',
            'config': {
                'sample_rate_hz': 16000,
                'pins': {
                    'clk': 4,
                    'dout': 5,
                    'dout2': 6,
                },
            },
        },
    )

    gpio_cfg = result['struct_init']['i2s_cfg']['pdm_tx']['gpio_cfg']
    assert gpio_cfg['dout2'] == 6
