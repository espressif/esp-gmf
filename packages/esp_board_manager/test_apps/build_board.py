#!/usr/bin/env python3
"""
Script to compile test apps with all available boards in ESP Board Manager.

This script:
1. Scans all available boards using `idf.py gen-bmgr-config -l`
2. For each board:
   - Generates board configuration
   - Sets the target chip (based on generated defaults)
   - Builds the project
3. Reports build results

Usage:
    python build_board.py [options]

Options:
    --skip-build          Only generate configs, don't build
    -b, --board NAME      Build only specific board
    --stop-on-error       Stop building if one board fails (default: continue)
    -p, --customer-path   Include boards from a custom directory
    --save-logs           Save full logs for all builds
     -a, --all-boards     Scan ALL boards
                          - Default: Only scans Main Boards
                          - With flag: Scans Main + Component + Custom Boards
"""

import sys
import subprocess
import argparse
import re
import shutil
import os
import glob
from pathlib import Path
from typing import Dict, List, Tuple, Optional
import time

BOARD_INFO_FILENAME = 'board_info.yaml'
ARTIFACT_ARCHIVE_ROOT = Path('logs') / 'artifacts'

class Colors:
    """ANSI color codes for terminal output"""
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'


def print_colored(message: str, color: str = Colors.ENDC):
    """Print colored message"""
    print(f'{color}{message}{Colors.ENDC}')


def get_script_dir() -> Path:
    """Get the directory where this script is located"""
    return Path(__file__).parent.absolute()


def get_board_manager_dir() -> Path:
    """Get the ESP Board Manager directory"""
    script_dir = get_script_dir()
    # test_apps -> esp_board_manager
    return script_dir.parent.absolute()


def run_command_get_output(cmd: List[str], cwd: Path) -> Tuple[bool, str]:
    """Run command and return success status and output"""
    try:
        result = subprocess.run(
            cmd,
            cwd=cwd,
            capture_output=True,
            text=True,
            check=False
        )
        return result.returncode == 0, result.stdout + result.stderr
    except Exception as e:
        return False, str(e)


def scan_all_boards(project_dir: Path, customer_path: Optional[str] = None, main_boards_only: bool = False) -> List[str]:
    """
    Scan available boards using `idf.py gen-bmgr-config -l`.

    Args:
        project_dir: Path to project directory (where idf.py runs)
        customer_path: Optional path to customer boards directory
        main_boards_only: If True, only return main boards

    Returns:
        List of board names
    """
    cmd = ['idf.py', 'gen-bmgr-config', '-l']
    if customer_path:
        cmd.extend(['-c', customer_path])

    print_colored(f'Running: {" ".join(cmd)}', Colors.OKCYAN)
    success, output = run_command_get_output(cmd, project_dir)

    if not success:
        print_colored(f'Error scanning boards: {output}', Colors.FAIL)
        # Try to show a helpful error if IDF_EXTRA_ACTIONS_PATH is missing
        if 'No such file or directory' in output or 'command not found' in output:
             print_colored('Please ensure IDF_EXTRA_ACTIONS_PATH is set correctly.', Colors.WARNING)
        return []

    boards = []
    current_section = ''

    # Parse output
    # Example output:
    # Main Boards:
    #   [1] board_name_1
    # Component Boards:
    #   [2] board_name_2

    for line in output.split('\n'):
        line = line.strip()
        if 'Main Boards:' in line:
            current_section = 'main'
            continue
        elif 'Component Boards:' in line:
            current_section = 'component'
            continue
        elif 'Custom Boards:' in line:
            current_section = 'custom'
            continue

        # Match pattern: [number] board_name
        match = re.search(r'\[\d+\]\s+([a-zA-Z0-9_]+)', line)
        if match:
            board_name = match.group(1)

            if main_boards_only:
                if current_section == 'main':
                    boards.append(board_name)
            else:
                boards.append(board_name)

    return boards


