#!/usr/bin/env python3
import re

def is_valid_board_name(name):
    '''
    Check if board name is valid.
    Valid names can contain letters (uppercase/lowercase), numbers, and underscores.
    Must not contain hyphens or other special characters.
    '''
    return bool(re.match(r'^[a-zA-Z0-9_]+$', name))

def run_tests():
    test_cases = [
        ('valid_lower', 'my_board'),
        ('valid_upper', 'MyBoard'),
        ('valid_number_start', '1board'),
        ('valid_mixed', 'Board_V1'),
        ('valid_underscore', 'long_name_with_underscores'),
        ('invalid_hyphen', 'my-board'),
        ('invalid_space', 'my board'),
        ('invalid_dot', 'my.board'),
        ('invalid_at', 'board@1'),
        ('invalid_special', 'board!'),
    ]

    print('='*60)
    print('Testing Board Naming Rules')
    print('Rule: ^[a-zA-Z0-9_]+$ (Letters, Numbers, Underscores)')
    print('='*60)
    print(f'{"Name":<30} | {"Valid?":<10} | {"Result":<10}')
    print('-' * 60)

    all_passed = True
    for label, name in test_cases:
        is_valid = is_valid_board_name(name)

        # Determine expected result based on label
        expected = not label.startswith('invalid')
        result = 'PASS' if is_valid == expected else 'FAIL'

        if result == 'FAIL':
            all_passed = False

        print(f'{name:<30} | {str(is_valid):<10} | {result:<10}')

    print('-' * 60)
    if all_passed:
        print('\n✅ All board naming tests passed!')
    else:
        print('\n❌ Some tests failed!')
        sys.exit(1)

if __name__ == '__main__':
    import sys
    run_tests()
