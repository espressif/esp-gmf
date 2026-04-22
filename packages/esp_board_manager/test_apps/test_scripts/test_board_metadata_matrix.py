"""
Matrix coverage for board metadata generation across current device and peripheral types.
"""

from copy import deepcopy
from pathlib import Path
import sys

import pytest
import yaml


def _generate_metadata(
    bmgr_root,
    tmp_path: Path,
    *,
    chip: str,
    peripherals: list,
    devices: list,
):
    sys.path.insert(0, str(bmgr_root))
    from gen_bmgr_config_codes import BoardConfigGenerator

    case_dir = tmp_path / 'case'
    case_dir.mkdir(parents=True, exist_ok=True)

    periph_yaml = case_dir / 'board_peripherals.yaml'
    dev_yaml = case_dir / 'board_devices.yaml'

    periph_yaml.write_text(
        yaml.safe_dump({'peripherals': peripherals}, sort_keys=False, allow_unicode=False),
        encoding='utf-8',
    )
    dev_yaml.write_text(
        yaml.safe_dump({'devices': devices}, sort_keys=False, allow_unicode=False),
        encoding='utf-8',
    )

    generator = BoardConfigGenerator(bmgr_root)
    generator.project_root = str(case_dir)

    peripherals_dict, periph_name_map, _ = generator.process_peripherals(str(periph_yaml))
    generator.process_devices(str(dev_yaml), peripherals_dict, periph_name_map)

    output_path = case_dir / 'components' / 'gen_bmgr_codes' / 'gen_board_metadata.yaml'
    generator.write_board_metadata('unit_test_board', chip, str(output_path))
    return yaml.safe_load(output_path.read_text(encoding='utf-8'))


