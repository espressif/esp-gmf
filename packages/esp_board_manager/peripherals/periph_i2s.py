# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# I2S peripheral config parser
VERSION = 'v1.0.0'

import sys

import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'generators'))

def get_includes() -> list:
    """Return list of required include headers for I2S peripheral"""
    return [
        'periph_i2s.h',
    ]

def validate_enum_value(value: str, enum_type: str) -> bool:
    """Validate if the enum value is within the valid range for ESP-IDF I2S driver.

    Args:
        value: The enum value to validate
        enum_type: The type of enum (clk_src, slot_mode, slot_mask, etc.)

    Returns:
        True if valid, False otherwise
    """
    # Define valid enum values based on ESP-IDF I2S driver
    valid_enums = {
        'clk_src': [
            'I2S_CLK_SRC_DEFAULT',
            'I2S_CLK_SRC_EXTERNAL',
            'I2S_CLK_SRC_APB',
            'I2S_CLK_SRC_PLL_F160M',
            'I2S_CLK_SRC_PLL_F240M'
        ],
        'slot_mode': [
            'I2S_SLOT_MODE_MONO',
            'I2S_SLOT_MODE_STEREO'
        ],
        'slot_bit_width': [
            'I2S_SLOT_BIT_WIDTH_AUTO',
            'I2S_SLOT_BIT_WIDTH_8BIT',
            'I2S_SLOT_BIT_WIDTH_16BIT',
            'I2S_SLOT_BIT_WIDTH_24BIT',
            'I2S_SLOT_BIT_WIDTH_32BIT'
        ],
        'std_slot_mask': [
            'I2S_STD_SLOT_LEFT',
            'I2S_STD_SLOT_RIGHT',
            'I2S_STD_SLOT_BOTH'
        ],
        'tdm_slot_mask': [
            'I2S_TDM_SLOT0',
            'I2S_TDM_SLOT1',
            'I2S_TDM_SLOT2',
            'I2S_TDM_SLOT3',
            'I2S_TDM_SLOT4',
            'I2S_TDM_SLOT5',
            'I2S_TDM_SLOT6',
            'I2S_TDM_SLOT7',
            'I2S_TDM_SLOT8',
            'I2S_TDM_SLOT9',
            'I2S_TDM_SLOT10',
            'I2S_TDM_SLOT11',
            'I2S_TDM_SLOT12',
            'I2S_TDM_SLOT13',
            'I2S_TDM_SLOT14',
            'I2S_TDM_SLOT15'
        ],
        'data_bit_width': [
            'I2S_DATA_BIT_WIDTH_8BIT',
            'I2S_DATA_BIT_WIDTH_16BIT',
            'I2S_DATA_BIT_WIDTH_24BIT',
            'I2S_DATA_BIT_WIDTH_32BIT'
        ],
        'mclk_multiple': [
            'I2S_MCLK_MULTIPLE_128',
            'I2S_MCLK_MULTIPLE_192',
            'I2S_MCLK_MULTIPLE_256',
            'I2S_MCLK_MULTIPLE_384',
            'I2S_MCLK_MULTIPLE_512',
            'I2S_MCLK_MULTIPLE_576',
            'I2S_MCLK_MULTIPLE_768',
            'I2S_MCLK_MULTIPLE_1024',
            'I2S_MCLK_MULTIPLE_1152'
        ],
        'pdm_data_fmt': [
            'I2S_PDM_DATA_FMT_PCM',
            'I2S_PDM_DATA_FMT_RAW'
        ],
        'pdm_sig_scale': [
            'I2S_PDM_SIG_SCALING_DIV_2',
            'I2S_PDM_SIG_SCALING_MUL_1',
            'I2S_PDM_SIG_SCALING_MUL_2',
            'I2S_PDM_SIG_SCALING_MUL_4'
        ],
        'pdm_line_mode': [
            'I2S_PDM_TX_ONE_LINE_CODEC',
            'I2S_PDM_TX_ONE_LINE_DAC',
            'I2S_PDM_TX_TWO_LINE_DAC'
        ],
        'pdm_slot_mask': [
            'I2S_PDM_SLOT_LEFT',
            'I2S_PDM_SLOT_RIGHT',
            'I2S_PDM_SLOT_BOTH',
            'I2S_PDM_RX_LINE0_SLOT_LEFT',
            'I2S_PDM_RX_LINE0_SLOT_RIGHT',
            'I2S_PDM_RX_LINE1_SLOT_LEFT',
            'I2S_PDM_RX_LINE1_SLOT_RIGHT',
            'I2S_PDM_RX_LINE2_SLOT_LEFT',
            'I2S_PDM_RX_LINE2_SLOT_RIGHT',
            'I2S_PDM_RX_LINE3_SLOT_LEFT',
            'I2S_PDM_RX_LINE3_SLOT_RIGHT',
            'I2S_PDM_LINE_SLOT_ALL'
        ],
        'pdm_dsr': [
            'I2S_PDM_DSR_8S',
            'I2S_PDM_DSR_16S'
        ]
    }

    if enum_type not in valid_enums:
        print(f"⚠️  WARNING: Unknown enum type '{enum_type}' for validation")
        return True  # Allow unknown types to pass

    if value not in valid_enums[enum_type]:
        print(f"❌ YAML VALIDATION ERROR: Invalid {enum_type} value '{value}'!")
        print(f'      File: board_peripherals.yaml')
        print(f'      Peripheral: I2S configuration')
        print(f"   ❌ Invalid value: '{value}'")
        print(f'   ✅ Valid values: {valid_enums[enum_type]}')
        print(f'   ℹ️ Please use one of the valid enum values listed above')
        return False

    return True

