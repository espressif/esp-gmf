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
    idf.py bmgr [options]
    idf.py gen-bmgr-config [options]   # legacy compatibility entry

This extension is automatically discovered by ESP-IDF v6.0+ when placed as 'idf_ext.py' in a component
directory. The extension will be loaded after project configuration with 'idf.py reconfigure' or 'idf.py build'.
"""

import os
import sys
import click
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple
import logging

# Add current directory to path for imports
current_dir = Path(__file__).parent
sys.path.insert(0, str(current_dir))

from gen_bmgr_config_codes import BoardConfigGenerator, resolve_board_name_or_index
from generators.utils.file_utils import (
    ensure_existing_directory,
    normalize_project_dir,
    resolve_path_from_base,
)
from generators.sdkconfig_manager import SDKConfigManager
from create_new_board import BoardCreator
import re

try:
    from idf_py_actions.errors import FatalError
except Exception:
    class FatalError(RuntimeError):
        """Fallback error type when idf_py_actions is unavailable."""
        pass


def _env_flag_true(var_name: str) -> bool:
    value = os.environ.get(var_name, '')
    return value.strip().lower() in {'1', 'true', 'yes', 'on'}


# idf.py subcommands that should not run global board-manager preflight (inject / sdkconfig warn).
_GLOBAL_CALLBACK_SKIP_TASKS = frozenset({
    'help',
    'list-targets',
    'python-clean',
    'fullclean',
    'clean',
    'docs',
    'erase-flash',
    'dfu-list',
    'gen-bmgr-config',
    'bmgr',
})


def _validate_mixed_bmgr_tasks(task_names: Tuple[str, ...]) -> None:
    """Disallow ``idf.py bmgr ... gen-bmgr-config`` (or reverse) in one invocation."""
    if 'gen-bmgr-config' in task_names and 'bmgr' in task_names:
        raise FatalError(
            '[Board Manager] Do not mix legacy and new command styles in one invocation. '
            'Use either "idf.py gen-bmgr-config -b <board>" or "idf.py bmgr -b <board>".'
        )


def _global_callback_needs_config(tasks) -> bool:
    return any(
        task.name not in _GLOBAL_CALLBACK_SKIP_TASKS
        and not (
            getattr(task, 'aliases', None)
            and any(alias in _GLOBAL_CALLBACK_SKIP_TASKS for alias in task.aliases)
        )
        for task in tasks
    )


@dataclass
class BmgrCommandArgs:
    list_boards: bool
    board_name: Optional[str]
    board_customer_path: Optional[str]
    peripherals_only: bool
    devices_only: bool
    kconfig_only: bool
    skip_sdkconfig_check: bool
    log_level: str
    clean: bool
    new_board: Optional[str]


BMGR_OPTIONS_REQUIRING_VALUE = frozenset({
    '-b',
    '--board',
    '-c',
    '--customer-path',
    '-n',
    '--new-board',
    '--log-level',
})


def _resolve_project_dir(global_args, fallback_project_path: str) -> str:
    if isinstance(global_args, dict):
        try:
            project_dir = ensure_existing_directory(global_args.get('project_dir'), 'Project directory')
        except ValueError as e:
            raise FatalError(f'[Board Manager] {e}')
        if project_dir:
            return project_dir
    try:
        fallback_project_dir = ensure_existing_directory(fallback_project_path, 'Project directory')
    except ValueError as e:
        raise FatalError(f'[Board Manager] {e}')
    if fallback_project_dir:
        return fallback_project_dir
    return normalize_project_dir(os.getcwd()) or os.getcwd()


def _find_positional_argument_after_command(argv: List[str], command_name: str) -> Optional[str]:
    command_seen = False
    skip_next = False

    for token in argv[1:]:
        if not command_seen:
            if token == command_name:
                command_seen = True
            continue

        if skip_next:
            skip_next = False
            continue

        if token in BMGR_OPTIONS_REQUIRING_VALUE:
            skip_next = True
            continue

        if any(token.startswith(f'{opt}=') for opt in BMGR_OPTIONS_REQUIRING_VALUE if opt.startswith('--')):
            continue

        if token.startswith('-'):
            continue

        return token

    return None


def _parse_bmgr_defaults_symbols(
    defaults_path: str,
) -> Tuple[Optional[str], Set[str], Set[str], Dict[str, Set[str]]]:
    """Parse expected board-manager symbols from board_manager.defaults."""
    selected_board = None
    device_types: Set[str] = set()
    peripheral_types: Set[str] = set()
    device_subtypes: Dict[str, Set[str]] = {}

    line_board = re.compile(r'^CONFIG_ESP_BOARD_([A-Z0-9_]+)=y\s*$')
    line_periph = re.compile(r'^CONFIG_ESP_BOARD_PERIPH_([A-Z0-9_]+)_SUPPORT=y\s*$')
    line_dev_sub = re.compile(r'^CONFIG_ESP_BOARD_DEV_([A-Z0-9_]+)_SUB_([A-Z0-9_]+)_SUPPORT=y\s*$')
    line_dev = re.compile(r'^CONFIG_ESP_BOARD_DEV_([A-Z0-9_]+)_SUPPORT=y\s*$')

    with open(defaults_path, 'r', encoding='utf-8') as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith('#'):
                continue

            m = line_periph.match(line)
            if m:
                peripheral_types.add(m.group(1).lower())
                continue

            m = line_dev_sub.match(line)
            if m:
                dev_type = m.group(1).lower()
                subtype = m.group(2).lower()
                device_types.add(dev_type)
                device_subtypes.setdefault(dev_type, set()).add(subtype)
                continue

            m = line_dev.match(line)
            if m:
                device_types.add(m.group(1).lower())
                continue

            m = line_board.match(line)
            if m:
                board_macro = m.group(1)
                # Exclude *_SUPPORT symbols; this regex runs last as a fallback.
                if not board_macro.endswith('_SUPPORT'):
                    selected_board = board_macro.lower()

    return selected_board, device_types, peripheral_types, device_subtypes


def action_extensions(base_actions: Dict, project_path: str) -> Dict:
    """
    IDF action extension entry point.

    Args:
        base_actions: Dictionary with actions already available for idf.py
        project_path: Working directory, may be defaulted to os.getcwd()

    Returns:
        Dictionary with action extensions for ESP Board Manager
    """
    board_name_cache: Dict[str, Set[str]] = {}

    def _collect_known_board_names(project_dir: str) -> Set[str]:
        cache_key = os.path.abspath(project_dir)
        if cache_key in board_name_cache:
            return board_name_cache[cache_key]

        try:
            from generators import get_config_generator
            cfg_gen = get_config_generator()(current_dir, project_dir=project_dir)
            all_boards = cfg_gen.scan_board_directories()
            board_names = set(all_boards.keys())
        except Exception:
            board_names = set()

        board_name_cache[cache_key] = board_names
        return board_names

    def _get_positional_board_candidate(task_name: Optional[str], project_dir: str) -> Optional[str]:
        if not task_name:
            return None
        if task_name.isdigit():
            return task_name
        if task_name in _collect_known_board_names(project_dir):
            return task_name
        return None

    def board_manager_global_callback(ctx, global_args, tasks):
        """Inject ``board_manager.defaults`` into SDKCONFIG_DEFAULTS when ``sdkconfig`` is absent; otherwise warn-only sdkconfig consistency check."""
        task_names = tuple(t.name for t in tasks)
        _validate_mixed_bmgr_tasks(task_names)
        if not _global_callback_needs_config(tasks):
            return
        proj_dir = _resolve_project_dir(global_args, project_path)
        patch_file = os.path.join(proj_dir, 'components', 'gen_bmgr_codes', 'board_manager.defaults')
        if not os.path.exists(patch_file):
            return

        sdk_file = os.path.join(proj_dir, 'sdkconfig')
        if os.path.exists(sdk_file):
            if _env_flag_true('ESP_BOARD_MANAGER_SKIP_SDKCONFIG_CHECK'):
                print('[Board Manager] sdkconfig consistency check skipped by ESP_BOARD_MANAGER_SKIP_SDKCONFIG_CHECK')
                return

            print('[Board Manager] Checking sdkconfig consistency before action execution...')
            selected_board, device_types, peripheral_types, device_subtypes = _parse_bmgr_defaults_symbols(patch_file)
            if not selected_board:
                print('[Board Manager] Warning: failed to parse selected board from board_manager.defaults.')
                print(
                    '[Board Manager] Please run: idf.py bmgr -b <board> '
                    '(legacy: idf.py gen-bmgr-config -b <board>) to refresh board metadata.'
                )
                return

            sdkconfig_manager = SDKConfigManager(current_dir, project_dir=proj_dir)
            check_result = sdkconfig_manager.ensure_sdkconfig_consistency(
                sdkconfig_path=sdk_file,
                selected_board=selected_board,
                device_types=device_types,
                peripheral_types=peripheral_types,
                device_subtypes=device_subtypes,
                auto_fix=False,
                project_path=proj_dir,
            )
            if not check_result.get('ok', False):
                issues = check_result.get('issues', [])
                print(f'[Board Manager] Detected {len(issues)} sdkconfig inconsistency issue(s):')
                for issue in issues:
                    print(f'[Board Manager]   - {issue}')
                print(
                    f'[Board Manager] Please run: idf.py bmgr -b {selected_board} '
                    f'(legacy: idf.py gen-bmgr-config -b {selected_board})'
                )
                print(
                    '[Board Manager] Or temporarily bypass this check with: '
                    'ESP_BOARD_MANAGER_SKIP_SDKCONFIG_CHECK=1 idf.py <action>'
                )
                print('[Board Manager] Warning: sdkconfig consistency check failed. Build continues without auto-fix in callback.')
            return

        defaults_list = []
        sdkconfig_defaults = os.path.join(proj_dir, 'sdkconfig.defaults')
        if os.path.exists(sdkconfig_defaults):
            defaults_list.append(os.path.abspath(sdkconfig_defaults))
        env_defaults = os.environ.get('SDKCONFIG_DEFAULTS', '')
        if env_defaults:
            for f in env_defaults.split(';'):
                f = f.strip()
                if f and f not in defaults_list:
                    defaults_list.append(f)
        define_cache_entries = global_args.get('define_cache_entry', [])
        sdkconfig_defaults_entry_index = None
        for i, entry in enumerate(define_cache_entries):
            if entry.startswith('SDKCONFIG_DEFAULTS='):
                sdkconfig_defaults_entry_index = i
                cache_defaults = entry.split('=', 1)[1]
                for f in cache_defaults.split(';'):
                    f = f.strip()
                    if f and f not in defaults_list:
                        defaults_list.append(f)
                break

        abs_patch_file = os.path.abspath(patch_file)
        if abs_patch_file not in defaults_list:
            defaults_list.append(abs_patch_file)

        os.environ['SDKCONFIG_DEFAULTS'] = ';'.join(defaults_list)

        if sdkconfig_defaults_entry_index is not None:
            define_cache_entries[sdkconfig_defaults_entry_index] = f'SDKCONFIG_DEFAULTS={";".join(defaults_list)}'
        print(f'[Board Manager] SDKCONFIG_DEFAULTS set to: {";".join(defaults_list)}')

    def _build_bmgr_command_args(kwargs) -> BmgrCommandArgs:
        return BmgrCommandArgs(
            list_boards=kwargs.get('list_boards', False),
            board_name=kwargs.get('board'),
            board_customer_path=kwargs.get('customer_path'),
            peripherals_only=kwargs.get('peripherals_only', False),
            devices_only=kwargs.get('devices_only', False),
            kconfig_only=kwargs.get('kconfig_only', False),
            skip_sdkconfig_check=kwargs.get('skip_sdkconfig_check', False),
            log_level=kwargs.get('log_level', 'INFO'),
            clean=kwargs.get('clean', False),
            new_board=kwargs.get('new_board'),
        )

    def _set_bmgr_log_level(log_level: str) -> None:
        log_level_map = {
            'DEBUG': logging.DEBUG,
            'INFO': logging.INFO,
            'WARNING': logging.WARNING,
            'ERROR': logging.ERROR
        }
        from generators.utils.logger import set_global_log_level
        set_global_log_level(log_level_map[log_level])

    def _resolve_command_project_dir(args) -> str:
        if hasattr(args, 'project_dir') and args.project_dir:
            try:
                project_dir = ensure_existing_directory(args.project_dir, 'Project directory')
            except ValueError as e:
                raise FatalError(f'[Board Manager] {e}')
            print(f'ℹ️  Using user-specified project directory: {project_dir}')
            return project_dir
        try:
            normalized_project_path = ensure_existing_directory(project_path, 'Project directory')
        except ValueError as e:
            raise FatalError(f'[Board Manager] {e}')
        if normalized_project_path:
            project_dir = normalized_project_path
            print(f'ℹ️  Using IDF-detected project directory: {project_dir}')
            return project_dir
        project_dir = normalize_project_dir(os.getcwd()) or os.getcwd()
        print(f'ℹ️  Using current working directory: {project_dir}')
        return project_dir

    def _validate_board_name(board_name: str) -> None:
        if not re.match(r'^[a-z0-9_]+$', board_name):
            raise ValueError(
                f'Invalid board name: {board_name}\n'
                'Board name must contain only lowercase letters, numbers, and underscores'
            )

    def _ensure_directory(path_obj: Path, create_message: str) -> None:
        if not path_obj.exists():
            path_obj.mkdir(parents=True, exist_ok=True)
            print(f'ℹ️  {create_message}: {path_obj}')
            return
        if not path_obj.is_dir():
            raise ValueError(f'Create path is not a directory: {path_obj}')

    def _resolve_new_board_target(new_board_arg: str, project_dir: str) -> Tuple[str, Path]:
        input_path = Path(new_board_arg).expanduser()
        project_root = Path(project_dir)
        if input_path.parent != Path('.'):
            board_name = input_path.name
            create_path = input_path.parent if input_path.is_absolute() else (project_root / input_path.parent)
        else:
            board_name = new_board_arg
            create_path = project_root / 'components'
        _validate_board_name(board_name)
        return board_name, create_path.resolve()

    def _run_new_board_command(script_dir: Path, new_board_arg: str, project_dir: str) -> None:
        print('ESP Board Manager - Create New Board')
        print('=' * 60)

        board_name, create_path = _resolve_new_board_target(new_board_arg, project_dir)
        default_components_dir = (Path(project_dir) / 'components').resolve()
        create_message = 'Created components directory' if create_path == default_components_dir else 'Created directory'
        _ensure_directory(create_path, create_message)

        print(f'ℹ️  Creating board "{board_name}"')
        print(f'ℹ️  Creating in directory: {create_path}')

        creator = BoardCreator(script_dir)
        success = creator.run(board_name, create_path, new_board_arg)
        if not success:
            raise RuntimeError('Board creation failed!')

    def _run_clean_command(generator: BoardConfigGenerator, project_dir: str) -> None:
        print('ESP Board Manager - Clean Generated Files')
        print('=' * 60)

        project_root = normalize_project_dir(project_dir)

        if not project_root:
            raise ValueError('Project root not found! Please run this command from a project directory.')

        if not generator.clear_generated_files(project_root):
            raise RuntimeError('Failed to clean generated files!')

        print('✅ Clean operation completed successfully!')

    def _print_grouped_boards(generator: BoardConfigGenerator, all_boards: Dict[str, str], customer_path: Optional[str]) -> None:
        if all_boards:
            print(f'✅ Found {len(all_boards)} board(s):')
            print()

            main_boards = {}
            customer_boards = {}
            component_boards = {}

            for board_name, board_path in all_boards.items():
                board_path_obj = Path(board_path)
                if board_path_obj.parent == generator.boards_dir:
                    main_boards[board_name] = board_path
                elif customer_path and board_path.startswith(customer_path):
                    customer_boards[board_name] = board_path
                else:
                    component_boards[board_name] = board_path

            board_idx = 1
            if main_boards:
                print('ℹ️  Main Boards:')
                for board_name in sorted(main_boards.keys()):
                    print(f'  [{board_idx}] {board_name}')
                    board_idx += 1
                print()

            if customer_boards:
                print('ℹ️  Customer Boards:')
                for board_name in sorted(customer_boards.keys()):
                    print(f'  [{board_idx}] {board_name}')
                    board_idx += 1
                print()

            if component_boards:
                print('ℹ️  Component Boards:')
                for board_name in sorted(component_boards.keys()):
                    print(f'  [{board_idx}] {board_name}')
                    board_idx += 1
                print()
        else:
            print('⚠️  No boards found!')

    def _run_list_boards_command(generator: BoardConfigGenerator, command_args: BmgrCommandArgs) -> None:
        print('ESP Board Manager - Board Listing')
        print('=' * 40)

        all_boards = generator.config_generator.scan_board_directories(command_args.board_customer_path)
        _print_grouped_boards(generator, all_boards, command_args.board_customer_path)
        print('✅ Board listing completed!')

    def _resolve_requested_board(generator: BoardConfigGenerator, command_args: BmgrCommandArgs) -> Optional[Dict[str, str]]:
        if not command_args.board_name:
            return None

        all_boards = generator.config_generator.scan_board_directories(command_args.board_customer_path)
        resolved_board = resolve_board_name_or_index(
            command_args.board_name,
            all_boards,
            generator,
            command_args.board_customer_path,
        )
        if resolved_board is None:
            if command_args.board_name.isdigit():
                raise ValueError(f'Board index {command_args.board_name} is out of range (1-{len(all_boards)})')
            raise ValueError(f'Board "{command_args.board_name}" not found\nAvailable boards: {sorted(all_boards.keys())}')

        command_args.board_name = resolved_board
        print(f'ℹ️  Resolved board: {resolved_board}')
        return all_boards

    def _run_generator_command(generator: BoardConfigGenerator, command_args: BmgrCommandArgs, project_dir: str) -> None:
        cached_boards = _resolve_requested_board(generator, command_args)

        if project_dir and project_dir != os.getcwd():
            original_cwd = os.getcwd()
            try:
                os.chdir(project_dir)
                print(f'ℹ️  Changed working directory to: {project_dir}')
                success = generator.run(command_args, cached_boards=cached_boards)
            finally:
                os.chdir(original_cwd)
                print(f'ℹ️  Restored working directory to: {original_cwd}')
        else:
            success = generator.run(command_args, cached_boards=cached_boards)

        if not success:
            raise RuntimeError('ESP Board Manager configuration generation failed!')

        print('✅ ESP Board Manager configuration generation completed successfully!')

    def _run_bmgr_gen(target_name: str, ctx, args, **kwargs) -> None:
        """
        Shared callback implementation for board-manager generation commands.

        Args:
            target_name: Name of the target/action
            ctx: Click context
            args: PropertyDict with global arguments
            **kwargs: Command arguments from Click
        """
        command_args = _build_bmgr_command_args(kwargs)

        # If no substantive action is specified, print this subcommand's help (aligned with esp-gmf idf_ext).
        action_args = [
            command_args.list_boards,
            command_args.board_name,
            command_args.new_board,
            command_args.clean,
            command_args.kconfig_only,
            command_args.peripherals_only,
            command_args.devices_only,
        ]
        if not any(action_args):
            try:
                cmd = ctx.command.get_command(ctx, target_name)
                if cmd:
                    with click.Context(cmd, info_name=target_name, parent=ctx) as cmd_ctx:
                        print(cmd.get_help(cmd_ctx))
                    sys.exit(0)
            except Exception as e:
                logging.debug('Failed to get subcommand help for %s: %s', target_name, e)
            print(ctx.get_help())
            sys.exit(0)

        _set_bmgr_log_level(command_args.log_level)
        project_dir = _resolve_command_project_dir(args)
        command_args.board_customer_path = resolve_path_from_base(command_args.board_customer_path, project_dir)

        # Create generator and run
        script_dir = current_dir
        generator = BoardConfigGenerator(script_dir, project_dir=project_dir)

        try:
            if command_args.new_board:
                _run_new_board_command(script_dir, command_args.new_board, project_dir)
                return
            if command_args.clean:
                _run_clean_command(generator, project_dir)
                return
            if command_args.list_boards:
                _run_list_boards_command(generator, command_args)
                return

            _run_generator_command(generator, command_args, project_dir)
        except KeyboardInterrupt:
            print('⚠️  Operation cancelled by user')
            sys.exit(1)
        except ValueError as e:
            print(f'❌ {e}')
            sys.exit(1)
        except RuntimeError as e:
            print(f'❌ {e}')
            sys.exit(1)
        except Exception as e:
            print(f'❌ Unexpected error: {e}')
            import traceback
            traceback.print_exc()
            sys.exit(1)

    def esp_bmgr_command_callback(target_name: str, ctx, args, **kwargs) -> None:
        """Shared entry for ``bmgr`` and ``gen-bmgr-config`` (only ``target_name`` differs for help / argv scan)."""
        project_dir = _resolve_project_dir({}, getattr(args, 'project_dir', project_path if project_path else os.getcwd()))
        positional_token = _find_positional_argument_after_command(list(sys.argv), target_name)
        positional_board = _get_positional_board_candidate(positional_token, project_dir)
        if kwargs.get('board') is None and positional_board is not None:
            raise FatalError(
                f'[Board Manager] Board value "{positional_board}" was passed as a positional argument. '
                'Use "idf.py bmgr -b <board>" or "idf.py gen-bmgr-config -b <board>".'
            )
        _run_bmgr_gen(target_name, ctx, args, **kwargs)

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
            'help': 'Path to custom boards directory (Leave unset to skip)',
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
            'names': ['--skip-sdkconfig-check'],
            'help': (
                'Skip CONFIG_IDF_TARGET and ESP_BOARD_* sdkconfig preflight warnings in idf_ext '
                '(generator step may still enforce its own rules)'
            ),
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
        'global_action_callbacks': [
            board_manager_global_callback,
        ],
        'actions': {
            'bmgr': {
                'callback': esp_bmgr_command_callback,
                'options': gen_bmgr_config_options,
                'short_help': 'Run ESP Board Manager',
                'help': """Run ESP Board Manager.

