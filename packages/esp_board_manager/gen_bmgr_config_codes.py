#!/usr/bin/env python3
"""
# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.
"""

"""
Board Manager Configuration Generator

This script generates C configuration files for board peripherals and devices
based on YAML configuration files. This is the refactored version using the new
modular architecture.

Now also includes Kconfig generation functionality that can be optionally enabled.
"""

import os
import sys
import argparse
import logging
import yaml
from pathlib import Path
from typing import Dict, List, Optional

# Add current directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from generators.utils.logger import setup_logger, get_logger, LoggerMixin
from generators import get_config_generator, get_sdkconfig_manager, get_dependency_manager, get_source_scanner
from generators.parser_loader import load_parsers
from generators.peripheral_parser import PeripheralParser
from generators.device_parser import DeviceParser
from generators.name_validator import parse_component_name


class BoardConfigGenerator(LoggerMixin):
    """Main board configuration generator class"""

    def get_license_header(self, file_description: str) -> str:
        """Generate license header for generated C files"""
        return f'''/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * {file_description}
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

'''

    def __init__(self, script_dir: Path):
        super().__init__()
        self.script_dir = script_dir

        # Display version information when creating the generator
        version_info = self.get_version_info()
        print('📋 Version Information:')
        print(f'   • Component Version: {version_info["component_version"]}')
        print(f'   • Git Commit: {version_info["git_commit_id"]} ({version_info["git_commit_date"]})')
        print(f'   • Generation Time: {version_info["generation_time"]}')
        print('')

        # Determine the root directory for all GMF Board Manager resources
        # Priority: 1. IDF_EXTRA_ACTIONS_PATH, 2. script_dir
        idf_extra_actions_path = os.environ.get('IDF_EXTRA_ACTIONS_PATH')
        if idf_extra_actions_path:
            self.root_dir = Path(idf_extra_actions_path)
            self.logger.info(f'ℹ️  Using IDF_EXTRA_ACTIONS_PATH as root directory: {self.root_dir}')
        else:
            self.root_dir = script_dir
            self.logger.info(f'ℹ️  Using script directory as root directory: {self.root_dir}')

        # All paths are now relative to root_dir
        self.boards_dir = self.root_dir / 'boards'
        self.peripherals_dir = self.root_dir / 'peripherals'
        self.devices_dir = self.root_dir / 'devices'
        self.gen_codes_dir = self.root_dir / 'gen_codes'

        # Ensure gen_codes directory exists
        self.gen_codes_dir.mkdir(exist_ok=True)

        # Initialize components
        self.config_generator = get_config_generator()(script_dir)
        self.sdkconfig_manager = get_sdkconfig_manager()(script_dir)
        self.dependency_manager = get_dependency_manager()(script_dir)
        self.source_scanner = get_source_scanner()(script_dir)

        # Initialize parsers
        self.peripheral_parser = PeripheralParser(script_dir)
        self.device_parser = DeviceParser(script_dir)

    def get_component_name(self, file_path):
        """Extract component name from file path."""
        name = os.path.splitext(os.path.basename(file_path))[0]
        if name.startswith('dev_'):
            return name[4:]  # Remove 'dev_' prefix
        if name.startswith('periph_'):
            return name[7:]  # Remove 'periph_' prefix
        return name

    def generate_kconfig_entry(self, name, path, is_device, default_value='n'):
        """Generate a Kconfig entry for a component."""
        component_type = 'Device' if is_device else 'Peripheral'
        prefix = 'DEV' if is_device else 'PERIPH'
        macro_name = f'ESP_BOARD_{prefix}_{name.upper()}_SUPPORT'

        entry = f"""config {macro_name}
    bool "{component_type} '{name}' support"
    default {default_value}
    help
        Enable {name} {component_type.lower()} support.
        This option enables the {name} {component_type.lower()} driver.
        The driver is located at: {path}

"""
        return entry

    def generate_board_kconfig(self, all_boards, board_customer_path=None, selected_board=None):
        """Generate board selection Kconfig content"""
        # Use the already scanned boards, no need to scan again
        if not all_boards:
            self.logger.error('No valid board directories found!')
            self.logger.info('Each board directory must contain a Kconfig file')
            return None

        # Determine the default board
        if selected_board and selected_board in all_boards:
            default_board = selected_board
        else:
            # Fallback to first available board if selected_board is not valid
            default_board = sorted(all_boards.keys())[0]

        # Generate Kconfig content
        kconfig_content = """menu "Board Selection"

visible if n

help
    Board selection is autogenerated and don't be selected directly

"""
        # Add board options
        for board in sorted(all_boards.keys()):
            # Convert hyphens to underscores for macro names
            macro = 'BOARD_' + board.upper().replace('-', '_')

            # Add default y for the selected board
            default_line = '\n    default y' if board == default_board else ''

            kconfig_content += f"""config {macro}
    bool "{board}"{default_line}
    help
        Use {board} board configuration.

"""

        # Add default selection
        kconfig_content += 'config BOARD_NAME\n'
        kconfig_content += '    string\n'

        for board in sorted(all_boards.keys()):
            # Convert hyphens to underscores for macro names
            macro = 'BOARD_' + board.upper().replace('-', '_')
            kconfig_content += f'    default "{board}" if {macro}\n'

        kconfig_content += '\nendmenu\n\n'

        return kconfig_content

    def generate_components_kconfig(self, peripheral_types=None, device_types=None):
        """Generate peripheral and device Kconfig content"""

        kconfig_content = "menu \"Peripheral Support\"\n\n"

        # Process peripherals using the root directory
        periph_dir = self.peripherals_dir
        periph_names = []

        for file in sorted(periph_dir.glob('periph_*/periph_*.h')):
            name = self.get_component_name(file)
            # Set default to 'y' if this peripheral type is used in the current board
            default_value = 'y' if (peripheral_types and name in peripheral_types) else 'n'
            kconfig_content += self.generate_kconfig_entry(
                name,
                f'peripherals/periph_{name}',
                False,
                default_value
            )
            periph_names.append(name)

        kconfig_content += "endmenu\n\nmenu \"Device Support\"\n\n"

        # Process devices using the root directory
        device_dir = self.devices_dir
        device_names = []

        if device_dir.exists():
            # Look for device folders
            for device_folder in sorted(device_dir.iterdir()):
                if not device_folder.is_dir():
                    continue

                device_type = device_folder.name
                if not device_type.startswith('dev_'):
                    continue

                # Look for header file in the device folder
                header_file = device_folder / f'{device_type}.h'
                if header_file.exists():
                    name = device_type[4:]  # Remove 'dev_' prefix
                    # Set default to 'y' if this device type is used in the current board
                    default_value = 'y' if (device_types and name in device_types) else 'n'
                    kconfig_content += self.generate_kconfig_entry(
                        name,
                        f'devices/{device_type}',
                        True,
                        default_value
                    )
                    device_names.append(name)
        else:
            # Fallback to old structure for backward compatibility
            for file in sorted(device_dir.glob('dev_*.h')):
                name = self.get_component_name(file)
                # Set default to 'y' if this device type is used in the current board
                default_value = 'y' if (device_types and name in device_types) else 'n'
                kconfig_content += self.generate_kconfig_entry(
                    name,
                    f'devices/dev_{name}',
                    True,
                    default_value
                )
                device_names.append(name)

        kconfig_content += 'endmenu\n'

        self.logger.info(f'✅ Generated Kconfig for {len(periph_names)} peripherals: {periph_names}')
        self.logger.info(f'✅ Generated Kconfig for {len(device_names)} devices: {device_names}')
        return kconfig_content

    def find_project_root(self, start_dir):
        """Find project root by looking for CMakeLists.txt containing 'project()' keyword"""
        current_dir = start_dir
        while current_dir != '/' and current_dir != '':
            cmake_file = os.path.join(current_dir, 'CMakeLists.txt')
            if os.path.exists(cmake_file):
                try:
                    with open(cmake_file, 'r', encoding='utf-8') as f:
                        content = f.read()
                        if 'project(' in content and ')' in content:
                            # Check if it's a complete project() declaration
                            lines = content.split('\n')
                            for line in lines:
                                line = line.strip()
                                if line.startswith('project(') and line.endswith(')'):
                                    return current_dir
                except (IOError, UnicodeDecodeError) as e:
                    self.logger.debug(f'Failed to read {cmake_file}: {e}')
                    pass

            parent_dir = os.path.dirname(current_dir)
            if parent_dir == current_dir:  # Avoid infinite loop
                break
            current_dir = parent_dir

        return None

    def find_components_boards(self, project_root):
        """Search for 'boards' directories in all components"""
        component_boards = []
        components_dir = os.path.join(project_root, 'components')

        if not os.path.exists(components_dir):
            return component_boards

        for component in os.listdir(components_dir):
            component_path = os.path.join(components_dir, component)
            boards_path = os.path.join(component_path, 'boards')

            if os.path.isdir(boards_path):
                for board in os.listdir(boards_path):
                    board_path = os.path.join(boards_path, board)
                    kconfig_path = os.path.join(board_path, 'Kconfig')

                    if os.path.isdir(board_path) and os.path.isfile(kconfig_path):
                        component_boards.append(board)

        return component_boards

    def generate_kconfig(self, all_boards, board_customer_path=None, peripheral_types=None, device_types=None, selected_board=None):
        """Generate unified Kconfig content"""
        try:
            # Generate unified Kconfig content without the outer menu wrapper
            kconfig_content = ''

            # Add board selection
            board_kconfig = self.generate_board_kconfig(all_boards, board_customer_path, selected_board)
            if board_kconfig is None:
                self.logger.error('Failed to generate board Kconfig')
                return False
            kconfig_content += board_kconfig + '\n'

            # Add components configuration with actual types from board
            kconfig_content += self.generate_components_kconfig(peripheral_types, device_types)

            # Write unified Kconfig file using the root directory
            kconfig_path = self.gen_codes_dir / 'Kconfig.in'
            self.logger.info(f'✅ Writing Kconfig file to: {kconfig_path}')

            # Ensure gen_codes directory exists
            kconfig_path.parent.mkdir(parents=True, exist_ok=True)

            with open(kconfig_path, 'w') as f:
                f.write(kconfig_content)

            return True

        except Exception as e:
            self.logger.error(f'Unexpected error during Kconfig generation: {e}')
            import traceback
            traceback.print_exc()
            return False

    def write_periph_c(self, periph_structs, peripherals, periph_parsers, out_path: str):
        """Write peripheral configuration C file"""
        # Ensure output directory exists
        Path(out_path).parent.mkdir(parents=True, exist_ok=True)

        with open(out_path, 'w') as f:
            f.write(self.get_license_header('Auto-generated peripheral configuration file'))
            f.write('#include <stdlib.h>\n')
            f.write('#include "esp_board_periph.h"\n')

            # Collect and write all required headers from peripheral modules
            all_includes = set()
            for p in peripherals:
                parse_entry = periph_parsers.get(p.type)
                if not parse_entry:
                    continue
                version, parse_func, get_includes = parse_entry
                if get_includes:
                    all_includes.update(get_includes())

            # Write collected headers
            for include in sorted(all_includes):
                f.write(f'#include "{include}"\n')
            f.write('\n')

            # Write config structures
            f.write('// Peripheral configuration structures\n')
            for s, p in zip(periph_structs, peripherals):
                struct_var = 'esp_bmgr_' + p.name.replace('-', '_') + '_cfg'
                # Make SPI configurations non-const to allow runtime modification
                if p.type == 'spi':
                    f.write(f"static {s['struct_type']} {struct_var} = {{\n")
                else:
                    f.write(f"const static {s['struct_type']} {struct_var} = {{\n")
                f.writelines('    ' + l + '\n' for l in self.config_generator.dict_to_c_initializer(s['struct_init'], 4))
                f.write('};\n\n')

            # Write descriptor array
            f.write('// Peripheral descriptor array\n')
            f.write('const esp_board_periph_desc_t g_esp_board_peripherals[] = {\n')
            N = len(peripherals)
            for i, p in enumerate(peripherals):
                if i < N - 1:
                    next_str = f'&g_esp_board_peripherals[{i+1}]'
                else:
                    next_str = 'NULL'
                struct_var = 'esp_bmgr_' + p.name.replace('-', '_') + '_cfg'
                f.write('    {\n')
                f.write(f'        .next = {next_str},\n')
                f.write(f'        .name = "{p.name}",\n')
                f.write(f'        .type = "{p.type}",\n')
                if p.format is None:
                    f.write(f'        .format = NULL,\n')
                else:
                    f.write(f'        .format = "{p.format}",\n')
                f.write(f'        .role = "{p.role}",\n')
                f.write(f'        .cfg = &{struct_var},\n')
                f.write(f'        .cfg_size = sizeof({struct_var}),\n')
                f.write(f'        .id = 0,\n')
                f.write('    },\n')
            f.write('};\n')

    def write_device_custom_h(self, device_structs, devices, out_path: str):
        """Write custom device structure definitions to header file only if custom devices exist"""
        # Check if there are any custom devices with struct definitions
        custom_devices = []
        for s, d in zip(device_structs, devices):
            if 'struct_definition' in s:
                custom_devices.append((s, d))

        self.logger.debug(f'   Found {len(custom_devices)} custom devices with struct definitions')

        if not custom_devices:
            # No custom devices, skip file creation
            self.logger.debug(f'   No custom devices found, skipping custom header file creation')
            return

        # Ensure output directory exists
        Path(out_path).parent.mkdir(parents=True, exist_ok=True)

        with open(out_path, 'w') as f:
            f.write(self.get_license_header('Auto-generated custom device structure definitions'))
            f.write('#pragma once\n\n')
            f.write('#include <stdint.h>\n')
            f.write('#include <stdbool.h>\n')
            f.write('#include "dev_custom.h"\n\n')

            f.write('// Custom device structure definitions\n')
            f.write('// These structures are dynamically generated based on YAML configuration\n\n')

            for s, d in custom_devices:
                f.write(f'// Structure definition for {d.name}\n')
                f.write(s['struct_definition'])
                f.write('\n\n')

    def write_device_c(self, device_structs, devices, device_parsers, extra_configs, extra_includes, out_path: str):
        """Write device configuration C file with custom structures and extra configurations"""
        # Ensure output directory exists
        Path(out_path).parent.mkdir(parents=True, exist_ok=True)

        with open(out_path, 'w') as f:
            f.write(self.get_license_header('Auto-generated device configuration file'))
            f.write('#include <stdlib.h>\n')
            f.write('#include "esp_board_device.h"\n')

            # Collect and write all required headers from device modules
            all_includes = set()
            for d in devices:
                parse_entry = device_parsers.get(d.type)
                if not parse_entry:
                    continue
                version, parse_func, get_includes = parse_entry
                if get_includes:
                    all_includes.update(get_includes())

            # Write collected headers
            for include in sorted(all_includes):
                f.write(f'#include "{include}"\n')

            # Add extra headers from extra_dev configurations
            for include in sorted(extra_includes):
                f.write(f'#include "{include}"\n')

            # Check if there are custom devices and include custom header
            has_custom_devices = any('struct_definition' in s for s in device_structs)
            if has_custom_devices:
                f.write('#include "gen_board_device_custom.h"\n')

            f.write('\n')

            # Write extra device configurations (read-only parameters)
            if extra_configs:
                f.write('// Extra device configurations (read-only parameters)\n')
                for config_name, config_data in extra_configs.items():
                    if isinstance(config_data, str):
                        # If config_data is already a C code string, write it directly
                        f.write(f'// Configuration for {config_name}\n')
                        f.write(config_data)
                        f.write('\n\n')
                    else:
                        # If config_data is a dictionary, convert to C
                        f.write(f'// Configuration for {config_name}\n')
                        f.write(f'const static {config_name}_config_t esp_bmgr_{config_name}_config = {{\n')
                        f.writelines('    ' + l + '\n' for l in self.config_generator.dict_to_c_initializer(config_data, 4))
                        f.write('};\n\n')
                f.write('\n')

            # Write config structures
            f.write('// Device configuration structures\n')
            for s, d in zip(device_structs, devices):
                struct_var = 'esp_bmgr_' + d.name.replace('-', '_') + '_cfg'
                struct_init = s['struct_init'].copy()
                struct_init['name'] = d.name  # Force use YAML name
                f.write(f"const static {s['struct_type']} {struct_var} = {{\n")
                f.writelines('    ' + l + '\n' for l in self.config_generator.dict_to_c_initializer(struct_init, 4))
                f.write('};\n\n')

            # Write descriptor array
            f.write('// Device descriptor array\n')
            f.write('const esp_board_device_desc_t g_esp_board_devices[] = {\n')
            N = len(devices)
            for i, d in enumerate(devices):
                if i < N - 1:
                    next_str = f'&g_esp_board_devices[{i+1}]'
                else:
                    next_str = 'NULL'
                struct_var = 'esp_bmgr_' + d.name.replace('-', '_') + '_cfg'
                # Get init_skip value, default to false (do not skip initialization)
                init_skip = getattr(d, 'init_skip', False)
                f.write('    {\n')
                f.write(f'        .next = {next_str},\n')
                f.write(f'        .name = "{d.name}",\n')
                f.write(f'        .type = "{d.type}",\n')
                f.write(f'        .cfg = &{struct_var},\n')
                f.write(f'        .cfg_size = sizeof({struct_var}),\n')
                f.write(f'        .init_skip = {str(init_skip).lower()},\n')
                f.write('    },\n')
            f.write('};\n')

    def write_periph_handles(self, peripherals, periph_parsers, out_path: str):
        """Write peripheral handle array C file with init/deinit function pointers"""
        # Ensure output directory exists
        Path(out_path).parent.mkdir(parents=True, exist_ok=True)

        # Collect unique (type, role) combinations
        periph_types = set((p.type, p.role) for p in peripherals)

        with open(out_path, 'w') as f:
            f.write(self.get_license_header('Auto-generated peripheral handle definition file'))
            f.write('#include <stddef.h>\n')
            f.write('#include "esp_board_periph.h"\n')

            # Include peripheral-specific header files
            for type_name, _ in sorted(periph_types):
                f.write(f'#include "periph_{type_name}.h"\n')
            f.write('\n')

            # Write handle array
            f.write('// Peripheral handle array\n')
            f.write('esp_board_periph_entry_t g_esp_board_periph_handles[] = {\n')

            # Sort types for stable output
            sorted_types = sorted(periph_types)
            N = len(sorted_types)
            for i, (type_name, role_str) in enumerate(sorted_types):
                if i < N - 1:
                    next_str = f'&g_esp_board_periph_handles[{i+1}]'
                else:
                    next_str = 'NULL'
                f.write('    {\n')
                f.write(f'        .next = {next_str},\n')
                f.write(f'        .type = "{type_name}",\n')
                f.write(f'        .role = "{role_str}",\n')
                f.write(f'        .init = periph_{type_name}_init,\n')
                f.write(f'        .deinit = periph_{type_name}_deinit\n')
                f.write('    },\n')
            f.write('};\n')

    def write_device_handles(self, devices, device_parsers, out_path: str):
        """Write device handle array C file with init/deinit function pointers"""
        # Ensure output directory exists
        Path(out_path).parent.mkdir(parents=True, exist_ok=True)

        with open(out_path, 'w') as f:
            f.write(self.get_license_header('Auto-generated device handle definition file'))
            f.write('#include <stddef.h>\n')
            f.write('#include "esp_board_device.h"\n')

            # Include device-specific header files
            device_types = set(d.type for d in devices)
            for dev_type in sorted(device_types):
                f.write(f'#include "dev_{dev_type}.h"\n')
            f.write('\n')

            # Write handle array
            f.write('// Device handle array\n')
            f.write('esp_board_device_handle_t g_esp_board_device_handles[] = {\n')
            N = len(devices)
            for i, d in enumerate(devices):
                if i < N - 1:
                    next_str = f'&g_esp_board_device_handles[{i+1}]'
                else:
                    next_str = 'NULL'
                f.write('    {\n')
                f.write(f'        .next = {next_str},\n')
                f.write(f'        .name = "{d.name}",\n')
                f.write(f'        .type = "{d.type}",\n')
                f.write('        .device_handle = NULL,\n')
                f.write(f'        .init = dev_{d.type}_init,\n')
                f.write(f'        .deinit = dev_{d.type}_deinit\n')
                f.write('    },\n')
            f.write('};\n')

    def write_board_info(self, board_path: str, out_path: str):
        """Write board information C file from board_info.yaml or use default values"""
        board_info_path = os.path.join(board_path, 'board_info.yaml')

        if not os.path.exists(board_info_path):
            self.logger.warning(f'⚠️  board_info.yaml not found at {board_info_path}')
            # Use default values
            board_name = 'unknown'
            chip = 'unknown'
            version = '1.0.0'
            description = 'Board configuration'
            manufacturer = 'ESPRESSIF'
        else:
            # Read board info from board_info.yaml
            with open(board_info_path, 'r', encoding='utf-8') as f:
                board_yml = yaml.safe_load(f)

            # Extract board information
            board_name = board_yml.get('board', 'unknown')
            chip = board_yml.get('chip', 'unknown')
            version = board_yml.get('version', '1.0.0')
            description = board_yml.get('description', 'Board configuration')
            manufacturer = board_yml.get('manufacturer', 'ESPRESSIF')

        # Ensure output directory exists
        Path(out_path).parent.mkdir(parents=True, exist_ok=True)

        with open(out_path, 'w') as f:
            f.write(self.get_license_header('Auto-generated board information file'))
            f.write('#include "esp_board_manager.h"\n\n')
            f.write('// Board information\n')
            f.write('const esp_board_info_t g_esp_board_info = {\n')
            f.write(f'    .name = "{board_name}",\n')
            f.write(f'    .chip = "{chip}",\n')
            f.write(f'    .version = "{version}",\n')
            f.write(f'    .description = "{description}",\n')
            f.write(f'    .manufacturer = "{manufacturer}",\n')
            f.write('};\n')

    def process_peripherals(self, periph_yaml_path: str) -> tuple:
        """Process peripherals from YAML and generate C configuration files, returns peripherals dict, name map, and types"""
        self.logger.debug('   Parsing peripheral YAML file...')
        peripherals = self.peripheral_parser.parse_peripherals_yaml_legacy(periph_yaml_path)

        # Flatten the list of peripherals to handle nested lists
        self.logger.debug('   📋 Flattening peripheral configurations...')
        flattened_peripherals = self.peripheral_parser.flatten_peripherals(peripherals)

        # Create a mapping from original names to parsed names
        periph_name_map = {}
        for p in flattened_peripherals:
            try:
                name_parser = parse_component_name(p.name)
                periph_name_map[p.name] = name_parser.name
            except ValueError as e:
                self.logger.warning(f'⚠️  WARNING: {e}')
                periph_name_map[p.name] = p.name

        peripherals_dict = {p.name: p for p in flattened_peripherals}
        self.logger.debug(f'   Loaded {len(flattened_peripherals)} peripheral configurations')

        # Extract peripheral types for Kconfig update
        peripheral_types = set()
        for p in flattened_peripherals:
            if hasattr(p, 'type') and p.type:
                peripheral_types.add(p.type)

        self.logger.debug('   Loading peripheral parsers...')
        periph_parsers = load_parsers([], prefix='periph_', base_dir=str(self.peripherals_dir))
        periph_structs = []

        self.logger.debug('   ⚙️  Generating peripheral structures...')
        for p in flattened_peripherals:
            parse_entry = periph_parsers.get(p.type)
            if not parse_entry:
                self.logger.warning(f'⚠️  WARNING: No parser for peripheral type {p.type}')
                continue
            version, parse_func, _ = parse_entry  # Unpack only what we need here
            # Pass complete peripheral information
            result = parse_func(p.name, {'format': p.format, 'role': p.role, 'config': p.config})
            periph_structs.append(result)

        self.logger.debug('   Writing peripheral configuration files...')
        # Generate files directly to components/gen_bmgr_codes
        if hasattr(self, 'project_root') and self.project_root:
            gen_bmgr_codes_dir = os.path.join(self.project_root, 'components', 'gen_bmgr_codes')
            os.makedirs(gen_bmgr_codes_dir, exist_ok=True)
            self.write_periph_c(periph_structs, flattened_peripherals, periph_parsers, out_path=os.path.join(gen_bmgr_codes_dir, 'gen_board_periph_config.c'))
            self.write_periph_handles(flattened_peripherals, periph_parsers, out_path=os.path.join(gen_bmgr_codes_dir, 'gen_board_periph_handles.c'))

        return peripherals_dict, periph_name_map, peripheral_types

    def process_devices(self, dev_yaml_path: str, peripherals_dict, periph_name_map,
                       board_path: Optional[str] = None, extra_configs: Dict = {}, extra_includes: set = set()):
        """Process devices from YAML and generate C configuration files, returns device types set"""
        self.logger.info('   Parsing device YAML file...')
        device_parsers = load_parsers([], prefix='dev_', base_dir=str(self.devices_dir))

        self.logger.debug(f"   Debug: peripherals_dict keys: {list(peripherals_dict.keys()) if peripherals_dict else 'None'}")

        try:
            devices = self.device_parser.parse_devices_yaml_legacy(dev_yaml_path, peripherals_dict)
        except ValueError as e:
            # The error message is already logged by device_parser, just re-raise to stop
            raise  # Re-raise the exception to stop the generation process

        # Parse device names
        self.logger.debug('   Processing device names...')
        for d in devices:
            try:
                name_parser = parse_component_name(d.name)
                d.name = name_parser.name
            except ValueError as e:
                self.logger.warning(f'⚠️  WARNING: {e}')

        self.logger.debug(f'   Loaded {len(devices)} device configurations')
        self.logger.debug('   Loading device parsers...')
        device_structs = []

        # Extract device types for Kconfig update
        device_types = set()
        for d in devices:
            if hasattr(d, 'type') and d.type:
                device_types.add(d.type)

        self.logger.debug('   ⚙️  Generating device structures...')
        for d in devices:
            parse_entry = device_parsers.get(d.type)
            if not parse_entry:
                self.logger.warning(f'⚠️  WARNING: No parser for device type {d.type}')
                continue
            version, parse_func, _ = parse_entry  # Unpack only what we need here
            # Create full config with peripherals
            full_config = {
                'type': d.type,
                'config': d.config,
                'peripherals': []  # Convert peripheral references to list
            }

            # Read the YAML file to get peripheral configurations and other device-level fields
            from generators.device_parser import load_yaml_with_includes
            dev_yml = load_yaml_with_includes(dev_yaml_path)
            for dev in dev_yml.get('devices', []):
                if dev.get('name') == d.name:
                    # Add device-level fields like chip
                    if 'chip' in dev:
                        full_config['chip'] = dev['chip']
                    # Parse peripheral names in the config
                    peripherals = []

                    # Get device-level peripherals
                    raw_peripherals = dev.get('peripherals', [])
                    flattened_peripherals = self.peripheral_parser.flatten_peripherals(raw_peripherals)
                    for periph in flattened_peripherals:
                        if isinstance(periph, dict):
                            periph_name = periph.get('name')
                            if periph_name:
                                # Use the mapped name if available
                                mapped_name = periph_name_map.get(periph_name, periph_name)
                                periph_copy = periph.copy()
                                periph_copy['name'] = mapped_name
                                peripherals.append(periph_copy)
                        else:
                            # For string peripheral references
                            mapped_name = periph_name_map.get(periph, periph)
                            peripherals.append({'name': mapped_name})
                    full_config['peripherals'] = peripherals
                    break

            try:
                result = parse_func(d.name, full_config, peripherals_dict)
                device_structs.append(result)
            except ValueError as e:
                raise
            except Exception as e:
                self.logger.error(f"❌ Device parser error for '{d.name}': {e}")
                raise

        self.logger.info(f'✅ Successfully validated {len(device_structs)} devices')
        self.logger.debug('   Writing device configuration files...')
        # Generate files directly to components/gen_bmgr_codes
        project_root = getattr(self, 'project_root', None)
        if project_root is None:
            project_root = os.getcwd()

        gen_bmgr_codes_dir = os.path.join(project_root, 'components', 'gen_bmgr_codes')
        os.makedirs(gen_bmgr_codes_dir, exist_ok=True)
        # Generate custom device structure header first
        self.write_device_custom_h(device_structs, devices, out_path=os.path.join(gen_bmgr_codes_dir, 'gen_board_device_custom.h'))
        self.write_device_c(device_structs, devices, device_parsers, extra_configs, extra_includes, out_path=os.path.join(gen_bmgr_codes_dir, 'gen_board_device_config.c'))
        self.write_device_handles(devices, device_parsers, out_path=os.path.join(gen_bmgr_codes_dir, 'gen_board_device_handles.c'))

        # Validate that extra_dev configurations are used in device configs
        self.logger.debug('   ✅ Validating extra device configurations...')
        self.dependency_manager.validate_extra_dev_usage(extra_configs, devices)

        return device_types

    def clear_gen_bmgr_codes_directory(self, project_root: str) -> bool:
        """Clear all files and directories in the gen_bmgr_codes directory before generating new ones"""
        try:
            gen_bmgr_codes_dir = os.path.join(project_root, 'components', 'gen_bmgr_codes')

            if os.path.exists(gen_bmgr_codes_dir):
                self.logger.debug(f'   Clearing gen_bmgr_codes directory: {gen_bmgr_codes_dir}')

                # Remove all files in the directory
                for filename in os.listdir(gen_bmgr_codes_dir):
                    file_path = os.path.join(gen_bmgr_codes_dir, filename)
                    if os.path.isfile(file_path):
                        os.remove(file_path)
                        self.logger.debug(f'   Removed file: {filename}')
                    elif os.path.isdir(file_path):
                        import shutil
                        shutil.rmtree(file_path)
                        self.logger.debug(f'   Removed directory: {filename}')

                self.logger.info(' ✅ gen_bmgr_codes directory cleared successfully')
            else:
                self.logger.debug(f'gen_bmgr_codes directory does not exist: {gen_bmgr_codes_dir}')

            return True
        except Exception as e:
            self.logger.error(f'❌ Error clearing gen_bmgr_codes directory: {e}')
            return False

    def get_chip_name_from_board_path(self, board_path: str) -> Optional[str]:
        """Extract chip name from board_info.yaml file"""
        from generators.utils.board_utils import get_chip_name_from_board_path as get_chip_name_legacy

        board_info_path = os.path.join(board_path, 'board_info.yaml')

        if not os.path.exists(board_info_path):
            self.logger.warning(f'⚠️  board_info.yaml not found at {board_info_path}')
            return None

        chip = get_chip_name_legacy(board_path)
        if chip:
            self.logger.debug(f'   Found chip name: {chip}')
            return chip
        else:
            self.logger.warning(f'⚠️  No chip field found in {board_info_path}')
            return None

    def get_version_info(self):
        """Get version information including component version, git commit ID and date, and generation time"""
        import subprocess
        import json
        from datetime import datetime

        version_info = {
            'component_version': 'Unknown',
            'git_commit_id': 'Unknown',
            'git_commit_date': 'Unknown',
            'generation_time': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        }

        # Get component version from idf_component.yml
        try:
            idf_component_path = self.script_dir / 'idf_component.yml'
            if idf_component_path.exists():
                with open(idf_component_path, 'r', encoding='utf-8') as f:
                    component_data = yaml.safe_load(f)
                    version_info['component_version'] = component_data.get('version', 'Unknown')
        except Exception as e:
            self.logger.warning(f'⚠️  Failed to read component version: {e}')

        # Get git commit information
        try:
            # Get current working directory (should be the git repository root)
            git_dir = self.script_dir
            while git_dir != git_dir.parent:
                if (git_dir / '.git').exists():
                    break
                git_dir = git_dir.parent

            if (git_dir / '.git').exists():
                # Get git commit ID
                result = subprocess.run(
                    ['git', 'rev-parse', 'HEAD'],
                    cwd=git_dir,
                    capture_output=True,
                    text=True,
                    timeout=10
                )
                if result.returncode == 0:
                    version_info['git_commit_id'] = result.stdout.strip()[:8]  # First 8 characters

                # Get git commit date
                result = subprocess.run(
                    ['git', 'log', '-1', '--format=%cd', '--date=short'],
                    cwd=git_dir,
                    capture_output=True,
                    text=True,
                    timeout=10
                )
                if result.returncode == 0:
                    version_info['git_commit_date'] = result.stdout.strip()
        except Exception as e:
            self.logger.warning(f'⚠️  Failed to get git information: {e}')

        return version_info

    def run(self, args):
        """Run the complete 8-step board configuration generation process"""
        self.logger.info('=== Board Manager Configuration Generator ===')

        # Initialize device and peripheral types sets
        device_types = set()
        peripheral_types = set()

        # 1. Scan all board directories (FIRST STEP - only scan once)
        self.logger.info('⚙️  Step 1/8: Scanning board directories...')

        # Show scanning directories
        self.logger.debug('   Scanning directories:')
        boards_dir = self.config_generator.boards_dir
        self.logger.debug(f'      • Default boards: {boards_dir}')
        if args.board_customer_path and args.board_customer_path != 'NONE':
            self.logger.debug(f'      • Customer boards: {args.board_customer_path}')
        else:
            self.logger.debug(f'      • Customer boards: (not specified)')

        # Show components boards path
        project_root = os.environ.get('PROJECT_DIR')
        if not project_root:
            # Start searching from current working directory, not script directory
            project_root = self.find_project_root(os.getcwd())

        # Store project_root for use in other methods
        self.project_root = project_root

        if project_root:
            components_dir = os.path.join(project_root, 'components')
            self.logger.debug(f'      • Components boards: {components_dir}')

            # Remove only CMakeCache.txt in build directory to avoid full clean when switching different chip boards
            try:
                cmake_cache_path = os.path.join(project_root, 'build', 'CMakeCache.txt')
                if os.path.exists(cmake_cache_path):
                    os.remove(cmake_cache_path)
                    self.logger.debug(f'   Removed build cache: {cmake_cache_path}')
            except Exception as e:
                self.logger.error(f'❌ Error: Failed to remove CMakeCache.txt: {e}')
                return False

            # Clear gen_bmgr_codes directory before generating new files
            if not self.clear_gen_bmgr_codes_directory(project_root):
                self.logger.error('❌ Error: Failed to clear gen_bmgr_codes directory!')
                return False
        else:
            self.logger.debug(f'      • Components boards: (project root not found)')

        all_boards = self.config_generator.scan_board_directories(args.board_customer_path)

        if not all_boards:
            self.logger.error('❌ Error: No valid board directories found!')
            self.logger.error('   Each board directory must contain a Kconfig file')
            return False

        self.logger.info(f'✅ Found {len(all_boards)} boards: {list(all_boards.keys())}')

        # 2. Get selected board from sdkconfig or command line
        self.logger.info('⚙️  Step 2/8: Reading board selection...')

        if args.board_name:
            selected_board = args.board_name
            self.logger.info(f'✅ Using board from command line: {selected_board}')
        else:
            selected_board = self.config_generator.get_selected_board_from_sdkconfig()
            self.logger.info(f'✅ Selected board from sdkconfig: {selected_board}')

        # Display board information
        if selected_board in all_boards:
            board_path = all_boards[selected_board]
            self.logger.info(f'✅ Board path: {board_path}')
        else:
            self.logger.warning(f"⚠️  Warning: Selected board '{selected_board}' not found in scanned boards")

        # 3. Find configuration files for selected board
        self.logger.info('⚙️  Step 3/8: Finding board configuration files...')
        periph_yaml_path, dev_yaml_path = self.config_generator.find_board_config_files(selected_board, all_boards)

        if not periph_yaml_path or not dev_yaml_path:
            self.logger.error('❌ Error: Could not find configuration files for selected board')
            if not periph_yaml_path:
                self.logger.error(f"   Missing: peripherals.yaml for board '{selected_board}'")
                self.logger.error(f"   Missing: peripherals.yaml for board '{selected_board}'")
            if not dev_yaml_path:
                self.logger.error(f"   Missing: board_devices.yaml for board '{selected_board}'")
                self.logger.error(f"   Missing: board_devices.yaml for board '{selected_board}'")
            return False

        self.logger.info(f'✅ Configuration files found:')
        self.logger.debug(f'      • Peripherals: {periph_yaml_path}')
        self.logger.debug(f'      • Devices: {dev_yaml_path}')

        # 4~5. Process peripherals and devices based on arguments
        peripherals_dict = None
        periph_name_map = None
        device_dependencies = {}  # Initialize device_dependencies

        # Set board path for global board utilities
        board_path = all_boards.get(selected_board) if selected_board in all_boards else None
        if board_path:
            from generators.utils.board_utils import set_board_path
            set_board_path(board_path)
            self.logger.debug(f'   Set global board path: {board_path}')

        if not args.devices_only:
            self.logger.info('⚙️  Step 4/8: Processing peripherals...')
            try:
                peripherals_dict, periph_name_map, peripheral_types = self.process_peripherals(periph_yaml_path)
                self.logger.info(f'✅ Peripheral processing completed: {len(peripheral_types)} types found')

            except Exception as e:
                self.logger.error(f'   Error processing peripherals: {e}')
                self.logger.error(f'❌ Error processing peripherals: {e}')
                return False

        if not args.peripherals_only:
            if peripherals_dict is None:
                # If we're only processing devices, we need to load peripherals for reference
                self.logger.info('⚙️  Step 4/8: Processing peripherals... (LOADING FOR REFERENCE)')
                self.logger.info('   4.1 Loading peripherals for device reference...')
                try:
                    peripherals = self.peripheral_parser.parse_peripherals_yaml_legacy(periph_yaml_path)
                    periph_name_map = {}
                    for p in peripherals:
                        try:
                            name_parser = parse_component_name(p.name)
                            periph_name_map[p.name] = name_parser.name
                            p.name = name_parser.name
                        except ValueError as e:
                            self.logger.warning(f'⚠️  WARNING: {e}')
                            periph_name_map[p.name] = p.name
                    flattened_peripherals = self.peripheral_parser.flatten_peripherals(peripherals)
                    peripherals_dict = {p.name: p for p in flattened_peripherals}

                    # Extract peripheral types for Kconfig update
                    peripheral_types = set()
                    for p in flattened_peripherals:
                        if hasattr(p, 'type') and p.type:
                            peripheral_types.add(p.type)
                except Exception as e:
                    self.logger.error(f'❌ Error loading peripherals for reference: {e}')
                    return False

            self.logger.info('⚙️  Step 5/8: Processing devices and dependencies...')

            # Get board path for extra_dev scanning
            board_path = all_boards.get(selected_board)
            extra_configs = {}
            extra_includes = set()
            # TODO: Remove this once we have a way to scan extra_dev files
            if board_path and False:
                self.logger.debug('   Scanning extra device configurations...')
                extra_configs, extra_includes = self.dependency_manager.scan_extra_dev_files(board_path)

            # Extract dependencies from board_devices.yaml
            self.logger.debug('   Extracting device dependencies...')
            device_dependencies = self.dependency_manager.extract_device_dependencies(dev_yaml_path)
            # Update idf_component.yml with new dependencies
            # Disable updating idf_component.yml, instead, dynamic dependencies rely on a Kconfig option.
            # self.logger.debug('   Updating idf_component.yml...')
            # self.dependency_manager.update_idf_component_dependencies(device_dependencies)

            # Scan board source files and update CMakeLists.txt
            self.logger.debug('   Scanning board source files...')
            board_source_files = self.source_scanner.scan_board_source_files(board_path)
            # self.logger.debug("   Updating CMakeLists.txt...")
            # self.source_scanner.update_cmakelists_with_board_sources(board_source_files)

            self.logger.debug('   Processing device configurations...')
            try:
                device_types = self.process_devices(dev_yaml_path, peripherals_dict, periph_name_map, board_path, extra_configs, extra_includes)
                self.logger.info(f'✅ Device processing completed: {len(device_types)} types found')
            except ValueError as e:
                # Re-raise ValueError to stop the generation process
                raise
            except Exception as e:
                # Print a single concise error here; detailed cause already logged above
                self.logger.error('❌ Error processing devices. See details above. Aborting.')
                return False
        else:
            self.logger.info('⏭️  Step 5/8: Processing devices... (SKIPPED)')

        # 6. Generate Kconfig if requested (SIXTH STEP - after device and peripheral processing)
        self.logger.info('⚙️  Step 6/8: Generating Kconfig menu system...')

        if not self.generate_kconfig(all_boards, args.board_customer_path, peripheral_types, device_types, selected_board):
            self.logger.error('❌ Error: Kconfig generation failed!')
            return False

        self.logger.info('✅ Kconfig generation completed successfully')

        # If only Kconfig generation is requested, exit early
        if args.kconfig_only:
            self.logger.info('ℹ️  Only Kconfig generation requested, skipping board configuration generation')
            return True

        # 7. Update sdkconfig based on board device and peripheral types
        self.logger.info('⚙️  Step 7/8: Updating SDK configuration...')

        # Check if auto-config is disabled via sdkconfig
        auto_config_disabled_via_sdkconfig = False
        if not args.disable_sdkconfig_auto_update:
            # Only check sdkconfig if not explicitly disabled via command line
            auto_config_disabled_via_sdkconfig = self.sdkconfig_manager.is_auto_config_disabled_in_sdkconfig()
            if auto_config_disabled_via_sdkconfig:
                self.logger.info('   ⏭️  Board-based SDK configuration update... (DISABLED via sdkconfig)')

        if args.disable_sdkconfig_auto_update or auto_config_disabled_via_sdkconfig:
            # Disabled by user via command line or sdkconfig
            if args.disable_sdkconfig_auto_update:
                self.logger.info('   ⏭️  Board-based SDK configuration update... (DISABLED via command line)')
            # auto_config_disabled_via_sdkconfig case already logged above
        elif args.sdkconfig_only:
            # Only check without enabling
            self.logger.info('   Checking sdkconfig features...')
            self.sdkconfig_manager.update_sdkconfig_from_board_types(
                device_types=device_types,
                peripheral_types=peripheral_types,
                sdkconfig_path=None,
                enable=False
            )
        else:
            # Default behavior: update sdkconfig based on board types and board selection
            self.logger.debug('   Updating sdkconfig based on board types and board selection...')

            # Get chip name from board_info.yaml
            board_path = all_boards.get(selected_board)
            if not board_path:
                self.logger.error(f'❌ Board path not found for {selected_board}')
                return False

            chip_name = self.get_chip_name_from_board_path(board_path)
            if not chip_name:
                self.logger.error(f'❌ Chip name not found in board_info.yaml for {selected_board}')
                return False

            result = self.sdkconfig_manager.update_sdkconfig_from_board_types(
                device_types=device_types,
                peripheral_types=peripheral_types,
                sdkconfig_path=str(Path.cwd()/'sdkconfig'),
                enable=True,
                board_name=selected_board,
                chip_name=chip_name
            )
            if result['enabled']:
                self.logger.info(f"✅ Updated {len(result['enabled'])} sdkconfig features")

        # 8. Write board information and setup components/gen_bmgr_codes
        self.logger.info('⚙️  Step 8/8: Writing board information and setting up components...')

        # Write board info directly to components/gen_bmgr_codes
        if project_root is None:
            self.logger.warning('⚠️  Project root not found, creating components directory in current directory')
            project_root = os.getcwd()

        gen_bmgr_codes_dir = os.path.join(project_root, 'components', 'gen_bmgr_codes')
        os.makedirs(gen_bmgr_codes_dir, exist_ok=True)
        if selected_board in all_boards:
            self.write_board_info(all_boards[selected_board], out_path=os.path.join(gen_bmgr_codes_dir, 'gen_board_info.c'))
        else:
            self.logger.warning(f'⚠️  Cannot write board info: board "{selected_board}" not found in all_boards')

        # Setup components/gen_bmgr_codes directory and build system
        if not self.setup_gen_bmgr_codes_component(project_root, board_path, device_dependencies, selected_board):
            self.logger.error('❌ Error: Failed to setup components/gen_bmgr_codes!')
            return False

        self.logger.info(f'✅ === Board configuration generation completed successfully for board: {selected_board} ===')
        return True

    def setup_gen_bmgr_codes_component(self, project_root: str, board_path: str, device_dependencies: dict, selected_board: str = None) -> bool:
        """
        Setup components/gen_bmgr_codes directory and build system.

        Args:
            project_root: Path to the project root directory
            board_path: Path to the selected board directory
            device_dependencies: Dictionary of device dependencies
            selected_board: Name of the selected board

        Returns:
            bool: True if setup was successful
        """
        try:
            # Handle None project_root
            if project_root is None:
                self.logger.warning('⚠️  Project root is None, using current directory')
                project_root = os.getcwd()

            # 1. Create components/gen_bmgr_codes directory
            components_dir = os.path.join(project_root, 'components')
            gen_bmgr_codes_dir = os.path.join(components_dir, 'gen_bmgr_codes')

            # Create components directory if it doesn't exist
            if not os.path.exists(components_dir):
                os.makedirs(components_dir)
                self.logger.info(f'      Created components directory: {components_dir}')

            # Create gen_bmgr_codes directory
            if not os.path.exists(gen_bmgr_codes_dir):
                os.makedirs(gen_bmgr_codes_dir)
                self.logger.info(f'      Created gen_bmgr_codes directory: {gen_bmgr_codes_dir}')

            # 2. Create CMakeLists.txt with board source paths
            # Get board source files and create SRC_DIRS list
            board_src_dirs = []
            if board_path and os.path.exists(board_path):
                # Calculate relative path from gen_bmgr_codes to board directory
                board_relative_path = os.path.relpath(board_path, gen_bmgr_codes_dir)
                board_src_dirs.append(f'"{board_relative_path}"')
                self.logger.info(f'   Added board source directory: {board_relative_path}')

            # Create SRC_DIRS and INCLUDE_DIRS strings
            src_dirs_str = ' '.join(['"."'] + board_src_dirs) if board_src_dirs else '"."'
            include_dirs_str = ' '.join(['"."'] + board_src_dirs) if board_src_dirs else '"."'

            # Add board information output to CMakeLists.txt
            board_info_output = ''
            if selected_board:
                board_info_output = f"""# Board information output
message(STATUS "Selected Board: {selected_board}")
message(STATUS "Board Path: {board_path if board_path else 'Not specified'}")

"""

            cmakelists_content = f"""{board_info_output}idf_component_register(
    SRC_DIRS {src_dirs_str}
    INCLUDE_DIRS {include_dirs_str}
    REQUIRES esp_board_manager
)

# This is equivalent to adding WHOLE_ARCHIVE option to the idf_component_register call above:
idf_component_set_property(${{COMPONENT_NAME}} WHOLE_ARCHIVE TRUE)
"""

            cmakelists_path = os.path.join(gen_bmgr_codes_dir, 'CMakeLists.txt')
            with open(cmakelists_path, 'w', encoding='utf-8') as f:
                f.write(cmakelists_content)

            self.logger.info(f'   Created CMakeLists.txt: {cmakelists_path}')

            # 3. Create idf_component.yml with dependencies
            idf_component_content = {
                'dependencies': {
                }
            }

            # Add device dependencies to idf_component.yml
            for component, version in device_dependencies.items():
                idf_component_content['dependencies'][component] = version

            idf_component_path = os.path.join(gen_bmgr_codes_dir, 'idf_component.yml')
            with open(idf_component_path, 'w', encoding='utf-8') as f:
                yaml.dump(idf_component_content, f, default_flow_style=False, sort_keys=False)

            # Check if BOARD_PATH is used in dependencies and validate format
            has_board_path_usage = False
            has_replacement = False

            # Check idf_component.yml dependencies and replace ${BOARD_PATH} immediately
            for component, version in idf_component_content['dependencies'].items():
                if isinstance(version, dict):
                    for key, value in version.items():
                        if key in ['path', 'override_path'] and isinstance(value, str) and 'BOARD_PATH' in value:
                            has_board_path_usage = True
                            if '${BOARD_PATH}' in value:
                                if board_path:
                                    absolute_board_path = os.path.abspath(board_path)
                                    # Replace ${BOARD_PATH} in the current value
                                    updated_value = value.replace('${BOARD_PATH}', absolute_board_path)
                                    idf_component_content['dependencies'][component][key] = updated_value
                                    has_replacement = True
                                    self.logger.info(f'✅ Replaced ${{BOARD_PATH}} in {component}.{key}: {value} -> {updated_value}')
                                else:
                                    self.logger.warning(f'⚠️  Found valid ${{BOARD_PATH}} in {component}.{key} but no board path provided')
                            else:
                                self.logger.warning(f'⚠️  BOARD_PATH invalid syntax: Expected ${{BOARD_PATH}}, got "{value}" in {component}.{key}')

            # Write updated idf_component.yml only if actual replacements were made
            if has_replacement:
                with open(idf_component_path, 'w', encoding='utf-8') as f:
                    yaml.dump(idf_component_content, f, default_flow_style=False, sort_keys=False)
                self.logger.info(f'   Successfully updated idf_component.yml with board path replacements')
            elif has_board_path_usage and not board_path:
                self.logger.warning('⚠️  BOARD_PATH found in dependencies but no board path provided')

            self.logger.info(f'   Created idf_component.yml: {idf_component_path}')
            # 4. Board source files are now referenced via SRC_DIRS in CMakeLists.txt
            # No need to copy files - they are referenced directly from board directory
            self.logger.info(f'✅ Board source files will be compiled from: {board_path}')
            self.logger.info(f'✅ Generated files directly to components/gen_bmgr_codes completed successfully!')

            return True

        except Exception as e:
            self.logger.error(f'❌ Error setting up gen_bmgr_codes component: {e}')
            return False

