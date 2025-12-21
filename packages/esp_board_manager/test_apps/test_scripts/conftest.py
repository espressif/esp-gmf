"""
pytest configuration file for ESP Board Manager tests
Provides fixtures and common utilities
"""

import pytest
import subprocess
import os
from pathlib import Path


@pytest.fixture(scope='session')
def bmgr_root():
    """Get the board manager root directory"""
    test_dir = Path(__file__).parent
    # test_dir = .../test_apps/test_scripts
    # test_dir.parent = .../test_apps
    # test_dir.parent.parent = .../esp_board_manager (board manager root)
    return test_dir.parent.parent


@pytest.fixture(scope='session')
def script_path(bmgr_root):
    """Get the path to gen_bmgr_config_codes.py"""
    return bmgr_root / 'gen_bmgr_config_codes.py'


@pytest.fixture
def run_bmgr_cmd(script_path, bmgr_root):
    """Fixture to run board manager commands"""
    def _run(args, check=True):
        """
        Run board manager command with given arguments

        Args:
            args: List of command arguments
            check: Whether to check return code (default True)

        Returns:
            subprocess.CompletedProcess object
        """
        cmd = ['python3', str(script_path)] + args
        result = subprocess.run(
            cmd,
            cwd=str(bmgr_root),
            capture_output=True,
            text=True
        )
        if check and result.returncode != 0:
            print(f"Command failed: {' '.join(cmd)}")
            print(f'STDOUT:\n{result.stdout}')
            print(f'STDERR:\n{result.stderr}')
        return result
    return _run


@pytest.fixture(scope='session')
def board_list(script_path, bmgr_root):
    """Get list of available boards"""
    # Run command directly without depending on run_bmgr_cmd fixture
    cmd = ['python3', str(script_path), '-l']
    result = subprocess.run(
        cmd,
        cwd=str(bmgr_root),
        capture_output=True,
        text=True
    )
    boards = []
    for line in result.stdout.split('\n'):
        if line.strip().startswith('[') and ']' in line:
            # Extract board name from "[1] board_name" format
            parts = line.split(']', 1)
            if len(parts) == 2:
                board_name = parts[1].strip()
                boards.append(board_name)
    return boards


@pytest.fixture(scope='session')
def valid_board(board_list):
    """Get a valid board name for testing"""
    return board_list[0] if board_list else 'esp_box_3'


@pytest.fixture(scope='session')
def board_count(board_list):
    """Get total number of boards"""
    return len(board_list)

