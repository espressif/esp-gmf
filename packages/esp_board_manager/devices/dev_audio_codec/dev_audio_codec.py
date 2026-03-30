# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# Audio codec device config parser
VERSION = 'v1.0.0'

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

def get_includes() -> list:
    """Return list of required include headers for audio codec device"""
    return [
        'dev_audio_codec.h'
    ]

def _parse_bool(value, default: bool) -> bool:
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        text = value.strip().lower()
        if text in ('1', 'true', 'yes', 'on'):
            return True
        if text in ('0', 'false', 'no', 'off'):
            return False
    return default

def _validate_peripheral_reference(device_name: str, periph_name: str, peripherals_dict) -> None:
    if peripherals_dict is not None and periph_name not in peripherals_dict:
        raise ValueError(f"Audio codec device {device_name} references undefined peripheral '{periph_name}'")

def _get_peripheral_base_config(peripherals_dict, periph_name: str) -> dict:
    if not peripherals_dict or periph_name not in peripherals_dict:
        return {}
    return peripherals_dict[periph_name].config

def _is_input_i2s_peripheral(peripherals_dict, periph_name: str) -> bool:
    if not peripherals_dict or periph_name not in peripherals_dict:
        return False

    periph = peripherals_dict[periph_name]
    if getattr(periph, 'type', None) != 'i2s':
        return False

    format_str = getattr(periph, 'format', None)
    if not format_str:
        return True
    return 'in' in str(format_str).lower().split('-')

def _parse_codec_peripherals(device_name: str, peripherals: list, peripherals_dict=None):
    """Parse non-ADC peripherals and collect ADC reuse peripheral name (if any)."""
    # These fields are struct members in dev_audio_codec_config_t (not pointers),
    # so keep zero/default struct initializers instead of None/NULL.
    pa_cfg = {
        'name': None,
        'port': 0,
        'active_level': 0,
        'gain': 0.0,
    }
    i2c_cfg = {
        'name': None,
        'port': 0,
        'address': 0,
        'frequency': 0,
    }
    i2s_cfg = {
        'name': None,
        'port': 0,
        'clk_src': 0,
        'tx_aux_out_io': -1,
        'tx_aux_out_line': 0,
        'tx_aux_out_invert': False,
    }
    adc_periph_name = ''
    adc_periph_count = 0

    for periph in peripherals:
        periph_name = periph.get('name', '')

        if periph_name.startswith('gpio'):
            _validate_peripheral_reference(device_name, periph_name, peripherals_dict)
            peripheral_config = _get_peripheral_base_config(peripherals_dict, periph_name)
            pa_cfg = {
                'name': periph_name,
                'port': peripheral_config.get('pin', -1),
                'active_level': int(periph.get('active_level', 0)),
                'gain': float(periph.get('gain', 0.0)),
            }
            continue

        if periph_name.startswith('i2c'):
            _validate_peripheral_reference(device_name, periph_name, peripherals_dict)
            peripheral_config = _get_peripheral_base_config(peripherals_dict, periph_name)
            i2c_cfg = {
                'name': periph_name,
                'port': peripheral_config.get('port', 0),
                'address': periph.get('address', 0x30),
                'frequency': int(periph.get('frequency', 400000)),
            }
            continue

        if periph_name.startswith('i2s'):
            _validate_peripheral_reference(device_name, periph_name, peripherals_dict)
            peripheral_config = _get_peripheral_base_config(peripherals_dict, periph_name)
            i2s_cfg = {
                'name': periph_name,
                'port': peripheral_config.get('port', 0),
                'clk_src': periph.get('clk_src', 0),
                'tx_aux_out_io': int(periph.get('tx_aux_out_io', -1)),
                'tx_aux_out_line': int(periph.get('tx_aux_out_line', 0)),
                'tx_aux_out_invert': _parse_bool(periph.get('tx_aux_out_invert', False), False),
            }
            continue

        if periph_name.startswith('adc'):
            _validate_peripheral_reference(device_name, periph_name, peripherals_dict)
            adc_periph_count += 1
            if adc_periph_count == 1:
                adc_periph_name = periph_name

    if adc_periph_count > 1:
        raise ValueError(
            f'Audio codec device {device_name} references multiple adc peripherals, only one is supported currently'
        )

    return pa_cfg, i2c_cfg, i2s_cfg, adc_periph_name