def save_log(project_dir: Path, log_type: str, board_name: str, content: str) -> Path:
    """Save log file and return the log file path"""
    log_dir = project_dir / 'logs'
    log_dir.mkdir(exist_ok=True)
    log_file = log_dir / f'{log_type}_{board_name}.log'
    try:
        with open(log_file, 'w', encoding='utf-8') as f:
            f.write(content)
        print_colored(f'  → {log_type.capitalize()} log saved to: logs/{log_file.name}', Colors.WARNING)
    except Exception:
        pass
    return log_file


def get_artifact_archive_dir(project_dir: Path, board_name: str) -> Path:
    """Return the per-board artifact archive directory."""
    return project_dir / ARTIFACT_ARCHIVE_ROOT / board_name


def prepare_artifact_archive_dir(project_dir: Path, board_name: str) -> Path:
    """Create a clean per-board artifact archive directory."""
    archive_dir = get_artifact_archive_dir(project_dir, board_name)
    if archive_dir.exists():
        shutil.rmtree(archive_dir)
    archive_dir.mkdir(parents=True, exist_ok=True)
    return archive_dir


def _copy_file_if_exists(src: Path, dst: Path) -> bool:
    if not src.exists() or not src.is_file():
        return False
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    return True


def archive_board_artifacts(project_dir: Path, board_name: str, include_build_files: bool = False) -> List[str]:
    """
    Archive high-value per-board artifacts for later diagnosis.

    Always archives:
      - dependencies.lock
      - sdkconfig
      - components/gen_bmgr_codes/board_manager.defaults

    Optionally archives selected build diagnostics:
      - build/compile_commands.json
      - build/CMakeCache.txt
      - build/project_description.json
      - build/config/sdkconfig.json
      - build/log/idf_py_stdout_output_*
      - build/log/idf_py_stderr_output_*
    """
    archive_dir = get_artifact_archive_dir(project_dir, board_name)
    archived: List[str] = []

    base_files = [
        (project_dir / 'dependencies.lock', archive_dir / 'dependencies.lock'),
        (project_dir / 'sdkconfig', archive_dir / 'sdkconfig'),
        (
            project_dir / 'components' / 'gen_bmgr_codes' / 'board_manager.defaults',
            archive_dir / 'board_manager.defaults',
        ),
    ]

    for src, dst in base_files:
        if _copy_file_if_exists(src, dst):
            archived.append(str(dst.relative_to(project_dir)))

    if not include_build_files:
        return archived

    build_files = [
        (project_dir / 'build' / 'compile_commands.json', archive_dir / 'build' / 'compile_commands.json'),
        (project_dir / 'build' / 'CMakeCache.txt', archive_dir / 'build' / 'CMakeCache.txt'),
        (project_dir / 'build' / 'project_description.json', archive_dir / 'build' / 'project_description.json'),
        (project_dir / 'build' / 'config' / 'sdkconfig.json', archive_dir / 'build' / 'config' / 'sdkconfig.json'),
    ]

    for src, dst in build_files:
        if _copy_file_if_exists(src, dst):
            archived.append(str(dst.relative_to(project_dir)))

    build_log_dir = project_dir / 'build' / 'log'
    for pattern in ('idf_py_stdout_output_*', 'idf_py_stderr_output_*'):
        for path_str in glob.glob(str(build_log_dir / pattern)):
            src = Path(path_str)
            dst = archive_dir / 'build' / 'log' / src.name
            if _copy_file_if_exists(src, dst):
                archived.append(str(dst.relative_to(project_dir)))

    return archived


def clean_build_dir(build_dir: Path, project_dir: Path) -> None:
    """Clean build directory by direct deletion"""
    print_colored(f'  → Cleaning build directory...', Colors.OKCYAN)
    try:
        shutil.rmtree(build_dir)
        print_colored(f'  ✓ Build directory cleaned', Colors.OKGREEN)
    except Exception as e:
        print_colored(f'  ✗ Failed to clean build directory: {e}', Colors.FAIL)