Usage:
    idf.py bmgr -b <board_name>                   # Specify board by name

    idf.py bmgr -b <board_index>                  # Specify board by index number

    idf.py bmgr -l                                # List all available boards

    idf.py bmgr -x                                # Clean generated files created by Board Manager

    idf.py bmgr -n xxx_board                      # Create a new board in components directory

Notes:When using idf.py, you must use the -b option to specify the board.
For positional argument support, run the script directly:
    python gen_bmgr_config_codes.py <board>

For more examples, see the README.md file.""",
            },
            'gen-bmgr-config': {
                'callback': esp_bmgr_command_callback,
                'options': gen_bmgr_config_options,
                'short_help': 'Generate ESP Board Manager configuration files',
                'help': """Generate ESP Board Manager configuration files for board peripherals and devices.

This command generates C configuration files based on YAML configuration files in the board directories.
It can process peripherals, devices, generate Kconfig menus, and update SDK configuration automatically.

Usage:
    idf.py gen-bmgr-config -b <board_name>        # Specify board by name

    idf.py gen-bmgr-config -b <board_index>       # Specify board by index number

    idf.py gen-bmgr-config -l                     # List all available boards

    idf.py gen-bmgr-config -x                     # Clean generated files created by gen-bmgr-config

    idf.py gen-bmgr-config -n xxx_board           # Create a new board in components directory

Note: When using idf.py, you must use the -b option to specify the board.
For positional argument support, run the script directly:
    python gen_bmgr_config_codes.py <board>

For more examples, see the README.md file.""",
            },
        }
    }

    return esp_actions