def _parse_adc_local_cfg(device_name: str, device_config: dict) -> dict:
    """Parse ADC local cfg (ADC mic local-create path) and normalize to one structure."""
    adc_local_cfg = device_config.get('adc_local_cfg', {})
    if adc_local_cfg is None:
        adc_local_cfg = {}
    if not isinstance(adc_local_cfg, dict):
        raise ValueError(f'Audio codec device {device_name} config.adc_local_cfg must be a map')

    sample_rate_hz = int(adc_local_cfg.get('sample_rate_hz', 16000))
    max_store_buf_size = int(adc_local_cfg.get('max_store_buf_size', 1024))
    conv_frame_size = int(adc_local_cfg.get('conv_frame_size', 256))
    unit_id = adc_local_cfg.get('unit_id', 'ADC_UNIT_1')
    atten = adc_local_cfg.get('atten', 'ADC_ATTEN_DB_0')
    bit_width = adc_local_cfg.get('bit_width', 'SOC_ADC_DIGI_MAX_BITWIDTH')
    adc_format = adc_local_cfg.get('format', 'ADC_DIGI_OUTPUT_FORMAT_TYPE2')

    channel_list = adc_local_cfg.get('channel_list', [])
    if channel_list is None:
        channel_list = []
    if not isinstance(channel_list, list):
        channel_list = [channel_list]
    adc_channel_list = [int(ch) for ch in channel_list]

    adc_patterns = adc_local_cfg.get('patterns', [])
    if adc_patterns is None:
        adc_patterns = []
    if not isinstance(adc_patterns, list):
        raise ValueError(f'Audio codec device {device_name} config.adc_local_cfg.patterns must be a list')
    if adc_channel_list and adc_patterns:
        raise ValueError(f'Audio codec device {device_name} adc_local_cfg cannot mix channel_list and patterns')

    has_adc_local = len(adc_channel_list) > 0 or len(adc_patterns) > 0
    adc_cfg_mode = 'BOARD_CODEC_ADC_CFG_MODE_SINGLE_UNIT'
    adc_pattern_num = 0
    adc_cfg_union = {
        'single_unit': {
            'unit_id': unit_id,
            'atten': atten,
            'bit_width': bit_width,
            'channel_id': [],
        }
    }

    if has_adc_local:
        if adc_patterns:
            adc_cfg_mode = 'BOARD_CODEC_ADC_CFG_MODE_PATTERN'
            parsed_patterns = []
            for item in adc_patterns:
                if not isinstance(item, dict):
                    raise ValueError(f'Audio codec device {device_name} adc_local_cfg.patterns item must be map')
                parsed_patterns.append({
                    'unit': item.get('unit', 'ADC_UNIT_1'),
                    'channel': int(item.get('channel', -1)),
                    'atten': item.get('atten', 'ADC_ATTEN_DB_0'),
                    'bit_width': item.get('bit_width', 'SOC_ADC_DIGI_MAX_BITWIDTH'),
                })
            adc_pattern_num = len(parsed_patterns)
            adc_cfg_union = {'patterns': parsed_patterns}

            if 'conv_mode' in adc_local_cfg:
                adc_conv_mode = adc_local_cfg.get('conv_mode')
            else:
                unit_set = {p['unit'] for p in parsed_patterns}
                if unit_set == {'ADC_UNIT_1'}:
                    adc_conv_mode = 'ADC_CONV_SINGLE_UNIT_1'
                elif unit_set == {'ADC_UNIT_2'}:
                    adc_conv_mode = 'ADC_CONV_SINGLE_UNIT_2'
                else:
                    adc_conv_mode = 'ADC_CONV_BOTH_UNIT'
        else:
            adc_pattern_num = len(adc_channel_list)
            adc_cfg_union = {
                'single_unit': {
                    'unit_id': unit_id,
                    'atten': atten,
                    'bit_width': bit_width,
                    'channel_id': adc_channel_list,
                }
            }
            adc_conv_mode = adc_local_cfg.get('conv_mode')
            if adc_conv_mode is None:
                adc_conv_mode = 'ADC_CONV_SINGLE_UNIT_1' if unit_id == 'ADC_UNIT_1' else 'ADC_CONV_SINGLE_UNIT_2'
    else:
        adc_conv_mode = adc_local_cfg.get('conv_mode', 'ADC_CONV_SINGLE_UNIT_1')

    return {
        'has_adc_local': has_adc_local,
        'sample_rate_hz': sample_rate_hz,
        'max_store_buf_size': max_store_buf_size,
        'conv_frame_size': conv_frame_size,
        'conv_mode': adc_conv_mode,
        'format': adc_format,
        'pattern_num': adc_pattern_num,
        'cfg_mode': adc_cfg_mode,
        'cfg': adc_cfg_union,
    }

def _validate_internal_adc_config(device_name: str, chip_name: str, device_config: dict,
                                  adc_periph_name: str, has_adc_local: bool,
                                  i2s_periph_name: str, peripherals_dict=None) -> None:
    if bool(device_config.get('adc_enabled', False)) and str(chip_name).lower() == 'internal':
        has_i2s_input_path = bool(i2s_periph_name) and _is_input_i2s_peripheral(peripherals_dict, i2s_periph_name)
        if adc_periph_name == '' and not has_adc_local and not has_i2s_input_path:
            raise ValueError(
                f'Audio codec device {device_name} (chip=internal, adc_enabled=true) requires either '
                'adc_* peripheral, input i2s peripheral, or config.adc_local_cfg'
            )

def _validate_direction_flags(device_name: str, adc_enabled: bool, dac_enabled: bool) -> None:
    """Validate mutually-exclusive ADC/DAC enablement for one logical audio_codec device."""
    if adc_enabled and dac_enabled:
        raise ValueError(
            f'Audio codec device {device_name} cannot enable both adc_enabled and dac_enabled. '
            'Please split full-duplex use cases into separate logical devices.'
        )

