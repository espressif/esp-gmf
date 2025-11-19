"""
Test error handling and edge cases
Tests various error conditions and invalid inputs
"""

import pytest


class TestInvalidBoardSpecifications:
    """Test various invalid board specifications"""

    def test_nonexistent_board_name(self, run_bmgr_cmd):
        """Test non-existent board name"""
        result = run_bmgr_cmd(['-b', 'nonexistent_board', '--kconfig-only'], check=False)
        assert result.returncode != 0
        assert 'not found' in result.stdout or 'not found' in result.stderr

    def test_empty_board_name(self, run_bmgr_cmd):
        """Test empty board name"""
        result = run_bmgr_cmd(['-b', '', '--kconfig-only'], check=False)
        assert result.returncode != 0

    @pytest.mark.parametrize('invalid_index', ['9999', '0', '-5'])
    def test_board_index_out_of_range(self, run_bmgr_cmd, invalid_index):
        """Test board index out of range"""
        result = run_bmgr_cmd(['-b', invalid_index, '--kconfig-only'], check=False)
        assert result.returncode != 0
        assert ('out of range' in result.stdout or 'out of range' in result.stderr or
                'not found' in result.stdout or 'not found' in result.stderr)

    def test_non_numeric_invalid_board(self, run_bmgr_cmd):
        """Test non-numeric invalid board identifier"""
        result = run_bmgr_cmd(['-b', 'abc123xyz', '--kconfig-only'], check=False)
        assert result.returncode != 0


class TestCustomerPathHandling:
    """Test customer path error handling"""

    def test_nonexistent_customer_path(self, run_bmgr_cmd, valid_board):
        """Test non-existent customer path shows warning but continues"""
        result = run_bmgr_cmd(['-b', valid_board, '-c', '/nonexistent/path', '--kconfig-only'], check=False)
        # Should either show warning or fail gracefully
        assert result.returncode in [0, 1]

    def test_customer_path_none(self, run_bmgr_cmd, valid_board):
        """Test customer path 'NONE' is handled correctly"""
        result = run_bmgr_cmd(['-b', valid_board, '-c', 'NONE', '--kconfig-only'])
        assert result.returncode == 0


class TestSpecialCharacters:
    """Test handling of special characters in inputs"""

    @pytest.mark.parametrize('special_name', [
        'board with spaces',
        'board@#$%^&*',
        'board/with/slash',
        'board\\with\\backslash',
    ])
    def test_board_name_special_characters(self, run_bmgr_cmd, special_name):
        """Test board names with special characters"""
        result = run_bmgr_cmd(['-b', special_name, '--kconfig-only'], check=False)
        assert result.returncode != 0

    def test_very_long_board_name(self, run_bmgr_cmd):
        """Test extremely long board name"""
        long_name = 'a_very_long_board_name' * 10
        result = run_bmgr_cmd(['-b', long_name, '--kconfig-only'], check=False)
        assert result.returncode != 0


class TestHelpAndUsage:
    """Test help options"""

    @pytest.mark.parametrize('help_flag', ['-h', '--help'])
    def test_help_options(self, run_bmgr_cmd, help_flag):
        """Test help flags"""
        result = run_bmgr_cmd([help_flag], check=False)
        # Help should exit with 0 and show usage
        assert result.returncode == 0
        assert 'usage:' in result.stdout or 'Usage:' in result.stdout


class TestMissingRequiredInfo:
    """Test behavior with missing required information"""

    def test_running_without_board_specification(self, run_bmgr_cmd):
        """Test running without -b parameter"""
        result = run_bmgr_cmd(['--kconfig-only'], check=False)
        # May succeed (reading from sdkconfig) or fail gracefully
        assert result.returncode in [0, 1]


class TestConflictingOptions:
    """Test handling of conflicting options"""

    def test_multiple_modes_dont_crash(self, run_bmgr_cmd, valid_board):
        """Test that multiple modes don't cause crashes"""
        result = run_bmgr_cmd(['-b', valid_board, '--peripherals-only', '--devices-only'], check=False)
        # Should not crash
        assert result.returncode in [0, 1]


class TestInvalidArguments:
    """Test invalid command line arguments"""

    def test_unknown_argument(self, run_bmgr_cmd):
        """Test unknown command line argument"""
        result = run_bmgr_cmd(['--unknown-argument'], check=False)
        assert result.returncode != 0
        assert ('unrecognized arguments' in result.stderr or
                'error' in result.stderr.lower())

    def test_missing_argument_value(self, run_bmgr_cmd):
        """Test missing value for -b parameter"""
        result = run_bmgr_cmd(['-b'], check=False)
        assert result.returncode != 0

