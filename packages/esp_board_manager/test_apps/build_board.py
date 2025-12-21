#!/usr/bin/env python3
"""
Script to compile test apps with all available boards in ESP Board Manager.

This script:
1. Scans all available boards
2. For each board:
   - Generates board configuration
   - Sets the target chip (if needed)
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
                          - Default: Only scans esp_board_manager/boards/
                          - With flag: Scans boards/ + components/*/boards/ + custom boards
"""

import sys
import subprocess
import argparse
import yaml
import shutil
from pathlib import Path
from typing import Dict, List, Tuple, Optional
import time

BOARD_INFO_FILENAME = 'board_info.yaml'

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


def scan_all_boards(board_manager_dir: Path, customer_path: Optional[str] = None, main_boards_only: bool = False) -> Dict[str, str]:
    """
    Scan available boards.

    Args:
        board_manager_dir: Path to ESP Board Manager directory
        customer_path: Optional path to customer boards directory
        main_boards_only: If True, only scan main boards directory

    Returns:
        Dictionary mapping board names to their paths
    """
    if main_boards_only:
        # Only scan main boards directory
        boards_dir = board_manager_dir / 'boards'
        all_boards = {}

        if not boards_dir.exists():
            print_colored(f'Error: Boards directory not found at {boards_dir}', Colors.FAIL)
            return all_boards

        print_colored(f'Scanning main boards directory: {boards_dir}', Colors.OKBLUE)

        for board_dir in boards_dir.iterdir():
            if board_dir.is_dir():
                kconfig_path = board_dir / 'Kconfig'
                if kconfig_path.exists():
                    all_boards[board_dir.name] = str(board_dir)
                    print_colored(f'  Found board: {board_dir.name}', Colors.OKGREEN)

        return all_boards

    # Use full scan functionality (includes component and customer boards)
    gen_script = board_manager_dir / 'gen_bmgr_config_codes.py'

    if not gen_script.exists():
        print_colored(f'Error: gen_bmgr_config_codes.py not found at {gen_script}', Colors.FAIL)
        sys.exit(1)

    # Use Python to import and use the board scanner directly
    sys.path.insert(0, str(board_manager_dir))

    try:
        from gen_bmgr_config_codes import BoardConfigGenerator

        generator = BoardConfigGenerator(board_manager_dir)
        all_boards = generator.config_generator.scan_board_directories(customer_path)

        return all_boards
    except Exception as e:
        print_colored(f'Error scanning boards: {e}', Colors.FAIL)
        sys.exit(1)


def get_chip_from_board(board_path: str) -> Optional[str]:
    """Get chip type from board_info.yaml"""
    board_info_path = Path(board_path) / BOARD_INFO_FILENAME

    if not board_info_path.exists():
        return None

    try:
        with open(board_info_path, 'r', encoding='utf-8') as f:
            board_info = yaml.safe_load(f)
            chip = board_info.get('chip', '').lower()
            return chip if chip else None
    except Exception as e:
        print_colored(
            f'Warning: Failed to read {BOARD_INFO_FILENAME} from {board_path}: {e}',
            Colors.WARNING,
        )
        return None


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


def build_board(
    board_name: str,
    board_path: str,
    project_dir: Path,
    board_manager_dir: Path,
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

    # Get chip type
    chip = get_chip_from_board(board_path)
    if not chip:
        error_msg = f'Could not determine chip type for board {board_name}'
        print_colored(f'  ✗ {error_msg}', Colors.FAIL)
        return False, error_msg

    print_colored(f'  Chip type: {chip}', Colors.OKBLUE)

    # Step 1: Generate board configuration
    gen_cmd = ['idf.py', 'gen-bmgr-config', '-b', board_name]
    success, output = run_command(gen_cmd, project_dir, f'Generate config for {board_name}')
    if not success:
        log_file = save_log(project_dir, 'config', board_name, output)
        return False, f'Failed to generate config: {output[:200]}\n\nFull log saved to: {log_file.name}'

    # Clean build directory before each board build
    build_dir = project_dir / 'build'
    if build_dir.exists():
        clean_build_dir(build_dir, project_dir)

    # Step 2: Set target chip
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
        return False, f'Failed to set target: {output[:200]}\n\nFull log saved to: {log_file.name}'

    # Step 3: Build (if not skipped)
    if skip_build:
        print_colored(f'  → Skipping build (--skip-build specified)', Colors.WARNING)
    else:
        build_cmd = ['idf.py', 'build']
        success, output = run_command(build_cmd, project_dir, f'Build {board_name}')

        # Save log if build fails or if save_logs is requested
        if not success or save_logs:
            log_file = save_log(project_dir, 'build', board_name, output)
            if save_logs and success:
                print_colored(f'  → Full build log saved to: {log_file.name}', Colors.OKBLUE)

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
             'Default only scans esp_board_manager/boards/. '
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

    if not (board_manager_dir / 'gen_bmgr_config_codes.py').exists():
        print_colored(f'Error: gen_bmgr_config_codes.py not found in {board_manager_dir}', Colors.FAIL)
        print_colored('Please ensure ESP Board Manager is properly set up', Colors.WARNING)
        sys.exit(1)

    print_colored('ESP Board Manager - Build All Boards', Colors.BOLD)
    print_colored('=' * 60, Colors.BOLD)
    print_colored(f'Project directory: {project_dir}', Colors.OKBLUE)
    print_colored(f'Board manager directory: {board_manager_dir}', Colors.OKBLUE)
    print()

    # Scan all boards
    # Default: only scan main boards directory unless --all-boards is specified
    main_boards_only = not args.all_boards
    print_colored('Scanning for available boards...', Colors.OKCYAN)
    all_boards = scan_all_boards(board_manager_dir, args.customer_path, main_boards_only)

    if not all_boards:
        print_colored('Error: No boards found!', Colors.FAIL)
        sys.exit(1)

    print_colored(f'Found {len(all_boards)} board(s):', Colors.OKGREEN)
    for board_name in sorted(all_boards.keys()):
        print_colored(f'  • {board_name}', Colors.OKBLUE)
    print()

    # Filter boards if specific board requested
    boards_to_build = {}
    if args.board:
        if args.board in all_boards:
            boards_to_build[args.board] = all_boards[args.board]
        else:
            print_colored(f"Error: Board '{args.board}' not found!", Colors.FAIL)
            print_colored(f"Available boards: {', '.join(sorted(all_boards.keys()))}", Colors.WARNING)
            sys.exit(1)
    else:
        boards_to_build = all_boards

    print_colored(f'Will process {len(boards_to_build)} board(s)', Colors.OKGREEN)
    print()

    # Build boards
    results = {}
    start_time = time.time()

    # Build boards sequentially
    for board_name, board_path in sorted(boards_to_build.items()):
        success, error = build_board(
            board_name,
            board_path,
            project_dir,
            board_manager_dir,
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
