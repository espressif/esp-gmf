# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# SPI peripheral config parser
VERSION = 'v1.0.0'

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'generators'))

def get_includes() -> list:
    """Return list of required include headers for SPI peripheral"""
    return [
        'periph_spi.h',
    ]

def parse(name: str, config: dict) -> dict:
    """Parse SPI peripheral configuration from YAML to C structure

    Args:
        name: Peripheral name
        config: Configuration dictionary from YAML

    Returns:
        Dictionary containing SPI configuration structure
    """
    try:
        # Get the config dictionary
        cfg = config.get('config', {})
        if not cfg:
            # If config is not found, try to use the entire config dict
            cfg = config

        # Get SPI port with validation
        # First try to get from spi_bus_config, then from config root
        spi_bus_config = cfg.get('spi_bus_config', {})
        spi_port = spi_bus_config.get('spi_port', cfg.get('spi_port', 0))

        # Handle both numeric and enum string values
        if isinstance(spi_port, int):
            if 0 <= spi_port <= 2:
                spi_port_enum = f'SPI{spi_port + 1}_HOST'
            else:
                raise ValueError(f'Invalid SPI port number: {spi_port}. Must be 0-2.')
        elif isinstance(spi_port, str):
            valid_ports = ['SPI1_HOST', 'SPI2_HOST', 'SPI3_HOST']
            if spi_port not in valid_ports:
                raise ValueError(f'Invalid SPI port: {spi_port}. Valid ports: {valid_ports}')
            spi_port_enum = spi_port
        else:
            raise ValueError(f'Invalid SPI port type: {type(spi_port)}. Must be int or str.')

        # spi_bus_config already obtained above

        # Parse spi_bus_config_t fields with union support
        # Handle union fields: data0_io_num (union with mosi_io_num)
        data0_io_num = spi_bus_config.get('data0_io_num', -1)
        mosi_io_num = spi_bus_config.get('mosi_io_num', -1)
        if data0_io_num != -1 and mosi_io_num != -1:
            raise ValueError('Cannot set both data0_io_num and mosi_io_num (they are union fields)')
        if data0_io_num != -1:
            mosi_io_num = data0_io_num
        if not isinstance(mosi_io_num, int):
            raise ValueError(f'Invalid mosi_io_num: {mosi_io_num}. Must be int.')

        # Handle union fields: data1_io_num (union with miso_io_num)
        data1_io_num = spi_bus_config.get('data1_io_num', -1)
        miso_io_num = spi_bus_config.get('miso_io_num', -1)
        if data1_io_num != -1 and miso_io_num != -1:
            raise ValueError('Cannot set both data1_io_num and miso_io_num (they are union fields)')
        if data1_io_num != -1:
            miso_io_num = data1_io_num
        if not isinstance(miso_io_num, int):
            raise ValueError(f'Invalid miso_io_num: {miso_io_num}. Must be int.')

        # Handle union fields: data2_io_num (union with quadwp_io_num)
        data2_io_num = spi_bus_config.get('data2_io_num', -1)
        quadwp_io_num = spi_bus_config.get('quadwp_io_num', -1)
        if data2_io_num != -1 and quadwp_io_num != -1:
            raise ValueError('Cannot set both data2_io_num and quadwp_io_num (they are union fields)')
        if data2_io_num != -1:
            quadwp_io_num = data2_io_num
        if not isinstance(quadwp_io_num, int):
            raise ValueError(f'Invalid quadwp_io_num: {quadwp_io_num}. Must be int.')

        # Handle union fields: data3_io_num (union with quadhd_io_num)
        data3_io_num = spi_bus_config.get('data3_io_num', -1)
        quadhd_io_num = spi_bus_config.get('quadhd_io_num', -1)
        if data3_io_num != -1 and quadhd_io_num != -1:
            raise ValueError('Cannot set both data3_io_num and quadhd_io_num (they are union fields)')
        if data3_io_num != -1:
            quadhd_io_num = data3_io_num
        if not isinstance(quadhd_io_num, int):
            raise ValueError(f'Invalid quadhd_io_num: {quadhd_io_num}. Must be int.')

        # Standard SPI pins
        sclk_io_num = spi_bus_config.get('sclk_io_num', -1)
        if not isinstance(sclk_io_num, int):
            raise ValueError(f'Invalid sclk_io_num: {sclk_io_num}. Must be int.')

        # Octal mode pins (optional)
        data4_io_num = spi_bus_config.get('data4_io_num', -1)
        if not isinstance(data4_io_num, int):
            raise ValueError(f'Invalid data4_io_num: {data4_io_num}. Must be int.')

        data5_io_num = spi_bus_config.get('data5_io_num', -1)
        if not isinstance(data5_io_num, int):
            raise ValueError(f'Invalid data5_io_num: {data5_io_num}. Must be int.')

        data6_io_num = spi_bus_config.get('data6_io_num', -1)
        if not isinstance(data6_io_num, int):
            raise ValueError(f'Invalid data6_io_num: {data6_io_num}. Must be int.')

        data7_io_num = spi_bus_config.get('data7_io_num', -1)
        if not isinstance(data7_io_num, int):
            raise ValueError(f'Invalid data7_io_num: {data7_io_num}. Must be int.')

        # Configuration parameters
        data_io_default_level = spi_bus_config.get('data_io_default_level', False)
        if not isinstance(data_io_default_level, bool):
            raise ValueError(f'Invalid data_io_default_level: {data_io_default_level}. Must be boolean.')

        max_transfer_sz = spi_bus_config.get('max_transfer_sz', 4092)
        if not isinstance(max_transfer_sz, int) or max_transfer_sz <= 0:
            raise ValueError(f'Invalid max_transfer_sz: {max_transfer_sz}. Must be positive int.')

        # Parse flags
        flags = spi_bus_config.get('flags', 0)
        if not isinstance(flags, int):
            raise ValueError(f'Invalid flags: {flags}. Must be int.')

        # Parse ISR CPU affinity
        isr_cpu_id = spi_bus_config.get('isr_cpu_id', 'ESP_INTR_CPU_AFFINITY_AUTO')
        valid_isr_cpu_ids = ['ESP_INTR_CPU_AFFINITY_AUTO', 'ESP_INTR_CPU_AFFINITY_CPU0', 'ESP_INTR_CPU_AFFINITY_CPU1']
        if isr_cpu_id not in valid_isr_cpu_ids:
            raise ValueError(f'Invalid isr_cpu_id: {isr_cpu_id}. Valid values: {valid_isr_cpu_ids}')

        # Parse interrupt flags
        intr_flags = spi_bus_config.get('intr_flags', 0)
        if not isinstance(intr_flags, int):
            raise ValueError(f'Invalid intr_flags: {intr_flags}. Must be int.')

        # Debug output showing union field resolution
        union_info = []
        if data0_io_num != -1:
            union_info.append(f'data0/mosi={data0_io_num}')
        if data1_io_num != -1:
            union_info.append(f'data1/miso={data1_io_num}')
        if data2_io_num != -1:
            union_info.append(f'data2/quadwp={data2_io_num}')
        if data3_io_num != -1:
            union_info.append(f'data3/quadhd={data3_io_num}')

        union_str = ', '.join(union_info) if union_info else 'standard'


        # Create configuration structure
        result = {
            'struct_type': 'periph_spi_config_t',
            'struct_var': f'{name}_cfg',
            'struct_init': {
                'spi_port': spi_port_enum,
                'spi_bus_config': {
                    'mosi_io_num': mosi_io_num,
                    'miso_io_num': miso_io_num,
                    'sclk_io_num': sclk_io_num,
                    'quadwp_io_num': quadwp_io_num,
                    'quadhd_io_num': quadhd_io_num,
                    'data4_io_num': data4_io_num,
                    'data5_io_num': data5_io_num,
                    'data6_io_num': data6_io_num,
                    'data7_io_num': data7_io_num,
                    'data_io_default_level': data_io_default_level,
                    'max_transfer_sz': max_transfer_sz,
                    'flags': flags,
                    'isr_cpu_id': isr_cpu_id,
                    'intr_flags': intr_flags
                }
            }
        }

        return result

    except ValueError as e:
        # Re-raise ValueError with more context
        raise ValueError(f"YAML validation error in SPI peripheral '{name}': {e}")
    except Exception as e:
        # Catch any other exceptions and provide context
        raise ValueError(f"Error parsing SPI peripheral '{name}': {e}")
