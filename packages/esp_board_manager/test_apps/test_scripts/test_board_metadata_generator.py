"""
Tests for unified board metadata generation.
"""

import sys

import yaml


def test_board_metadata_file_contains_direct_device_and_peripheral_io(bmgr_root, tmp_path):
    sys.path.insert(0, str(bmgr_root))
    from gen_bmgr_config_codes import BoardConfigGenerator

    periph_yaml = tmp_path / 'board_peripherals.yaml'
    periph_yaml.write_text(
        """
peripherals:
  - name: i2c_master
    type: i2c
    config:
      port: 0
      pins:
        sda: 18
        scl: 23
  - name: uart_debug
    type: uart
    config:
      uart_num: 0
      tx_io_num: 43
      rx_io_num: 44
""".strip()
        + '\n',
        encoding='utf-8',
    )

    dev_yaml = tmp_path / 'board_devices.yaml'
    dev_yaml.write_text(
        """
devices:
  - name: camera
    type: camera
    sub_type: csi
    dependencies:
      espressif/esp_cam_sensor: "*"
    peripherals:
      - name: i2c_master
        frequency: 400000
    config:
      csi_config:
        dont_init_ldo: false
        reset_io: 26
        xclk_config:
          xclk_pin: 11
""".strip()
        + '\n',
        encoding='utf-8',
    )

    generator = BoardConfigGenerator(bmgr_root)
    generator.project_root = str(tmp_path)

    peripherals_dict, periph_name_map, _ = generator.process_peripherals(str(periph_yaml))
    generator.process_devices(str(dev_yaml), peripherals_dict, periph_name_map)

    output_path = tmp_path / 'components' / 'gen_bmgr_codes' / 'gen_board_metadata.yaml'
    generator.write_board_metadata('unit_test_board', 'esp32p4', str(output_path))

    output_text = output_path.read_text(encoding='utf-8')
    metadata = yaml.safe_load(output_text)

    assert metadata['board'] == 'unit_test_board'
    assert metadata['chip'] == 'esp32p4'
    assert metadata['devices']['camera']['type'] == 'camera'
    assert metadata['devices']['camera']['sub_type'] == 'csi'
    assert metadata['devices']['camera']['peripherals'] == ['i2c_master']
    assert metadata['devices']['camera']['dependencies'] == {'espressif/esp_cam_sensor': '*'}
    assert metadata['devices']['camera']['io'] == {
        'reset_io': 26,
        'xclk_pin': 11,
    }
    assert 'sda_io_num' not in metadata['devices']['camera']['io']
    assert metadata['peripherals']['i2c_master']['io'] == {
        'sda_io_num': 18,
        'scl_io_num': 23,
    }
    assert 'format' not in metadata['peripherals']['i2c_master']
    assert '\n\ndevices:\n' in output_text
    assert '\n\nperipherals:\n' in output_text
    assert '\ndevices:\n  camera:\n' in output_text
    assert '\nperipherals:\n  i2c_master:\n' in output_text
    assert '\n\n  uart_debug:\n' in output_text


def test_adc_metadata_skips_io_when_channel_header_is_missing(bmgr_root, monkeypatch):
    sys.path.insert(0, str(bmgr_root))
    from peripherals.periph_adc import periph_adc as mod
    from generators.board_metadata_generator import BoardMetadataGenerator

    def _raise_missing_header(*args, **kwargs):
        raise FileNotFoundError('adc_channel.h not found')

    monkeypatch.setattr(mod, 'adc_channels_to_gpios', _raise_missing_header)

    result = mod.parse(
        'adc_audio_in',
        {
            'role': 'continuous',
            'config': {
                'unit_id': 'ADC_UNIT_1',
                'atten': 'ADC_ATTEN_DB_0',
                'bit_width': 'ADC_BITWIDTH_DEFAULT',
                'channel_id': [4],
            },
        },
    )

    metadata = BoardMetadataGenerator().generate_metadata_dict(
        board_name='unit_test_board',
        chip_name='esp32',
        device_artifacts=[],
        peripheral_artifacts=[
            {
                'name': 'adc_audio_in',
                'type': 'adc',
                'role': 'continuous',
                'format': None,
                'raw': {},
                'result': result,
                'parse_func': mod.parse,
            }
        ],
    )

    assert 'io' not in metadata['peripherals']['adc_audio_in']


def test_metadata_yaml_renders_io_lists_inline(bmgr_root, tmp_path):
    sys.path.insert(0, str(bmgr_root))
    from devices.dev_audio_codec import dev_audio_codec as mod
    from generators.board_metadata_generator import BoardMetadataGenerator

    result = mod.parse(
        'audio_adc',
        {
            'type': 'audio_codec',
            'chip': 'internal',
            'peripherals': [],
            'config': {
                'adc_enabled': True,
                'adc_local_cfg': {
                    'unit_id': 'ADC_UNIT_1',
                    'channel_list': [6, 7],
                },
            },
        },
    )

    output_path = tmp_path / 'gen_board_metadata.yaml'
    BoardMetadataGenerator().write_metadata_file(
        output_path=str(output_path),
        board_name='unit_test_board',
        chip_name='esp32',
        device_artifacts=[
            {
                'name': 'audio_adc',
                'type': 'audio_codec',
                'sub_type': None,
                'peripherals': [],
                'dependencies': {},
                'raw': {},
                'result': result,
                'parse_func': mod.parse,
            }
        ],
        peripheral_artifacts=[],
    )

    output_text = output_path.read_text(encoding='utf-8')
    assert 'channel_id: [34, 35]' in output_text
    assert 'channel_id:\n' not in output_text


def test_audio_codec_adc_metadata_skips_io_when_channel_header_is_missing(bmgr_root, monkeypatch):
    sys.path.insert(0, str(bmgr_root))
    from devices.dev_audio_codec import dev_audio_codec as mod
    from generators.board_metadata_generator import BoardMetadataGenerator

    def _raise_missing_header(*args, **kwargs):
        raise FileNotFoundError('adc_channel.h not found')

    monkeypatch.setattr(mod, 'adc_channels_to_gpios', _raise_missing_header)

    result = mod.parse(
        'audio_adc',
        {
            'type': 'audio_codec',
            'chip': 'internal',
            'peripherals': [],
            'config': {
                'adc_enabled': True,
                'adc_local_cfg': {
                    'unit_id': 'ADC_UNIT_1',
                    'channel_list': [6],
                },
            },
        },
    )

    metadata = BoardMetadataGenerator().generate_metadata_dict(
        board_name='unit_test_board',
        chip_name='esp32',
        device_artifacts=[
            {
                'name': 'audio_adc',
                'type': 'audio_codec',
                'sub_type': None,
                'peripherals': [],
                'dependencies': {},
                'raw': {},
                'result': result,
                'parse_func': mod.parse,
            }
        ],
        peripheral_artifacts=[],
    )

    assert 'io' not in metadata['devices']['audio_adc']
