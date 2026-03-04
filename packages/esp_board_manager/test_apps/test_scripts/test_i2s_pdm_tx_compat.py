"""
Tests for PDM TX compatibility behavior in the I2S peripheral parser.
"""

import sys


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
