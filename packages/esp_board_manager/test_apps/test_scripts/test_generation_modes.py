"""
Test generation modes
Tests --peripherals-only, --devices-only, --kconfig-only
"""

import pytest


class TestKconfigOnlyMode:
    """Test Kconfig-only generation mode"""

    def test_kconfig_only_with_valid_board(self, run_bmgr_cmd, valid_board):
        """Test --kconfig-only mode succeeds"""
        result = run_bmgr_cmd(['-b', valid_board, '--kconfig-only'])
        assert result.returncode == 0
        assert 'Kconfig generation completed' in result.stdout

    def test_kconfig_only_skips_board_config(self, run_bmgr_cmd, valid_board):
        """Test that --kconfig-only skips board configuration generation"""
        result = run_bmgr_cmd(['-b', valid_board, '--kconfig-only'])
        assert result.returncode == 0
        assert ('Only Kconfig generation requested' in result.stdout or
                'skipping board configuration generation' in result.stdout)


class TestPeripheralsOnlyMode:
    """Test peripherals-only generation mode"""

    def test_peripherals_only_with_valid_board(self, run_bmgr_cmd, valid_board):
        """Test --peripherals-only mode succeeds"""
        result = run_bmgr_cmd(['-b', valid_board, '--peripherals-only'])
        assert result.returncode == 0
        assert 'Peripheral processing completed' in result.stdout

    def test_peripherals_only_skips_devices(self, run_bmgr_cmd, valid_board):
        """Test that --peripherals-only skips device processing"""
        result = run_bmgr_cmd(['-b', valid_board, '--peripherals-only'])
        assert result.returncode == 0
        assert 'Processing devices' in result.stdout and 'SKIPPED' in result.stdout


class TestDevicesOnlyMode:
    """Test devices-only generation mode"""

    def test_devices_only_with_valid_board(self, run_bmgr_cmd, valid_board):
        """Test --devices-only mode succeeds"""
        result = run_bmgr_cmd(['-b', valid_board, '--devices-only'])
        assert result.returncode == 0
        assert 'Device processing completed' in result.stdout

    def test_devices_only_loads_peripherals_for_reference(self, run_bmgr_cmd, valid_board):
        """Test that --devices-only loads peripherals for reference"""
        result = run_bmgr_cmd(['-b', valid_board, '--devices-only'])
        assert result.returncode == 0
        assert 'LOADING FOR REFERENCE' in result.stdout


class TestModeCombinations:
    """Test combinations of different modes"""

    def test_peripherals_only_with_kconfig(self, run_bmgr_cmd, valid_board):
        """Test combining --peripherals-only with --kconfig-only"""
        result = run_bmgr_cmd(['-b', valid_board, '--peripherals-only', '--kconfig-only'])
        assert result.returncode == 0
        assert 'Kconfig generation completed' in result.stdout

    def test_devices_only_with_kconfig(self, run_bmgr_cmd, valid_board):
        """Test combining --devices-only with --kconfig-only"""
        result = run_bmgr_cmd(['-b', valid_board, '--devices-only', '--kconfig-only'])
        assert result.returncode == 0
        assert 'Kconfig generation completed' in result.stdout

    def test_both_peripherals_and_devices_only(self, run_bmgr_cmd, valid_board):
        """Test using both --peripherals-only and --devices-only"""
        # This should handle gracefully, not crash
        result = run_bmgr_cmd(['-b', valid_board, '--peripherals-only', '--devices-only'], check=False)
        # Should not crash, exit code 0 or 1 is acceptable
        assert result.returncode in [0, 1]


class TestModeRequirements:
    """Test that modes require valid board selection"""

    @pytest.mark.parametrize('mode', [
        '--kconfig-only',
        '--peripherals-only',
        '--devices-only',
    ])
    def test_modes_require_valid_board(self, run_bmgr_cmd, mode):
        """Test that modes fail with invalid board"""
        result = run_bmgr_cmd(['-b', 'invalid_board', mode], check=False)
        assert result.returncode != 0
        assert 'not found' in result.stdout or 'not found' in result.stderr

