#!/usr/bin/env python3

import os
import argparse
import subprocess
import build_apps

from pathlib import Path
from collections import defaultdict
from typing import List, Set, Optional, Dict, Tuple

# Git commands
GIT_DIFF_CMD = ['git', 'diff', '--name-only']
GIT_STATUS_CMD = ['git', 'status', '--porcelain']

# Logging functions for cleaner code
def print_debug(msg: str) -> None:
    build_apps.print_debug(msg)

def print_info(msg: str) -> None:
    build_apps.print_info(msg)

def print_warning(msg: str) -> None:
    build_apps.print_warning(msg)

def print_error(msg: str) -> None:
    build_apps.print_error(msg)

def get_project_root() -> Path:
    """Get the project root directory using git"""
    try:
        result = subprocess.run(
            ['git', 'rev-parse', '--show-toplevel'],
            capture_output = True, text = True, check = True
        )
        return Path(result.stdout.strip())
    except subprocess.CalledProcessError as e:
        print_error(f'Failed to get project root: {e}')
        return Path(__file__).resolve().parents[2]  # Fallback to relative path

# Script Configuration
# -------------------

CONFIG = {
    # Project configuration
    'project_root': get_project_root(),
    'excluded_dirs': [
        'managed_components',
        'gmf_examples',
    ],
    'test_file_name': 'test_cxx_build.cpp',

    # Build configuration
    'target': 'all',

    # Source file types
    'monitored_file_types': {
        '.c',    # C source files
        '.h',    # C header files
        '.cpp',  # C++ source files
        '.hpp',  # C++ header files
        '.inc',  # Include files
        '.cxx',  # Alternative C++ source files
        '.cc',   # Alternative C++ source files
    }
}

def get_ci_modified_files() -> Optional[Set[Path]]:
    """Get modified files in CI environment"""
    if not (base_sha := os.getenv('CI_MERGE_REQUEST_DIFF_BASE_SHA')) or not (ref_name := os.getenv('CI_COMMIT_REF_NAME')):
        return None

    try:
        # Fetch branches
        for branch in [base_sha, ref_name]:
            subprocess.run(
                ['git', 'fetch', 'origin', f'{branch}:origin/{branch}'],
                check = True, capture_output = True
            )
        
        # Get changed files
        result = subprocess.run(
            GIT_DIFF_CMD + [f'origin/{base_sha}', f'origin/{ref_name}'],
            capture_output = True, text = True, check = True
        )
        return {CONFIG['project_root'] / path for path in result.stdout.splitlines()}
    except subprocess.CalledProcessError as e:
        print_error(f'Failed to get MR diff: {e}')
        return None

def get_local_modified_files() -> Optional[Set[Path]]:
    """Get modified files in local environment"""
    try:
        result = subprocess.run(
            GIT_DIFF_CMD + ['HEAD^', 'HEAD'],
            capture_output = True, text = True, check = True
        )
        return {CONFIG['project_root'] / path for path in result.stdout.splitlines()}
    except subprocess.CalledProcessError:
        try:
            result = subprocess.run(
                GIT_STATUS_CMD,
                capture_output = True, text = True, check = True
            )
            return {CONFIG['project_root'] / line[3:].strip().split(' -> ')[-1]
                   for line in result.stdout.splitlines()
                   if line.strip() and len(line) > 3}
        except subprocess.CalledProcessError as e:
            print_error(f'Failed to get git status: {e}')
            return None

def get_modified_files() -> Optional[Dict[Path, Set[Path]]]:
    """Get files modified between current and previous commit, grouped by directory"""
    if os.getenv('CI'):
        print_info('Running in CI environment')
        modified = get_ci_modified_files()
    else:
        modified = get_local_modified_files()

    if modified is None:
        return None

    # Group modified files by directory and filter by type
    grouped_files = defaultdict(set)
    for file in modified:
        if file.suffix.lower() in CONFIG['monitored_file_types']:
            grouped_files[file.parent].add(file)
    
    return grouped_files

def get_test_apps() -> List[Tuple[Path, Path]]:
    """Get all test_apps directories with their test file paths"""
    paths = build_apps.get_app_paths(
        str(CONFIG['project_root']),
        target = CONFIG['target'],
        target_dir_type = build_apps.APP_TYPE_TEST_APPS,
        exclude_apps = CONFIG['excluded_dirs']
    )
    return [(Path(p), Path(p) / 'main' / CONFIG['test_file_name']) for p in paths]

def should_update_test_file(test_app_path: Path, test_file: Path, modified_files: Dict[Path, Set[Path]]) -> bool:
    """Check if test_cxx_build.cpp needs to be updated"""
    if modified_files is None:
        print_warning(f'Could not determine changes, skipping update check for: {test_app_path}')
        return False

    # Get parent directory of test_app
    test_app_parent = test_app_path.parent

    # Check if any files in or under the component directory were modified
    for dir_path, files in modified_files.items():
        if test_app_parent in dir_path.parents or dir_path == test_app_parent:
            # Skip if only the test file itself was modified
            if len(files) == 1 and next(iter(files)) == test_file:
                continue
            print_debug(f'Found modified source files in: {dir_path}')
            return True
    
    return False

def main() -> None:
    # Get all test_apps directories and their test files
    test_apps = get_test_apps()
    if not test_apps:
        print_warning('No test_apps directory found')
        return

    # Get modified files grouped by directory
    modified_files = get_modified_files()

    # Check each test_apps directory
    for test_app, test_file in test_apps:
        print_debug(f'\nChecking directory: {test_app}')
        
        # Check if test_cxx_build.cpp exists
        if not test_file.exists():
            print_warning(f'Warning: {test_file} does not exist, please create it')
            continue

        # Check if test_cxx_build.cpp needs to be updated
        if should_update_test_file(test_app, test_file, modified_files):
            print_warning(f'Note: Related component changes detected, please consider updating {test_file}')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description = 'Check and manage test_cxx_build.cpp files in test apps',
        formatter_class = argparse.ArgumentDefaultsHelpFormatter,
    )

    parser.add_argument(
        '-v', '--verbose',
        action = 'count',
        default = 0,
        help = 'Show verbose log message',
    )

    arguments = parser.parse_args()
    build_apps.setup_logging(verbose = arguments.verbose)
    main()
