# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
Tests for utility functions
"""

import unittest
from pathlib import Path
import tempfile
import shutil

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from generators.utils.file_utils import should_skip_directory, should_skip_file, find_project_root
from generators.utils.yaml_utils import load_yaml_safe, save_yaml_safe, merge_yaml_data
from generators.settings import BoardManagerConfig


class TestFileUtils(unittest.TestCase):
    """Test file utility functions"""

    def test_should_skip_directory(self):
        """Test directory skipping logic"""
        # Should skip
        self.assertTrue(should_skip_directory('.git'))
        self.assertTrue(should_skip_directory('__pycache__'))
        self.assertTrue(should_skip_directory('build'))
        self.assertTrue(should_skip_directory('.vscode'))

        # Should not skip
        self.assertFalse(should_skip_directory('boards'))
        self.assertFalse(should_skip_directory('devices'))
        self.assertFalse(should_skip_directory('peripherals'))

    def test_should_skip_file(self):
        """Test file skipping logic"""
        # Should skip
        self.assertTrue(should_skip_file('.DS_Store'))
        self.assertTrue(should_skip_file('Thumbs.db'))

        # Should not skip
        self.assertFalse(should_skip_file('main.py'))
        self.assertFalse(should_skip_file('config.yml'))

    def test_find_project_root(self):
        """Test project root finding"""
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)

            # Create a mock project structure
            cmake_file = temp_path / 'CMakeLists.txt'
            components_dir = temp_path / 'components'

            cmake_file.touch()
            components_dir.mkdir()

            # Should find the project root
            found_root = find_project_root(temp_path)
            self.assertEqual(found_root, temp_path)

    def test_find_project_root_not_found(self):
        """Test project root finding when not found"""
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)

            # No CMakeLists.txt or components directory
            found_root = find_project_root(temp_path)
            self.assertIsNone(found_root)


class TestYamlUtils(unittest.TestCase):
    """Test YAML utility functions"""

    def test_load_yaml_safe(self):
        """Test safe YAML loading"""
        with tempfile.NamedTemporaryFile(mode='w', suffix='.yml', delete=False) as f:
            f.write('key: value\nnested:\n  key: nested_value\n')
            yaml_file = Path(f.name)

        try:
            data = load_yaml_safe(yaml_file)
            self.assertIsNotNone(data)
            self.assertEqual(data['key'], 'value')
            self.assertEqual(data['nested']['key'], 'nested_value')
        finally:
            yaml_file.unlink()

    def test_save_yaml_safe(self):
        """Test safe YAML saving"""
        test_data = {
            'key': 'value',
            'nested': {
                'key': 'nested_value'
            }
        }

        with tempfile.NamedTemporaryFile(suffix='.yml', delete=False) as f:
            yaml_file = Path(f.name)

        try:
            success = save_yaml_safe(yaml_file, test_data)
            self.assertTrue(success)

            # Verify the file was written correctly
            loaded_data = load_yaml_safe(yaml_file)
            self.assertEqual(loaded_data, test_data)
        finally:
            yaml_file.unlink()

    def test_merge_yaml_data(self):
        """Test YAML data merging"""
        base_data = {
            'key1': 'value1',
            'nested': {
                'key1': 'nested1',
                'key2': 'nested2'
            }
        }

        new_data = {
            'key2': 'value2',
            'nested': {
                'key1': 'new_nested1',
                'key3': 'nested3'
            }
        }

        merged = merge_yaml_data(base_data, new_data)

        # Check that new data takes precedence
        self.assertEqual(merged['key1'], 'value1')  # Unchanged
        self.assertEqual(merged['key2'], 'value2')  # New
        self.assertEqual(merged['nested']['key1'], 'new_nested1')  # Overridden
        self.assertEqual(merged['nested']['key2'], 'nested2')  # Unchanged
        self.assertEqual(merged['nested']['key3'], 'nested3')  # New


class TestBoardManagerConfig(unittest.TestCase):
    """Test BoardManagerConfig class"""

    def test_should_skip_directory(self):
        """Test directory skipping with config"""
        self.assertTrue(BoardManagerConfig.should_skip_directory('.git'))
        self.assertTrue(BoardManagerConfig.should_skip_directory('__pycache__'))
        self.assertFalse(BoardManagerConfig.should_skip_directory('boards'))

    def test_should_skip_file(self):
        """Test file skipping with config"""
        self.assertTrue(BoardManagerConfig.should_skip_file('.DS_Store'))
        self.assertFalse(BoardManagerConfig.should_skip_file('main.py'))


if __name__ == '__main__':
    unittest.main()
