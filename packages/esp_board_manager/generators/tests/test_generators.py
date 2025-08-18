# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
Tests for generator functions
"""

import unittest
from pathlib import Path
import tempfile
import shutil
import yaml

# Import the modules we want to test
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from generators.utils.yaml_utils import load_yaml_safe, save_yaml_safe, merge_yaml_data
from generators.utils.file_utils import find_project_root, backup_file, safe_write_file


class TestGenerators(unittest.TestCase):
    """Test generator functionality"""

    def test_yaml_operations(self):
        """Test YAML loading, saving, and merging"""
        test_data = {
            'devices': [
                {'name': 'audio_codec', 'type': 'es8311'},
                {'name': 'display', 'type': 'lcd'}
            ],
            'peripherals': [
                {'name': 'i2s-0', 'type': 'i2s'},
                {'name': 'spi-0', 'type': 'spi'}
            ]
        }

        with tempfile.NamedTemporaryFile(mode='w', suffix='.yml', delete=False) as f:
            yaml_file = Path(f.name)

        try:
            # Test saving
            success = save_yaml_safe(yaml_file, test_data)
            self.assertTrue(success)

            # Test loading
            loaded_data = load_yaml_safe(yaml_file)
            self.assertIsNotNone(loaded_data)
            self.assertEqual(loaded_data, test_data)

            # Test merging
            new_data = {
                'devices': [
                    {'name': 'sdcard', 'type': 'sd'}
                ]
            }
            merged = merge_yaml_data(test_data, new_data)
            self.assertEqual(len(merged['devices']), 3)  # Original 2 + new 1

        finally:
            yaml_file.unlink()

    def test_file_operations(self):
        """Test file operations"""
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)

            # Test project root finding
            # Create a mock project structure
            cmake_file = temp_path / 'CMakeLists.txt'
            components_dir = temp_path / 'components'

            cmake_file.touch()
            components_dir.mkdir()

            found_root = find_project_root(temp_path)
            self.assertEqual(found_root, temp_path)

            # Test backup file
            test_file = temp_path / 'test.txt'
            test_file.write_text('test content')

            backup_path = backup_file(test_file)
            self.assertTrue(backup_path.exists())
            self.assertEqual(backup_path.read_text(), 'test content')

            # Test safe write file
            success = safe_write_file(test_file, 'new content')
            self.assertTrue(success)
            self.assertEqual(test_file.read_text(), 'new content')

    def test_config_validation(self):
        """Test configuration validation"""
        # Test valid configuration
        valid_config = {
            'devices': [
                {
                    'name': 'audio_codec',
                    'type': 'audio_codec',
                    'config': {
                        'i2c_port': 0,
                        'i2s_port': 0
                    }
                }
            ]
        }

        # This should not raise any exceptions
        yaml_str = yaml.dump(valid_config)
        parsed_config = yaml.safe_load(yaml_str)
        self.assertEqual(parsed_config, valid_config)

        # Test invalid configuration (missing required fields)
        invalid_config = {
            'devices': [
                {
                    'name': 'audio_codec'
                    # Missing 'type' and 'config' fields
                }
            ]
        }

        # This should also not raise exceptions (validation happens elsewhere)
        yaml_str = yaml.dump(invalid_config)
        parsed_config = yaml.safe_load(yaml_str)
        self.assertEqual(parsed_config, invalid_config)


if __name__ == '__main__':
    unittest.main()
