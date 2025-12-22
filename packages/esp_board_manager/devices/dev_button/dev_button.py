# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

#  Button device config parser
VERSION = 'v1.0.0'

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

def get_includes() -> list:
    """Return list of required include headers for  button device"""
    return [
        'dev_button.h'
    ]

def parse(name: str, config: dict, peripherals_dict=None) -> dict:
    """Parse  button device configuration from YAML to C structure"""

    # Parse the device name - use name directly for C naming
    c_name = name.replace('-', '_')  # Convert hyphens to underscores for C naming

    # Get the device config
    device_config = config.get('config', {})
    peripherals = config.get('peripherals', [])

    # Get button type
    button_type = config.get('sub_type')
    if button_type not in ['gpio', 'adc_single', 'adc_multi']:
        raise ValueError(f"YAML validation error in  button device: Invalid button type '{button_type}'. Valid types: ['gpio', 'adc_single', 'adc_multi']")

    # Parse timing configuration
    long_press_time = int(device_config.get('long_press_time', 2000))
    short_press_time = int(device_config.get('short_press_time', 100))
    button_timing_cfg = {
        'long_press_time': long_press_time,
        'short_press_time': short_press_time
    }

    # Parse event configuration (Optimized - bitfield storage)
    events_config = device_config.get('events_cfg', {})

    # Parse enabled events bitfield
    enabled_events = {
        'press_down': int(events_config.get('press_down', True)),
        'press_up': int(events_config.get('press_up', True)),
        'single_click': int(events_config.get('single_click', True)),
        'double_click': int(events_config.get('double_click', True)),
        'multi_click': int(True if events_config.get('multi_click') else False),  # tentative enable if present
        'long_press_start': int(events_config.get('long_press_start', True)),
        'long_press_hold': int(events_config.get('long_press_hold', False)),
        'long_press_up': int(events_config.get('long_press_up', True)),
        'press_repeat': int(events_config.get('press_repeat', False)),
        'press_repeat_done': int(events_config.get('press_repeat_done', False)),
        'press_end': int(events_config.get('press_end', False))
    }

    # Build events configuration structure
    events_cfg = {
        'enabled_events': enabled_events,
    }

    extra_configs = []
    if enabled_events['multi_click'] == 1:
        # Handle multi_click array
        multi_click_value = events_config.get('multi_click')
        if multi_click_value is not None and isinstance(multi_click_value, list):
            multi_click_counts = []
            multi_click_counts = [int(x) for x in multi_click_value]
            name = c_name + '_click[]'
            ptr = c_name + '_click'
            extra_configs.append({
                'struct_type': 'uint8_t',
                'struct_var': name.upper(),
                'struct_init': multi_click_counts,
            })
            events_cfg['multi_click'] = {
                'click_counts': ptr.upper(),
                'count': len(multi_click_counts)
            }
        else:
            raise ValueError('YAML validation error in  button device: Multi-click value must be an array.')

    if enabled_events['long_press_start'] == 1:
        # Handle long_press_start_time array
        long_press_start_times = events_config.get('long_press_start_time')
        if long_press_start_times is not None and isinstance(long_press_start_times, list):
            long_press_start_durations = []
            long_press_start_durations = [int(x) for x in long_press_start_times]
            name = c_name + '_lp_start[]'
            ptr = c_name + '_lp_start'
            extra_configs.append({
                'struct_type': 'uint16_t',
                'struct_var': name.upper(),
                'struct_init': long_press_start_durations,
            })
            events_cfg['long_press_start'] = {
                'durations_ms': ptr.upper(),
                'count': len(long_press_start_durations)
            }
        elif long_press_start_times is not None and not isinstance(long_press_start_times, list):
            raise ValueError('YAML validation error in  button device: Long press start time value must be an array.')

    if enabled_events['long_press_up'] == 1:
        # Handle long_press_up_time array
        long_press_up_times = events_config.get('long_press_up_time')
        if long_press_up_times is not None and isinstance(long_press_up_times, list):
            long_press_up_durations = []
            long_press_up_durations = [int(x) for x in long_press_up_times]
            name = c_name + '_lp_up[]'
            ptr = c_name + '_lp_up'
            extra_configs.append({
                'struct_type': 'uint16_t',
                'struct_var': name.upper(),
                'struct_init': long_press_up_durations,
            })
            events_cfg['long_press_up'] = {
                'durations_ms': ptr.upper(),
                'count': len(long_press_up_durations)
            }
        elif long_press_up_times is not None and not isinstance(long_press_up_times, list):
            raise ValueError('YAML validation error in  button device: Long press up time value must be an array.')

    # Initialize button configuration structure with nested events structure
    button_config = {
        'sub_type': button_type,
        'button_timing_cfg': button_timing_cfg,
        'events_cfg': events_cfg,
        'sub_cfg': {}
    }

    # Parse GPIO button configuration
    if button_type == 'gpio':
        # Get GPIO peripheral name from device config or peripherals list
        gpio_name = device_config.get('gpio_name')

        # If gpio_name is not provided in config, try to extract from peripherals
        if not gpio_name and peripherals:
            for periph in peripherals:
                if isinstance(periph, dict):
                    periph_name = periph.get('name', '')
                    if periph_name.startswith('gpio'):
                        gpio_name = periph_name
                        break
                elif isinstance(periph, str) and periph.startswith('gpio'):
                    gpio_name = periph
                    break

        if gpio_name is None:
            raise ValueError(f"YAML validation error in  button device: Missing 'gpio_name' field for GPIO button")

        # Validate GPIO peripheral exists if peripherals_dict is provided
        if peripherals_dict is not None and gpio_name not in peripherals_dict:
            raise ValueError(f" button device {name} references undefined peripheral '{gpio_name}'")

        # Get active level
        active_level = int(device_config.get('active_level', 0))
        if active_level not in [0, 1]:
            raise ValueError(f"YAML validation error in  button device: Invalid active_level '{active_level}'. Valid values: [0, 1]")

        # Get power save configuration
        enable_power_save = bool(device_config.get('enable_power_save', False))

        # Get pull configuration
        disable_pull = bool(device_config.get('disable_pull', False))

        # Build GPIO button configuration
        gpio_cfg = {
            'gpio_name': gpio_name,
            'active_level': active_level,
            'enable_power_save': enable_power_save,
            'disable_pull': disable_pull
        }
        button_config['sub_cfg']['gpio'] = gpio_cfg

    # Parse ADC button configuration
    elif button_type == 'adc_multi' or button_type == 'adc_single':
        # Get ADC peripheral name from device config or peripherals list
        adc_name = device_config.get('adc_name')

        # If adc_name is not provided in config, try to extract from peripherals
        if not adc_name and peripherals:
            for periph in peripherals:
                if isinstance(periph, dict):
                    periph_name = periph.get('name', '')
                    if periph_name.startswith('adc'):
                        adc_name = periph_name
                        break
                elif isinstance(periph, str) and periph.startswith('adc'):
                    adc_name = periph
                    break

        if adc_name is None:
            raise ValueError(f"YAML validation error in  button device: Missing 'adc_name' field for ADC button")

        # Validate ADC peripheral exists if peripherals_dict is provided
        if peripherals_dict is not None and adc_name not in peripherals_dict:
            raise ValueError(f" button device {name} references undefined peripheral '{adc_name}'")

        # Determine configuration based on sub_type
        if button_type == 'adc_multi':
            # Multi-button configuration
            button_num = device_config.get('button_num')
            voltage_range = device_config.get('voltage_range')
            button_labels = device_config.get('button_labels')

            if button_num is None:
                raise ValueError(f'YAML validation error in  button device: Missing button_num field for ADC multi-button configuration')
            if voltage_range is None:
                raise ValueError(f'YAML validation error in  button device: Missing voltage_range field for ADC multi-button configuration')

            button_num = int(button_num)
            if button_num <= 1:
                raise ValueError(f'YAML validation error in  button device: button_num must be > 1 for multi-button configuration')
            if not isinstance(voltage_range, list):
                raise ValueError(f'YAML validation error in  button device: voltage_range must be a list')
            if len(voltage_range) != button_num:
                raise ValueError(f'YAML validation error in  button device: voltage_range length ({len(voltage_range)}) must match button_num ({button_num})')

            max_voltage = device_config.get('max_voltage', 3000)
            voltage_range = [int(v) for v in voltage_range]
            # Validate button_labels if provided
            if button_labels is not None:
                if not isinstance(button_labels, list):
                    raise ValueError(f'YAML validation error in  button device: button_labels must be a list')
                if len(button_labels) != button_num:
                    raise ValueError(f'YAML validation error in  button device: button_labels length ({len(button_labels)}) must match button_num ({button_num})')
                # Keep as list of strings
                button_labels = [f'"{label}"' for label in button_labels]
            else:
                button_labels = []

            # Build ADC button configuration with multi union
            adc_cfg = {
                'adc_name': adc_name,
                'multi': {
                    'button_num': button_num,
                    'voltage_range': voltage_range,
                    'button_labels': button_labels,
                    'max_voltage': max_voltage
                }
            }
        else:  # button_type == 'adc_single'
            # Single-button configuration
            button_index = int(device_config.get('button_index', 0))
            min_voltage = int(device_config.get('min_voltage', 0))
            max_voltage = int(device_config.get('max_voltage', 500))
            if min_voltage >= max_voltage:
                raise ValueError(f'YAML validation error in  button device: min_voltage ({min_voltage}) must be less than max_voltage ({max_voltage})')

            # Build ADC button configuration with single union
            adc_cfg = {
                'adc_name': adc_name,
                'single': {
                    'button_index': button_index,
                    'min_voltage': min_voltage,
                    'max_voltage': max_voltage
                }
            }
        button_config['sub_cfg']['adc'] = adc_cfg

    # Create the result structure matching ESP Board Manager format
    result = {
        'struct_type': 'dev_button_config_t',
        'struct_var': f'{c_name}_cfg',
        'struct_init': button_config,
        'extra_configs': extra_configs
    }

    return result