def main():
    """Main entry point with command line argument parsing and error handling"""
    print('ESP Board Manager - Configuration Generator')
    print('=' * 60)

    # Parse command line arguments
    parser = argparse.ArgumentParser(
        description='Board Manager Configuration Generator (Refactored)',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    python gen_bmgr_config_codes.py                                   # Use sdkconfig and default boards (auto-sets CONFIG_IDF_TARGET)
    python gen_bmgr_config_codes.py -b echoear_core_board_v1_0        # Specify board directly (auto-sets CONFIG_IDF_TARGET)
    python gen_bmgr_config_codes.py -c /custom/boards                 # Add customer boards directory
    python gen_bmgr_config_codes.py -c /path/to/single/board          # Add single board directory
    python gen_bmgr_config_codes.py -b my_board -c /custom/boards     # Both options
    python gen_bmgr_config_codes.py --peripherals-only                # Only process peripherals
    python gen_bmgr_config_codes.py --devices-only                    # Only process devices
    python gen_bmgr_config_codes.py --kconfig-only                    # Generate Kconfig menu system (default enabled)
    python gen_bmgr_config_codes.py --sdkconfig-only                  # Only process sdkconfig features
    python gen_bmgr_config_codes.py --disable-sdkconfig-auto-update   # Disable automatic sdkconfig update
    python gen_bmgr_config_codes.py --log-level DEBUG                 # Set log level to DEBUG
            """
    )

    parser.add_argument(
        '-b', '--board',
        dest='board_name',
        help='Specify board name directly (bypasses sdkconfig reading)'
    )

    parser.add_argument(
        '-c', '--customer-path', '--custom',
        dest='board_customer_path',
        help='Path to customer boards directory or single board directory (use "NONE" to skip)'
    )

    parser.add_argument(
        '--peripherals-only',
        action='store_true',
        help='Only process peripherals (skip devices)'
    )

    parser.add_argument(
        '--devices-only',
        action='store_true',
        help='Only process devices (skip peripherals)'
    )

    parser.add_argument(
        '--disable-sdkconfig-auto-update',
        action='store_true',
        help='Disable automatic sdkconfig feature enabling (default is enabled)'
    )

    parser.add_argument(
        '--kconfig-only',
        action='store_true',
        help='Generate Kconfig menu system for board and component selection (default enabled)'
    )

    parser.add_argument(
        '--sdkconfig-only',
        action='store_true',
        help='Only process sdkconfig features without generating Kconfig'
    )

    parser.add_argument(
        '--list-boards', '-l',
        action='store_true',
        help='List all available boards and exit'
    )

    parser.add_argument(
        '--log-level',
        choices=['DEBUG', 'INFO', 'WARNING', 'ERROR'],
        default='INFO',
        help='Set log level (default: INFO)'
    )

    args = parser.parse_args()

    # Set global log level first
    log_level_map = {
        'DEBUG': logging.DEBUG,
        'INFO': logging.INFO,
        'WARNING': logging.WARNING,
        'ERROR': logging.ERROR
    }
    from generators.utils.logger import set_global_log_level
    set_global_log_level(log_level_map[args.log_level])

    # Setup logging with global level
    setup_logger('board_config_generator')
    logger = get_logger('main')

    # Create generator and run
    script_dir = Path(__file__).parent
    generator = BoardConfigGenerator(script_dir)

    # Handle list-boards option
    if args.list_boards:
        print('GMF Board Manager - Board Listing')
        print('=' * 40)

        try:
            # Scan and display boards
            all_boards = generator.config_generator.scan_board_directories(args.board_customer_path)

            if all_boards:
                generator.logger.info(f'Found {len(all_boards)} board(s):')
                print()

                # Group boards by source
                main_boards = {}
                customer_boards = {}
                component_boards = {}

                for board_name, board_path in all_boards.items():
                    board_path_obj = Path(board_path)
                    if board_path_obj.parent == generator.boards_dir:
                        main_boards[board_name] = board_path
                    elif args.board_customer_path and board_path.startswith(args.board_customer_path):
                        customer_boards[board_name] = board_path
                    else:
                        component_boards[board_name] = board_path

                # Display main boards
                if main_boards:
                    generator.logger.info('Main Boards:')
                    for board_name in sorted(main_boards.keys()):
                        generator.logger.info(f'  • {board_name}')
                    print()

                # Display customer boards
                if customer_boards:
                    generator.logger.info('Customer Boards:')
                    for board_name in sorted(customer_boards.keys()):
                        generator.logger.info(f'  • {board_name}')
                    print()

                # Display component boards
                if component_boards:
                    generator.logger.info('Component Boards:')
                    for board_name in sorted(component_boards.keys()):
                        generator.logger.info(f'  • {board_name}')
                    print()
            else:
                generator.logger.warning('No boards found!')

            generator.logger.info('Board listing completed!')
            return

        except Exception as e:
            generator.logger.error(f'Error listing boards: {e}')
            import traceback
            traceback.print_exc()
            sys.exit(1)

    try:
        success = generator.run(args)
        if not success:
            generator.logger.error('❌ Configuration generation failed!')
            sys.exit(1)
        generator.logger.info('✅ GMF Board Manager setup completed successfully!')
    except KeyboardInterrupt:
        generator.logger.info('Operation cancelled by user')
        print('⚠️  Operation cancelled by user')
        sys.exit(1)
    except ValueError as e:
        # Show the detailed error message
        print(f'❌ {e}')
        sys.exit(1)
    except Exception as e:
        generator.logger.error(f'Unexpected error: {e}')
        print(f'❌ Unexpected error: {e}')
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == '__main__':
    main()
