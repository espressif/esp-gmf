# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# DSI peripheral config parser
VERSION = 'v1.0.0'

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'generators'))

def get_includes() -> list:
    """Return list of required include headers for MIPI_DSI peripheral"""
    return [
        'esp_lcd_mipi_dsi.h',
    ]

def parse(name: str, config: dict) -> dict:
    """Parse MIPI_DSI peripheral configuration from YAML to C structure

    Args:
        name: Peripheral name
        config: Configuration dictionary from YAML

    Returns:
        Dictionary containing DSI configuration structure for esp_lcd_dsi_bus_config_t
    """
    try:
        # Get the config dictionary
        bus_cfg = config.get('config', {})

        # Get bus_id with validation
        bus_id = bus_cfg.get('bus_id', 0)
        if not isinstance(bus_id, int) or bus_id < 0:
            raise ValueError(f'Invalid bus_id: {bus_id}. Must be an integer >= 0.')

        # Get num_data_lanes
        num_data_lanes = bus_cfg.get('data_lanes', 2)
        if not isinstance(num_data_lanes, int) or num_data_lanes < 0:
            raise ValueError(f'Invalid data_lanes: {num_data_lanes}. Must be an integer >= 0.')

        # Get phy_clk_src with validation
        phy_clk_src = bus_cfg.get('phy_clk_src', 0)
        valid_phy_clk_srcs = [
            0,  # Set it to 0 and then let the driver configure it
            'MIPI_DSI_PHY_CLK_SRC_DEFAULT',
            'MIPI_DSI_PHY_CLK_SRC_PLL_F20M',
            'MIPI_DSI_PHY_CLK_SRC_PLL_F25M',
            'MIPI_DSI_PHY_CLK_SRC_RC_FAST'
        ]
        if phy_clk_src not in valid_phy_clk_srcs:
            raise ValueError(f'Invalid phy_clk_src: {phy_clk_src}. Valid values: {valid_phy_clk_srcs}')

        # Get lane_bit_rate_mbps
        lane_bit_rate_mbps = bus_cfg.get('lane_bit_rate_mbps', 0)
        if not isinstance(lane_bit_rate_mbps, int) or lane_bit_rate_mbps <= 0:
            raise ValueError(f'Invalid lane_bit_rate_mbps: {lane_bit_rate_mbps}. Must be an integer > 0.')

        # Create base config
        result = {
            'struct_type': 'esp_lcd_dsi_bus_config_t',
            'struct_var': f'{name}_cfg',
            'struct_init': {
                'bus_id': bus_id,
                'num_data_lanes': num_data_lanes,
                'phy_clk_src': phy_clk_src,
                'lane_bit_rate_mbps': lane_bit_rate_mbps
            }
        }
        return result

    except ValueError as e:
        # Re-raise ValueError with more context
        raise ValueError(f"YAML validation error in DSI peripheral '{name}': {e}")
    except Exception as e:
        # Catch any other exceptions and provide context
        raise ValueError(f"Error parsing DSI peripheral '{name}': {e}")
