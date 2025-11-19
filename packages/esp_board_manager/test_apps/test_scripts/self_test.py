#!/usr/bin/env python3
"""
Self-test script - Runs a few sample tests to demonstrate functionality
Does not require pytest installation
"""

import sys
import subprocess
from pathlib import Path


class Colors:
    GREEN = '\033[0;32m'
    RED = '\033[0;31m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    NC = '\033[0m'


def run_command(args, check_success=True):
    """Run a command and return result"""
    script_dir = Path(__file__).parent
    root_dir = script_dir.parent.parent.parent

    cmd = ['python3', str(root_dir / 'gen_bmgr_config_codes.py')] + args
    result = subprocess.run(
        cmd,
        cwd=str(root_dir),
        capture_output=True,
        text=True
    )
    return result


def test_list_boards():
    """Test list boards functionality"""
    print(f'{Colors.BLUE}Test 1:{Colors.NC} List boards with -l')
    result = run_command(['-l'])

    if result.returncode == 0 and 'Main Boards:' in result.stdout:
        print(f'  {Colors.GREEN}✅ PASSED{Colors.NC}')
        return True
    else:
        print(f'  {Colors.RED}❌ FAILED{Colors.NC}')
        return False


def test_select_by_index():
    """Test board selection by index"""
    print(f'{Colors.BLUE}Test 2:{Colors.NC} Select board by index 1')
    result = run_command(['-b', '1', '--kconfig-only'])

    if result.returncode == 0 and 'Resolved board:' in result.stdout:
        print(f'  {Colors.GREEN}✅ PASSED{Colors.NC}')
        return True
    else:
        print(f'  {Colors.RED}❌ FAILED{Colors.NC}')
        return False


def test_select_by_name():
    """Test board selection by name"""
    print(f'{Colors.BLUE}Test 3:{Colors.NC} Select board by name')
    result = run_command(['-b', 'esp_box_3', '--kconfig-only'])

    if result.returncode == 0:
        print(f'  {Colors.GREEN}✅ PASSED{Colors.NC}')
        return True
    else:
        print(f'  {Colors.RED}❌ FAILED{Colors.NC}')
        return False


def test_invalid_board():
    """Test invalid board handling"""
    print(f'{Colors.BLUE}Test 4:{Colors.NC} Invalid board name (should fail)')
    result = run_command(['-b', 'invalid_board', '--kconfig-only'], check_success=False)

    if result.returncode != 0 and 'not found' in result.stdout:
        print(f'  {Colors.GREEN}✅ PASSED{Colors.NC} (correctly failed)')
        return True
    else:
        print(f'  {Colors.RED}❌ FAILED{Colors.NC}')
        return False


def test_invalid_index():
    """Test invalid index handling"""
    print(f'{Colors.BLUE}Test 5:{Colors.NC} Invalid index (should fail)')
    result = run_command(['-b', '999', '--kconfig-only'], check_success=False)

    if result.returncode != 0 and 'out of range' in result.stdout:
        print(f'  {Colors.GREEN}✅ PASSED{Colors.NC} (correctly failed)')
        return True
    else:
        print(f'  {Colors.RED}❌ FAILED{Colors.NC}')
        return False


def test_log_levels():
    """Test log levels"""
    print(f'{Colors.BLUE}Test 6:{Colors.NC} Log level DEBUG')
    result = run_command(['-b', '1', '--log-level', 'DEBUG', '--kconfig-only'])

    if result.returncode == 0:
        print(f'  {Colors.GREEN}✅ PASSED{Colors.NC}')
        return True
    else:
        print(f'  {Colors.RED}❌ FAILED{Colors.NC}')
        return False


def test_help():
    """Test help option"""
    print(f'{Colors.BLUE}Test 7:{Colors.NC} Help option')
    result = run_command(['-h'], check_success=False)

    if result.returncode == 0 and 'usage:' in result.stdout:
        print(f'  {Colors.GREEN}✅ PASSED{Colors.NC}')
        return True
    else:
        print(f'  {Colors.RED}❌ FAILED{Colors.NC}')
        return False


def main():
    print()
    print('╔══════════════════════════════════════════════════════════════╗')
    print('║         ESP Board Manager - Python Self Test                ║')
    print('╚══════════════════════════════════════════════════════════════╝')
    print()
    print('Running sample tests to demonstrate functionality...')
    print()
    print('─' * 70)
    print()

    tests = [
        test_list_boards,
        test_select_by_index,
        test_select_by_name,
        test_invalid_board,
        test_invalid_index,
        test_log_levels,
        test_help,
    ]

    results = []
    for test_func in tests:
        try:
            result = test_func()
            results.append(result)
        except Exception as e:
            print(f'  {Colors.RED}❌ ERROR:{Colors.NC} {e}')
            results.append(False)
        print()

    print('─' * 70)
    print()

    passed = sum(results)
    total = len(results)

    print(f'Summary:')
    print(f'  Total: {total}')
    print(f'  {Colors.GREEN}Passed: {passed}{Colors.NC}')
    print(f'  {Colors.RED}Failed: {total - passed}{Colors.NC}')
    print()

    if passed == total:
        print('╔══════════════════════════════════════════════════════════════╗')
        print(f'║         {Colors.GREEN}✅ ALL TESTS PASSED! ✅{Colors.NC}                          ║')
        print('╚══════════════════════════════════════════════════════════════╝')
        print()
        print('This was a basic demonstration of the Python test suite.')
        print()
        print('For full test coverage, install pytest:')
        print('  pip3 install pytest pytest-html pytest-cov')
        print()
        print('Then run the complete test suite:')
        print('  ./run_pytest.sh')
        return 0
    else:
        print('╔══════════════════════════════════════════════════════════════╗')
        print(f'║         {Colors.RED}❌ SOME TESTS FAILED! ❌{Colors.NC}                        ║')
        print('╚══════════════════════════════════════════════════════════════╝')
        return 1


if __name__ == '__main__':
    sys.exit(main())

