"""
Compatibility behavior tests for the I2S peripheral parser.
"""

import sys
from pathlib import Path

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

def _write_soc_caps(tmp_path: Path, chip: str, text: str) -> Path:
    idf = tmp_path / 'idf'
    caps = idf / 'components' / 'soc' / chip / 'include' / 'soc' / 'soc_caps.h'
    caps.parent.mkdir(parents=True)
    caps.write_text(text, encoding='utf-8')
    return idf

def _pdm_rx_slot_cfg(mod):
    result = mod.parse(
        'i2s_audio_in',
        {
            'type': 'i2s',
            'role': 'master',
            'format': 'pdm-in',
            'config': {
                'sample_rate_hz': 16000,
                'pins': {
                    'clk': 4,
                    'din': 5,
                },
            },
        },
    )
    return result['struct_init']['i2s_cfg']['pdm_rx']['slot_cfg']

def test_pdm_rx_omits_hp_filter_fields_when_soc_cap_is_missing(bmgr_root, tmp_path, monkeypatch):
    sys.path.insert(0, str(bmgr_root))
    from peripherals.periph_i2s import periph_i2s as mod

    idf = _write_soc_caps(tmp_path, 'esp32s3', '#define SOC_I2S_HW_VERSION_2 (1)\n')
    monkeypatch.setenv('IDF_PATH', str(idf))
    monkeypatch.setattr(mod, 'get_effective_chip_name', lambda: 'esp32s3')

    slot_cfg = _pdm_rx_slot_cfg(mod)
    assert 'hp_en' not in slot_cfg
    assert 'hp_cut_off_freq_hz' not in slot_cfg
    assert 'amplify_num' not in slot_cfg

def test_pdm_rx_keeps_hp_filter_fields_for_p4_soc_cap(bmgr_root, tmp_path, monkeypatch):
    sys.path.insert(0, str(bmgr_root))
    from peripherals.periph_i2s import periph_i2s as mod

    idf = _write_soc_caps(
        tmp_path,
        'esp32p4',
        '#define SOC_I2S_HW_VERSION_2 (1)\n'
        '#define SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER (1)\n',
    )
    monkeypatch.setenv('IDF_PATH', str(idf))
    monkeypatch.setattr(mod, 'get_effective_chip_name', lambda: 'esp32p4')

    slot_cfg = _pdm_rx_slot_cfg(mod)
    assert slot_cfg['hp_en'] is True
    assert slot_cfg['hp_cut_off_freq_hz'] == 35.5
    assert slot_cfg['amplify_num'] == 1

def test_pdm_rx_keeps_hp_filter_fields_for_s31_when_idf6_soc_cap_exists(bmgr_root, tmp_path, monkeypatch):
    sys.path.insert(0, str(bmgr_root))
    from peripherals.periph_i2s import periph_i2s as mod

    idf = _write_soc_caps(
        tmp_path,
        'esp32s31',
        '#define SOC_I2S_HW_VERSION_2 (1)\n'
        '#define SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER (1)\n',
    )
    monkeypatch.setenv('IDF_PATH', str(idf))
    monkeypatch.setattr(mod, 'get_effective_chip_name', lambda: 'esp32s31')

    slot_cfg = _pdm_rx_slot_cfg(mod)
    assert slot_cfg['hp_en'] is True
    assert slot_cfg['hp_cut_off_freq_hz'] == 35.5
    assert slot_cfg['amplify_num'] == 1