def run_command(cmd: List[str], cwd: Path, description: str) -> Tuple[bool, str]:
    """
    Run a shell command and return success status and output.

    Args:
        cmd: Command to run as list
        cwd: Working directory
        description: Description of what the command does

    Returns:
        Tuple of (success: bool, output: str)
    """
    print_colored(f'  → {description}...', Colors.OKCYAN)

    try:
        result = subprocess.run(
            cmd,
            cwd=cwd,
            capture_output=True,
            text=True,
            check=False
        )

        success = result.returncode == 0
        output = result.stdout + result.stderr

        if success:
            print_colored(f'  ✓ {description} completed', Colors.OKGREEN)
        else:
            print_colored(f'  ✗ {description} failed (exit code: {result.returncode})', Colors.FAIL)
            if output:
                # For build errors, show the last 100 lines (most relevant)
                # For other errors, show first 500 chars
                if 'build' in description.lower() or 'ninja' in output.lower():
                    lines = output.split('\n')
                    error_lines = lines[-100:] if len(lines) > 100 else lines
                    error_text = '\n'.join(error_lines)
                    print_colored(f'    Last {len(error_lines)} lines of output:', Colors.WARNING)
                    print_colored(f"    {'-'*56}", Colors.WARNING)
                    for line in error_lines:
                        print_colored(f'    {line}', Colors.WARNING)
                    print_colored(f"    {'-'*56}", Colors.WARNING)
                else:
                    print_colored(f'    Output: {output[:500]}', Colors.WARNING)

        return success, output
    except Exception as e:
        print_colored(f'  ✗ {description} failed with exception: {e}', Colors.FAIL)
        return False, str(e)


def get_chip_from_defaults(project_dir: Path) -> Optional[str]:
    """Get chip type from generated board_manager.defaults"""
    defaults_file = project_dir / 'components' / 'gen_bmgr_codes' / 'board_manager.defaults'

    if not defaults_file.exists():
        return None

    try:
        content = defaults_file.read_text()
        # Look for CONFIG_IDF_TARGET="esp32s3" or similar
        match = re.search(r'CONFIG_IDF_TARGET="([^"]+)"', content)
        if match:
            return match.group(1)

        # Fallback: look for CONFIG_IDF_TARGET=esp32s3
        match = re.search(r'CONFIG_IDF_TARGET=([a-z0-9]+)', content)
        if match:
            return match.group(1)

        return None
    except Exception:
        return None


