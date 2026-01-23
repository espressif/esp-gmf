# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# LEDC peripheral config parser
VERSION = 'v1.0.0'

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'generators'))

def get_includes() -> list:
    """Return list of required include headers for LEDC peripheral"""
    return [
        'periph_ledc.h',
    ]

def parse(name: str, config: dict) -> dict:
    """Parse LEDC peripheral configuration from YAML to C structure

    Args:
        name: Peripheral name
        config: Configuration dictionary from YAML

    Returns:
        Dictionary containing LEDC configuration structure
    """
    try:
        # Get the config dictionary
        cfg = config.get('config', {})
        if not cfg:
            # If config is not found, try to use the entire config dict
            cfg = config

        # Get GPIO pin number with validation
        gpio = cfg.get('gpio_num', -1)
        if gpio < 0:
            raise ValueError(f'Invalid GPIO pin number: {gpio}. Must be >= 0.')

        # Get LEDC channel with validation
        channel = cfg.get('channel', 'LEDC_CHANNEL_0')

        # Handle both numeric and enum string values
        if isinstance(channel, int):
            if 0 <= channel <= 7:
                channel_enum = f'LEDC_CHANNEL_{channel}'
            else:
                raise ValueError(f'Invalid LEDC channel number: {channel}. Must be 0-7.')
        elif isinstance(channel, str):
            valid_channels = [
                'LEDC_CHANNEL_0', 'LEDC_CHANNEL_1', 'LEDC_CHANNEL_2', 'LEDC_CHANNEL_3',
                'LEDC_CHANNEL_4', 'LEDC_CHANNEL_5', 'LEDC_CHANNEL_6', 'LEDC_CHANNEL_7'
            ]
            if channel not in valid_channels:
                raise ValueError(f'Invalid LEDC channel: {channel}. Valid channels: {valid_channels}')
            channel_enum = channel
        else:
            raise ValueError(f'Invalid LEDC channel type: {type(channel)}. Must be int or str.')

        # Get LEDC timer_sel with validation
        timer_sel = cfg.get('timer_sel', 'LEDC_TIMER_0')

        # Handle both numeric and enum string values
        if isinstance(timer_sel, int):
            if 0 <= timer_sel <= 3:
                timer_enum = f'LEDC_TIMER_{timer_sel}'
            else:
                raise ValueError(f'Invalid LEDC timer number: {timer_sel}. Must be 0-3.')
        elif isinstance(timer_sel, str):
            valid_timers = ['LEDC_TIMER_0', 'LEDC_TIMER_1', 'LEDC_TIMER_2', 'LEDC_TIMER_3']
            if timer_sel not in valid_timers:
                raise ValueError(f'Invalid LEDC timer: {timer_sel}. Valid timers: {valid_timers}')
            timer_enum = timer_sel
        else:
            raise ValueError(f'Invalid LEDC timer type: {type(timer_sel)}. Must be int or str.')

        # Get frequency with validation
        freq_hz = cfg.get('freq_hz', 4000)
        if freq_hz <= 0:
            raise ValueError(f'Invalid frequency: {freq_hz}. Must be > 0.')

        # Get duty cycle with validation
        duty = cfg.get('duty', 0)
        if duty < 0:
            raise ValueError(f'Invalid duty cycle: {duty}. Must be >= 0.')

        # Get duty resolution with validation
        duty_resolution = cfg.get('duty_resolution', 'LEDC_TIMER_13_BIT')

        # Handle both numeric and enum string values
        if isinstance(duty_resolution, int):
            if 1 <= duty_resolution <= 20:
                duty_resolution_enum = f'LEDC_TIMER_{duty_resolution}_BIT'
            else:
                raise ValueError(f'Invalid duty resolution number: {duty_resolution}. Must be 1-20.')
        elif isinstance(duty_resolution, str):
            valid_duty_resolutions = [
                'LEDC_TIMER_1_BIT', 'LEDC_TIMER_2_BIT', 'LEDC_TIMER_3_BIT', 'LEDC_TIMER_4_BIT',
                'LEDC_TIMER_5_BIT', 'LEDC_TIMER_6_BIT', 'LEDC_TIMER_7_BIT', 'LEDC_TIMER_8_BIT',
                'LEDC_TIMER_9_BIT', 'LEDC_TIMER_10_BIT', 'LEDC_TIMER_11_BIT', 'LEDC_TIMER_12_BIT',
                'LEDC_TIMER_13_BIT', 'LEDC_TIMER_14_BIT', 'LEDC_TIMER_15_BIT', 'LEDC_TIMER_16_BIT',
                'LEDC_TIMER_17_BIT', 'LEDC_TIMER_18_BIT', 'LEDC_TIMER_19_BIT', 'LEDC_TIMER_20_BIT'
            ]
            if duty_resolution not in valid_duty_resolutions:
                raise ValueError(f'Invalid duty resolution: {duty_resolution}. Valid resolutions: {valid_duty_resolutions}')
            duty_resolution_enum = duty_resolution
        else:
            raise ValueError(f'Invalid duty resolution type: {type(duty_resolution)}. Must be int or str.')

        # Get speed mode with validation
        speed_mode = cfg.get('speed_mode', 'LEDC_LOW_SPEED_MODE')
        valid_speed_modes = ['LEDC_LOW_SPEED_MODE', 'LEDC_HIGH_SPEED_MODE']
        if speed_mode not in valid_speed_modes:
            raise ValueError(f'Invalid speed mode: {speed_mode}. Valid modes: {valid_speed_modes}')

        # Get output inversion with validation
        output_invert = cfg.get('output_invert', False)
        if not isinstance(output_invert, bool):
            raise ValueError(f'Invalid output_invert value: {output_invert}. Must be boolean.')

        # Get sleep mode with validation
        sleep_mode = cfg.get('sleep_mode', 'LEDC_SLEEP_MODE_NO_ALIVE_NO_PD')
        valid_sleep_modes = [
            'LEDC_SLEEP_MODE_NO_ALIVE_NO_PD',
            'LEDC_SLEEP_MODE_NO_ALIVE_ALLOW_PD',
            'LEDC_SLEEP_MODE_KEEP_ALIVE'
        ]
        if sleep_mode not in valid_sleep_modes:
            raise ValueError(f'Invalid sleep_mode: {sleep_mode}. Valid modes: {valid_sleep_modes}')

        # Create configuration structure
        result = {
            'struct_type': 'periph_ledc_config_t',
            'struct_var': f'{name}_cfg',
            'struct_init': {
                'handle': {
                    'channel': channel_enum,
                    'speed_mode': speed_mode
                },
                'gpio_num': gpio,
                'duty': duty,
                'freq_hz': freq_hz,
                'duty_resolution': duty_resolution_enum,
                'timer_sel': timer_enum,
                'sleep_mode': sleep_mode,
                'output_invert': output_invert
            }
        }

        # Use logger for debug output
        from generators.utils.logger import get_logger
        logger = get_logger('periph_ledc')
        logger.debug(f'LEDC {name} channel={channel_enum}, gpio={gpio}, freq={freq_hz}Hz, duty={duty}, duty_resolution={duty_resolution_enum}')
        return result

    except ValueError as e:
        # Re-raise ValueError with more context
        raise ValueError(f"YAML validation error in LEDC peripheral '{name}': {e}")
    except Exception as e:
        # Catch any other exceptions and provide context
        raise ValueError(f"Error parsing LEDC peripheral '{name}': {e}")
