#!/usr/bin/env python3
"""
Diagnostic script to help identify pytest issues
Run this to see what's wrong with the test setup
"""

import sys
import subprocess
from pathlib import Path


def check_pytest():
    """Check if pytest is installed"""
    try:
        result = subprocess.run(
            ['pytest', '--version'],
            capture_output=True,
            text=True
        )
        if result.returncode == 0:
            print(f'✅ pytest installed: {result.stdout.strip()}')
            return True
        else:
            print('❌ pytest not working properly')
            return False
    except FileNotFoundError:
        print('❌ pytest not found')
        print('   Install with: pip3 install pytest')
        return False


def check_imports():
    """Check if test files can be imported"""
    test_dir = Path(__file__).parent
    test_files = sorted(test_dir.glob('test_v2_*.py'))

    print('\nChecking test file imports:')
    all_good = True
    for test_file in test_files:
        module_name = test_file.stem
        try:
            # Try to compile the file
            with open(test_file, 'r') as f:
                compile(f.read(), test_file.name, 'exec')
            print(f'  ✅ {test_file.name}')
        except Exception as e:
            print(f'  ❌ {test_file.name}: {e}')
            all_good = False
    return all_good


def check_fixtures():
    """Check if conftest.py is valid"""
    conftest_path = Path(__file__).parent / 'conftest.py'
    if not conftest_path.exists():
        print('\n❌ conftest.py not found')
        return False

    print('\nChecking conftest.py:')
    try:
        with open(conftest_path, 'r') as f:
            compile(f.read(), 'conftest.py', 'exec')
        print('  ✅ conftest.py syntax OK')
        return True
    except Exception as e:
        print(f'  ❌ conftest.py error: {e}')
        return False


def run_collection_test():
    """Try to collect tests"""
    print('\nTrying to collect tests:')
    try:
        result = subprocess.run(
            ['pytest', '--collect-only', '-q', 'test_v2_*.py'],
            capture_output=True,
            text=True,
            cwd=Path(__file__).parent
        )
        if result.returncode == 0:
            lines = result.stdout.strip().split('\n')
            test_count = len([l for l in lines if l.strip() and not l.startswith('<')])
            print(f'  ✅ Collected {test_count} tests')
            return True
        else:
            print(f'  ❌ Collection failed')
            print(f'  STDERR: {result.stderr}')
            return False
    except Exception as e:
        print(f'  ❌ Error: {e}')
        return False


def run_sample_test():
    """Run a single test to see if it works"""
    print('\nRunning a sample test:')
    try:
        result = subprocess.run(
            ['pytest', '-v', 'test_v2_board_selection.py::TestBoardSelectionByName::test_select_valid_board_by_name'],
            capture_output=True,
            text=True,
            cwd=Path(__file__).parent,
            timeout=30
        )
        if 'PASSED' in result.stdout:
            print('  ✅ Sample test passed')
            return True
        elif 'FAILED' in result.stdout:
            print('  ❌ Sample test failed')
            print('\n  Output:')
            print('  ' + '\n  '.join(result.stdout.split('\n')[-20:]))
            return False
        elif 'ERROR' in result.stdout:
            print('  ❌ Sample test error')
            print('\n  Output:')
            print('  ' + '\n  '.join(result.stdout.split('\n')[-20:]))
            return False
        else:
            print('  ⚠️  Unknown result')
            return False
    except subprocess.TimeoutExpired:
        print('  ❌ Test timed out')
        return False
    except Exception as e:
        print(f'  ❌ Error: {e}')
        return False


def main():
    print('╔══════════════════════════════════════════════════════════════╗')
    print('║         Pytest Diagnostic Tool                              ║')
    print('╚══════════════════════════════════════════════════════════════╝')
    print()

    checks = [
        ('pytest installation', check_pytest),
        ('test file syntax', check_imports),
        ('conftest.py', check_fixtures),
    ]

    results = []
    for name, check_func in checks:
        try:
            result = check_func()
            results.append(result)
        except Exception as e:
            print(f'\n❌ Error in {name}: {e}')
            results.append(False)

    # Only run these if basics pass
    if all(results):
        print('\nBasic checks passed, running pytest checks:')
        results.append(run_collection_test())
        results.append(run_sample_test())

    print('\n' + '=' * 70)
    print('\nDiagnostic Summary:')
    passed = sum(results)
    total = len(results)
    print(f'  Checks passed: {passed}/{total}')

    if all(results):
        print('\n✅ All checks passed! You can run:')
        print('   ./run_pytest.sh')
    else:
        print('\n❌ Some checks failed. Please:')
        print('   1. Install pytest: pip3 install pytest')
        print('   2. Check the errors above')
        print("   3. Make sure you're in the correct directory")

    return 0 if all(results) else 1


if __name__ == '__main__':
    sys.exit(main())