def build_board(
    board_name: str,
    project_dir: Path,
    customer_path: Optional[str] = None,
    skip_build: bool = False,
    save_logs: bool = False
) -> Tuple[bool, str]:
    """
    Build a single board.

    Returns:
        Tuple of (success: bool, error_message: str)
    """
    print_colored(f"\n{'='*60}", Colors.BOLD)
    print_colored(f'Building board: {board_name}', Colors.HEADER)
    print_colored(f"{'='*60}", Colors.BOLD)
    prepare_artifact_archive_dir(project_dir, board_name)

    # Step 1: Generate board configuration
    gen_cmd = ['idf.py', 'gen-bmgr-config', '-b', board_name]
    if customer_path:
        gen_cmd.extend(['-c', customer_path])

    success, output = run_command(gen_cmd, project_dir, f'Generate config for {board_name}')
    if not success:
        log_file = save_log(project_dir, 'config', board_name, output)
        archived = archive_board_artifacts(project_dir, board_name, include_build_files=False)
        return False, f'Failed to generate config: {output[:200]}\n\nFull log saved to: {log_file.name}'

    # Step 2: Get chip type from generated defaults
    chip = get_chip_from_defaults(project_dir)
    if not chip:
        error_msg = f'Could not determine chip type from board_manager.defaults for board {board_name}'
        print_colored(f'  ✗ {error_msg}', Colors.FAIL)
        archive_board_artifacts(project_dir, board_name, include_build_files=False)
        return False, error_msg

    print_colored(f'  Chip type: {chip}', Colors.OKBLUE)

    # Step 3: Clean build directory
    build_dir = project_dir / 'build'
    if build_dir.exists():
        clean_build_dir(build_dir, project_dir)

    # Step 4: Set target chip
    set_target_cmd = ['idf.py', 'set-target', chip]
    success, output = run_command(set_target_cmd, project_dir, f'Set target to {chip}')

    # If set-target fails due to invalid build dir, clean and retry
    if not success and "doesn't seem to be a CMake build directory" in output:
        print_colored(f'  → Build directory is invalid, cleaning and retrying...', Colors.WARNING)
        if build_dir.exists():
            clean_build_dir(build_dir, project_dir)
        success, output = run_command(set_target_cmd, project_dir, f'Set target to {chip} (retry)')

    if not success:
        log_file = save_log(project_dir, 'set_target', board_name, output)
        archive_board_artifacts(project_dir, board_name, include_build_files=True)
        return False, f'Failed to set target: {output[:200]}\n\nFull log saved to: {log_file.name}'

    # Step 5: Build (if not skipped)
    if skip_build:
        print_colored(f'  → Skipping build (--skip-build specified)', Colors.WARNING)
        archived = archive_board_artifacts(project_dir, board_name, include_build_files=False)
        if archived:
            print_colored(
                f'  → Archived {len(archived)} artifact file(s) to {ARTIFACT_ARCHIVE_ROOT / board_name}',
                Colors.OKBLUE,
            )
    else:
        build_cmd = ['idf.py', 'build']
        success, output = run_command(build_cmd, project_dir, f'Build {board_name}')

        # Save log if build fails or if save_logs is requested
        if not success or save_logs:
            log_file = save_log(project_dir, 'build', board_name, output)
            if save_logs and success:
                print_colored(f'  → Full build log saved to: {log_file.name}', Colors.OKBLUE)

        archived = archive_board_artifacts(
            project_dir,
            board_name,
            include_build_files=(not success) or save_logs,
        )
        if archived:
            print_colored(
                f'  → Archived {len(archived)} artifact file(s) to {ARTIFACT_ARCHIVE_ROOT / board_name}',
                Colors.OKBLUE,
            )

        if not success:
            # Extract error summary
            lines = output.split('\n')
            error_keywords = ['error:', 'failed:', 'fatal:', 'ninja: error']
            error_lines = [line for line in lines if any(kw in line.lower() for kw in error_keywords)]
            error_summary = '\n'.join(error_lines[-20:] if error_lines else lines[-30:])
            return False, f'Build failed:\n{error_summary}\n\nFull log saved to: {log_file.name}'

    print_colored(f'  ✓ Successfully processed {board_name}', Colors.OKGREEN)
    return True, ''