def get_enum_value(value: str, full_enum: str, enum_type: str = None) -> str:
    """Helper function to get enum value with validation.

    Args:
        value: The value from YAML config
        full_enum: The complete ESP-IDF enum value to use if value is not provided
        enum_type: The type of enum for validation (optional)

    Returns:
        The enum value to use

    Raises:
        ValueError: If validation fails and the value is not empty
    """
    if not value:
        result = full_enum
    else:
        result = value

    # Validate the enum value if enum_type is provided
    if enum_type and not validate_enum_value(result, enum_type):
        # If validation fails and we have a non-empty value, raise an exception
        if value:  # Only raise if user provided a value (not empty)
            raise ValueError(f"Invalid {enum_type} value '{value}'. Please use one of the valid enum values.")
        else:
            # If no value provided, use default and show warning
            print(f"⚠️  WARNING: Using default value '{full_enum}' due to validation failure")
            result = full_enum

    return result

def parse_i2s_format(format_str: str) -> dict:
    """Parse I2S format string into configuration values.

    Args:
        format_str: Format string (e.g. 'std-out', 'pdm-out', 'std-in')

    Returns:
        Dictionary containing mode, direction and config_type values
    """
    if not format_str:
        return {
            'mode': 'I2S_COMM_MODE_STD',
            'direction': 'I2S_DIR_TX',  # Default to TX
            'config_type': 'std'
        }

    parts = format_str.lower().split('-')
    result = {}

    # Parse mode and config type
    if 'std' in parts:
        result['mode'] = 'I2S_COMM_MODE_STD'
        result['config_type'] = 'std'
    elif 'tdm' in parts:
        result['mode'] = 'I2S_COMM_MODE_TDM'
        result['config_type'] = 'tdm'
    elif 'pdm' in parts:
        result['mode'] = 'I2S_COMM_MODE_PDM'
        result['config_type'] = 'pdm'
    else:
        result['mode'] = 'I2S_COMM_MODE_STD'  # Default to standard mode
        result['config_type'] = 'std'

    # Parse direction
    if 'out' in parts:
        result['direction'] = 'I2S_DIR_TX'  # TX for output
    elif 'in' in parts:
        result['direction'] = 'I2S_DIR_RX'  # RX for input
    else:
        result['direction'] = 'I2S_DIR_TX'  # Default to TX

    return result

