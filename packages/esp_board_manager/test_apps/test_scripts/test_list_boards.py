"""
Test list boards functionality
Tests -l/--list-boards parameter
"""

import pytest
import re


class TestListBoardsBasic:
    """Basic list boards functionality"""

    def test_list_boards_with_short_flag(self, run_bmgr_cmd):
        """Test listing boards with -l flag"""
        result = run_bmgr_cmd(['-l'])
        assert result.returncode == 0
        assert 'Main Boards:' in result.stdout

    def test_list_boards_with_long_flag(self, run_bmgr_cmd):
        """Test listing boards with --list-boards flag"""
        result = run_bmgr_cmd(['--list-boards'])
        assert result.returncode == 0
        assert 'Main Boards:' in result.stdout

    def test_output_contains_boards(self, run_bmgr_cmd, board_count):
        """Test that output contains board entries"""
        result = run_bmgr_cmd(['-l'])
        assert result.returncode == 0
        # Count board entries with format [N]
        board_entries = re.findall(r'\[\d+\]', result.stdout)
        assert len(board_entries) == board_count


class TestBoardNumbering:
    """Test board numbering format and uniqueness"""

    def test_boards_numbered_sequentially(self, run_bmgr_cmd):
        """Test that boards are numbered sequentially"""
        result = run_bmgr_cmd(['-l'])
        assert result.returncode == 0
        assert '[1]' in result.stdout
        assert '[2]' in result.stdout
        assert '[3]' in result.stdout

    def test_each_board_has_unique_index(self, run_bmgr_cmd):
        """Test that each board has a unique index"""
        result = run_bmgr_cmd(['-l'])
        assert result.returncode == 0

        # Extract all indices
        indices = re.findall(r'\[(\d+)\]', result.stdout)
        # Check uniqueness
        assert len(indices) == len(set(indices)), 'Duplicate indices found'

    def test_numbering_starts_at_one(self, run_bmgr_cmd):
        """Test that numbering starts at 1"""
        result = run_bmgr_cmd(['-l'])
        assert result.returncode == 0
        indices = [int(x) for x in re.findall(r'\[(\d+)\]', result.stdout)]
        assert min(indices) == 1

    def test_numbering_is_consecutive(self, run_bmgr_cmd, board_count):
        """Test that numbering is consecutive (no gaps)"""
        result = run_bmgr_cmd(['-l'])
        assert result.returncode == 0
        indices = sorted([int(x) for x in re.findall(r'\[(\d+)\]', result.stdout)])
        expected = list(range(1, board_count + 1))
        assert indices == expected


class TestBoardCategories:
    """Test board categorization"""

    def test_main_boards_category_exists(self, run_bmgr_cmd):
        """Test that Main Boards category is present"""
        result = run_bmgr_cmd(['-l'])
        assert result.returncode == 0
        assert 'Main Boards:' in result.stdout

    def test_at_least_one_board_listed(self, run_bmgr_cmd, board_count):
        """Test that at least one board is listed"""
        result = run_bmgr_cmd(['-l'])
        assert result.returncode == 0
        assert board_count > 0, 'No boards found'


class TestListExitBehavior:
    """Test exit behavior and side effects"""

    def test_list_exits_successfully(self, run_bmgr_cmd):
        """Test that list command exits with code 0"""
        result = run_bmgr_cmd(['-l'])
        assert result.returncode == 0

    def test_list_boards_no_errors(self, run_bmgr_cmd):
        """Test that list command produces no errors"""
        result = run_bmgr_cmd(['-l'])
        assert result.returncode == 0
        # Check for common error indicators
        assert 'Error' not in result.stderr
        assert 'Failed' not in result.stderr


class TestListWithOtherOptions:
    """Test list boards combined with other options"""

    def test_list_ignores_board_parameter(self, run_bmgr_cmd, valid_board):
        """Test that -l ignores -b parameter"""
        result = run_bmgr_cmd(['-l', '-b', valid_board])
        assert result.returncode == 0
        assert 'Main Boards:' in result.stdout

    def test_list_with_log_level(self, run_bmgr_cmd):
        """Test list boards with --log-level"""
        result = run_bmgr_cmd(['-l', '--log-level', 'DEBUG'])
        assert result.returncode == 0
        assert 'Main Boards:' in result.stdout

    def test_list_with_customer_path(self, run_bmgr_cmd):
        """Test list boards with -c parameter"""
        result = run_bmgr_cmd(['-l', '-c', 'NONE'])
        assert result.returncode == 0
        assert 'Main Boards:' in result.stdout
