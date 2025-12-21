# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# MCPWM peripheral config parser
VERSION = 'v1.1.0'

import sys

import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'generators'))

def get_includes() -> list:
    """Return list of required include headers for MCPWM peripheral"""
    return [
        'periph_mcpwm.h',
    ]

def parse(name: str, config: dict) -> dict:
    """Parse MCPWM peripheral configuration from YAML to C structure

    Args:
        name: Peripheral name
        config: Configuration dictionary from YAML

    Returns:
        Dictionary containing MCPWM configuration structure
    """
    try:
        # Get the config dictionary
        cfg = config.get('config', {})

        # Timer configuration
        timer_config = cfg.get('timer_config', {})
        group_id = timer_config.get('group_id', 0)
        clk_src = timer_config.get('clk_src', 'MCPWM_TIMER_CLK_SRC_DEFAULT')
        resolution_hz = timer_config.get('resolution_hz', 1000000)
        period_ticks = timer_config.get('period_ticks', 20000)
        count_mode = timer_config.get('count_mode', 'MCPWM_TIMER_COUNT_MODE_UP')
        intr_priority = timer_config.get('intr_priority', 0)

        # Timer flags
        timer_flags = timer_config.get('flags', {})
        update_period_on_empty = timer_flags.get('update_period_on_empty', False)
        update_period_on_sync = timer_flags.get('update_period_on_sync', False)
        allow_pd = timer_flags.get('allow_pd', False)

        # Validate timer parameters
        if resolution_hz <= 0:
            raise ValueError(f'Invalid resolution: {resolution_hz}. Must be > 0.')
        if period_ticks <= 0:
            raise ValueError(f'Invalid period: {period_ticks}. Must be > 0.')

        # Operator configuration
        operator_config = cfg.get('operator_config', {})
        operator_group_id = operator_config.get('group_id', 0)
        operator_intr_priority = operator_config.get('intr_priority', 0)
        if operator_group_id != group_id:
            raise ValueError(f'Operator and timer should reside in the same group.')

        # Operator flags
        operator_flags = operator_config.get('flags', {})
        update_gen_action_on_tez = operator_flags.get('update_gen_action_on_tez', False)
        update_gen_action_on_tep = operator_flags.get('update_gen_action_on_tep', False)
        update_gen_action_on_sync = operator_flags.get('update_gen_action_on_sync', False)
        update_dead_time_on_tez = operator_flags.get('update_dead_time_on_tez', False)
        update_dead_time_on_tep = operator_flags.get('update_dead_time_on_tep', False)
        update_dead_time_on_sync = operator_flags.get('update_dead_time_on_sync', False)

        # Comparator configuration - support multiple comparators
        comparator_configs = cfg.get('comparator_configs', [])
        if not comparator_configs:
            # Backward compatibility: support single comparator config
            single_comparator_config = cfg.get('comparator_config', {})
            if single_comparator_config:
                comparator_configs = [single_comparator_config]
            else:
                # Default to one comparator if none specified
                comparator_configs = [{}]

        # Handle case where comparator_configs is a single dict (incorrect format)
        if isinstance(comparator_configs, dict):
            # Try to extract multiple comparators from the dict
            comparator_configs = []
            # Look for keys that might indicate multiple comparators
            for key in list(cfg.get('comparator_configs', {}).keys()):
                if key.isdigit():  # If key is a number, treat as comparator index
                    comparator_configs.append(cfg['comparator_configs'][key])

            # If no numbered keys found, treat as single comparator
            if not comparator_configs:
                comparator_configs = [cfg.get('comparator_configs', {})]

        # Parse each comparator configuration
        comparator_configs_parsed = []
        for i, comp_config in enumerate(comparator_configs):
            comparator_intr_priority = comp_config.get('intr_priority', 0)
            # Comparator flags
            comparator_flags = comp_config.get('flags', {})
            update_cmp_on_tez = comparator_flags.get('update_cmp_on_tez', True)
            update_cmp_on_tep = comparator_flags.get('update_cmp_on_tep', False)
            update_cmp_on_sync = comparator_flags.get('update_cmp_on_sync', False)

            comparator_configs_parsed.append({
                'intr_priority': comparator_intr_priority,
                'flags': {
                    'update_cmp_on_tez': 1 if update_cmp_on_tez else 0,
                    'update_cmp_on_tep': 1 if update_cmp_on_tep else 0,
                    'update_cmp_on_sync': 1 if update_cmp_on_sync else 0
                }
            })

        # Generator configuration
        generator_config = cfg.get('generator_config', {})
        gpio_num = generator_config.get('gpio_num', -1)
        if gpio_num < 0:
            raise ValueError(f'Invalid GPIO pin number: {gpio_num}. Must be >= 0.')

        # Generator flags
        generator_flags = generator_config.get('flags', {})
        invert_pwm = generator_flags.get('invert_pwm', False)
        io_loop_back = generator_flags.get('io_loop_back', False)
        io_od_mode = generator_flags.get('io_od_mode', False)
        pull_up = generator_flags.get('pull_up', False)
        pull_down = generator_flags.get('pull_down', False)

        # Create configuration structure
        result = {
            'struct_type': 'periph_mcpwm_config_t',
            'struct_var': f'{name}_cfg',
            'struct_init': {
                'timer_config': {
                    'group_id': group_id,
                    'clk_src': clk_src,
                    'resolution_hz': resolution_hz,
                    'period_ticks': period_ticks,
                    'count_mode': count_mode,
                    'intr_priority': intr_priority,
                    'flags': {
                        'update_period_on_empty': 1 if update_period_on_empty else 0,
                        'update_period_on_sync': 1 if update_period_on_sync else 0,
                        'allow_pd': 1 if allow_pd else 0
                    }
                },
                'operator_config': {
                    'group_id': operator_group_id,
                    'intr_priority': operator_intr_priority,
                    'flags': {
                        'update_gen_action_on_tez': 1 if update_gen_action_on_tez else 0,
                        'update_gen_action_on_tep': 1 if update_gen_action_on_tep else 0,
                        'update_gen_action_on_sync': 1 if update_gen_action_on_sync else 0,
                        'update_dead_time_on_tez': 1 if update_dead_time_on_tez else 0,
                        'update_dead_time_on_tep': 1 if update_dead_time_on_tep else 0,
                        'update_dead_time_on_sync': 1 if update_dead_time_on_sync else 0
                    }
                },
                'num_comparators': len(comparator_configs_parsed),
                'comparator_cfgs': comparator_configs_parsed,
                'generator_config': {
                    'gen_gpio_num': gpio_num,
                    'flags': {
                        'invert_pwm': 1 if invert_pwm else 0,
                        'io_loop_back': 1 if io_loop_back else 0,
                        'io_od_mode': 1 if io_od_mode else 0,
                        'pull_up': 1 if pull_up else 0,
                        'pull_down': 1 if pull_down else 0
                    }
                },
            }
        }

        return result

    except ValueError as e:
        # Re-raise ValueError with more context
        raise ValueError(f"YAML validation error in MCPWM peripheral '{name}': {e}")
    except Exception as e:
        # Catch any other exceptions and provide context
        raise ValueError(f"Error parsing MCPWM peripheral '{name}': {e}")
