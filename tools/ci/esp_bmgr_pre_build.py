#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Script to batch process applications:
1. Get application list using build_apps.py
2. Execute idf.py set-target for each application
3. Execute idf.py bmgr for each application (with IDF_EXTRA_ACTIONS_PATH set)
"""

import os
import sys
import json
import argparse
import subprocess
import logging
from typing import Dict, List, Optional
from pathlib import Path
from build_apps import (
    get_app_paths,
    APP_TYPE_ALL,
    APP_TYPE_EXAMPLE,
    APP_TYPE_TEST_APPS,
    print_debug,
    print_info,
    print_warning,
    print_error,
)

# Initialize logging for print functions
logging.basicConfig(
    level=logging.INFO,
    format='%(message)s'
)

def get_app_list(
    project_path: str,
    target: str,
    target_dir_type: str = APP_TYPE_ALL,
    no_require_pytest: bool = False
) -> List[str]:
    """
    Get application list using build_apps.py's get_app_paths API.

    Args:
        project_path: Project root path
        target: Target chip (e.g., esp32, esp32s3, etc.)
        target_dir_type: Application directory type (APP_TYPE_ALL/APP_TYPE_EXAMPLE/APP_TYPE_TEST_APPS)
        no_require_pytest: Whether to require pytest files
                         (Note: get_app_paths internally sets no_require_pytest=True)

    Returns:
        List of application paths
    """
    print_info(f'[Step 1] Getting application list (type: {target_dir_type})...')

    # Set environment variable (get_cmake_apps requires PROJECT_PATH)
    # Only set if not exists or value differs to avoid unnecessary overwrite
    current_project_path = os.environ.get('PROJECT_PATH')
    if current_project_path != project_path:
        os.environ['PROJECT_PATH'] = project_path

    try:
        # Directly call get_app_paths API
        # get_app_paths internally sets no_require_pytest=True, so no extra handling needed
        app_list = get_app_paths(
            path=project_path,
            target=target,
            target_dir_type=target_dir_type,
            exclude_apps=None
        )

        if not app_list:
            print_warning('Warning: No applications found')
            return []

        print_info(f'Found {len(app_list)} applications')
        for app in app_list:
            print_info(f'  - {app}')

        return app_list

    except Exception as e:
        print_error(f'Error: Failed to get application list: {e}')
        import traceback
        traceback.print_exc()
        sys.exit(1)


def _find_bmgr_actions_path(app_path: str) -> Optional[str]:
    """
    Locate esp_board_manager's idf_ext.py under the app's managed_components.

    After bmgr moved to the IDF Component Registry, IDF 5.x does not auto-load
    component idf_ext.py unless IDF_EXTRA_ACTIONS_PATH points at the component dir.
    """
    candidates = (
        os.path.join(app_path, 'managed_components', 'espressif__esp_board_manager'),
        os.path.join(app_path, 'managed_components', 'esp_board_manager'),
    )
    for path in candidates:
        if os.path.isfile(os.path.join(path, 'idf_ext.py')):
            return path
    return None


def _idf_env_for_app(app_path: str) -> Optional[Dict[str, str]]:
    """Build subprocess env with IDF_EXTRA_ACTIONS_PATH set to the managed bmgr component."""
    bmgr_path = _find_bmgr_actions_path(app_path)
    if not bmgr_path:
        return None
    env = os.environ.copy()
    env['IDF_EXTRA_ACTIONS_PATH'] = bmgr_path
    return env


def _execute_command_for_app(
    app_path: str,
    cmd: List[str],
    dry_run: bool = False,
    use_warning_for_failure: bool = False,
    env: Optional[Dict[str, str]] = None,
) -> None:
    """
    Execute a command for a single application.

    Args:
        app_path: Application path
        cmd: Command to execute (as list of strings)
        dry_run: Whether to simulate execution only
        use_warning_for_failure: If True, use print_warning for failures; otherwise use print_error
        env: Optional environment for subprocess (defaults to os.environ)
    """
    if not os.path.isdir(app_path):
        print_warning(f'  Warning: Directory does not exist, skipping')
        return

    if dry_run:
        print_info(f'  [Dry run] Skipping actual execution')
        return

    try:
        cmd_str = ' '.join(cmd)
        result = subprocess.run(
            cmd,
            cwd=app_path,
            env=env if env is not None else os.environ,
            capture_output=True,
            text=True,
            check=False
        )

        if result.returncode == 0:
            print_info(f'  ✓ Success: {cmd_str}')
        else:
            error_func = print_warning if use_warning_for_failure else print_error
            error_func(f'  ✗ Failed (return code: {result.returncode}): {cmd_str}')
            if result.stderr:
                error_func(f'  Error: {result.stderr[:200]}')

    except Exception as e:
        cmd_str = ' '.join(cmd)
        print_error(f'  ✗ Exception: {cmd_str} - {e}')


def set_target_for_app(app_path: str, target: str, dry_run: bool = False) -> None:
    """
    Execute idf.py set-target for a single application.

    Args:
        app_path: Application path
        target: Target chip
        dry_run: Whether to simulate execution only
    """
    cmd = ['idf.py', 'set-target', target]
    _execute_command_for_app(app_path, cmd, dry_run, use_warning_for_failure=False)


def _is_depending_on_esp_board_manager(app_path: str) -> bool:
    """
    Check if the project depends on esp_board_manager component.

    Args:
        app_path: Application path

    Returns:
        True if esp_board_manager is found in build_components, False otherwise
    """
    project_desc_path = os.path.join(app_path, 'build', 'project_description.json')

    if not os.path.exists(project_desc_path):
        print_warning(f'  Warning: {project_desc_path} does not exist, skipping gen-bmgr-config')
        return False

    try:
        with open(project_desc_path, 'r', encoding='utf-8') as f:
            project_desc = json.load(f)

        build_components = project_desc.get('build_components', [])
        # The component may appear as either:
        #   - 'esp_board_manager'              (legacy in-tree override_path)
        #   - 'espressif__esp_board_manager'   (managed via the IDF Component Registry)
        bmgr_aliases = ('esp_board_manager', 'espressif__esp_board_manager')
        if any(alias in build_components for alias in bmgr_aliases):
            return True
        else:
            print_info(f'  Info: esp_board_manager not found in build_components, skipping gen-bmgr-config')
            return False

    except json.JSONDecodeError as e:
        print_warning(f'  Warning: Failed to parse {project_desc_path}: {e}, skipping gen-bmgr-config')
        return False
    except Exception as e:
        print_warning(f'  Warning: Error reading {project_desc_path}: {e}, skipping gen-bmgr-config')
        return False


def set_board_for_app(app_path: str, board: str, dry_run: bool = False) -> None:
    """
    Execute idf.py bmgr (or legacy gen-bmgr-config) for a single application.
    Only executes if the project depends on esp_board_manager component.

    Args:
        app_path: Application path
        board: Target board
        dry_run: Whether to simulate execution only
    """
    if not _is_depending_on_esp_board_manager(app_path):
        return

    if not board or not board.strip():
        print_info('  Info: --board is empty, skipping bmgr board config')
        return

    idf_env = _idf_env_for_app(app_path)
    if idf_env is None:
        print_warning(
            '  Warning: esp_board_manager is in build_components but '
            'managed_components/.../idf_ext.py was not found; skipping board config. '
            'Ensure idf.py set-target completed and the component was downloaded.'
        )
        return

    bmgr_path = idf_env['IDF_EXTRA_ACTIONS_PATH']
    print_info(f'  Info: IDF_EXTRA_ACTIONS_PATH={bmgr_path}')

    # Prefer the new action name; gen-bmgr-config remains a legacy alias in idf_ext.py.
    cmd = ['idf.py', 'bmgr', '-b', board]
    _execute_command_for_app(
        app_path, cmd, dry_run, use_warning_for_failure=True, env=idf_env
    )


def main():
    parser = argparse.ArgumentParser(
        description='Batch process applications: get list, set target, configure board',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  # Process all applications for esp32s3
  python esp_bmgr_pre_build.py --target esp32s3 --board esp32_s3_korvo2_v3

  # Dry run only, no actual modifications
  python esp_bmgr_pre_build.py --target esp32s3 --board esp32_s3_korvo2_v3 --dry-run

  # Specify project path
  python esp_bmgr_pre_build.py --target esp32 --board esp32_s3_korvo2_v3 --project-path /path/to/project

  # Process only applications in test_apps directories
  python esp_bmgr_pre_build.py --target esp32s3 --board esp32_s3_korvo2_v3 --target-dir-type test_apps

  # Process only example applications (excluding test_apps)
  python esp_bmgr_pre_build.py --target esp32s3 --board esp32_s3_korvo2_v3 --target-dir-type example

  # Skip set-target step
  python esp_bmgr_pre_build.py --target esp32s3 --board esp32_s3_korvo2_v3 --skip-set-target
'''
    )

    parser.add_argument(
        '--target',
        required=True,
        help='Target chip (e.g., esp32, esp32s3, esp32c3, etc.)'
    )

    parser.add_argument(
        '--board',
        required=True,
        help='Target board (e.g., lyrat_mini_v1_1, esp32_s3_korvo2_v3, esp32_s3_korvo2l, etc.)'
    )

    parser.add_argument(
        '--project-path',
        default=None,
        help='Project root path (default: two levels up from script directory)'
    )

    parser.add_argument(
        '--dry-run',
        action='store_true',
        help='Simulate execution without making actual changes'
    )

    parser.add_argument(
        '--skip-set-target',
        action='store_true',
        help='Skip idf.py set-target step'
    )

    parser.add_argument(
        '--no-require-pytest',
        action='store_true',
        help='Do not require pytest files in applications'
    )

    parser.add_argument(
        '--target-dir-type',
        choices=[APP_TYPE_ALL, APP_TYPE_EXAMPLE, APP_TYPE_TEST_APPS],
        default=APP_TYPE_ALL,
        help='Application directory type: "all" (default) - all applications, '
             '"test_apps" - only applications in test_apps directories, '
             '"example" - only example applications (excluding test_apps)'
    )

    args = parser.parse_args()

    # Determine project path
    if args.project_path:
        project_path = os.path.abspath(args.project_path)
    else:
        # Default to two levels up from script directory
        script_dir = Path(__file__).parent.absolute()
        project_path = str(script_dir.parent.parent)

    if not os.path.isdir(project_path):
        print_error(f'Error: Project path does not exist: {project_path}')
        sys.exit(1)

    print_info(f'Project path: {project_path}')

    if args.dry_run:
        print_info('\n===== DRY RUN MODE =====')

    print_info(f'Target chip: {args.target}')
    print_info(f'Application type: {args.target_dir_type}\n')

    # Step 1: Get application list
    app_list = get_app_list(project_path, args.target, args.target_dir_type, args.no_require_pytest)

    if not app_list:
        print_warning('No applications found, exiting')
        return

    # Process each application: set target chip (optional) and configure board
    if not args.skip_set_target:
        print_info(f'\n[Step 2] Executing idf.py set-target {args.target} and idf.py bmgr -b {args.board} for applications...')
    else:
        print_info(f'\n[Step 2] Executing idf.py bmgr -b {args.board} for applications...')

    for i, app_path in enumerate(app_list, 1):
        print_info(f'\n[{i}/{len(app_list)}] Processing: {app_path}')

        if not args.skip_set_target:
            set_target_for_app(app_path, args.target, args.dry_run)

        set_board_for_app(app_path, args.board, args.dry_run)

    if args.dry_run:
        print_info('\nThis was a dry run. Remove --dry-run to execute actual changes')


if __name__ == '__main__':
    main()
