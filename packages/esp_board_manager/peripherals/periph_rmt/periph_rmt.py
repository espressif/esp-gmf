# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# RMT peripheral config parser
VERSION = 'v1.0.0'

import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'generators'))

from generators.utils.logger import get_logger
logger = get_logger('periph_rmt')

# Map string roles to enum values
RMT_ROLE_MAP = {
    'tx': 'ESP_BOARD_PERIPH_ROLE_TX',
    'rx': 'ESP_BOARD_PERIPH_ROLE_RX'
}

def get_includes():
    return ['periph_rmt.h']


def parse(name: str, full_config: dict, peripherals_dict=None) -> dict:
    cfg = full_config.get('config', {})

    # Get role
    role = full_config.get('role', 'tx')
    role_l = role.lower()
    if role_l not in ('tx', 'rx'):
        logger.error("Invalid RMT mode '%s' for %s", role, name)
        raise ValueError(f"Invalid RMT mode '{role}'")

    # Convert string role to enum value
    enum_role = RMT_ROLE_MAP.get(role_l)
    if enum_role is None:
        raise ValueError(f"Invalid RMT role '{role}' for RMT device '{name}'")

    # Build channel configuration based on role
    if role_l == 'tx':
        # TX channel configuration - using rmt_tx_channel_config_t structure
        tx_cfg = cfg

        tx_config = {
            'gpio_num': int(tx_cfg.get('gpio_num', -1)),
            'clk_src': tx_cfg.get('clk_src', ''),
            'resolution_hz': int(tx_cfg.get('resolution_hz', 10000000)),
            'mem_block_symbols': int(tx_cfg.get('mem_block_symbols', 64)),
            'trans_queue_depth': int(tx_cfg.get('trans_queue_depth', 4)),
            'intr_priority': int(tx_cfg.get('intr_priority', 1)),
            'flags': {
                'invert_out': bool(tx_cfg.get('flags', {}).get('invert_out', False)),
                'with_dma': bool(tx_cfg.get('flags', {}).get('with_dma', False)),
                'io_loop_back': bool(tx_cfg.get('flags', {}).get('io_loop_back', False)),
                'io_od_mode': bool(tx_cfg.get('flags', {}).get('io_od_mode', False)),
                'allow_pd': bool(tx_cfg.get('flags', {}).get('allow_pd', False)),
                'init_level': int(tx_cfg.get('flags', {}).get('init_level', 0)),
            }
        }
        channel_config = {'tx': tx_config}
    else:
        # RX channel configuration - using rmt_rx_channel_config_t structure
        rx_cfg = cfg

        rx_config = {
            'gpio_num': int(rx_cfg.get('gpio_num', -1)),
            'clk_src': rx_cfg.get('clk_src', ''),
            'resolution_hz': int(rx_cfg.get('resolution_hz', 1000000)),
            'mem_block_symbols': int(rx_cfg.get('mem_block_symbols', 64)),
            'intr_priority': int(rx_cfg.get('intr_priority', 1)),
            'flags': {
                'invert_in': bool(rx_cfg.get('flags', {}).get('invert_in', False)),
                'with_dma': bool(rx_cfg.get('flags', {}).get('with_dma', False)),
                'io_loop_back': bool(rx_cfg.get('flags', {}).get('io_loop_back', False)),
                'allow_pd': bool(rx_cfg.get('flags', {}).get('allow_pd', False)),
            }
        }
        channel_config = {'rx': rx_config}

    struct_init = {
        'role': enum_role,
        'channel_config': channel_config,
    }

    return {
        'struct_type': 'periph_rmt_config_t',
        'struct_var': f'{name}_cfg',
        'struct_init': struct_init,
    }