def _build_disabled_adc_cfg() -> dict:
    """Return zeroed ADC config for devices that do not use ADC at all."""
    return {
        'periph_name': None,
        'sample_rate_hz': 0,
        'max_store_buf_size': 0,
        'conv_frame_size': 0,
        'conv_mode': 0,
        'format': 0,
        'pattern_num': 0,
        'cfg_mode': 0,
        'cfg': {
            'single_unit': {
                'unit_id': 0,
                'atten': 0,
                'bit_width': 0,
                'channel_id': [],
            }
        },
    }

def parse(name: str, config: dict, peripherals_dict=None) -> dict:
    # Parse the device name - use name directly for C naming
    c_name = name.replace('-', '_')  # Convert hyphens to underscores for C naming

    # Get the device config and peripherals
    device_config = config.get('config', {})
    peripherals = config.get('peripherals', [])

    # Get chip name from device level
    chip_name = config.get('chip', 'unknown')

    pa_cfg, i2c_cfg, i2s_cfg, adc_periph_name = _parse_codec_peripherals(name, peripherals, peripherals_dict)
    adc_enabled = bool(device_config.get('adc_enabled', False))
    dac_enabled = bool(device_config.get('dac_enabled', False))
    _validate_direction_flags(name, adc_enabled, dac_enabled)

    if adc_periph_name != '':
        has_adc_local = False
        adc_cfg = {
            'periph_name': adc_periph_name,
        }
    else:
        adc_local_cfg = _parse_adc_local_cfg(name, device_config)
        has_adc_local = adc_local_cfg['has_adc_local']
        if has_adc_local:
            adc_cfg = {
                'sample_rate_hz': adc_local_cfg['sample_rate_hz'],
                'max_store_buf_size': adc_local_cfg['max_store_buf_size'],
                'conv_frame_size': adc_local_cfg['conv_frame_size'],
                'conv_mode': adc_local_cfg['conv_mode'],
                'format': adc_local_cfg['format'],
                'pattern_num': adc_local_cfg['pattern_num'],
                'cfg_mode': adc_local_cfg['cfg_mode'],
                'cfg': adc_local_cfg['cfg'],
            }
        else:
            adc_cfg = _build_disabled_adc_cfg()
    data_if_type = 1 if (adc_periph_name != '' or has_adc_local) else 0
    _validate_internal_adc_config(
        name,
        chip_name,
        device_config,
        adc_periph_name,
        has_adc_local,
        i2s_cfg.get('name'),
        peripherals_dict,
    )

    # Convert string masks to integers
    adc_channel_mask = device_config.get('adc_channel_mask', '0011')
    if isinstance(adc_channel_mask, str):
        # Convert binary string to hex (e.g., "1110" -> 0xE)
        adc_channel_mask = int(adc_channel_mask, 2)

    dac_channel_mask = device_config.get('dac_channel_mask', '0')
    if isinstance(dac_channel_mask, str):
        # Convert binary string to hex (e.g., "1110" -> 0xE)
        dac_channel_mask = int(dac_channel_mask, 2)

    # Parse ADC channel labels
    adc_channel_labels = device_config.get('adc_channel_labels', [])
    # Convert list to comma-separated string if it's a list
    if isinstance(adc_channel_labels, list):
        adc_channel_labels_str = ','.join(adc_channel_labels) if adc_channel_labels else ''
    else:
        adc_channel_labels_str = str(adc_channel_labels) if adc_channel_labels else ''

    # Parse metadata if provided
    metadata = device_config.get('metadata', None)
    metadata_size = len(metadata) if metadata else 0

    result = {
        'struct_type': 'dev_audio_codec_config_t',
        'struct_var': f'{c_name}_cfg',  # Use the correct C name format
        'struct_init': {
            'name': c_name,  # Use the correct C name
            'chip': chip_name,  # Add chip field
            'type': str(config.get('type', 'audio_codec')),
            'data_if_type': data_if_type,
            'adc_enabled': adc_enabled,
            'adc_max_channel': int(device_config.get('adc_max_channel', 0)),
            'adc_channel_mask': adc_channel_mask,
            'adc_channel_labels': adc_channel_labels_str,
            'adc_init_gain': int(device_config.get('adc_init_gain', 0)),
            'dac_enabled': dac_enabled,
            'dac_max_channel': int(device_config.get('dac_max_channel', 0)),
            'dac_channel_mask': dac_channel_mask,
            'dac_init_gain': int(device_config.get('dac_init_gain', 0)),
            'pa_cfg': pa_cfg,
            'i2c_cfg': i2c_cfg,
            'i2s_cfg': i2s_cfg,
            'adc_cfg': adc_cfg,
            'metadata': metadata,
            'metadata_size': metadata_size,
            'mclk_enabled': bool(device_config.get('mclk_enabled', False)),
            'aec_enabled': bool(device_config.get('aec', False)),
            'eq_enabled': bool(device_config.get('eq', False)),
            'alc_enabled': bool(device_config.get('alc', False))
        }
    }
    return result