PERIPHERAL_CASES = [
    {
        'id': 'periph_adc_continuous',
        'chip': 'esp32',
        'peripherals': [
            {
                'name': 'adc_audio_in',
                'type': 'adc',
                'role': 'continuous',
                'config': {
                    'unit_id': 'ADC_UNIT_1',
                    'atten': 'ADC_ATTEN_DB_0',
                    'bit_width': 'ADC_BITWIDTH_DEFAULT',
                    'channel_id': [4],
                },
            }
        ],
        'expected': {
            'type': 'adc',
            'role': 'continuous',
            'io': {
                'channel_id': [32],
            },
        },
    },
    {
        'id': 'periph_adc_pattern',
        'chip': 'esp32c3',
        'peripherals': [
            {
                'name': 'adc_audio_pattern',
                'type': 'adc',
                'role': 'continuous',
                'config': {
                    'patterns': [
                        {
                            'unit': 'ADC_UNIT_1',
                            'channel': 2,
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
                },
            }
        ],
        'expected': {
            'type': 'adc',
            'role': 'continuous',
            'io': {
                'channel': [2, 5],
            },
        },
    },
    {
        'id': 'periph_anacmpr',
        'chip': 'esp32p4',
        'peripherals': [
            {
                'name': 'anacmpr_unit_0',
                'type': 'anacmpr',
                'config': {
                    'unit': 0,
                    'ref_src': 'ANA_CMPR_REF_SRC_INTERNAL',
                    'cross_type': 'ANA_CMPR_CROSS_ANY',
                    'clk_src': 'ANA_CMPR_CLK_SRC_DEFAULT',
                    'intr_priority': 0,
                },
            }
        ],
        'expected': {
            'type': 'anacmpr',
        },
    },
    {
        'id': 'periph_dac_oneshot',
        'chip': 'esp32',
        'peripherals': [
            {
                'name': 'dac_oneshot',
                'type': 'dac',
                'role': 'oneshot',
                'config': {
                    'channel': 0,
                },
            }
        ],
        'expected': {
            'type': 'dac',
            'role': 'oneshot',
        },
    },
    {
        'id': 'periph_dsi',
        'chip': 'esp32p4',
        'peripherals': [
            {
                'name': 'dsi_display',
                'type': 'dsi',
                'config': {
                    'bus_id': 0,
                    'data_lanes': 2,
                    'phy_clk_src': 0,
                    'lane_bit_rate_mbps': 1000,
                },
            }
        ],
        'expected': {
            'type': 'dsi',
        },
    },
    {
        'id': 'periph_gpio',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'gpio_signal',
                'type': 'gpio',
                'config': {
                    'pin': 18,
                    'mode': 'GPIO_MODE_OUTPUT',
                },
            }
        ],
        'expected': {
            'type': 'gpio',
            'io': {
                'pin': 18,
            },
        },
    },
    {
        'id': 'periph_i2c',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'i2c_master',
                'type': 'i2c',
                'config': {
                    'port': 0,
                    'pins': {
                        'sda': 17,
                        'scl': 18,
                    },
                },
            }
        ],
        'expected': {
            'type': 'i2c',
            'role': 'master',
            'io': {
                'sda_io_num': 17,
                'scl_io_num': 18,
            },
        },
    },
    {
        'id': 'periph_i2s_std',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'i2s_audio_out',
                'type': 'i2s',
                'role': 'master',
                'format': 'std-out',
                'config': {
                    'pins': {
                        'mclk': 1,
                        'bclk': 2,
                        'ws': 3,
                        'dout': 4,
                    },
                },
            }
        ],
        'expected': {
            'type': 'i2s',
            'role': 'master',
            'format': 'std-out',
            'io': {
                'mclk': 1,
                'bclk': 2,
                'ws': 3,
                'dout': 4,
            },
        },
    },
    {
        'id': 'periph_i2s_tdm',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'i2s_audio_tdm',
                'type': 'i2s',
                'role': 'master',
                'format': 'tdm-out',
                'config': {
                    'pins': {
                        'mclk': 5,
                        'bclk': 6,
                        'ws': 7,
                        'dout': 8,
                        'din': 9,
                    },
                },
            }
        ],
        'expected': {
            'type': 'i2s',
            'role': 'master',
            'format': 'tdm-out',
            'io': {
                'mclk': 5,
                'bclk': 6,
                'ws': 7,
                'dout': 8,
                'din': 9,
            },
        },
    },
    {
        'id': 'periph_i2s_pdm',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'i2s_audio_pdm_in',
                'type': 'i2s',
                'role': 'master',
                'format': 'pdm-in',
                'config': {
                    'pins': {
                        'clk': 10,
                        'din0': 11,
                        'din1': 12,
                    },
                },
            }
        ],
        'expected': {
            'type': 'i2s',
            'role': 'master',
            'format': 'pdm-in',
            'io': {
                'clk': 10,
                'dins': [11, 12],
            },
        },
    },
    {
        'id': 'periph_ldo',
        'chip': 'esp32p4',
        'peripherals': [
            {
                'name': 'ldo_mipi',
                'type': 'ldo',
                'config': {
                    'ldo_cfg': {
                        'chan_id': 3,
                        'voltage_mv': 2500,
                        'adjustable': 1,
                        'owned_by_hw': 0,
                    }
                },
            }
        ],
        'expected': {
            'type': 'ldo',
        },
    },
    {
        'id': 'periph_ledc',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'ledc_backlight',
                'type': 'ledc',
                'config': {
                    'gpio_num': 21,
                },
            }
        ],
        'expected': {
            'type': 'ledc',
            'io': {
                'gpio_num': 21,
            },
        },
    },
    {
        'id': 'periph_mcpwm',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'mcpwm_group_0',
                'type': 'mcpwm',
                'config': {
                    'generator_config': {
                        'gpio_num': 22,
                    },
                },
            }
        ],
        'expected': {
            'type': 'mcpwm',
            'io': {
                'gen_gpio_num': 22,
            },
        },
    },
    {
        'id': 'periph_pcnt',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'pcnt_rotary',
                'type': 'pcnt',
                'config': {
                    'channel_count': 2,
                    'channel_list': [
                        {
                            'channel': 0,
                            'channel_config': {
                                'edge_gpio_num': 4,
                                'level_gpio_num': 5,
                            },
                        },
                        {
                            'channel': 1,
                            'channel_config': {
                                'edge_gpio_num': 6,
                                'level_gpio_num': 7,
                            },
                        },
                    ],
                    'watch_point_count': 0,
                    'watch_point_list': [],
                },
            }
        ],
        'expected': {
            'type': 'pcnt',
            'io': {
                'edge_gpio_num': [4, 6],
                'level_gpio_num': [5, 7],
            },
        },
    },
    {
        'id': 'periph_rmt_tx',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'rmt_tx',
                'type': 'rmt',
                'role': 'tx',
                'config': {
                    'gpio_num': 18,
                },
            }
        ],
        'expected': {
            'type': 'rmt',
            'role': 'tx',
            'io': {
                'gpio_num': 18,
            },
        },
    },
    {
        'id': 'periph_rmt_rx',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'rmt_rx',
                'type': 'rmt',
                'role': 'rx',
                'config': {
                    'gpio_num': 19,
                },
            }
        ],
        'expected': {
            'type': 'rmt',
            'role': 'rx',
            'io': {
                'gpio_num': 19,
            },
        },
    },
    {
        'id': 'periph_sdm',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'sdm_out',
                'type': 'sdm',
                'config': {
                    'gpio_num': 23,
                },
            }
        ],
        'expected': {
            'type': 'sdm',
            'io': {
                'gpio_num': 23,
            },
        },
    },
    {
        'id': 'periph_spi',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'spi_master',
                'type': 'spi',
                'config': {
                    'spi_port': 1,
                    'spi_bus_config': {
                        'mosi_io_num': 12,
                        'miso_io_num': 13,
                        'sclk_io_num': 14,
                    },
                },
            }
        ],
        'expected': {
            'type': 'spi',
            'role': 'master',
            'io': {
                'mosi_io_num': 12,
                'miso_io_num': 13,
                'sclk_io_num': 14,
            },
        },
    },
    {
        'id': 'periph_uart',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'uart_debug',
                'type': 'uart',
                'config': {
                    'uart_num': 1,
                    'tx_io_num': 43,
                    'rx_io_num': 44,
                },
            }
        ],
        'expected': {
            'type': 'uart',
            'io': {
                'tx_io_num': 43,
                'rx_io_num': 44,
            },
        },
    },
]


