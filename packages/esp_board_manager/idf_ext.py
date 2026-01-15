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
from create_new_board import BoardCreator
import re


def action_extensions(base_actions: Dict, project_path: str) -> Dict:
    """
    IDF action extension entry point.

    Args:
        base_actions: Dictionary with actions already available for idf.py
        project_path: Working directory, may be defaulted to os.getcwd()

    Returns:
        Dictionary with action extensions for ESP Board Manager
    """

    def board_manager_global_callback(ctx, global_args, tasks):
        """
        Global callback that injects board manager configuration before build actions.

        This function adds board_manager.defaults to SDKCONFIG_DEFAULTS before
        build/menuconfig/reconfigure actions, ensuring board-specific configurations
        are automatically applied by ESP-IDF build system.

        Behavior:
        ---------
        1. Always includes 'sdkconfig.defaults' as the first entry (ESP-IDF standard)
        2. Merges configurations from:
           - Environment variable SDKCONFIG_DEFAULTS
           - CMake cache entry from -D SDKCONFIG_DEFAULTS=xxx
        3. Appends board_manager.defaults to the final list
        4. Updates both:
           - Environment variable (used when no -D parameter)
           - CMake cache entry (used when -D parameter specified, takes precedence)

        Priority in ESP-IDF's project.cmake:
        -------------------------------------
        CMake variable (-D) > Environment variable > Default file (sdkconfig.defaults)

        Therefore, we must update the CMake cache entry when user specifies -D,
        otherwise the environment variable will be completely overridden.

        Example scenarios:
        ------------------
        Scenario 1 - No -D parameter:
          idf.py build
          Result: sdkconfig.defaults;board_manager.defaults

        Scenario 2 - With -D parameter:
          idf.py -D SDKCONFIG_DEFAULTS=custom.defaults build
          Result: sdkconfig.defaults;custom.defaults;board_manager.defaults

        Args:
            ctx: Click context
            global_args: Dictionary of all available global arguments
            tasks: List of Task objects to be executed
        """
        # Actions that require configuration injection
        config_actions = {'build', 'reconfigure', 'menuconfig'}
        # Check if any of the tasks require configuration
        needs_config = any(
            task.name in config_actions or
            (hasattr(task, 'aliases') and task.aliases and
             any(alias in config_actions for alias in task.aliases))
            for task in tasks
        )

        if not needs_config:
            return
        # Get project directory from global_args (more reliable than module-level project_path)
        proj_dir = global_args.get('project_dir', project_path if project_path else os.getcwd())

        # board_manager.defaults file path (project root directory)
        patch_file = os.path.join(proj_dir, 'board_manager.defaults')
        if not os.path.exists(patch_file):
            return

        # Parse existing SDKCONFIG_DEFAULTS from multiple sources
        # Note: sdkconfig.defaults should always be included as ESP-IDF standard
        defaults_list = []
        # Check if sdkconfig.defaults exists
        sdkconfig_defaults = os.path.join(proj_dir, 'sdkconfig.defaults')
        if os.path.exists(sdkconfig_defaults):
            defaults_list.append(os.path.abspath(sdkconfig_defaults))
        # 1. Check environment variable
        env_defaults = os.environ.get('SDKCONFIG_DEFAULTS', '')
        if env_defaults:
            for f in env_defaults.split(';'):
                f = f.strip()
                if f and f not in defaults_list:
                    defaults_list.append(f)
        # 2. Check CMake cache entry from -D SDKCONFIG_DEFAULTS=xxx
        # Note: CMake variable will override environment variable in ESP-IDF's project.cmake
        # So we need to modify the define_cache_entry to include board_manager.defaults
        define_cache_entries = global_args.get('define_cache_entry', [])
        sdkconfig_defaults_entry_index = None
        for i, entry in enumerate(define_cache_entries):
            if entry.startswith('SDKCONFIG_DEFAULTS='):
                sdkconfig_defaults_entry_index = i
                cache_defaults = entry.split('=', 1)[1]
                # Parse semicolon-separated list
                for f in cache_defaults.split(';'):
                    f = f.strip()
                    if f and f not in defaults_list:
                        defaults_list.append(f)
                break

        # Add board_manager.defaults if not already in list
        abs_patch_file = os.path.abspath(patch_file)
        if abs_patch_file not in defaults_list:
            defaults_list.append(abs_patch_file)

        # Update both environment variable and CMake cache entry
        # Environment variable: for when no -D is specified
        os.environ['SDKCONFIG_DEFAULTS'] = ';'.join(defaults_list)

        # CMake cache entry: for when -D is specified (this takes precedence)
        if sdkconfig_defaults_entry_index is not None:
            # Update existing entry
            define_cache_entries[sdkconfig_defaults_entry_index] = f'SDKCONFIG_DEFAULTS={";".join(defaults_list)}'
        else:
            # Add new entry if user didn't specify -D SDKCONFIG_DEFAULTS
            # (environment variable will be used in this case)
            pass
        print(f'[Board Manager] SDKCONFIG_DEFAULTS set to: {";".join(defaults_list)}')

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
        new_board = kwargs.get('new_board', None)

        mock_args = MockArgs(
            list_boards=kwargs.get('list_boards', False),
            board_name=board_name,
            board_customer_path=kwargs.get('customer_path', None),
            peripherals_only=kwargs.get('peripherals_only', False),
            devices_only=kwargs.get('devices_only', False),
            kconfig_only=kwargs.get('kconfig_only', False),
            log_level=kwargs.get('log_level', 'INFO'),
            clean=kwargs.get('clean', False),
            new_board=new_board
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

        # Handle new-board option
        if mock_args.new_board:
            print('ESP Board Manager - Create New Board')
            print('=' * 60)

            try:
                # Parse board name and path from the new_board argument
                # Format can be: "xxx_board" or "xxx_path/xxx_board"
                new_board_arg = mock_args.new_board
                # Parse the argument to extract board_name and create_path
                # Logic from create_board.py main function
                create_path = None
                board_name = new_board_arg

                # Check if argument contains path separator
                # Use Path to handle both Unix and Windows paths
                input_path = Path(new_board_arg)

                # Check if the input looks like a path (has parent directory)
                if input_path.parent != Path('.'):
                    # Input contains path components
                    potential_board_name = input_path.name
                    potential_path = input_path.parent

                    # Validate potential board name format
                    if re.match(r'^[a-z0-9_]+$', potential_board_name):
                        board_name = potential_board_name
                        create_path = potential_path.resolve()
                    else:
                        # Last part is not a valid board name, use full path as create_path
                        create_path = input_path.resolve()
                else:
                    # No path separator, just board name
                    # Default to components directory in current directory
                    create_path = Path.cwd() / 'components'

                    # Create components directory if it doesn't exist
                    if not create_path.exists():
                        try:
                            create_path.mkdir(parents=True, exist_ok=True)
                            print(f'ℹ️  Created components directory: {create_path}')
                        except Exception as e:
                            print(f'❌ Failed to create components directory {create_path}: {e}')
                            sys.exit(1)

                # Validate board name
                if not re.match(r'^[a-z0-9_]+$', board_name):
                    print(f'❌ Invalid board name: {board_name}')
                    print('Board name must contain only lowercase letters, numbers, and underscores')
                    sys.exit(1)

                # Validate and create path if needed
                if create_path:
                    if not create_path.exists():
                        # Auto-create the directory if it doesn't exist
                        try:
                            create_path.mkdir(parents=True, exist_ok=True)
                            print(f'ℹ️  Created directory: {create_path}')
                        except Exception as e:
                            print(f'❌ Failed to create directory {create_path}: {e}')
                            sys.exit(1)
                    elif not create_path.is_dir():
                        print(f'❌ Create path is not a directory: {create_path}')
                        sys.exit(1)

                print(f'ℹ️  Creating board "{board_name}"')
                if create_path:
                    print(f'ℹ️  Creating in directory: {create_path}')
                else:
                    print(f'ℹ️  Creating in current directory: {os.getcwd()}')

                # Create board creator and run
                creator = BoardCreator(script_dir)
                success = creator.run(board_name, create_path, new_board_arg)

                if not success:
                    print('❌ Board creation failed!')
                    sys.exit(1)

                return

            except Exception as e:
                print(f'❌ Error creating board: {e}')
                import traceback
                traceback.print_exc()
                sys.exit(1)

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
            'names': ['-n', '--new-board'],
            'help': 'Create a new board with specified name or path (e.g., "xxx_board" or "xxx_path/xxx_board")',
            'type': str,
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
            'help': 'Only generate Kconfig menu without board switching (skips sdkconfig deletion and board code generation)',
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
        'global_action_callbacks': [board_manager_global_callback],
        'actions': {
            'gen-bmgr-config': {
                'callback': esp_gen_bmgr_config_callback,
                'options': gen_bmgr_config_options,
                'short_help': 'Generate ESP Board Manager configuration files',
                'help': """Generate ESP Board Manager configuration files for board peripherals and devices.

This command generates C configuration files based on YAML configuration files in the board directories.
It can process peripherals, devices, generate Kconfig menus, and update SDK configuration automatically.

Usage:
    idf.py gen-bmgr-config -b <board_name>        # Specify board by name

    idf.py gen-bmgr-config -b <board_index>       # Specify board by index number

    idf.py gen-bmgr-config --list-boards          # List all available boards

    idf.py gen-bmgr-config -x                     # Clean generated files created by gen-bmgr-config

    idf.py gen-bmgr-config --clean                # Clean generated files created by gen-bmgr-config (same as -x)

    idf.py gen-bmgr-config -n xxx_board           # Create a new board in components directory

    idf.py gen-bmgr-config -n xxx_path/xxx_board  # Create a new board in specified path

Note: When using idf.py, you must use the -b option to specify the board.
For positional argument support, run the script directly:
    python gen_bmgr_config_codes.py <board>

For more examples, see the README.md file.""",
            },
        }
    }

    return esp_actions