def parse(name: str, config: dict) -> dict:
    """Parse I2S peripheral configuration from YAML to C structure

    Args:
        name: Peripheral name
        config: Configuration dictionary from YAML

    Returns:
        Dictionary containing C structure configuration

    Raises:
        ValueError: If YAML validation fails
    """
    try:
        # Get format string from parent scope
        format_str = config.get('format', '')  # Get format from parent scope
        if not format_str:
            print(f'WARNING: No format string provided for {name}, using default std-out')
            format_str = 'std-out'
        format_config = parse_i2s_format(format_str)
        # Use logger for debug output
        from generators.utils.logger import get_logger
        logger = get_logger('periph_i2s')
        logger.debug(f"I2S {name} format '{format_str}' to: {format_config}")
        # Get role from parent scope
        role = config.get('role', 'master')  # Get role from parent scope
        role = f'I2S_ROLE_{role.upper()}'

        # Get the config dictionary
        cfg = config.get('config', {})

        # Get port from config, with fallback to parsing from name for backward compatibility
        port = cfg.get('port', 0)  # Default to port 0 if not specified

        # Get pins and invert_flags configurations
        pins = cfg.get('pins', {})
        invert_flags = cfg.get('invert_flags', {})

        # Create base config
        config_dict = {
            'struct_type': 'periph_i2s_config_t',
            'struct_var': f'{name}_cfg',
            'struct_init': {
                'port': f'I2S_NUM_{port}',
                'role': role,
                'mode': format_config['mode'],
                'direction': format_config['direction'],
                'i2s_cfg': {
                    format_config['config_type']: {}  # This will be filled below
                }
            }
        }

        # Add mode-specific config
        if format_config['config_type'] == 'std':
            # Clock config
            config_dict['struct_init']['i2s_cfg']['std'] = {
                'clk_cfg': {
                    'sample_rate_hz': int(cfg.get('sample_rate_hz', 48000)),
                    'clk_src': get_enum_value(cfg.get('clk_src'), 'I2S_CLK_SRC_DEFAULT', 'clk_src'),
                    'ext_clk_freq_hz': 0,  # Only used when clk_src is EXTERNAL
                    'mclk_multiple': 'I2S_MCLK_MULTIPLE_' + str(cfg.get('mclk_multiple', 256))
                },
                # Slot config
                'slot_cfg': {
                    'data_bit_width': f"I2S_DATA_BIT_WIDTH_{cfg.get('data_bit_width', 16)}BIT",
                    'slot_bit_width': get_enum_value(cfg.get('slot_bit_width'), 'I2S_SLOT_BIT_WIDTH_AUTO', 'slot_bit_width'),
                    'slot_mode': get_enum_value(cfg.get('slot_mode'), 'I2S_SLOT_MODE_STEREO', 'slot_mode'),
                    'slot_mask': get_enum_value(cfg.get('slot_mask'), 'I2S_STD_SLOT_BOTH', 'std_slot_mask'),
                    'ws_width': int(cfg.get('ws_width', 16)),
                    'ws_pol': bool(cfg.get('ws_pol', False)),
                    'bit_shift': bool(cfg.get('bit_shift', True)),
                    'left_align': bool(cfg.get('left_align', True)),
                    'big_endian': bool(cfg.get('big_endian', False)),
                    'bit_order_lsb': bool(cfg.get('bit_order_lsb', False))
                },
                # GPIO config
                'gpio_cfg': {
                    'mclk': int(pins.get('mclk', -1)),
                    'bclk': int(pins.get('bclk', -1)),
                    'ws': int(pins.get('ws', -1)),
                    'dout': int(pins.get('dout', -1)),
                    'din': int(pins.get('din', -1)),
                    'invert_flags': {
                        'mclk_inv': bool(invert_flags.get('mclk_inv', False)),
                        'bclk_inv': bool(invert_flags.get('bclk_inv', False)),
                        'ws_inv': bool(invert_flags.get('ws_inv', False))
                    }
                }
            }

        elif format_config['config_type'] == 'tdm':
            # Clock config
            config_dict['struct_init']['i2s_cfg']['tdm'] = {
                'clk_cfg': {
                    'sample_rate_hz': int(cfg.get('sample_rate_hz', 48000)),
                    'clk_src': get_enum_value(cfg.get('clk_src'), 'I2S_CLK_SRC_DEFAULT', 'clk_src'),
                    'ext_clk_freq_hz': 0,  # Only used when clk_src is EXTERNAL
                    'mclk_multiple': 'I2S_MCLK_MULTIPLE_' + str(cfg.get('mclk_multiple', 256)),
                    'bclk_div': int(cfg.get('bclk_div', 8))
                },
                # Slot config
                'slot_cfg': {
                    'data_bit_width': f"I2S_DATA_BIT_WIDTH_{cfg.get('data_bit_width', 16)}BIT",
                    'slot_bit_width': get_enum_value(cfg.get('slot_bit_width'), 'I2S_SLOT_BIT_WIDTH_AUTO', 'slot_bit_width'),
                    'slot_mode': get_enum_value(cfg.get('slot_mode'), 'I2S_SLOT_MODE_STEREO', 'slot_mode'),
                    'slot_mask': cfg.get('slot_mask', 'I2S_TDM_SLOT0 | I2S_TDM_SLOT1' if cfg.get('slot_mode', 'STEREO') == 'STEREO' else 'I2S_TDM_SLOT0'),
                    'ws_width': int(cfg.get('ws_width', 16)),
                    'ws_pol': bool(cfg.get('ws_pol', False)),
                    'bit_shift': bool(cfg.get('bit_shift', True)),
                    'left_align': bool(cfg.get('left_align', False)),
                    'big_endian': bool(cfg.get('big_endian', False)),
                    'bit_order_lsb': bool(cfg.get('bit_order_lsb', False)),
                    'skip_mask': bool(cfg.get('skip_mask', False)),
                    'total_slot': int(cfg.get('total_slot', 2))
                },
                # GPIO config
                'gpio_cfg': {
                    'mclk': int(pins.get('mclk', -1)),
                    'bclk': int(pins.get('bclk', -1)),
                    'ws': int(pins.get('ws', -1)),
                    'dout': int(pins.get('dout', -1)),
                    'din': int(pins.get('din', -1)),
                    'invert_flags': {
                        'mclk_inv': bool(invert_flags.get('mclk_inv', False)),
                        'bclk_inv': bool(invert_flags.get('bclk_inv', False)),
                        'ws_inv': bool(invert_flags.get('ws_inv', False))
                    }
                }
            }

        elif format_config['config_type'] == 'pdm':
            if format_config['direction'] == 'I2S_DIR_TX':  # TX for output
                config_dict['struct_init']['i2s_cfg'] = {
                    'pdm_tx': {  # Use pdm_tx for output
                        # Clock config
                        'clk_cfg': {
                            'sample_rate_hz': int(cfg.get('sample_rate_hz', 48000)),
                            'clk_src': get_enum_value(cfg.get('clk_src'), 'I2S_CLK_SRC_DEFAULT', 'clk_src'),
                            'mclk_multiple': 'I2S_MCLK_MULTIPLE_' + str(cfg.get('mclk_multiple', 256)),
                            'up_sample_fp': int(cfg.get('up_sample_fp', 960)),
                            'up_sample_fs': int(cfg.get('up_sample_fs', 480)),
                            'bclk_div': int(cfg.get('bclk_div', 8))
                        },
                        # Slot config
                        'slot_cfg': {
                            'data_bit_width': f"I2S_DATA_BIT_WIDTH_{cfg.get('data_bit_width', 16)}BIT",
                            'slot_bit_width': get_enum_value(cfg.get('slot_bit_width'), 'I2S_SLOT_BIT_WIDTH_AUTO', 'slot_bit_width'),
                            'slot_mode': get_enum_value(cfg.get('slot_mode'), 'I2S_SLOT_MODE_STEREO', 'slot_mode'),
                            'slot_mask': get_enum_value(cfg.get('slot_mask'), 'I2S_PDM_SLOT_BOTH', 'pdm_slot_mask'),
                            'data_fmt': get_enum_value(cfg.get('data_fmt'), 'I2S_PDM_DATA_FMT_PCM', 'pdm_data_fmt'),
                            'sd_prescale': int(cfg.get('sd_prescale', 0)),
                            'sd_scale': get_enum_value(cfg.get('sd_scale'), 'I2S_PDM_SIG_SCALING_MUL_1', 'pdm_sig_scale'),
                            'hp_scale': get_enum_value(cfg.get('hp_scale'), 'I2S_PDM_SIG_SCALING_MUL_1', 'pdm_sig_scale'),
                            'lp_scale': get_enum_value(cfg.get('lp_scale'), 'I2S_PDM_SIG_SCALING_MUL_1', 'pdm_sig_scale'),
                            'sinc_scale': get_enum_value(cfg.get('sinc_scale'), 'I2S_PDM_SIG_SCALING_MUL_1', 'pdm_sig_scale'),
                            'line_mode': get_enum_value(cfg.get('line_mode'), 'I2S_PDM_TX_ONE_LINE_CODEC', 'pdm_line_mode'),
                            'hp_en': bool(cfg.get('hp_en', True)),
                            'hp_cut_off_freq_hz': float(cfg.get('hp_cut_off_freq_hz', 35.5)),
                            'sd_dither': int(cfg.get('sd_dither', 0)),
                            'sd_dither2': int(cfg.get('sd_dither2', 1))
                        },
                        # GPIO config
                        'gpio_cfg': {
                            'clk': int(pins.get('clk', -1)),
                            'dout': int(pins.get('dout', -1)),
                            'dout2': int(pins.get('dout2', -1)) if 'dout2' in pins else -1,  # Second data pin for dual-line DAC mode
                            'invert_flags': {
                                'clk_inv': bool(invert_flags.get('clk_inv', False))
                            }
                        }
                    }
                }
            else:  # I2S_DIR_RX - Use PDM RX configuration for input direction
                config_dict['struct_init']['i2s_cfg'] = {
                    'pdm_rx': {  # Use pdm_rx for input
                        # Clock config
                        'clk_cfg': {
                            'sample_rate_hz': int(cfg.get('sample_rate_hz', 48000)),
                            'clk_src': get_enum_value(cfg.get('clk_src'), 'I2S_CLK_SRC_DEFAULT', 'clk_src'),
                            'mclk_multiple': 'I2S_MCLK_MULTIPLE_' + str(cfg.get('mclk_multiple', 256)),
                            'dn_sample_mode': get_enum_value(cfg.get('dn_sample_mode'), 'I2S_PDM_DSR_8S', 'pdm_dsr'),
                            'bclk_div': int(cfg.get('bclk_div', 8))
                        },
                        # Slot config
                        'slot_cfg': {
                            'data_bit_width': f"I2S_DATA_BIT_WIDTH_{cfg.get('data_bit_width', 16)}BIT",
                            'slot_bit_width': get_enum_value(cfg.get('slot_bit_width'), 'I2S_SLOT_BIT_WIDTH_AUTO', 'slot_bit_width'),
                            'slot_mode': get_enum_value(cfg.get('slot_mode'), 'I2S_SLOT_MODE_STEREO', 'slot_mode'),
                            'slot_mask': get_enum_value(cfg.get('slot_mask'), 'I2S_PDM_SLOT_BOTH', 'pdm_slot_mask'),
                            'data_fmt': get_enum_value(cfg.get('data_fmt'), 'I2S_PDM_DATA_FMT_PCM', 'pdm_data_fmt'),
                            'hp_en': bool(cfg.get('hp_en', True)),
                            'hp_cut_off_freq_hz': float(cfg.get('hp_cut_off_freq_hz', 35.5)),
                            'amplify_num': int(cfg.get('amplify_num', 1))
                        },
                        # GPIO config
                        'gpio_cfg': {
                            'clk': int(pins.get('clk', -1)),
                            'din': int(pins.get('din', -1)) if 'din' in pins else -1,  # Single din pin for PDM RX
                            'dins': [
                                int(pins.get('din0', -1)),
                                int(pins.get('din1', -1)),
                                int(pins.get('din2', -1)),
                                int(pins.get('din3', -1))
                            ] if any(f'din{i}' in pins for i in range(4)) else None,  # Multiple din pins for PDM RX
                            'invert_flags': {
                                'clk_inv': bool(invert_flags.get('clk_inv', False))
                            }
                        }
                    }
                }

        return config_dict

    except ValueError as e:
        # Re-raise ValueError with more context
        raise ValueError(f"YAML validation error in I2S peripheral '{name}': {e}")
    except Exception as e:
        # Catch any other exceptions and provide context
        raise ValueError(f"Error parsing I2S peripheral '{name}': {e}")
