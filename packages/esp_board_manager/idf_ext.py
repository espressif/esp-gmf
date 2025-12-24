#!/usr/bin/env python3
"""
# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.
"""

"""
ESP Board Manager IDF Action Extension

This module provides IDF action integration for the ESP Board Manager configuration generator.
It converts the standalone gen_bmgr_config_codes.py script into an IDF action accessible via:
    idf.py gen-bmgr-config [options]

This extension is automatically discovered by ESP-IDF v6.0+ when placed as 'idf_ext.py' in a component
directory. The extension will be loaded after project configuration with 'idf.py reconfigure' or 'idf.py build'.
"""

import os
import sys
from pathlib import Path
from typing import Dict, List
import logging

# Add current directory to path for imports
current_dir = Path(__file__).parent
sys.path.insert(0, str(current_dir))

from gen_bmgr_config_codes import BoardConfigGenerator, resolve_board_name_or_index
from generators.utils.file_utils import find_project_root as find_project_root_util


def action_extensions(base_actions: Dict, project_path: str) -> Dict:
    """
    IDF action extension entry point.

    Args:
        base_actions: Dictionary with actions already available for idf.py
        project_path: Working directory, may be defaulted to os.getcwd()

    Returns:
        Dictionary with action extensions for ESP Board Manager
    """

    def esp_gen_bmgr_config_callback(target_name: str, ctx, args, **kwargs) -> None:
        """
        Callback function for the gen-bmgr-config command.

        Args:
            target_name: Name of the target/action
            ctx: Click context
            args: PropertyDict with global arguments
            **kwargs: Command arguments from Click
        """
        # Create a mock args object to maintain compatibility with existing code
        class MockArgs:
            def __init__(self, **kwargs):
                for key, value in kwargs.items():
                    setattr(self, key, value)

        # Convert Click args to the format expected by BoardConfigGenerator
        board_name = kwargs.get('board', None)

        mock_args = MockArgs(
            list_boards=kwargs.get('list_boards', False),
            board_name=board_name,
            board_customer_path=kwargs.get('customer_path', None),
            peripherals_only=kwargs.get('peripherals_only', False),
            devices_only=kwargs.get('devices_only', False),
            kconfig_only=kwargs.get('kconfig_only', False),
            log_level=kwargs.get('log_level', 'INFO'),
            clean=kwargs.get('clean', False)
        )

        # Set global log level first
        log_level_map = {
            'DEBUG': logging.DEBUG,
            'INFO': logging.INFO,
            'WARNING': logging.WARNING,
            'ERROR': logging.ERROR
        }
        from generators.utils.logger import set_global_log_level
        set_global_log_level(log_level_map[mock_args.log_level])

        # Determine the project directory to use
        # Priority: 1. User specified --project-dir, 2. IDF detected project_path, 3. Current working directory
        project_dir = None
        if hasattr(args, 'project_dir') and args.project_dir:
            project_dir = args.project_dir
            print(f'ℹ️  Using user-specified project directory: {project_dir}')
        elif project_path:
            project_dir = project_path
            print(f'ℹ️  Using IDF-detected project directory: {project_dir}')
        else:
            project_dir = os.getcwd()
            print(f'ℹ️  Using current working directory: {project_dir}')

        # Create generator and run
        script_dir = current_dir
        generator = BoardConfigGenerator(script_dir)

        # Handle clean option
        if mock_args.clean:
            print('ESP Board Manager - Clean Generated Files')
            print('=' * 60)

            try:
                # Find project root
                project_root = project_dir
                if not project_root:
                    project_root_path = find_project_root_util(Path(os.getcwd()))
                    project_root = str(project_root_path) if project_root_path else None

                if not project_root:
                    print('❌ Project root not found! Please run this command from a project directory.')
                    sys.exit(1)

                success = generator.clear_generated_files(project_root)
                if not success:
                    print('❌ Failed to clean generated files!')
                    sys.exit(1)

                print('✅ Clean operation completed successfully!')
                return

            except Exception as e:
                print(f'❌ Error cleaning generated files: {e}')
                import traceback
                traceback.print_exc()
                sys.exit(1)

        # Handle list-boards option
        if mock_args.list_boards:
            print('ESP Board Manager - Board Listing')
            print('=' * 40)

            try:
                # Scan and display boards
                all_boards = generator.config_generator.scan_board_directories(mock_args.board_customer_path)

                if all_boards:
                    print(f'✅ Found {len(all_boards)} board(s):')
                    print()

                    # Group boards by source
                    main_boards = {}
                    customer_boards = {}
                    component_boards = {}

                    for board_name, board_path in all_boards.items():
                        board_path_obj = Path(board_path)
                        if board_path_obj.parent == generator.boards_dir:
                            main_boards[board_name] = board_path
                        elif mock_args.board_customer_path and board_path.startswith(mock_args.board_customer_path):
                            customer_boards[board_name] = board_path
                        else:
                            component_boards[board_name] = board_path

                    # Create ordered list for numbering
                    board_idx = 1

                    # Display main boards
                    if main_boards:
                        print('ℹ️  Main Boards:')
                        for board_name in sorted(main_boards.keys()):
                            print(f'  [{board_idx}] {board_name}')
                            board_idx += 1
                        print()

                    # Display customer boards
                    if customer_boards:
                        print('ℹ️  Customer Boards:')
                        for board_name in sorted(customer_boards.keys()):
                            print(f'  [{board_idx}] {board_name}')
                            board_idx += 1
                        print()

                    # Display component boards
                    if component_boards:
                        print('ℹ️  Component Boards:')
                        for board_name in sorted(component_boards.keys()):
                            print(f'  [{board_idx}] {board_name}')
                            board_idx += 1
                        print()
                else:
                    print('⚠️  No boards found!')

                print('✅ Board listing completed!')
                return

            except Exception as e:
                print(f'❌ Error listing boards: {e}')
                import traceback
                traceback.print_exc()
                sys.exit(1)

        # Resolve board name from name or index
        cached_boards = None
        if mock_args.board_name:
            all_boards = generator.config_generator.scan_board_directories(mock_args.board_customer_path)
            cached_boards = all_boards  # Cache for reuse in run()
            resolved_board = resolve_board_name_or_index(mock_args.board_name, all_boards, generator, mock_args.board_customer_path)
            if resolved_board is None:
                if mock_args.board_name.isdigit():
                    print(f'❌ Board index {mock_args.board_name} is out of range (1-{len(all_boards)})')
                else:
                    print(f'❌ Board "{mock_args.board_name}" not found')
                    print(f'Available boards: {sorted(all_boards.keys())}')
                sys.exit(1)
            mock_args.board_name = resolved_board
            print(f'ℹ️  Resolved board: {resolved_board}')

        # Set the working directory for the generator to use the correct project context
        try:
            if project_dir and project_dir != os.getcwd():
                original_cwd = os.getcwd()
                try:
                    os.chdir(project_dir)
                    print(f'ℹ️  Changed working directory to: {project_dir}')
                    success = generator.run(mock_args, cached_boards=cached_boards)
                finally:
                    os.chdir(original_cwd)
                    print(f'ℹ️  Restored working directory to: {original_cwd}')
            else:
                success = generator.run(mock_args, cached_boards=cached_boards)

            if not success:
                print('❌ ESP Board Manager configuration generation failed!')
                sys.exit(1)
            print('✅ ESP Board Manager configuration generation completed successfully!')
        except KeyboardInterrupt:
            print('⚠️  Operation cancelled by user')
            sys.exit(1)
        except ValueError as e:
            print(f'❌ {e}')
            sys.exit(1)
        except Exception as e:
            print(f'❌ Unexpected error: {e}')
            import traceback
            traceback.print_exc()
            sys.exit(1)

    # Define command options
    gen_bmgr_config_options = [
        {
            'names': ['-l', '--list-boards'],
            'help': 'List all available boards and exit',
            'is_flag': True,
        },
        {
            'names': ['-b', '--board'],
            'help': 'Specify board name or index number (bypasses sdkconfig reading)',
            'type': str,
        },
        {
            'names': ['-c', '--customer-path'],
            'help': 'Path to customer boards directory (use "NONE" to skip)',
            'type': str,
        },
        {
            'names': ['-x', '--clean'],
            'help': 'Clean generated .c and .h files, and reset CMakeLists.txt and idf_component.yml',
            'is_flag': True,
        },
        {
            'names': ['--peripherals-only'],
            'help': 'Only process peripherals (skip devices)',
            'is_flag': True,
        },
        {
            'names': ['--devices-only'],
            'help': 'Only process devices (skip peripherals)',
            'is_flag': True,
        },
        {
            'names': ['--kconfig-only'],
            'help': 'Generate Kconfig menu system for board and component selection (default enabled)',
            'is_flag': True,
        },
        {
            'names': ['--log-level'],
            'help': 'Set the log level (DEBUG, INFO, WARNING, ERROR)',
            'type': str,
            'default': 'INFO',
        },
    ]

    # Define the actions
    esp_actions = {
        'version': '1',
        'actions': {
            'gen-bmgr-config': {
                'callback': esp_gen_bmgr_config_callback,
                'options': gen_bmgr_config_options,
                'short_help': 'Generate ESP Board Manager configuration files',
                'help': """Generate ESP Board Manager configuration files for board peripherals and devices.

This command generates C configuration files based on YAML configuration files in the board directories.
It can process peripherals, devices, generate Kconfig menus, and update SDK configuration automatically.

Usage:
    idf.py gen-bmgr-config -b <board_name>      # Specify board by name

    idf.py gen-bmgr-config -b <board_index>     # Specify board by index number

    idf.py gen-bmgr-config --list-boards        # List all available boards

    idf.py gen-bmgr-config -x                   # Clean generated files created by gen-bmgr-config

    idf.py gen-bmgr-config --clean              # Clean generated files created by gen-bmgr-config (same as -x)

Note: When using idf.py, you must use the -b option to specify the board.
For positional argument support, run the script directly:
    python gen_bmgr_config_codes.py <board>

For more examples, see the README.md file.""",
            },
        }
    }

    return esp_actions
