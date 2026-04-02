# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
Separated tests: board_peripherals.yaml vs board_devices.yaml
(empty / whitespace-only vs YAML syntax error).
"""

import unittest
import tempfile
import shutil
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from generators.peripheral_parser import PeripheralParser
from generators.device_parser import DeviceParser, load_yaml_with_includes
from generators.utils.yaml_utils import BoardConfigYamlError


BMGR_ROOT = Path(__file__).resolve().parent.parent.parent


class TestBoardPeripheralsYaml(unittest.TestCase):
    """board_peripherals.yaml — empty allowed; syntax error aborts."""

    def test_whitespace_only_file_yields_no_peripherals(self):
        with tempfile.NamedTemporaryFile(
            mode='w', suffix='.yaml', delete=False, encoding='utf-8'
        ) as f:
            f.write('  \n\t\n')
            path = f.name
        try:
            pp = PeripheralParser(BMGR_ROOT)
            d, m, types = pp.parse_peripherals_yaml(path)
            self.assertEqual(d, {})
            self.assertEqual(m, {})
            self.assertEqual(types, [])
        finally:
            Path(path).unlink(missing_ok=True)

    def test_explicit_empty_peripherals_list(self):
        with tempfile.NamedTemporaryFile(
            mode='w', suffix='.yaml', delete=False, encoding='utf-8'
        ) as f:
            f.write('peripherals: []\n')
            path = f.name
        try:
            pp = PeripheralParser(BMGR_ROOT)
            d, m, types = pp.parse_peripherals_yaml(path)
            self.assertEqual(d, {})
            self.assertEqual(types, [])
        finally:
            Path(path).unlink(missing_ok=True)

    def test_yaml_syntax_error_aborts(self):
        with tempfile.NamedTemporaryFile(
            mode='w', suffix='.yaml', delete=False, encoding='utf-8'
        ) as f:
            f.write('a: b: c\n')  # invalid YAML
            path = f.name
        try:
            pp = PeripheralParser(BMGR_ROOT)
            with self.assertRaises(BoardConfigYamlError) as ctx:
                pp.parse_peripherals_yaml(path)
            self.assertEqual(ctx.exception.reason, BoardConfigYamlError.REASON_YAML_SYNTAX)
        finally:
            Path(path).unlink(missing_ok=True)


class TestBoardDevicesYaml(unittest.TestCase):
    """board_devices.yaml — empty / no devices allowed; syntax error aborts."""

    def _devices_only_dir(self) -> Path:
        """Directory with only board_devices.yaml so load_yaml_with_includes does not merge extras."""
        d = Path(tempfile.mkdtemp())
        return d

    def test_whitespace_only_main_file_yields_empty_dict(self):
        d = self._devices_only_dir()
        try:
            main = d / 'board_devices.yaml'
            main.write_text('   \n', encoding='utf-8')
            data = load_yaml_with_includes(str(main))
            self.assertEqual(data, {})
            dp = DeviceParser(BMGR_ROOT)
            devices = dp.parse_devices_yaml_legacy(str(main), {})
            self.assertEqual(devices, [])
        finally:
            shutil.rmtree(d, ignore_errors=True)

    def test_explicit_empty_devices_list(self):
        d = self._devices_only_dir()
        try:
            main = d / 'board_devices.yaml'
            main.write_text('devices: []\n', encoding='utf-8')
            data = load_yaml_with_includes(str(main))
            self.assertEqual(data.get('devices'), [])
            dp = DeviceParser(BMGR_ROOT)
            devices = dp.parse_devices_yaml_legacy(str(main), {})
            self.assertEqual(devices, [])
        finally:
            shutil.rmtree(d, ignore_errors=True)

    def test_yaml_syntax_error_aborts(self):
        d = self._devices_only_dir()
        try:
            main = d / 'board_devices.yaml'
            main.write_text('a: b: c\n', encoding='utf-8')
            with self.assertRaises(BoardConfigYamlError) as ctx:
                load_yaml_with_includes(str(main))
            self.assertEqual(ctx.exception.reason, BoardConfigYamlError.REASON_YAML_SYNTAX)
        finally:
            shutil.rmtree(d, ignore_errors=True)


if __name__ == '__main__':
    unittest.main()