def main():
    parser = argparse.ArgumentParser(
        description='Build test apps with all available boards',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )

    parser.add_argument(
        '--skip-build',
        action='store_true',
        help='Only generate configs, don\'t build'
    )

    parser.add_argument(
        '-b', '--board',
        type=str,
        help='Build only specific board'
    )

    parser.add_argument(
        '--stop-on-error',
        action='store_true',
        help='Stop building if one board fails (default: continue on error)'
    )

    parser.add_argument(
        '-p', '--customer-path',
        type=str,
        help='Path to customer boards directory'
    )

    parser.add_argument(
        '--save-logs',
        action='store_true',
        help='Save full build logs to files (build_<board>.log)'
    )

    parser.add_argument(
        '-a', '--all-boards',
        action='store_true',
        help='Scan ALL board directories: main boards + component boards + customer boards. '
             'Default only scans Main Boards. '
             'Use this for complete CI/CD testing before release.'
    )

    args = parser.parse_args()

    # Get directories
    project_dir = get_script_dir()
    board_manager_dir = get_board_manager_dir()

    # Validate directories
    if not (project_dir / 'CMakeLists.txt').exists():
        print_colored(f'Error: CMakeLists.txt not found in {project_dir}', Colors.FAIL)
        print_colored('Please run this script from the test_apps directory', Colors.WARNING)
        sys.exit(1)

    # Check for IDF_EXTRA_ACTIONS_PATH
    if 'IDF_EXTRA_ACTIONS_PATH' not in os.environ:
        print_colored('Warning: IDF_EXTRA_ACTIONS_PATH environment variable not set.', Colors.WARNING)
        print_colored('Ensure it points to esp_board_manager for idf.py extensions to work.', Colors.WARNING)

    print_colored('ESP Board Manager - Build All Boards', Colors.BOLD)
    print_colored('=' * 60, Colors.BOLD)
    print_colored(f'Project directory: {project_dir}', Colors.OKBLUE)
    print_colored(f'Board manager directory: {board_manager_dir}', Colors.OKBLUE)
    print()

    # Scan all boards
    # Default: only scan main boards directory unless --all-boards is specified
    main_boards_only = not args.all_boards
    print_colored('Scanning for available boards...', Colors.OKCYAN)

    all_boards = scan_all_boards(project_dir, args.customer_path, main_boards_only)

    if not all_boards:
        print_colored('Error: No boards found!', Colors.FAIL)
        sys.exit(1)

    print_colored(f'Found {len(all_boards)} board(s):', Colors.OKGREEN)
    for board_name in sorted(all_boards):
        print_colored(f'  • {board_name}', Colors.OKBLUE)
    print()

    # Filter boards if specific board requested
    boards_to_build = []
    if args.board:
        if args.board in all_boards:
            boards_to_build.append(args.board)
        else:
            print_colored(f"Error: Board '{args.board}' not found!", Colors.FAIL)
            print_colored(f"Available boards: {', '.join(sorted(all_boards))}", Colors.WARNING)
            sys.exit(1)
    else:
        boards_to_build = all_boards

    print_colored(f'Will process {len(boards_to_build)} board(s)', Colors.OKGREEN)
    print()

    # Build boards
    results = {}
    start_time = time.time()

    # Build boards sequentially
    for board_name in sorted(boards_to_build):
        success, error = build_board(
            board_name,
            project_dir,
            args.customer_path,
            args.skip_build,
            args.save_logs
        )
        results[board_name] = (success, error)

        if not success and args.stop_on_error:
            print_colored(f'\nStopping due to error', Colors.FAIL)
            break

    # Print summary
    elapsed_time = time.time() - start_time
    print_colored('\n' + '=' * 60, Colors.BOLD)
    print_colored('Build Summary', Colors.HEADER)
    print_colored('=' * 60, Colors.BOLD)

    successful = [name for name, (success, _) in results.items() if success]
    failed = [name for name, (success, _) in results.items() if not success]

    print_colored(f'\nTotal boards: {len(results)}', Colors.OKBLUE)
    print_colored(f'Successful: {len(successful)}', Colors.OKGREEN)
    print_colored(f'Failed: {len(failed)}', Colors.FAIL if failed else Colors.OKGREEN)
    print_colored(f'Time elapsed: {elapsed_time:.2f} seconds', Colors.OKBLUE)

    if successful:
        print_colored(f'\n✓ Successful boards:', Colors.OKGREEN)
        for name in sorted(successful):
            print_colored(f'  • {name}', Colors.OKGREEN)

    if failed:
        print_colored(f'\n✗ Failed boards:', Colors.FAIL)
        for name in sorted(failed):
            error = results[name][1]
            print_colored(f'  • {name}', Colors.FAIL)
            if error:
                # Show more error details
                error_lines = error.split('\n')
                if len(error_lines) > 10:
                    print_colored(f'    Error (showing last 10 lines):', Colors.WARNING)
                    for line in error_lines[-10:]:
                        print_colored(f'      {line}', Colors.WARNING)
                else:
                    print_colored(f'    Error:', Colors.WARNING)
                    for line in error_lines:
                        print_colored(f'      {line}', Colors.WARNING)

        # Show log file locations
        print_colored(f'\n📝 Full logs saved for failed boards (in logs/ directory):', Colors.OKBLUE)
        for name in sorted(failed):
            log_files = []
            for log_type in ['config', 'set_target', 'build']:
                log_file = project_dir / 'logs' / f'{log_type}_{name}.log'
                if log_file.exists():
                    log_files.append(str(log_file.name))
            if log_files:
                print_colored(f"  • {name}: {', '.join(log_files)}", Colors.OKBLUE)

    print()

    # Exit with error code if any builds failed
    sys.exit(1 if failed else 0)

if __name__ == '__main__':
    main()
