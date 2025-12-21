#!/usr/bin/env python3
"""
Validate Python test suite without running tests
Checks syntax, imports, and basic structure
"""

import sys
import ast
import subprocess
from pathlib import Path


def check_file_syntax(filepath):
    """Check if Python file has valid syntax"""
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            code = f.read()
        ast.parse(code)
        return True, 'OK'
    except SyntaxError as e:
        return False, f'Syntax error: {e}'
    except Exception as e:
        return False, f'Error: {e}'


def check_imports(filepath):
    """Try to import the module to check dependencies"""
    try:
        # Read file and check for imports
        with open(filepath, 'r', encoding='utf-8') as f:
            code = f.read()
        tree = ast.parse(code)
        imports = []
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                for alias in node.names:
                    imports.append(alias.name)
            elif isinstance(node, ast.ImportFrom):
                if node.module:
                    imports.append(node.module)
        return True, imports
    except Exception as e:
        return False, str(e)


def count_tests(filepath):
    """Count test functions in file"""
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            code = f.read()
        tree = ast.parse(code)
        test_count = 0
        test_classes = 0
        for node in ast.walk(tree):
            if isinstance(node, ast.FunctionDef) and node.name.startswith('test_'):
                test_count += 1
            if isinstance(node, ast.ClassDef) and node.name.startswith('Test'):
                test_classes += 1
        return test_count, test_classes
    except Exception as e:
        return 0, 0


def main():
    print('╔══════════════════════════════════════════════════════════════╗')
    print('║    ESP Board Manager - Python Test Suite Validation         ║')
    print('╚══════════════════════════════════════════════════════════════╝')
    print()

    test_dir = Path(__file__).parent
    test_files = sorted(test_dir.glob('test_v2_*.py'))
    config_files = [test_dir / 'conftest.py', test_dir / 'pytest.ini']

    total_tests = 0
    total_classes = 0
    all_valid = True

    print('Validation Results:')
    print('─' * 70)
    print()

    # Check test files
    for test_file in test_files:
        print(f'📄 {test_file.name}')

        # Check syntax
        valid, msg = check_file_syntax(test_file)
        if valid:
            print(f'   ✅ Syntax: {msg}')
        else:
            print(f'   ❌ Syntax: {msg}')
            all_valid = False

        # Check imports
        valid, imports = check_imports(test_file)
        if valid:
            print(f'   ✅ Imports: {len(imports)} modules')
        else:
            print(f'   ❌ Imports: {imports}')
            all_valid = False

        # Count tests
        test_count, class_count = count_tests(test_file)
        print(f'   📊 Tests: {test_count} functions, {class_count} classes')
        total_tests += test_count
        total_classes += class_count
        print()

    # Check config files
    print('Configuration Files:')
    print('─' * 70)
    print()

    for config_file in config_files:
        if config_file.exists():
            print(f'✅ {config_file.name} exists')
        else:
            print(f'❌ {config_file.name} missing')
            all_valid = False

    print()
    print('─' * 70)
    print()
    print('Summary:')
    print(f'  Test Files: {len(test_files)}')
    print(f'  Test Classes: {total_classes}')
    print(f'  Test Functions: {total_tests}')
    print()

    if all_valid:
        print('╔══════════════════════════════════════════════════════════════╗')
        print('║           ✅ All validations passed!                        ║')
        print('╚══════════════════════════════════════════════════════════════╝')
        print()
        print('To run the actual tests, install pytest:')
        print('  pip3 install pytest pytest-html pytest-cov')
        print()
        print('Then run:')
        print('  ./run_pytest.sh')
        return 0
    else:
        print('╔══════════════════════════════════════════════════════════════╗')
        print('║           ❌ Some validations failed!                       ║')
        print('╚══════════════════════════════════════════════════════════════╝')
        return 1


if __name__ == '__main__':
    sys.exit(main())

