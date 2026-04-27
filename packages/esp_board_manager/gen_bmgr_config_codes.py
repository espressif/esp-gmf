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
import re
import shutil
import sys
import argparse
import logging
import yaml
from pathlib import Path
from typing import Dict, List, Optional, Set

# Add current directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from generators.utils.logger import setup_logger, get_logger, LoggerMixin
from generators.utils.file_utils import (
    ensure_existing_directory,
    normalize_project_dir,
    resolve_path_from_base,
    resolve_project_root_dir,
)
from generators import get_config_generator, get_sdkconfig_manager, get_dependency_manager, get_source_scanner
from generators.parser_loader import load_parsers
from generators.peripheral_parser import PeripheralParser
from generators.device_parser import DeviceParser
from generators.name_validator import parse_component_name
from generators.board_metadata_generator import BoardMetadataGenerator
from generators.utils.board_schema_version import (
    warn_if_invalid_board_yaml_schema_version,
    resolved_board_info_version_string,
)
from generators.utils.yaml_utils import BoardConfigYamlError


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

    def __init__(self, script_dir: Path, project_dir: Optional[str] = None):
        super().__init__()
        self.script_dir = script_dir
        self.project_dir = normalize_project_dir(project_dir)

        # Display version information when creating the generator
        version_info = self.get_version_info()
        print('📋 Version Information:')
        print(f'   • Component Version: {version_info["component_version"]}')
        print(f'   • Git Commit: {version_info["git_commit_id"]} ({version_info["git_commit_date"]})')
        print(f'   • Generation Time: {version_info["generation_time"]}')
        print('')

        # Determine the root directory for all GMF Board Manager resources
        # Use script directory as root directory
        self.root_dir = script_dir
        self.logger.info(f'ℹ️  Using script directory as root directory: {self.root_dir}')

        # All paths are now relative to root_dir
        self.boards_dir = self.root_dir / 'boards'
        self.peripherals_dir = self.root_dir / 'peripherals'
        self.devices_dir = self.root_dir / 'devices'
        self.gen_codes_dir = self.root_dir / 'gen_codes'

        # Ensure gen_codes directory exists
        self.gen_codes_dir.mkdir(exist_ok=True)

        # Initialize components with root_dir
        self.config_generator = get_config_generator()(self.root_dir, project_dir=self.project_dir)
        self.sdkconfig_manager = get_sdkconfig_manager()(self.root_dir, project_dir=self.project_dir)
        self.dependency_manager = get_dependency_manager()(self.root_dir)
        self.source_scanner = get_source_scanner()(self.root_dir)

        # Initialize parsers with root_dir
        self.peripheral_parser = PeripheralParser(self.root_dir)
        self.device_parser = DeviceParser(self.root_dir)
        self.metadata_generator = BoardMetadataGenerator()
        self._last_peripheral_metadata_artifacts = []
        self._last_device_metadata_artifacts = []
        self._valid_periph_roles: Optional[Set[str]] = None

    def set_project_dir(self, project_dir: Optional[str]) -> None:
        """Update the effective project directory shared across helpers."""
        self.project_dir = normalize_project_dir(project_dir)
        self.config_generator.set_project_root(self.project_dir)
        self.sdkconfig_manager.set_project_path(self.project_dir)

    def resolve_project_dir(self) -> Optional[str]:
        """Resolve and cache the current effective project directory."""
        if self.project_dir is not None:
            self.set_project_dir(ensure_existing_directory(self.project_dir, 'Project directory'))
            return self.project_dir
        project_dir = resolve_project_root_dir(
            explicit_project_dir=self.project_dir,
            start_dir=Path(os.getcwd()),
        )
        self.set_project_dir(project_dir)
        return self.project_dir

    def get_effective_project_dir(self) -> str:
        """Return the best-known project directory, falling back to cwd if needed."""
        return getattr(self, 'project_root', None) or self.project_dir or os.getcwd()

    def get_component_name(self, file_path):
        """Extract component name from file path."""
        name = os.path.splitext(os.path.basename(file_path))[0]
        if name.startswith('dev_'):
            return name[4:]  # Remove 'dev_' prefix
        if name.startswith('periph_'):
            return name[7:]  # Remove 'periph_' prefix
        return name

    def _board_macro_name(self, board_name: str) -> str:
        """Convert a board name into the corresponding Kconfig symbol."""
        return 'ESP_BOARD_' + board_name.upper().replace('-', '_')

    def generate_kconfig_entry(self, name, path, is_device, default_value='n'):
        """Generate a Kconfig entry for a component."""
        component_type = 'Device' if is_device else 'Peripheral'
        prefix = 'DEV' if is_device else 'PERIPH'
        macro_name = f'ESP_BOARD_{prefix}_{name.upper()}_SUPPORT'

        entry = f"""config {macro_name}
    bool "{component_type} '{name}' support"
    help
        Enable {name} {component_type.lower()} support.
        This option enables the {name} {component_type.lower()} driver.
        The driver is located at: {path}

"""
        return entry

    def generate_nested_kconfig_entry(self, name, path, is_device, default_value='n', sub_types=None, sub_type_separator='_SUB_'):
        """Generate a nested Kconfig entry for a component with sub_types.

        Args:
            name: Component name
            path: Component path
            is_device: Whether this is a device (True) or peripheral (False)
            default_value: Default value for the config
            sub_types: List of sub-types for this component
            sub_type_separator: Separator used in sub-type macro names (default: '_SUB_')
        """
        component_type = 'Device' if is_device else 'Peripheral'
        prefix = 'DEV' if is_device else 'PERIPH'
        macro_name = f'ESP_BOARD_{prefix}_{name.upper()}_SUPPORT'

        entry = f"""config {macro_name}
    bool "{component_type} '{name}' support"
    help
        Enable {name} {component_type.lower()} support.
        This option enables the {name} {component_type.lower()} driver.
        The driver is located at: {path}

"""

        # Add sub_type configurations if they exist
        if sub_types:
            entry += f"""if {macro_name}

"""
            for sub_type in sub_types:
                sub_macro_name = f'ESP_BOARD_{prefix}_{name.upper()}{sub_type_separator}{sub_type.upper()}_SUPPORT'
                entry += f"""    config {sub_macro_name}
        bool "{component_type} '{name}' {sub_type} sub-type support"
        help
            Enable {name} {sub_type} sub-type support.
            This option enables the {name} {sub_type} sub-type driver.

"""
            entry += f"""endif

"""

        return entry

    def _generate_device_kconfig_entry(self, name, path, default_value, sub_types, device_names):
        """Helper method to generate device Kconfig entry (reduces code duplication)."""
        if sub_types:
            # Generate nested Kconfig entry with sub_types
            kconfig_content = self.generate_nested_kconfig_entry(
                name,
                path,
                True,
                default_value,
                sub_types
            )
            device_names.append(name)
            # Add sub_type names to device_names for logging
            for sub_type in sub_types:
                device_names.append(f'{name}_{sub_type}')
        else:
            # Generate regular Kconfig entry without sub_types
            kconfig_content = self.generate_kconfig_entry(
                name,
                path,
                True,
                default_value
            )
            device_names.append(name)

        return kconfig_content

    def generate_components_kconfig(self):
        """Generate peripheral and device Kconfig content (fully static, all default n).

        The actual peripheral/device enablement is driven by board_manager.defaults,
        not by default values in Kconfig.in. This makes Kconfig.in completely static.
        """

        kconfig_content = "menu \"Peripheral Support\"\n\n"

        # Process peripherals using the root directory
        periph_dir = self.peripherals_dir
        periph_names = []

        for file in sorted(periph_dir.glob('periph_*/periph_*.h')):
            name = self.get_component_name(file)
            # Always default n — board_manager.defaults handles enablement
            kconfig_content += self.generate_kconfig_entry(
                name,
                f'peripherals/periph_{name}',
                False,
                'n'
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

                    # Scan all sub_types from source files (static scan, not from board YAML)
                    sub_types = self._scan_device_sub_types(device_folder, device_type)

                    # Generate Kconfig entry using helper method (always default n via omitted default)
                    kconfig_content += self._generate_device_kconfig_entry(
                        name,
                        f'devices/{device_type}',
                        'n',
                        sub_types,
                        device_names
                    )
        else:
            # Fallback to old structure for backward compatibility
            for file in sorted(device_dir.glob('dev_*.h')):
                name = self.get_component_name(file)

                # Generate Kconfig entry using helper method (always default n via omitted default)
                kconfig_content += self._generate_device_kconfig_entry(
                    name,
                    f'devices/dev_{name}',
                    'n',
                    None,
                    device_names
                )

        kconfig_content += 'endmenu\n'

        self.logger.debug(f'✅ Generated Kconfig for {len(periph_names)} peripherals: {periph_names}')
        self.logger.debug(f'✅ Generated Kconfig for {len(device_names)} devices: {device_names}')
        return kconfig_content

    def _scan_device_sub_types(self, device_folder, device_type):
        """Scan device sub_types from source files in a device folder.

        Looks for files matching pattern: {device_type}_sub_*.c or {device_type}_sub_*.h
        Returns a sorted list of sub_type names, or None if no sub_types found.
        """
        sub_types = set()
        # Look for sub_type source files: dev_xxx_sub_yyy.c
        for sub_file in device_folder.glob(f'{device_type}_sub_*.c'):
            # Extract sub_type name from filename: dev_camera_sub_csi.c -> csi
            stem = sub_file.stem  # e.g. dev_camera_sub_csi
            prefix = f'{device_type}_sub_'
            if stem.startswith(prefix):
                sub_type = stem[len(prefix):]
                if sub_type:
                    sub_types.add(sub_type)
        # Also check header files
        for sub_file in device_folder.glob(f'{device_type}_sub_*.h'):
            stem = sub_file.stem
            prefix = f'{device_type}_sub_'
            if stem.startswith(prefix):
                sub_type = stem[len(prefix):]
                if sub_type:
                    sub_types.add(sub_type)
        return sorted(sub_types) if sub_types else None


    def generate_kconfig(self, _all_boards):
        """Generate static Kconfig content for device/peripheral capability symbols only."""
        try:
            kconfig_content = self.generate_components_kconfig()

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

    def generate_selected_board_kconfig_projbuild(self, selected_board=None, project_root=None):
        """Generate project-side Kconfig symbols for the currently selected board."""
        if not project_root:
            self.logger.warning('⚠️  project_root not set, skipping selected-board Kconfig.projbuild generation')
            return True

        gen_bmgr_codes_dir = os.path.join(project_root, 'components', 'gen_bmgr_codes')
        kconfig_projbuild_path = os.path.join(gen_bmgr_codes_dir, 'Kconfig.projbuild')

        if not selected_board:
            if os.path.exists(kconfig_projbuild_path):
                os.remove(kconfig_projbuild_path)
                self.logger.debug('   Removed Kconfig.projbuild (no selected board)')
            return True

        board_macro = self._board_macro_name(selected_board)
        kconfig_content = '# Auto-generated by esp_board_manager\n'
        kconfig_content += '# DO NOT MODIFY THIS FILE MANUALLY\n\n'
        kconfig_content += f'config {board_macro}\n'
        kconfig_content += '    bool\n'
        kconfig_content += '    default y\n\n'
        kconfig_content += 'config ESP_BOARD_NAME\n'
        kconfig_content += '    string\n'
        kconfig_content += f'    default "{selected_board}"\n'

        # Write the file
        os.makedirs(gen_bmgr_codes_dir, exist_ok=True)
        with open(kconfig_projbuild_path, 'w', encoding='utf-8') as f:
            f.write(kconfig_content)

        self.logger.info(f'✅ Generated selected-board Kconfig.projbuild at: {kconfig_projbuild_path}')
        return True

    def _role_to_enum(self, role_str: str) -> str:
        """Convert role string to enum value after validating it against the public enum list."""
        if not role_str:
            return 'ESP_BOARD_PERIPH_ROLE_NONE'

        normalized_role = role_str.strip().lower().replace('-', '_')
        valid_roles = self._load_valid_periph_roles()
        if normalized_role not in valid_roles:
            raise ValueError(
                f"Unsupported peripheral role '{role_str}'. "
                f"Allowed roles: {', '.join(sorted(valid_roles))}"
            )

        role_upper = normalized_role.upper()
        return f'ESP_BOARD_PERIPH_ROLE_{role_upper}'

    def _load_valid_periph_roles(self) -> Set[str]:
        """Load valid peripheral role names from the public board manager enum definition."""
        if self._valid_periph_roles is not None:
            return self._valid_periph_roles

        defs_path = self.root_dir / 'include' / 'esp_board_manager_defs.h'
        try:
            defs_text = defs_path.read_text(encoding='utf-8')
        except OSError as exc:
            raise RuntimeError(f'Failed to read peripheral role definitions from {defs_path}: {exc}') from exc

        roles = {
            match.group(1).lower()
            for match in re.finditer(r'ESP_BOARD_PERIPH_ROLE_([A-Z0-9_]+)\s*=', defs_text)
        }
        if not roles:
            raise RuntimeError(f'No peripheral roles found in {defs_path}')

        self._valid_periph_roles = roles
        return roles

    def write_periph_c(self, generated_peripherals, periph_parsers, out_path: str):
        """Write peripheral configuration C file"""
        # Ensure output directory exists
        Path(out_path).parent.mkdir(parents=True, exist_ok=True)
        peripherals = [p for p, _ in generated_peripherals]

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

            # Check if peripherals list is empty
            if not generated_peripherals:
                # Write descriptor array with NULL sentinel when empty
                f.write('// Peripheral descriptor array (empty - no peripherals defined)\n')
                f.write('const esp_board_periph_desc_t g_esp_board_peripherals[] = {\n')
                f.write('    {\n')
                f.write('        .next = NULL,\n')
                f.write('        .name = NULL,\n')
                f.write('        .type = NULL,\n')
                f.write('        .format = NULL,\n')
                f.write('        .role = ESP_BOARD_PERIPH_ROLE_NONE,\n')
                f.write('        .cfg = NULL,\n')
                f.write('        .cfg_size = 0,\n')
                f.write('        .id = 0,\n')
                f.write('    },\n')
                f.write('};\n')
                return

            # Write config structures
            f.write('// Peripheral configuration structures\n')
            for p, s in generated_peripherals:
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
            N = len(generated_peripherals)
            for i, (p, _) in enumerate(generated_peripherals):
                if i < N - 1:
                    next_str = f'&g_esp_board_peripherals[{i+1}]'
                else:
                    next_str = 'NULL'
                struct_var = 'esp_bmgr_' + p.name.replace('-', '_') + '_cfg'
                role_enum = self._role_to_enum(p.role)
                f.write('    {\n')
                f.write(f'        .next = {next_str},\n')
                f.write(f'        .name = "{p.name}",\n')
                f.write(f'        .type = "{p.type}",\n')
                if p.format is None:
                    f.write(f'        .format = NULL,\n')
                else:
                    f.write(f'        .format = "{p.format}",\n')
                f.write(f'        .role = {role_enum},\n')
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

            # Check if devices list is empty
            if not devices:
                # Write descriptor array with NULL sentinel when empty
                f.write('// Device descriptor array (empty - no devices defined)\n')
                f.write('const esp_board_device_desc_t g_esp_board_devices[] = {\n')
                f.write('    {\n')
                f.write('        .next = NULL,\n')
                f.write('        .name = NULL,\n')
                f.write('        .chip = NULL,\n')
                f.write('        .type = NULL,\n')
                f.write('        .sub_type = NULL,\n')
                f.write('        .cfg = NULL,\n')
                f.write('        .cfg_size = 0,\n')
                f.write('        .init_skip = false,\n')
                f.write('    },\n')
                f.write('};\n')
                return

            # Write config structures
            f.write('// Device configuration structures\n')
            for s, d in zip(device_structs, devices):
                if 'extra_configs' in s:
                    # Process extra configs first if presented
                    for e in s['extra_configs']:
                        e_struct_init = e['struct_init'].copy()
                        f.write(f"static {e['struct_type']} {e['struct_var']} = {{\n")
                        f.writelines('    ' + l + '\n' for l in self.config_generator.dict_to_c_initializer(e_struct_init, 4))
                        f.write('};\n\n')
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
                chip = getattr(d, 'chip', None)
                # Get power_ctrl_device value, default to None
                power_ctrl_device = getattr(d, 'power_ctrl_device', None)
                # Get sub_type value, default to None
                sub_type = getattr(d, 'sub_type', None)
                f.write('    {\n')
                f.write(f'        .next = {next_str},\n')
                f.write(f'        .name = "{d.name}",\n')
                if chip is not None:
                    f.write(f'        .chip = "{chip}",\n')
                else:
                    f.write('        .chip = NULL,\n')
                f.write(f'        .type = "{d.type}",\n')
                if sub_type is not None:
                    f.write(f'        .sub_type = "{sub_type}",\n')
                else:
                    f.write('        .sub_type = NULL,\n')
                f.write(f'        .cfg = &{struct_var},\n')
                f.write(f'        .cfg_size = sizeof({struct_var}),\n')
                f.write(f'        .init_skip = {str(init_skip).lower()},\n')
                # Only write power_ctrl_device if it's configured
                if power_ctrl_device is not None:
                    f.write(f'        .power_ctrl_device = "{(d.power_ctrl_device)}",\n')
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

            # Check if peripheral types list is empty
            if not periph_types:
                # Write handle array with NULL sentinel when empty
                f.write('// Peripheral handle array (empty - no peripherals defined)\n')
                f.write('esp_board_periph_entry_t g_esp_board_periph_handles[] = {\n')
                f.write('    {\n')
                f.write('        .next = NULL,\n')
                f.write('        .type = NULL,\n')
                f.write('        .role = ESP_BOARD_PERIPH_ROLE_NONE,\n')
                f.write('        .init = NULL,\n')
                f.write('        .deinit = NULL\n')
                f.write('    },\n')
                f.write('};\n')
                return

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
                f.write(f'        .role = {self._role_to_enum(role_str)},\n')
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

            # Check if devices list is empty
            if not devices:
                # Write handle array with NULL sentinel when empty
                f.write('// Device handle array (empty - no devices defined)\n')
                f.write('esp_board_device_handle_t g_esp_board_device_handles[] = {\n')
                f.write('    {\n')
                f.write('        .next = NULL,\n')
                f.write('        .name = NULL,\n')
                f.write('        .chip = NULL,\n')
                f.write('        .type = NULL,\n')
                f.write('        .device_handle = NULL,\n')
                f.write('        .init = NULL,\n')
                f.write('        .deinit = NULL\n')
                f.write('    },\n')
                f.write('};\n')
                return

            # Write handle array
            f.write('// Device handle array\n')
            f.write('esp_board_device_handle_t g_esp_board_device_handles[] = {\n')
            N = len(devices)
            for i, d in enumerate(devices):
                if i < N - 1:
                    next_str = f'&g_esp_board_device_handles[{i+1}]'
                else:
                    next_str = 'NULL'
                chip = getattr(d, 'chip', None)
                f.write('    {\n')
                f.write(f'        .next = {next_str},\n')
                f.write(f'        .name = "{d.name}",\n')
                if chip is not None:
                    f.write(f'        .chip = "{chip}",\n')
                else:
                    f.write('        .chip = NULL,\n')
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
            # Read board info from board_info.yaml (use load_board_yaml_file for unified error handling)
            from generators.utils.yaml_utils import load_board_yaml_file
            board_yml = load_board_yaml_file(board_info_path)

            if not isinstance(board_yml, dict):
                self.logger.warning(f'⚠️  board_info.yaml has unexpected structure at {board_info_path}, using defaults')
                board_yml = {}

            if board_yml.get('version') is not None:
                warn_if_invalid_board_yaml_schema_version(
                    self.logger, board_yml.get('version'), board_info_path
                )

            # Extract board information
            board_name = board_yml.get('board', 'unknown')
            chip = board_yml.get('chip', 'unknown')
            version = resolved_board_info_version_string(board_yml.get('version'))
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
        self._last_peripheral_metadata_artifacts = []

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
        try:
            periph_parsers = load_parsers([], prefix='periph_', base_dir=str(self.peripherals_dir))
        except Exception as e:
            raise RuntimeError(f'Failed to load peripheral parsers: {e}') from e

        generated_peripherals = []

        self.logger.debug('   ⚙️  Generating peripheral structures...')
        for p in flattened_peripherals:
            parse_entry = periph_parsers.get(p.type)
            if not parse_entry:
                msg = f"No parser found for peripheral '{p.name}' (type: {p.type})"
                self.logger.error(f'❌ {msg}')
                raise RuntimeError(msg)
            version, parse_func, _ = parse_entry  # Unpack only what we need here
            # Pass complete peripheral information
            try:
                result = parse_func(p.name, {'format': p.format, 'role': p.role, 'config': p.config})
            except ValueError as e:
                raise ValueError(
                    f"Failed to generate configuration for peripheral '{p.name}' (type: {p.type}): {e}"
                ) from e
            except Exception as e:
                raise RuntimeError(
                    f"Failed to generate configuration for peripheral '{p.name}' (type: {p.type}): {e}"
                ) from e
            generated_peripherals.append((p, result))
            self._last_peripheral_metadata_artifacts.append({
                'name': p.name,
                'type': p.type,
                'role': p.role,
                'format': p.format,
                'raw': {
                    'name': p.name,
                    'type': p.type,
                    'role': p.role,
                    'format': p.format,
                    'config': p.config,
                },
                'result': result,
                'parse_func': parse_func,
            })

        if len(generated_peripherals) != len(flattened_peripherals):
            raise RuntimeError(
                f'Peripheral generation count mismatch: generated {len(generated_peripherals)} '
                f'for {len(flattened_peripherals)} peripherals'
            )

        self.logger.debug('   Writing peripheral configuration files...')
        # Generate files directly to components/gen_bmgr_codes
        project_root = self.get_effective_project_dir()
        gen_bmgr_codes_dir = self._get_gen_bmgr_codes_dir(project_root)
        os.makedirs(gen_bmgr_codes_dir, exist_ok=True)
        self.write_periph_c(generated_peripherals, periph_parsers, out_path=os.path.join(gen_bmgr_codes_dir, 'gen_board_periph_config.c'))
        self.write_periph_handles(flattened_peripherals, periph_parsers, out_path=os.path.join(gen_bmgr_codes_dir, 'gen_board_periph_handles.c'))

        return peripherals_dict, periph_name_map, peripheral_types

    def process_devices(self, dev_yaml_path: str, peripherals_dict, periph_name_map,
                       board_path: Optional[str] = None, extra_configs: Dict = {}, extra_includes: set = set()):
        """Process devices from YAML and generate C configuration files, returns device types set"""
        self.logger.debug('   Parsing device YAML file...')
        device_parsers = load_parsers([], prefix='dev_', base_dir=str(self.devices_dir))
        self._last_device_metadata_artifacts = []

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

        # Extract device types and sub_types for Kconfig update
        device_types = set()
        device_subtypes = {}  # {device_type: set of sub_types}
        for d in devices:
            if hasattr(d, 'type') and d.type:
                device_types.add(d.type)
                # Check if device has sub_type information
                if hasattr(d, 'sub_type') and d.sub_type:
                    if d.type not in device_subtypes:
                        device_subtypes[d.type] = set()
                    device_subtypes[d.type].add(d.sub_type)

        from generators.device_parser import load_yaml_with_includes
        dev_yml = load_yaml_with_includes(dev_yaml_path)
        raw_devices_by_name = {}
        raw_devices = dev_yml.get('devices') or []
        for dev in raw_devices:
            raw_name = dev.get('name')
            if not raw_name:
                continue
            normalized_name = raw_name
            try:
                normalized_name = parse_component_name(raw_name).name
            except ValueError:
                pass
            raw_devices_by_name[normalized_name] = dev

        self.logger.debug('   ⚙️  Generating device structures...')
        for d in devices:
            parse_entry = device_parsers.get(d.type)
            if not parse_entry:
                self.logger.warning(f'⚠️  WARNING: No parser for device type {d.type}')
                continue
            version, parse_func, _ = parse_entry  # Unpack only what we need here
            raw_dev = raw_devices_by_name.get(d.name, {})

            # Create full config with peripherals
            full_config = {
                'name': d.name,
                'type': d.type,
                'config': d.config,
                'peripherals': []  # Convert peripheral references to list
            }

            # Add device-level fields like chip and sub_type
            if 'chip' in raw_dev:
                full_config['chip'] = raw_dev['chip']
            if 'sub_type' in raw_dev:
                full_config['sub_type'] = raw_dev['sub_type']

            peripherals = []
            raw_peripherals = raw_dev.get('peripherals', [])
            flattened_peripherals = self.peripheral_parser.flatten_peripherals(raw_peripherals)
            for periph in flattened_peripherals:
                if isinstance(periph, dict):
                    periph_name = periph.get('name')
                    if periph_name:
                        mapped_name = periph_name_map.get(periph_name, periph_name)
                        periph_copy = periph.copy()
                        periph_copy['name'] = mapped_name
                        peripherals.append(periph_copy)
                else:
                    mapped_name = periph_name_map.get(periph, periph)
                    peripherals.append({'name': mapped_name})
            full_config['peripherals'] = peripherals

            try:
                result = parse_func(d.name, full_config, peripherals_dict)
                device_structs.append(result)
                self._last_device_metadata_artifacts.append({
                    'name': d.name,
                    'type': d.type,
                    'sub_type': full_config.get('sub_type'),
                    'peripherals': [
                        periph.get('name')
                        for periph in peripherals
                        if isinstance(periph, dict) and periph.get('name')
                    ],
                    'dependencies': raw_dev.get('dependencies', {}) if isinstance(raw_dev, dict) else {},
                    'raw': raw_dev,
                    'result': result,
                    'parse_func': parse_func,
                })
            except ValueError as e:
                raise
            except Exception as e:
                self.logger.error(f"❌ Device parser error for '{d.name}': {e}")
                raise

        self.logger.info(f'✅ Successfully validated {len(device_structs)} devices')
        self.logger.debug('   Writing device configuration files...')
        # Generate files directly to components/gen_bmgr_codes
        project_root = self.get_effective_project_dir()
        gen_bmgr_codes_dir = self._get_gen_bmgr_codes_dir(project_root)
        os.makedirs(gen_bmgr_codes_dir, exist_ok=True)
        # Generate custom device structure header first
        self.write_device_custom_h(device_structs, devices, out_path=os.path.join(gen_bmgr_codes_dir, 'gen_board_device_custom.h'))
        self.write_device_c(device_structs, devices, device_parsers, extra_configs, extra_includes, out_path=os.path.join(gen_bmgr_codes_dir, 'gen_board_device_config.c'))
        self.write_device_handles(devices, device_parsers, out_path=os.path.join(gen_bmgr_codes_dir, 'gen_board_device_handles.c'))

        # Validate that extra_dev configurations are used in device configs
        self.logger.debug('   ✅ Validating extra device configurations...')
        self.dependency_manager.validate_extra_dev_usage(extra_configs, devices)

        return device_types, device_subtypes

    def _get_gen_bmgr_codes_dir(self, project_root: str) -> str:
        """Get the gen_bmgr_codes directory path"""
        return os.path.join(project_root, 'components', 'gen_bmgr_codes')

    def write_board_metadata(self, board_name: str, chip_name: str, out_path: str) -> dict:
        """Write unified board metadata YAML."""
        return self.metadata_generator.write_metadata_file(
            output_path=out_path,
            board_name=board_name,
            chip_name=chip_name,
            device_artifacts=self._last_device_metadata_artifacts,
            peripheral_artifacts=self._last_peripheral_metadata_artifacts,
        )

    def _delete_files_by_extension(self, gen_bmgr_codes_dir: str, extensions: Optional[tuple]) -> List[str]:
        """Delete files with specified extensions in gen_bmgr_codes directory

        Args:
            gen_bmgr_codes_dir: Path to gen_bmgr_codes directory
            extensions: Tuple of file extensions to delete (e.g., ('.c', '.h')). Use None to delete all files.

        Returns:
            List of deleted filenames
        """
        deleted_files = []
        if not os.path.exists(gen_bmgr_codes_dir):
            return deleted_files

        for filename in os.listdir(gen_bmgr_codes_dir):
            file_path = os.path.join(gen_bmgr_codes_dir, filename)
            if os.path.isfile(file_path):
                # If extensions is None, delete all files; otherwise check extension
                if extensions is None or filename.endswith(extensions):
                    os.remove(file_path)
                    deleted_files.append(filename)
                    self.logger.debug(f'   Deleted: {filename}')

        return deleted_files

    def _delete_all_directories(self, gen_bmgr_codes_dir: str) -> List[str]:
        """Delete all subdirectories in gen_bmgr_codes directory

        Args:
            gen_bmgr_codes_dir: Path to gen_bmgr_codes directory

        Returns:
            List of deleted directory names
        """
        deleted_dirs = []
        if not os.path.exists(gen_bmgr_codes_dir):
            return deleted_dirs

        for filename in os.listdir(gen_bmgr_codes_dir):
            file_path = os.path.join(gen_bmgr_codes_dir, filename)
            if os.path.isdir(file_path):
                shutil.rmtree(file_path)
                deleted_dirs.append(filename)
                self.logger.debug(f'   Removed directory: {filename}')

        return deleted_dirs

    def _reset_cmakelists(self, gen_bmgr_codes_dir: str) -> bool:
        """Reset CMakeLists.txt to default state (SRC_DIRS and INCLUDE_DIRS set to ".")"""
        cmakelists_path = os.path.join(gen_bmgr_codes_dir, 'CMakeLists.txt')
        if os.path.exists(cmakelists_path):
            cmakelists_content = """idf_component_register(
    SRC_DIRS "."
    INCLUDE_DIRS "."
    REQUIRES
)

# This is equivalent to adding WHOLE_ARCHIVE option to the idf_component_register call above:
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
"""
            with open(cmakelists_path, 'w', encoding='utf-8') as f:
                f.write(cmakelists_content)
            self.logger.info('✅ Cleared CMakeLists.txt (SRC_DIRS and INCLUDE_DIRS set to ".")')
            return True
        else:
            self.logger.debug('   CMakeLists.txt does not exist, skipping')
            return False

    def _reset_idf_component_yml(self, gen_bmgr_codes_dir: str) -> bool:
        """Reset idf_component.yml to default state (dependencies set to empty)"""
        idf_component_path = os.path.join(gen_bmgr_codes_dir, 'idf_component.yml')
        if os.path.exists(idf_component_path):
            idf_component_content = {
                'dependencies': {}
            }
            with open(idf_component_path, 'w', encoding='utf-8') as f:
                yaml.dump(idf_component_content, f, default_flow_style=False, sort_keys=False)
            self.logger.info('✅ Cleared idf_component.yml (dependencies set to empty)')
            return True
        else:
            self.logger.debug('   idf_component.yml does not exist, skipping')
            return False

    def clear_gen_bmgr_codes_directory(self, project_root: str) -> bool:
        """Clear all files and directories in the gen_bmgr_codes directory before generating new ones"""
        try:
            gen_bmgr_codes_dir = self._get_gen_bmgr_codes_dir(project_root)

            if os.path.exists(gen_bmgr_codes_dir):
                self.logger.debug(f'   Clearing gen_bmgr_codes directory: {gen_bmgr_codes_dir}')

                # Remove all files using shared method
                deleted_files = self._delete_files_by_extension(gen_bmgr_codes_dir, None)
                # Remove all directories using shared method
                deleted_dirs = self._delete_all_directories(gen_bmgr_codes_dir)

                if deleted_files or deleted_dirs:
                    self.logger.info(f'✅ gen_bmgr_codes directory cleared successfully ({len(deleted_files)} files, {len(deleted_dirs)} directories)')
                else:
                    self.logger.info('✅ gen_bmgr_codes directory cleared successfully (already empty)')
            else:
                self.logger.debug(f'gen_bmgr_codes directory does not exist: {gen_bmgr_codes_dir}')

            return True
        except Exception as e:
            self.logger.error(f'❌ Error clearing gen_bmgr_codes directory: {e}')
            return False

    def clear_generated_files(self, project_root: str) -> bool:
        """Clear generated .c and .h files, and reset CMakeLists.txt and idf_component.yml"""
        try:
            gen_bmgr_codes_dir = self._get_gen_bmgr_codes_dir(project_root)

            if not os.path.exists(gen_bmgr_codes_dir):
                self.logger.warning(f'⚠️  gen_bmgr_codes directory does not exist: {gen_bmgr_codes_dir}')
                return True

            self.logger.info(f'⚙️  Clearing generated files in: {gen_bmgr_codes_dir}')

            # 1. Delete all .c and .h files using shared method
            deleted_files = self._delete_files_by_extension(gen_bmgr_codes_dir, ('.c', '.h'))

            if deleted_files:
                self.logger.info(f'✅ Deleted {len(deleted_files)} generated files: {", ".join(deleted_files)}')
            else:
                self.logger.info('✅ No .c or .h files found to delete')

            # 2. Reset CMakeLists.txt using shared method
            self._reset_cmakelists(gen_bmgr_codes_dir)

            # 3. Reset idf_component.yml using shared method
            self._reset_idf_component_yml(gen_bmgr_codes_dir)

            # 4. Delete board_manager.defaults file
            self.sdkconfig_manager.clear_board_manager_defaults(project_root)

            self.logger.info('✅ Generated files cleared successfully')
            return True

        except Exception as e:
            self.logger.error(f'❌ Error clearing generated files: {e}')
            import traceback
            traceback.print_exc()
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

    def get_current_board_name(self, project_root: str) -> Optional[str]:
        """Get the current board name from gen_board_info.c"""
        try:
            gen_bmgr_codes_dir = self._get_gen_bmgr_codes_dir(project_root)
            board_info_c = os.path.join(gen_bmgr_codes_dir, 'gen_board_info.c')

            if not os.path.exists(board_info_c):
                return None

            with open(board_info_c, 'r', encoding='utf-8') as f:
                content = f.read()

            # Search for .name = "board_name"
            import re
            match = re.search(r'\.name\s*=\s*"([^"]+)"', content)
            if match:
                return match.group(1)
        except Exception as e:
            self.logger.warning(f'⚠️  Failed to read current board name: {e}')

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

    def run(self, args, cached_boards=None):
        """Run the complete 8-step board configuration generation process

        Args:
            args: Command line arguments
            cached_boards: Optional dict of pre-scanned boards to avoid re-scanning
        """
        self.logger.info('=== Board Manager Configuration Generator ===')

        # Initialize device and peripheral types sets
        device_types = set()
        peripheral_types = set()

        # 1. Scan all board directories and clean environment
        # NOTE: Reordered to determine selected board before cleaning environment
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
        project_root = self.resolve_project_dir()

        # Store project_root for use in other methods
        self.project_root = project_root

        if project_root:
            components_dir = os.path.join(project_root, 'components')
            self.logger.debug(f'      • Components boards: {components_dir}')
        else:
            self.logger.debug(f'      • Components boards: (project root not found)')

        # Use cached boards if available, otherwise scan
        if cached_boards is not None:
            self.logger.debug('   Using cached board list (skipping re-scan)')
            all_boards = cached_boards
        else:
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
            # Check for board name consistency (moved from scan phase)
            self.config_generator.check_board_name_consistency(board_path, selected_board)
        else:
            self.logger.warning(f"⚠️  Warning: Selected board '{selected_board}' not found in scanned boards")

        current_board = None
        sdkconfig_preserved_same_board = False
        sdkconfig_path = os.path.join(project_root, 'sdkconfig') if project_root else str(Path.cwd() / 'sdkconfig')

        if project_root:
            # Capture current board name BEFORE clearing the directory
            current_board = self.get_current_board_name(project_root)

            # Clean environment before generating new configuration
            self.logger.debug('   Cleaning environment...')

            # 1.1 Remove CMakeCache.txt in build directory
            try:
                cmake_cache_path = os.path.join(project_root, 'build', 'CMakeCache.txt')
                if os.path.exists(cmake_cache_path):
                    os.remove(cmake_cache_path)
                    self.logger.debug(f'   Removed build cache: {cmake_cache_path}')
            except Exception as e:
                self.logger.error(f'❌ Error: Failed to remove CMakeCache.txt: {e}')
                return False

            # 1.2 Clear gen_bmgr_codes directory before generating new files
            if not self.clear_gen_bmgr_codes_directory(project_root):
                self.logger.error('❌ Error: Failed to clear gen_bmgr_codes directory!')
                return False

            # 1.3 Backup and remove sdkconfig to prevent configuration pollution
            # Skip for --kconfig-only as it only generates Kconfig menu without board switching
            if not args.kconfig_only:
                try:
                    # Check if we should preserve sdkconfig (if board hasn't changed)
                    should_clean_sdkconfig = True
                    if os.path.exists(sdkconfig_path):
                        if current_board and current_board == selected_board:
                            self.logger.info(f'ℹ️  Board unchanged ({selected_board}), preserving sdkconfig')
                            should_clean_sdkconfig = False
                            sdkconfig_preserved_same_board = True

                        if should_clean_sdkconfig:
                            # Backup next to generated board artifacts (fixed name, overwritten each time)
                            gen_bmgr_codes_dir = self._get_gen_bmgr_codes_dir(project_root)
                            os.makedirs(gen_bmgr_codes_dir, exist_ok=True)
                            backup_path = os.path.join(gen_bmgr_codes_dir, 'sdkconfig.bmgr_board.old')
                            shutil.copy2(sdkconfig_path, backup_path)
                            self.logger.debug(f'   Backed up sdkconfig to: {backup_path}')
                            # Remove original sdkconfig
                            os.remove(sdkconfig_path)
                            self.logger.info(f'⚠️  Removed sdkconfig by backup to {backup_path} to prevent configuration pollution')
                except Exception as e:
                    self.logger.error(f'❌ Error: Failed to backup/remove sdkconfig: {e}')
                    return False

        # 3. Find configuration files for selected board
        self.logger.info('⚙️  Step 3/8: Finding board configuration files...')
        periph_yaml_path, dev_yaml_path = self.config_generator.find_board_config_files(selected_board, all_boards)

        if not periph_yaml_path or not dev_yaml_path:
            self.logger.error('❌ Error: Could not find configuration files for selected board')
            if not periph_yaml_path:
                self.logger.error(f"   Missing: peripherals.yaml for board '{selected_board}'")
            if not dev_yaml_path:
                self.logger.error(f"   Missing: board_devices.yaml for board '{selected_board}'")
            return False

        self.logger.debug(f'✅ Configuration files found:')
        self.logger.debug(f'      • Peripherals: {periph_yaml_path}')
        self.logger.debug(f'      • Devices: {dev_yaml_path}')

        # 4~5. Process peripherals and devices based on arguments
        peripherals_dict = None
        periph_name_map = None
        device_dependencies = {}  # Initialize device_dependencies
        peripheral_types = set()
        device_types = set()
        device_subtypes = {}  # Initialize device_subtypes

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
                self.logger.debug(f'✅ Peripheral processing completed: {len(peripheral_types)} types found')
                self.logger.info(f'✅ Successfully validated {len(peripherals_dict)} peripherals')

            except BoardConfigYamlError as e:
                self.logger.error(f'❌ Board YAML error (peripherals), aborting: {e}')
                return False
            except Exception as e:
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
                except BoardConfigYamlError as e:
                    self.logger.error(f'❌ Board YAML error (peripherals), aborting: {e}')
                    return False
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
            try:
                device_dependencies = self.dependency_manager.extract_device_dependencies(dev_yaml_path)
            except BoardConfigYamlError as e:
                self.logger.error(f'❌ Board YAML error (devices), aborting: {e}')
                return False
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
                device_types, device_subtypes = self.process_devices(dev_yaml_path, peripherals_dict, periph_name_map, board_path, extra_configs, extra_includes)
                self.logger.debug(f'✅ Device processing completed: {len(device_types)} types found')
            except BoardConfigYamlError as e:
                self.logger.error(f'❌ Board YAML error (devices), aborting: {e}')
                return False
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

        # Generate fully static Kconfig.in (internal boards only, no default y)
        if not self.generate_kconfig(all_boards):
            self.logger.error('❌ Error: Kconfig generation failed!')
            return False

        # Generate project-side Kconfig definitions for the selected board.
        if not self.generate_selected_board_kconfig_projbuild(selected_board, project_root):
            self.logger.error('❌ Error: Selected-board Kconfig.projbuild generation failed!')
            return False

        self.logger.debug('✅ Kconfig generation completed successfully')

        # If only Kconfig generation is requested, exit early
        if args.kconfig_only:
            self.logger.info('ℹ️  Only Kconfig generation requested, skipping board configuration generation')
            return True

        # Some generation outputs can still be produced outside a full ESP-IDF project.
        # In that case, keep using the current working directory as the artifact root
        # instead of aborting later when project_root is None.
        project_artifact_root = project_root
        if project_artifact_root is None:
            project_artifact_root = self.get_effective_project_dir()
            self.logger.warning(
                f'⚠️  Project root not found, using current directory for generated project artifacts: {project_artifact_root}'
            )

        # 7. Update sdkconfig based on board device and peripheral types
        self.logger.info('⚙️  Step 7/8: Updating SDK configuration...')

        # Get chip name from board_info.yaml (needed for both sdkconfig and sdkconfig.defaults)
        board_path = all_boards.get(selected_board)
        if not board_path:
            self.logger.error(f'❌ Board path not found for {selected_board}')
            return False

        chip_name = self.get_chip_name_from_board_path(board_path)
        if not chip_name:
            self.logger.error(f'❌ Chip name not found in board_info.yaml for {selected_board}')
            return False

        # Keep current flow unchanged by limiting this check to:
        # - existing sdkconfig
        # - same-board rerun path where sdkconfig is preserved
        # - non-kconfig-only mode
        if (sdkconfig_preserved_same_board and
                os.path.exists(sdkconfig_path) and
                not getattr(args, 'skip_sdkconfig_check', False)):
            self.logger.info('   Checking CONFIG_IDF_TARGET matches board chip...')
            ok_tgt, tgt_err = self.sdkconfig_manager.check_idf_target_matches_sdkconfig(
                sdkconfig_path=sdkconfig_path,
                expected_chip=chip_name,
            )
            if not ok_tgt:
                self.logger.error(f'❌ {tgt_err}')
                return False

            self.logger.info('   Checking sdkconfig consistency for board-manager symbols...')
            consistency_result = self.sdkconfig_manager.ensure_sdkconfig_consistency(
                sdkconfig_path=sdkconfig_path,
                selected_board=selected_board,
                device_types=device_types,
                peripheral_types=peripheral_types,
                device_subtypes=device_subtypes,
                auto_fix=True,
                project_path=project_root,
            )
            if not consistency_result.get('ok', False):
                self.logger.error('❌ sdkconfig consistency check failed')
                return False
            if consistency_result.get('fixed', False):
                fixed_items = consistency_result.get('fixed_items', [])
                self.logger.info(f'✅ sdkconfig inconsistencies auto-fixed ({len(fixed_items)} items)')
                self.logger.info('   Tip: run "idf.py reconfigure" before build to refresh generated config files')
        elif getattr(args, 'skip_sdkconfig_check', False):
            self.logger.info('   ⏭️  sdkconfig consistency check skipped by command line option')

        # # Check if auto-config is disabled via sdkconfig
        # auto_config_disabled_via_sdkconfig = False
        # if not args.disable_sdkconfig_auto_update:
        #     # Only check sdkconfig if not explicitly disabled via command line
        #     auto_config_disabled_via_sdkconfig = self.sdkconfig_manager.is_auto_config_disabled_in_sdkconfig()
        #     if auto_config_disabled_via_sdkconfig:
        #         self.logger.info('   ⏭️  Board-based SDK configuration update... (DISABLED via sdkconfig)')

        # if args.disable_sdkconfig_auto_update or auto_config_disabled_via_sdkconfig:
        #     # Disabled by user via command line or sdkconfig
        #     if args.disable_sdkconfig_auto_update:
        #         self.logger.info('   ⏭️  Board-based SDK configuration update... (DISABLED via command line)')
        #     # auto_config_disabled_via_sdkconfig case already logged above
        # elif args.sdkconfig_only:
        #     # Only check without enabling
        #     self.logger.info('   Checking sdkconfig features...')
        #     self.sdkconfig_manager.update_sdkconfig_from_board_types(
        #         device_types=device_types,
        #         peripheral_types=peripheral_types,
        #         sdkconfig_path=None,
        #         enable=False
        #     )
        # else:
        #     # Default behavior: update sdkconfig based on board device/peripheral types
        #     # Note: Board selection and chip target are managed by generate_board_manager_defaults()
        #     # which writes to board_manager.defaults. ESP-IDF will use those during build.
        #     self.logger.debug('   Updating sdkconfig based on board types...')

        #     result = self.sdkconfig_manager.update_sdkconfig_from_board_types(
        #         device_types=device_types,
        #         peripheral_types=peripheral_types,
        #         sdkconfig_path=str(Path.cwd()/'sdkconfig'),
        #         enable=True
        #     )
        #     if result['enabled']:
        #         self.logger.info(f"✅ Updated {len(result['enabled'])} sdkconfig features")

        # Apply board-specific sdkconfig defaults from sdkconfig.defaults.board
        # Write to board_manager.defaults in project root instead of modifying sdkconfig.defaults
        # This avoids conflicts with user's sdkconfig.defaults file
        # Also adds CONFIG_IDF_TARGET, CONFIG_ESP_BOARD_XXX and CONFIG_ESP_BOARD_NAME
        board_manager_defaults_file = str(Path(project_artifact_root) / 'components' / 'gen_bmgr_codes' / 'board_manager.defaults')

        board_defaults_result = self.sdkconfig_manager.generate_board_manager_defaults(
            board_path=board_path,
            project_path=project_artifact_root,
            board_name=selected_board,
            chip_name=chip_name,  # chip_name from board_info.yaml
            output_file=board_manager_defaults_file,  # Write to project_root/board_manager.defaults
            device_types=device_types,
            peripheral_types=peripheral_types,
            device_subtypes=device_subtypes,
        )
        if board_defaults_result['added']:
            self.logger.info(f'✅ Generated board-specific defaults to {board_manager_defaults_file}')
            self.logger.debug(f'   The file will be automatically applied when compilation occurs')

        # 8. Write board information and setup components/gen_bmgr_codes
        self.logger.info('⚙️  Step 8/8: Writing board information and setting up components...')

        # Write board info directly to components/gen_bmgr_codes
        gen_bmgr_codes_dir = self._get_gen_bmgr_codes_dir(project_artifact_root)
        os.makedirs(gen_bmgr_codes_dir, exist_ok=True)
        if selected_board in all_boards:
            self.write_board_info(all_boards[selected_board], out_path=os.path.join(gen_bmgr_codes_dir, 'gen_board_info.c'))
        else:
            self.logger.warning(f'⚠️  Cannot write board info: board "{selected_board}" not found in all_boards')

        metadata_path = os.path.join(gen_bmgr_codes_dir, 'gen_board_metadata.yaml')
        self.write_board_metadata(
            board_name=selected_board,
            chip_name=chip_name,
            out_path=metadata_path,
        )

        # Setup components/gen_bmgr_codes directory and build system
        if not self.setup_gen_bmgr_codes_component(project_artifact_root, board_path, device_dependencies, selected_board):
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
                project_root = self.get_effective_project_dir()

            # 1. Create components/gen_bmgr_codes directory
            gen_bmgr_codes_dir = self._get_gen_bmgr_codes_dir(project_root)
            components_dir = os.path.dirname(gen_bmgr_codes_dir)

            # Create components directory if it doesn't exist
            if not os.path.exists(components_dir):
                os.makedirs(components_dir)
                self.logger.info(f'      Created components directory: {components_dir}')

            # Create gen_bmgr_codes directory
            if not os.path.exists(gen_bmgr_codes_dir):
                os.makedirs(gen_bmgr_codes_dir)
                self.logger.info(f'      Created gen_bmgr_codes directory: {gen_bmgr_codes_dir}')

            # Create .gitignore file to ignore all generated files
            gitignore_path = os.path.join(gen_bmgr_codes_dir, '.gitignore')
            try:
                with open(gitignore_path, 'w', encoding='utf-8') as f:
                    f.write('*\n')
                self.logger.debug(f'      Created .gitignore in: {gen_bmgr_codes_dir}')
            except Exception as e:
                self.logger.warning(f'⚠️  Failed to create .gitignore: {e}')

            # 2. Create CMakeLists.txt with board source paths
            # Get board source files and create SRC_DIRS list
            board_src_dirs = []
            if board_path and os.path.exists(board_path):
                # Calculate relative path from gen_bmgr_codes to board directory
                board_relative_path = os.path.relpath(board_path, gen_bmgr_codes_dir)
                board_relative_path = Path(board_relative_path).as_posix()
                board_path = Path(board_path).as_posix()
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
            self.logger.debug(f'✅ Board source files will be compiled from: {board_path}')
            self.logger.info(f'✅ Generated files directly to components/gen_bmgr_codes completed successfully!')

            return True

        except Exception as e:
            self.logger.error(f'❌ Error setting up gen_bmgr_codes component: {e}')
            return False

def resolve_board_name_or_index(board_input: str, all_boards: dict, generator, board_customer_path: Optional[str] = None) -> Optional[str]:
    """Resolve board name from input (board name or index number)

    Args:
        board_input: Board name or index number (as string)
        all_boards: Dictionary of all available boards
        generator: BoardConfigGenerator instance (for boards_dir access)
        board_customer_path: Optional customer boards path

    Returns:
        Board name or None if not found
    """
    # Check if input is a number
    if board_input.isdigit():
        board_idx = int(board_input)
        if board_idx < 1 or board_idx > len(all_boards):
            return None

        # Group boards by source (same as list-boards display)
        main_boards = {}
        customer_boards = {}
        component_boards = {}

        resolved_customer = str(Path(board_customer_path).resolve()) if board_customer_path else None
        for board_name, board_path in all_boards.items():
            board_path_obj = Path(board_path)
            if board_path_obj.parent == generator.boards_dir:
                main_boards[board_name] = board_path
            elif resolved_customer and str(board_path_obj.resolve()).startswith(resolved_customer):
                customer_boards[board_name] = board_path
            else:
                component_boards[board_name] = board_path

        # Create ordered list (same order as display)
        ordered_boards = []
        ordered_boards.extend(sorted(main_boards.keys()))
        ordered_boards.extend(sorted(customer_boards.keys()))
        ordered_boards.extend(sorted(component_boards.keys()))

        return ordered_boards[board_idx - 1]
    else:
        # Direct board name
        return board_input if board_input in all_boards else None

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
    python gen_bmgr_config_codes.py esp_vocat_board_v1_0           # Specify board directly (auto-sets CONFIG_IDF_TARGET)
    python gen_bmgr_config_codes.py 1                                 # Specify board by index number
    python gen_bmgr_config_codes.py -b esp_vocat_board_v1_0        # Specify board using -b parameter (auto-sets CONFIG_IDF_TARGET)
    python gen_bmgr_config_codes.py -b 1                              # Specify board by index number using -b
    python gen_bmgr_config_codes.py -b my_board -c /custom/boards     # Both -b and -c options
    python gen_bmgr_config_codes.py --peripherals-only                # Only process peripherals
    python gen_bmgr_config_codes.py --devices-only                    # Only process devices
    python gen_bmgr_config_codes.py --kconfig-only                    # Generate Kconfig menu system (default enabled)
    python gen_bmgr_config_codes.py --log-level DEBUG                 # Set log level to DEBUG
    python gen_bmgr_config_codes.py -x                                # Clear generated files and reset configs
    python gen_bmgr_config_codes.py --clean                           # Clear generated files and reset configs (same as -x)
            """
    )

    parser.add_argument(
        'board',
        nargs='?',
        help='Board name or index number (bypasses sdkconfig reading)'
    )

    parser.add_argument(
        '-b', '--board',
        dest='board_name',
        help='Specify board name or index number (bypasses sdkconfig reading, overrides positional argument)'
    )

    parser.add_argument(
        '-c', '--customer-path',
        dest='board_customer_path',
        help='Path to customer boards directory or single board directory (use "NONE" to skip)'
    )

    parser.add_argument(
        '--project-dir',
        dest='project_dir',
        help='Project root directory used to resolve relative paths and generated outputs'
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
        '--kconfig-only',
        action='store_true',
        help='Only generate Kconfig menu without board switching (skips sdkconfig deletion and board code generation)'
    )

    parser.add_argument(
        '--skip-sdkconfig-check',
        dest='skip_sdkconfig_check',
        action='store_true',
        help='Skip CONFIG_IDF_TARGET check and board-manager ESP_BOARD_* sdkconfig consistency when sdkconfig is preserved',
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

    parser.add_argument(
        '-x', '--clean',
        action='store_true',
        help='Clean generated .c and .h files, and reset CMakeLists.txt and idf_component.yml'
    )

    args = parser.parse_args()

    # Merge positional argument with -b parameter (priority: -b > positional)
    if args.board_name is None and args.board:
        args.board_name = args.board

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

    project_dir = resolve_project_root_dir(
        explicit_project_dir=args.project_dir,
        start_dir=Path(os.getcwd()),
    )
    if args.project_dir:
        try:
            project_dir = ensure_existing_directory(project_dir, 'Project directory')
        except ValueError as e:
            print(f'❌ {e}')
            sys.exit(1)
    args.project_dir = project_dir

    if args.board_customer_path and args.board_customer_path != 'NONE':
        args.board_customer_path = resolve_path_from_base(args.board_customer_path, project_dir)

    # Create generator and run
    script_dir = Path(__file__).parent
    generator = BoardConfigGenerator(script_dir, project_dir=project_dir)

    # Handle clean option
    if args.clean:
        print('ESP Board Manager - Clean Generated Files')
        print('=' * 60)

        try:
            project_root = generator.resolve_project_dir()

            if not project_root:
                generator.logger.error('❌ Project root not found! Please run this command from a project directory.')
                sys.exit(1)

            success = generator.clear_generated_files(project_root)
            if not success:
                generator.logger.error('❌ Failed to clean generated files!')
                sys.exit(1)

            generator.logger.info('✅ Clean operation completed successfully!')
            return

        except Exception as e:
            generator.logger.error(f'Error cleaning generated files: {e}')
            import traceback
            traceback.print_exc()
            sys.exit(1)

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

                resolved_customer = str(Path(args.board_customer_path).resolve()) if args.board_customer_path else None
                for board_name, board_path in all_boards.items():
                    board_path_obj = Path(board_path)
                    if board_path_obj.parent == generator.boards_dir:
                        main_boards[board_name] = board_path
                    elif resolved_customer and str(board_path_obj.resolve()).startswith(resolved_customer):
                        customer_boards[board_name] = board_path
                    else:
                        component_boards[board_name] = board_path

                # Create ordered list for numbering
                board_idx = 1

                # Display main boards
                if main_boards:
                    generator.logger.info('Main Boards:')
                    for board_name in sorted(main_boards.keys()):
                        generator.logger.info(f'  [{board_idx}] {board_name}')
                        board_idx += 1
                    print()

                # Display customer boards
                if customer_boards:
                    generator.logger.info('Customer Boards:')
                    for board_name in sorted(customer_boards.keys()):
                        generator.logger.info(f'  [{board_idx}] {board_name}')
                        board_idx += 1
                    print()

                # Display component boards
                if component_boards:
                    generator.logger.info('Component Boards:')
                    for board_name in sorted(component_boards.keys()):
                        generator.logger.info(f'  [{board_idx}] {board_name}')
                        board_idx += 1
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

    # Resolve board name from name or index
    cached_boards = None
    if args.board_name:
        all_boards = generator.config_generator.scan_board_directories(args.board_customer_path)
        cached_boards = all_boards  # Cache for reuse in run()
        resolved_board = resolve_board_name_or_index(args.board_name, all_boards, generator, args.board_customer_path)
        if resolved_board is None:
            if args.board_name.isdigit():
                generator.logger.error(f'❌ Board index {args.board_name} is out of range (1-{len(all_boards)})')
            else:
                generator.logger.error(f'❌ Board "{args.board_name}" not found')
                generator.logger.info(f'Available boards: {sorted(all_boards.keys())}')
            sys.exit(1)
        args.board_name = resolved_board
        generator.logger.info(f'ℹ️  Resolved board: {resolved_board}')

    try:
        success = generator.run(args, cached_boards=cached_boards)
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
