# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

VERSION = 'v1.0.0'

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

DEV_LCD_TOUCH_I2C_MAX_ADDR_COUNT = 4

DEV_LCD_TOUCH_IO_LIST = {
    'i2c': [
        'rst_gpio_num',
        'int_gpio_num',
    ],
}


def get_includes() -> list:
    return [
        'dev_lcd_touch.h',
    ]


def _iter_peripheral_entries(peripherals_list):
    if not peripherals_list:
        return
    for periph in peripherals_list:
        if isinstance(periph, dict):
            yield periph


def _find_i2c_peripheral(device_name: str, full_config: dict, peripherals_dict=None) -> dict:
    matched = []
    for periph in _iter_peripheral_entries(full_config.get('peripherals')):
        periph_name = periph.get('name', '')
        if not periph_name and 'i2c_addr' in periph:
            raise ValueError(f'LCD touch device {device_name} I2C peripheral requires name')
        if periph_name.startswith('i2c'):
            if peripherals_dict is not None and periph_name not in peripherals_dict:
                raise ValueError(f"LCD touch device {device_name} references undefined peripheral '{periph_name}'")
            matched.append(periph)
    if len(matched) == 1:
        return matched[0]
    if len(matched) > 1:
        raise ValueError(
            f'LCD touch device {device_name} references multiple I2C peripherals; keep exactly one I2C entry in peripherals'
        )
    raise ValueError(f'LCD touch device {device_name} requires one I2C peripheral entry in peripherals')


def _parse_addr_value(addr) -> int:
    if isinstance(addr, str):
        return int(addr, 0)
    return int(addr)


def _parse_i2c_addr_list(device_name: str, i2c_periph: dict) -> list:
    if 'i2c_addr' not in i2c_periph:
        raise ValueError(f'LCD touch device {device_name} I2C peripheral requires i2c_addr')
    raw_addrs = i2c_periph['i2c_addr']
    if not isinstance(raw_addrs, list):
        raw_addrs = [raw_addrs]
    addrs = []
    for addr in raw_addrs:
        value = _parse_addr_value(addr)
        if value > 0:
            addrs.append(value)
    if not addrs:
        raise ValueError(f'LCD touch device {device_name} requires at least one i2c_addr')
    if len(addrs) > DEV_LCD_TOUCH_I2C_MAX_ADDR_COUNT:
        raise ValueError(
            f'LCD touch device {device_name} supports at most {DEV_LCD_TOUCH_I2C_MAX_ADDR_COUNT} I2C addresses'
        )
    for addr in addrs:
        if addr > 0xfe or (addr & 0x1):
            raise ValueError(
                f'LCD touch device {device_name} i2c_addr 0x{addr:02x} must be an 8-bit left-shifted address'
            )
    while len(addrs) < DEV_LCD_TOUCH_I2C_MAX_ADDR_COUNT:
        addrs.append(0)
    return addrs


def _parse_i2c_sub_config(name: str, full_config: dict, peripherals_dict=None) -> dict:
    sub_config = full_config.get('config', {})
    io_i2c_config = sub_config.get('io_i2c_config') or {}
    i2c_periph = _find_i2c_peripheral(name, full_config, peripherals_dict)
    i2c_name = i2c_periph.get('name')
    if not i2c_name:
        raise ValueError(f'LCD touch device {name} I2C peripheral requires name')
    if peripherals_dict is not None and i2c_name not in peripherals_dict:
        raise ValueError(f"LCD touch device {name} references undefined peripheral '{i2c_name}'")

    addrs = _parse_i2c_addr_list(name, i2c_periph)
    flags = io_i2c_config.get('flags', {})
    return {
        'i2c_name': i2c_name,
        'i2c_addr_count': len([addr for addr in addrs if addr > 0]),
        'i2c_addr': [f'0x{addr:02x}' for addr in addrs],
        'io_i2c_config': {
            'dev_addr': 0,
            'control_phase_bytes': int(io_i2c_config.get('control_phase_bytes', 1)),
            'dc_bit_offset': int(io_i2c_config.get('dc_bit_offset', 0)),
            'lcd_cmd_bits': int(io_i2c_config.get('lcd_cmd_bits', 8)),
            'lcd_param_bits': int(io_i2c_config.get('lcd_param_bits', 0)),
            'scl_speed_hz': int(io_i2c_config.get('scl_speed_hz', 100000)),
            'flags': {
                'dc_low_on_data': bool(flags.get('dc_low_on_data', False)),
                'disable_control_phase': bool(flags.get('disable_control_phase', True)),
            },
        },
    }


def _parse_touch_config(full_config: dict) -> dict:
    touch_config = full_config.get('config', {}).get('touch_config') or {}
    levels = touch_config.get('levels') or {}
    flags = touch_config.get('flags') or {}
    return {
        'x_max': int(touch_config.get('x_max', 320)),
        'y_max': int(touch_config.get('y_max', 240)),
        'rst_gpio_num': int(touch_config.get('rst_gpio_num', -1)),
        'int_gpio_num': int(touch_config.get('int_gpio_num', -1)),
        'levels': {
            'reset': int(levels.get('reset', 0)),
            'interrupt': int(levels.get('interrupt', 0)),
        },
        'flags': {
            'swap_xy': bool(flags.get('swap_xy', False)),
            'mirror_x': bool(flags.get('mirror_x', False)),
            'mirror_y': bool(flags.get('mirror_y', False)),
        },
        'process_coordinates': None,
        'interrupt_callback': None,
        'user_data': None,
        'driver_data': None,
    }


def parse(name: str, config: dict, peripherals_dict=None) -> dict:
    c_name = name.replace('-', '_')
    sub_type = config.get('sub_type')
    if not sub_type:
        raise ValueError(f"LCD touch device '{name}' is missing required 'sub_type' field")
    if sub_type == 'spi':
        raise ValueError(f"LCD touch device '{name}' sub_type 'spi' is reserved but not implemented yet")
    if sub_type != 'i2c':
        raise ValueError(f"LCD touch device '{name}' has invalid sub_type '{sub_type}'. Must be 'i2c'")

    return {
        'struct_type': 'dev_lcd_touch_config_t',
        'struct_var': f'{c_name}_cfg',
        'struct_init': {
            'name': c_name,
            'chip': config.get('chip', 'unknown'),
            'type': str(config.get('type', 'lcd_touch')),
            'sub_type': sub_type,
            'touch_config': _parse_touch_config(config),
            'sub_cfg': {
                'i2c': _parse_i2c_sub_config(name, config, peripherals_dict),
            },
        },
    }
