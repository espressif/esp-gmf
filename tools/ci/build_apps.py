# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Build and filter applications for CI testing.

This script provides functionality to:
1. Filter applications based on directory type:
   - test_apps: Applications in test_apps directories
   - example: All applications except in test_apps directories
   - all: All applications (default)

2. Build filtered applications:
   - Supports parallel builds with --parallel-count and --parallel-index
   - Configurable build directory pattern
   - Handles multiple sdkconfig files
   - Checks for pytest files in applications

3. Find mode (--find):
   - Lists filtered applications without building
   - Outputs results as a bash array (APP_LIST)

Example usage:
1. Build all test apps:
   python build_apps.py /path/to/project --target-dir-type test_apps

2. Find example apps and export as array:
   python build_apps.py /path/to/project --find --target-dir-type example
   echo "Found ${#APP_LIST[@]} apps"

3. Build specific configuration:
   python build_apps.py /path/to/project --config="sdkconfig.ci=default"

Note: Build rules in .build_test_rules.yml files may exclude some applications
      based on target, configuration or other criteria.
"""

import argparse
import sys
import os
import glob
from pathlib import Path
from typing import List
import tempfile
import logging
from idf_build_apps import App, build_apps, find_apps, setup_logging, utils, CMakeApp

if not os.environ.get('PROJECT_PATH'):
    print(PROJECT_PATH_MISSING_MSG, file=sys.stderr)
    sys.exit(1)

PROJECT_ROOT = Path(__file__).parent.parent.absolute()
APPS_BUILD_PER_JOB = 30
IGNORE_WARNINGS = [
    r'1/2 app partitions are too small',
    r'This clock source will be affected by the DFS of the power management',
    r'The current IDF version does not support using the gptimer API',
    r'DeprecationWarning: pkg_resources is deprecated as an API',
    r'The smallest .+ partition is nearly full \(\d+% free space left\)!',
]

# ANSI color codes for terminal output
RED = '\033[91m'
GREEN = '\033[92m'
YELLOW = '\033[38;5;214m'
RESET = '\033[0m'

# Application type constants
APP_TYPE_ALL = 'all'
APP_TYPE_EXAMPLE = 'example'
APP_TYPE_TEST_APPS = 'test_apps'
APP_TYPES = [APP_TYPE_ALL, APP_TYPE_TEST_APPS, APP_TYPE_EXAMPLE]

# Icons and message templates
ERROR_ICON = '[ERROR]'
WARNING_ICON = '[WARN]'

PYTEST_MISSING_MSG = 'The following paths do not contain pytest files (pytest_*.py):'
PYTEST_ADD_MSG = 'Please add pytest files to apps'
NO_TEST_APPS_MSG = 'No test_apps directory found in given paths!'
NO_EXAMPLE_APPS_MSG = 'No example apps found in given paths!'
PROJECT_PATH_MISSING_MSG = 'Please set the environment variable PROJECT_PATH to the repository path.'

# Build and collection message templates
FOUND_APPS_MSG = 'Found {} apps after filtering'
COLLECTED_PATHS_MSG = 'Collected {} paths for build.'
COLLECTED_TEST_APPS_MSG = 'Collected {} test_apps directories for pytest build.'
COLLECTED_EXAMPLE_APPS_MSG = 'Collected {} example apps for build.'
PARALLEL_COUNT_SUGGESTION_MSG = 'Suggest setting the parallel count to {} for this build job'

# Message templates for removed apps
REMOVED_BY_RULES_MSG = 'The following apps were removed by .build_test_rules.yml rules:'

LOG_LEVELS = {
    0: logging.WARNING,
    1: logging.INFO,
    2: logging.DEBUG
}

def print_debug(message):
    logging.debug(f'\033[37m{message}\033[0m')

def print_info(message):
    logging.info(f'\033[37m{message}\033[0m')

def print_warning(message):
    logging.warning(f'\033[33m{message}\033[0m')

def print_error(message):
    logging.error(f'\033[31m{message}\033[0m')


def _check_idf_build_apps_version():
    """Check idf_build_apps version: 2.x uses newer API, 1.x uses older API"""
    try:
        import idf_build_apps
        version = getattr(idf_build_apps, '__version__', '1.0.0')
        major_version = int(version.split('.')[0])
        return major_version >= 2
    except:
        # Default to older API if version detection fails
        return False

def get_cmake_apps(
    paths,
    target,
    config_rules_str,
    default_build_targets,
    build_dir='build_@t_@w',
):  # type: (List[str], str, List[str], List[str], str) -> List[App]
    use_new_api = _check_idf_build_apps_version()

    if use_new_api:
        # Newer API with build_log_filename
        apps = find_apps(
            paths,
            recursive=True,
            target=target,
            build_dir=build_dir,
            config_rules_str=config_rules_str,
            build_log_filename='build_log.txt',
            size_json_filename='size.json',
            check_warnings=True,
            no_preserve=False,
            default_build_targets=default_build_targets,
            manifest_files=[str(p) for p in Path(os.environ['PROJECT_PATH']).glob('**/.build_test_rules.yml')],
        )
    else:
        # Older API with build_log_path
        apps = find_apps(
            paths,
            recursive=True,
            target=target,
            build_dir=build_dir,
            config_rules_str=config_rules_str,
            build_log_path='build_log.txt',
            size_json_path='size.json',
            check_warnings=True,
            default_build_targets=default_build_targets,
            manifest_files=[str(p) for p in Path(os.environ['PROJECT_PATH']).glob('**/.build_test_rules.yml')],
        )
    return apps

def _collect_app_without_pytest(app_path, apps_without_pytest):
    pytest_files = glob.glob(os.path.join(app_path, 'pytest_*.py'))
    if not pytest_files:
        apps_without_pytest.append(app_path)

def _check_target_dir_type(app_path, target_dir_type):
    """
    Note on APP_TYPE_EXAMPLE:
    Despite its name suggesting only example apps, it actually matches all non-test apps.
    - APP_TYPE_TEST_APPS: strictly matches apps in test_apps directories
    - APP_TYPE_EXAMPLE: matches all apps EXCEPT those in test_apps directories
    - APP_TYPE_ALL: matches all apps regardless of directory
    """
    if target_dir_type == APP_TYPE_TEST_APPS:
        return APP_TYPE_TEST_APPS in app_path.split(os.sep)
    elif target_dir_type == APP_TYPE_EXAMPLE:
        # Note: This matches all non-test apps, not just examples
        return APP_TYPE_TEST_APPS not in app_path.split(os.sep)
    return True

def _collect_valid_app(root, target_dir_type, check_pytest, filtered_paths, apps_without_pytest):
    if 'managed_components' in root.split(os.sep):
        return

    if CMakeApp.is_app(root) and _check_target_dir_type(root, target_dir_type):
        filtered_paths.append(root)
        if check_pytest:
            _collect_app_without_pytest(root, apps_without_pytest)

def find_apps_with_filter(paths, target_dir_type=APP_TYPE_ALL, check_pytest=False):
    """
    Find applications with target_dir_type

    Args:
        paths: List of paths to search
        target_dir_type: Type of target directories to find (APP_TYPE_TEST_APPS, APP_TYPE_EXAMPLE, APP_TYPE_ALL)
        check_pytest: If True, also check for missing pytest files

    Returns:
        tuple: (filtered_paths, apps_without_pytest)
        - filtered_paths: List of application paths that match the target_dir_type
        - apps_without_pytest: List of apps missing pytest files (empty list if check_pytest=False)
    """
    # If no filtering is requested, return original paths (for backward compatibility)
    if target_dir_type == APP_TYPE_ALL and not check_pytest:
        return paths, []

    filtered_paths = []
    apps_without_pytest = []

    for base_path in paths:
        # First check if base_path itself is an app
        _collect_valid_app(base_path, target_dir_type, check_pytest, filtered_paths, apps_without_pytest)

        # If not an app or even if it is, still walk through subdirectories
        for root, dirs, files in os.walk(base_path):
            _collect_valid_app(root, target_dir_type, check_pytest, filtered_paths, apps_without_pytest)

    return filtered_paths, apps_without_pytest

def check_pytest_files(app_paths, is_error=False, output_to_stderr=False):
    """
    Check if pytest files exist in app directories and warn if missing

    Args:
        app_paths: List of application paths to check
        is_error: If True, display as error; if False, display as warning
        output_to_stderr: If True, output messages to stderr instead of stdout

    Returns:
        bool: True if all apps have pytest files, False if any are missing
    """
    apps_without_pytest = []
    for app_path in app_paths:
        pytest_files = glob.glob(os.path.join(app_path, 'pytest_*.py'))
        if not pytest_files:
            apps_without_pytest.append(app_path)

    if apps_without_pytest:
        if is_error:
            print_error(f'{ERROR_ICON} {PYTEST_MISSING_MSG}')
            for path in apps_without_pytest:
                print_error(f'  - {path}')
            print_error(PYTEST_ADD_MSG)
        else:
            print_warning(f'{WARNING_ICON} {PYTEST_MISSING_MSG}')
            for path in apps_without_pytest:
                print_warning(f'  - {path}')
            print_warning(PYTEST_ADD_MSG)
        return False

    return True

def _get_removed_apps(filtered_paths, built_apps):
    """
    Find apps that were removed by build rules

    Args:
        filtered_paths: Original filtered app paths
        built_apps: Final apps that will be built

    Returns:
        List of app paths that were removed by build rules
    """
    built_paths = set(app.app_dir for app in built_apps)
    return [path for path in filtered_paths if path not in built_paths]

def _handle_find_mode(args, filtered_paths):
    """Handle find mode: list filtered applications without building"""
    default_build_targets = args.default_build_targets.split(',') if args.default_build_targets else None
    apps = get_cmake_apps(filtered_paths, args.target, args.config, default_build_targets, args.build_dir)

    exclude_apps = getattr(args, 'exclude_apps', [])
    if exclude_apps:
        apps = [app for app in apps if app.name not in exclude_apps]

    # Deduplicate app paths (same app may have multiple configs/targets)
    unique_paths = []
    seen_paths = set()
    for app in apps:
        if app.app_dir not in seen_paths:
            unique_paths.append(app.app_dir)
            seen_paths.add(app.app_dir)

    # Output as bash array and set environment variable
    sorted_paths = sorted(unique_paths)

    # Check for missing pytest files before setting environment variable
    if not args.no_require_pytest:
        if not check_pytest_files(sorted_paths, is_error=True, output_to_stderr=True):
            sys.exit(1)

    # Check for apps removed by build rules
    removed_apps = _get_removed_apps(filtered_paths, apps)
    if removed_apps:
        print_warning(f'\n{WARNING_ICON} {REMOVED_BY_RULES_MSG}')
        for path in removed_apps:
            print_warning(f'  - {path}')

    # Output the array command to stdout (for eval)
    paths_str = '" "'.join(sorted_paths)
    print('APP_LIST=("' + paths_str + '")')

    print_info(f'Successfully created array with {len(sorted_paths)} elements')

def _handle_build_mode(args, filtered_paths):
    """Handle build mode: build the filtered applications"""
    if args.target_dir_type == APP_TYPE_TEST_APPS:
        print_info(COLLECTED_TEST_APPS_MSG.format(len(filtered_paths)))
    elif args.target_dir_type == APP_TYPE_EXAMPLE:
        print_info(COLLECTED_EXAMPLE_APPS_MSG.format(len(filtered_paths)))
    else:
        print_info(COLLECTED_PATHS_MSG.format(len(filtered_paths)))

    default_build_targets = args.default_build_targets.split(',') if args.default_build_targets else None
    apps = get_cmake_apps(filtered_paths, args.target, args.config, default_build_targets, args.build_dir)

    # Check for apps removed by build rules
    removed_apps = _get_removed_apps(filtered_paths, apps)
    if removed_apps:
        print_warning(f'\n{WARNING_ICON} {REMOVED_BY_RULES_MSG}')
        for path in removed_apps:
            print_warning(f'  - {path}')

    # Check for missing pytest files in collected apps
    unique_app_paths = list(set(app.app_dir for app in apps))
    if not args.no_require_pytest:
        if not check_pytest_files(unique_app_paths, is_error=True):
            sys.exit(1)  # Exit with error code if pytest files are missing

    exclude_apps = getattr(args, 'exclude_apps', [])
    apps_to_build = [app for app in apps if app.name not in exclude_apps] if exclude_apps else apps[:]

    print_info(FOUND_APPS_MSG.format(len(apps_to_build)))
    print_info(PARALLEL_COUNT_SUGGESTION_MSG.format(len(apps_to_build) // APPS_BUILD_PER_JOB + 1))
    print_info('\nApps to build:')
    for app in apps_to_build:
        print_info(f'  - {app.app_dir}')
    print_info('')

    return build_apps(
        apps_to_build,
        parallel_count=args.parallel_count,
        parallel_index=args.parallel_index,
        dry_run=False,
        collect_size_info=args.collect_size_info,
        keep_going=True,
        ignore_warning_strs=IGNORE_WARNINGS,
        copy_sdkconfig=True,
    )

def main(args):
    """Main entry point for the script"""
    # Filter apps based on target directory type
    filtered_paths, apps_without_pytest = find_apps_with_filter(args.paths, target_dir_type=args.target_dir_type)
    if not filtered_paths:
        if args.target_dir_type == APP_TYPE_TEST_APPS:
            print_error(NO_TEST_APPS_MSG)
        elif args.target_dir_type == APP_TYPE_EXAMPLE:
            print_error(NO_EXAMPLE_APPS_MSG)
        sys.exit(1)

    if args.find:
        _handle_find_mode(args, filtered_paths)
    else:
        ret_code = _handle_build_mode(args, filtered_paths)
        sys.exit(ret_code)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Build all the apps for different test types. Will auto remove those non-test apps binaries',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument('paths', nargs='*', help='Paths to the apps to build.')
    parser.add_argument(
        '-t', '--target',
        default=APP_TYPE_ALL,
        help='Build apps for given target. could pass "all" to get apps for all targets',
    )
    parser.add_argument(
        '--config',
        default=['sdkconfig.ci=default', 'sdkconfig.ci.*=', '=default'],
        action='append',
        help='Adds configurations (sdkconfig file names) to build. This can either be '
        'FILENAME[=NAME] or FILEPATTERN. FILENAME is the name of the sdkconfig file, '
        'relative to the project directory, to be used. Optional NAME can be specified, '
        'which can be used as a name of this configuration. FILEPATTERN is the name of '
        'the sdkconfig file, relative to the project directory, with at most one wildcard. '
        'The part captured by the wildcard is used as the name of the configuration.',
    )
    parser.add_argument(
        '--parallel-count', default=1, type=int, help='Number of parallel build jobs.'
    )
    parser.add_argument(
        '--parallel-index',
        default=1,
        type=int,
        help='Index (1-based) of the job, out of the number specified by --parallel-count.',
    )
    parser.add_argument(
        '--collect-size-info',
        type=argparse.FileType('w'),
        help='If specified, the test case name and size info json will be written to this file',
    )
    parser.add_argument(
        '--exclude-apps',
        nargs='*',
        default=[],
        help='Exclude build apps',
    )
    parser.add_argument(
        '--default-build-targets',
        default=None,
        help='default build targets used in manifest files',
    )
    parser.add_argument(
        '--build-dir',
        default='build_@t_@w',
        help='Build directory pattern. Use @t for target and @w for wildcard. Default: build_@t_@w',
        dest='build_dir',
    )
    parser.add_argument(
        '-v', '--verbose',
        action='count', default=0,
        help='Show verbose log message',
    )
    parser.add_argument(
        '--target-dir-type',
        choices=APP_TYPES,
        default=APP_TYPE_ALL,
        help='Type of applications to build: '
        '"all" (default) - all applications, '
        '"test_apps" - only test applications in test_apps directories, '
        '"example" - only example applications (excluding test_apps directories).',
        dest='target_dir_type'
    )
    parser.add_argument(
        '--find',
        action='store_true',
        help='Only find and output the filtered app paths, do not build. '
        'Uses the same filtering logic as build including .build_test_rules.yml files. '
        'Can be used with --target-dir-type to filter application types.',
    )
    parser.add_argument(
        '--no-require-pytest',
        action='store_true',
        help='Disable the requirement for pytest files. By default, missing pytest files '
        'will cause the script to exit with error code 1.',
    )

    arguments = parser.parse_args()

    logging.basicConfig(
        level = LOG_LEVELS[min(arguments.verbose, 2)],  # Limit the maximum log level to DEBUG
        format = '%(message)s'
    )
    if not arguments.paths:
        arguments.paths = [PROJECT_ROOT]
    setup_logging(verbose=arguments.verbose)
    main(arguments)