DEVICE_CASES = [
    {
        'id': 'dev_audio_codec_dac',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'gpio_power_amp',
                'type': 'gpio',
                'config': {'pin': 10, 'mode': 'GPIO_MODE_OUTPUT'},
            },
            {
                'name': 'i2s_audio_out',
                'type': 'i2s',
                'role': 'master',
                'format': 'std-out',
                'config': {'pins': {'bclk': 1, 'ws': 2, 'dout': 3}},
            },
            {
                'name': 'i2c_master',
                'type': 'i2c',
                'config': {'port': 0, 'pins': {'sda': 17, 'scl': 18}},
            },
        ],
        'device': {
            'name': 'audio_dac',
            'type': 'audio_codec',
            'chip': 'generic_codec',
            'config': {
                'dac_enabled': True,
                'dac_max_channel': 2,
                'dac_channel_mask': '11',
            },
            'peripherals': [
                {'name': 'gpio_power_amp'},
                {'name': 'i2s_audio_out'},
                {'name': 'i2c_master', 'address': 0x30, 'frequency': 400000},
            ],
        },
        'expected': {
            'type': 'audio_codec',
            'peripherals': ['gpio_power_amp', 'i2s_audio_out', 'i2c_master'],
        },
    },
    {
        'id': 'dev_audio_codec_adc_local',
        'chip': 'esp32',
        'peripherals': [],
        'device': {
            'name': 'audio_adc',
            'type': 'audio_codec',
            'chip': 'internal',
            'config': {
                'adc_enabled': True,
                'adc_local_cfg': {
                    'unit_id': 'ADC_UNIT_1',
                    'channel_list': [6, 7],
                },
            },
        },
        'expected': {
            'type': 'audio_codec',
            'io': {
                'channel_id': [34, 35],
            },
        },
    },
    {
        'id': 'dev_audio_codec_adc_pattern',
        'chip': 'esp32c3',
        'peripherals': [],
        'device': {
            'name': 'audio_adc',
            'type': 'audio_codec',
            'chip': 'internal',
            'config': {
                'adc_enabled': True,
                'adc_local_cfg': {
                    'patterns': [
                        {
                            'unit': 'ADC_UNIT_1',
                            'channel': 2,
                            'atten': 'ADC_ATTEN_DB_0',
                            'bit_width': 'SOC_ADC_DIGI_MAX_BITWIDTH',
                        },
                        {
                            'unit': 'ADC_UNIT_2',
                            'channel': 0,
                            'atten': 'ADC_ATTEN_DB_0',
                            'bit_width': 'SOC_ADC_DIGI_MAX_BITWIDTH',
                        },
                    ],
                },
            },
        },
        'expected': {
            'type': 'audio_codec',
            'io': {
                'channel': [2, 5],
            },
        },
    },
    {
        'id': 'dev_button_gpio',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'gpio_button',
                'type': 'gpio',
                'config': {'pin': 4, 'mode': 'GPIO_MODE_INPUT'},
            },
        ],
        'device': {
            'name': 'gpio_button_0',
            'type': 'button',
            'sub_type': 'gpio',
            'config': {
                'active_level': 0,
            },
            'peripherals': [
                {'name': 'gpio_button'},
            ],
        },
        'expected': {
            'type': 'button',
            'sub_type': 'gpio',
            'peripherals': ['gpio_button'],
        },
    },
    {
        'id': 'dev_button_adc_single',
        'chip': 'esp32',
        'peripherals': [
            {
                'name': 'adc_oneshot',
                'type': 'adc',
                'role': 'oneshot',
                'config': {
                    'unit_id': 'ADC_UNIT_1',
                    'channel_id': 4,
                    'atten': 'ADC_ATTEN_DB_0',
                    'bit_width': 'ADC_BITWIDTH_DEFAULT',
                },
            },
        ],
        'device': {
            'name': 'adc_button_0',
            'type': 'button',
            'sub_type': 'adc_single',
            'config': {
                'button_index': 0,
                'min_voltage': 0,
                'max_voltage': 500,
            },
            'peripherals': [
                {'name': 'adc_oneshot'},
            ],
        },
        'expected': {
            'type': 'button',
            'sub_type': 'adc_single',
            'peripherals': ['adc_oneshot'],
        },
    },
    {
        'id': 'dev_button_adc_multi',
        'chip': 'esp32',
        'peripherals': [
            {
                'name': 'adc_oneshot',
                'type': 'adc',
                'role': 'oneshot',
                'config': {
                    'unit_id': 'ADC_UNIT_1',
                    'channel_id': 4,
                    'atten': 'ADC_ATTEN_DB_0',
                    'bit_width': 'ADC_BITWIDTH_DEFAULT',
                },
            },
        ],
        'device': {
            'name': 'adc_button_group',
            'type': 'button',
            'sub_type': 'adc_multi',
            'config': {
                'button_num': 2,
                'voltage_range': [380, 820],
                'button_labels': ['A', 'B'],
                'max_voltage': 3000,
            },
            'peripherals': [
                {'name': 'adc_oneshot'},
            ],
        },
        'expected': {
            'type': 'button',
            'sub_type': 'adc_multi',
            'peripherals': ['adc_oneshot'],
        },
    },
    {
        'id': 'dev_camera_dvp',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'i2c_master',
                'type': 'i2c',
                'config': {'port': 0, 'pins': {'sda': 17, 'scl': 18}},
            },
        ],
        'device': {
            'name': 'camera',
            'type': 'camera',
            'sub_type': 'dvp',
            'config': {
                'dvp_config': {
                    'reset_io': 10,
                    'pclk_io': 12,
                    'xclk_io': 11,
                    'vsync_io': 13,
                    'data_io': {
                        'data_io_0': 14,
                        'data_io_1': 15,
                    },
                },
            },
            'peripherals': [
                {'name': 'i2c_master', 'frequency': 400000},
            ],
        },
        'expected': {
            'type': 'camera',
            'sub_type': 'dvp',
            'peripherals': ['i2c_master'],
            'io': {
                'reset_io': 10,
                'pclk_io': 12,
                'xclk_io': 11,
                'vsync_io': 13,
                'data_io_0': 14,
                'data_io_1': 15,
            },
        },
    },
    {
        'id': 'dev_camera_csi',
        'chip': 'esp32p4',
        'peripherals': [
            {
                'name': 'i2c_master',
                'type': 'i2c',
                'config': {'port': 0, 'pins': {'sda': 17, 'scl': 18}},
            },
            {
                'name': 'ldo_mipi',
                'type': 'ldo',
                'config': {'ldo_cfg': {'chan_id': 3, 'voltage_mv': 2500, 'adjustable': 1, 'owned_by_hw': 0}},
            },
        ],
        'device': {
            'name': 'camera',
            'type': 'camera',
            'sub_type': 'csi',
            'config': {
                'csi_config': {
                    'reset_io': 20,
                    'dont_init_ldo': True,
                    'xclk_config': {'xclk_pin': 21},
                },
            },
            'peripherals': [
                {'name': 'i2c_master', 'frequency': 400000},
                {'name': 'ldo_mipi'},
            ],
        },
        'expected': {
            'type': 'camera',
            'sub_type': 'csi',
            'peripherals': ['i2c_master', 'ldo_mipi'],
            'io': {
                'reset_io': 20,
                'xclk_pin': 21,
            },
        },
    },
    {
        'id': 'dev_camera_spi',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'i2c_master',
                'type': 'i2c',
                'config': {'port': 0, 'pins': {'sda': 17, 'scl': 18}},
            },
        ],
        'device': {
            'name': 'camera',
            'type': 'camera',
            'sub_type': 'spi',
            'config': {
                'spi_config': {
                    'spi_cs_pin': 10,
                    'spi_sclk_pin': 11,
                    'spi_data0_io_pin': 12,
                    'xclk_pin': 13,
                },
            },
            'peripherals': [
                {'name': 'i2c_master', 'frequency': 100000},
            ],
        },
        'expected': {
            'type': 'camera',
            'sub_type': 'spi',
            'peripherals': ['i2c_master'],
            'io': {
                'spi_cs_pin': 10,
                'spi_sclk_pin': 11,
                'spi_data0_io_pin': 12,
                'xclk_pin': 13,
            },
        },
    },
    {
        'id': 'dev_custom',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'i2c_master',
                'type': 'i2c',
                'config': {'port': 0, 'pins': {'sda': 17, 'scl': 18}},
            },
            {
                'name': 'gpio_aux',
                'type': 'gpio',
                'config': {'pin': 9, 'mode': 'GPIO_MODE_OUTPUT'},
            },
        ],
        'device': {
            'name': 'custom_sensor',
            'type': 'custom',
            'config': {
                'sensor_id': 1,
            },
            'peripherals': [
                {'name': 'i2c_master'},
                {'name': 'gpio_aux'},
            ],
            'dependencies': {
                'esp_driver_i2c': {
                    'require': 'public',
                }
            },
        },
        'expected': {
            'type': 'custom',
            'peripherals': ['i2c_master', 'gpio_aux'],
            'dependencies': {
                'esp_driver_i2c': {
                    'require': 'public',
                }
            },
        },
    },
    {
        'id': 'dev_display_lcd_dsi',
        'chip': 'esp32p4',
        'peripherals': [
            {
                'name': 'ldo_mipi',
                'type': 'ldo',
                'config': {'ldo_cfg': {'chan_id': 3, 'voltage_mv': 2500, 'adjustable': 1, 'owned_by_hw': 0}},
            },
            {
                'name': 'dsi_display',
                'type': 'dsi',
                'config': {'bus_id': 0, 'data_lanes': 2, 'phy_clk_src': 0, 'lane_bit_rate_mbps': 1000},
            },
        ],
        'device': {
            'name': 'display_lcd',
            'type': 'display_lcd',
            'sub_type': 'dsi',
            'dependencies': {
                'espressif/esp_lcd_generic': '*',
            },
            'config': {
                'reset_gpio_num': 27,
                'dpi_config': {
                    'video_timing': {
                        'h_size': 800,
                        'v_size': 480,
                    },
                },
            },
            'peripherals': [
                {'name': 'ldo_mipi'},
                {'name': 'dsi_display'},
            ],
        },
        'expected': {
            'type': 'display_lcd',
            'sub_type': 'dsi',
            'peripherals': ['ldo_mipi', 'dsi_display'],
            'dependencies': {
                'espressif/esp_lcd_generic': '*',
            },
            'io': {
                'reset_gpio_num': 27,
            },
        },
    },
    {
        'id': 'dev_display_lcd_spi',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'spi_master',
                'type': 'spi',
                'config': {
                    'spi_port': 1,
                    'spi_bus_config': {
                        'mosi_io_num': 12,
                        'miso_io_num': 13,
                        'sclk_io_num': 14,
                    },
                },
            },
        ],
        'device': {
            'name': 'display_lcd',
            'type': 'display_lcd',
            'sub_type': 'spi',
            'dependencies': {
                'espressif/esp_lcd_generic': '*',
            },
            'config': {
                'io_spi_config': {
                    'cs_gpio_num': 5,
                    'dc_gpio_num': 6,
                },
                'lcd_panel_config': {
                    'reset_gpio_num': 7,
                },
            },
            'peripherals': [
                {'name': 'spi_master'},
            ],
        },
        'expected': {
            'type': 'display_lcd',
            'sub_type': 'spi',
            'peripherals': ['spi_master'],
            'dependencies': {
                'espressif/esp_lcd_generic': '*',
            },
            'io': {
                'cs_gpio_num': 5,
                'dc_gpio_num': 6,
                'reset_gpio_num': 7,
            },
        },
    },
    {
        'id': 'dev_display_lcd_parlio',
        'chip': 'esp32s3',
        'peripherals': [],
        'device': {
            'name': 'display_lcd',
            'type': 'display_lcd',
            'sub_type': 'parlio',
            'dependencies': {
                'espressif/esp_lcd_generic': '*',
            },
            'config': {
                'io_parl_config': {
                    'dc_gpio_num': 3,
                    'clk_gpio_num': 4,
                    'cs_gpio_num': 5,
                    'data_gpio_nums': [6, 7, 8, 9],
                },
                'lcd_panel_config': {
                    'reset_gpio_num': 10,
                },
            },
        },
        'expected': {
            'type': 'display_lcd',
            'sub_type': 'parlio',
            'dependencies': {
                'espressif/esp_lcd_generic': '*',
            },
            'io': {
                'dc_gpio_num': 3,
                'clk_gpio_num': 4,
                'cs_gpio_num': 5,
                'data_gpio_nums': [6, 7, 8, 9],
                'reset_gpio_num': 10,
            },
        },
    },
    {
        'id': 'dev_fs_fat_sdmmc',
        'chip': 'esp32s3',
        'peripherals': [],
        'device': {
            'name': 'fs_sdcard',
            'type': 'fs_fat',
            'sub_type': 'sdmmc',
            'config': {
                'sub_config': {
                    'pins': {
                        'clk': 15,
                        'cmd': 7,
                        'd0': 4,
                    }
                },
            },
        },
        'expected': {
            'type': 'fs_fat',
            'sub_type': 'sdmmc',
            'io': {
                'clk': 15,
                'cmd': 7,
                'd0': 4,
            },
        },
    },
    {
        'id': 'dev_fs_fat_spi',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'spi_master',
                'type': 'spi',
                'config': {
                    'spi_port': 1,
                    'spi_bus_config': {
                        'mosi_io_num': 12,
                        'miso_io_num': 13,
                        'sclk_io_num': 14,
                    },
                },
            },
        ],
        'device': {
            'name': 'fs_sdcard',
            'type': 'fs_fat',
            'sub_type': 'spi',
            'config': {
                'sub_config': {
                    'cs_gpio_num': 15,
                },
            },
            'peripherals': [
                {'name': 'spi_master'},
            ],
        },
        'expected': {
            'type': 'fs_fat',
            'sub_type': 'spi',
            'peripherals': ['spi_master'],
            'io': {
                'cs_gpio_num': 15,
            },
        },
    },
    {
        'id': 'dev_fs_spiffs',
        'chip': 'esp32s3',
        'peripherals': [],
        'device': {
            'name': 'fs_spiffs',
            'type': 'fs_spiffs',
            'config': {
                'base_path': '/spiffs',
            },
        },
        'expected': {
            'type': 'fs_spiffs',
        },
    },
    {
        'id': 'dev_gpio_ctrl',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'gpio_power',
                'type': 'gpio',
                'config': {'pin': 16, 'mode': 'GPIO_MODE_OUTPUT'},
            },
        ],
        'device': {
            'name': 'power_control',
            'type': 'gpio_ctrl',
            'config': {
                'active_level': 1,
                'default_level': 0,
            },
            'peripherals': [
                {'name': 'gpio_power'},
            ],
        },
        'expected': {
            'type': 'gpio_ctrl',
            'peripherals': ['gpio_power'],
        },
    },
    {
        'id': 'dev_gpio_expander',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'i2c_master',
                'type': 'i2c',
                'config': {'port': 0, 'pins': {'sda': 17, 'scl': 18}},
            },
        ],
        'device': {
            'name': 'gpio_expander',
            'type': 'gpio_expander',
            'chip': 'tca9554',
            'dependencies': {
                'espressif/esp_io_expander_generic': '*',
            },
            'config': {
                'max_pins': 8,
                'output_io_mask': [1],
                'output_io_level_mask': [1],
            },
            'peripherals': [
                {'name': 'i2c_master', 'i2c_addr': [0x70]},
            ],
        },
        'expected': {
            'type': 'gpio_expander',
            'peripherals': ['i2c_master'],
            'dependencies': {
                'espressif/esp_io_expander_generic': '*',
            },
        },
    },
    {
        'id': 'dev_lcd_touch_i2c',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'i2c_master',
                'type': 'i2c',
                'config': {'port': 0, 'pins': {'sda': 17, 'scl': 18}},
            },
        ],
        'device': {
            'name': 'lcd_touch',
            'type': 'lcd_touch_i2c',
            'chip': 'generic_touch',
            'dependencies': {
                'espressif/esp_lcd_touch_generic': '*',
            },
            'config': {
                'io_i2c_config': {
                    'peripherals': [
                        {'name': 'i2c_master'},
                    ],
                },
                'touch_config': {
                    'rst_gpio_num': 8,
                    'int_gpio_num': 9,
                },
            },
            'peripherals': [
                {'name': 'i2c_master'},
            ],
        },
        'expected': {
            'type': 'lcd_touch_i2c',
            'peripherals': ['i2c_master'],
            'dependencies': {
                'espressif/esp_lcd_touch_generic': '*',
            },
            'io': {
                'rst_gpio_num': 8,
                'int_gpio_num': 9,
            },
        },
    },
    {
        'id': 'dev_ledc_ctrl',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'ledc_backlight',
                'type': 'ledc',
                'config': {'gpio_num': 21},
            },
        ],
        'device': {
            'name': 'lcd_brightness',
            'type': 'ledc_ctrl',
            'config': {
                'default_percent': 100,
            },
            'peripherals': [
                {'name': 'ledc_backlight'},
            ],
        },
        'expected': {
            'type': 'ledc_ctrl',
            'peripherals': ['ledc_backlight'],
        },
    },
    {
        'id': 'dev_power_ctrl',
        'chip': 'esp32s3',
        'peripherals': [
            {
                'name': 'gpio_power',
                'type': 'gpio',
                'config': {'pin': 16, 'mode': 'GPIO_MODE_OUTPUT'},
            },
        ],
        'device': {
            'name': 'audio_power_ctrl',
            'type': 'power_ctrl',
            'sub_type': 'gpio',
            'peripherals': [
                {'name': 'gpio_power', 'active_level': 1},
            ],
        },
        'expected': {
            'type': 'power_ctrl',
            'sub_type': 'gpio',
            'peripherals': ['gpio_power'],
        },
    },
]


@pytest.mark.parametrize('case', PERIPHERAL_CASES, ids=[case['id'] for case in PERIPHERAL_CASES])
def test_board_metadata_matrix_for_peripherals(bmgr_root, tmp_path, case):
    metadata = _generate_metadata(
        bmgr_root,
        tmp_path,
        chip=case['chip'],
        peripherals=deepcopy(case['peripherals']),
        devices=[],
    )

    peripheral_name = case['peripherals'][0]['name']
    assert metadata['peripherals'][peripheral_name] == case['expected']


@pytest.mark.parametrize('case', DEVICE_CASES, ids=[case['id'] for case in DEVICE_CASES])
def test_board_metadata_matrix_for_devices(bmgr_root, tmp_path, case):
    metadata = _generate_metadata(
        bmgr_root,
        tmp_path,
        chip=case['chip'],
        peripherals=deepcopy(case['peripherals']),
        devices=[deepcopy(case['device'])],
    )

    device_name = case['device']['name']
    assert metadata['devices'][device_name] == case['expected']
