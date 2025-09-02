# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
Tests for parser functions
"""

import unittest
from pathlib import Path
import tempfile
import shutil

# Import the modules we want to test
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from generators.name_validator import parse_component_name


class TestNameParser(unittest.TestCase):
    """Test name parser functionality"""

    def test_parse_component_name_function(self):
        """Test parse_component_name helper function"""
        # Valid name
        parser = parse_component_name('i2s-0-1')
        self.assertTrue(parser.valid)
        self.assertEqual(parser.label, 'i2s')
        self.assertEqual(parser.index, 0)
        self.assertEqual(parser.subindex, 1)

        # Test name property
        self.assertEqual(parser.name, 'i2s-0-1')
        self.assertEqual(parser.type, 'i2s')
        self.assertEqual(parser.c_name, 'i2s_0_1')

        # Test without subindex
        parser = parse_component_name('i2s-1')
        self.assertTrue(parser.valid)
        self.assertEqual(parser.label, 'i2s')
        self.assertEqual(parser.index, 1)
        self.assertEqual(parser.subindex, -1)
        self.assertEqual(parser.name, 'i2s-1')
        self.assertEqual(parser.c_name, 'i2s_1_0')

        # Test simple name
        parser = parse_component_name('es8311')
        self.assertTrue(parser.valid)
        self.assertEqual(parser.label, 'es8311')
        self.assertEqual(parser.index, -1)
        self.assertEqual(parser.subindex, -1)
        self.assertEqual(parser.name, 'es8311')
        self.assertEqual(parser.c_name, 'es8311_0_0')

        # Invalid name should raise ValueError
        with self.assertRaises(ValueError):
            parse_component_name('I2S')

        with self.assertRaises(ValueError):
            parse_component_name('i2s-0-1-2')  # Too many parts

        with self.assertRaises(ValueError):
            parse_component_name('i2s-abc')  # Invalid index


if __name__ == '__main__':
    unittest.main()
