# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded import Dut

@pytest.mark.parametrize('target', ['esp32', 'esp32s3', 'esp32p4', 'esp32s31'], indirect=True)
def test_cli(dut: Dut) -> None:
    dut.expect(r'Entering main application loop', timeout=30)

    dut.write('help')
    dut.expect(r'Exit the application')

    dut.write('tone')
    dut.expect(r'Starting flash tone playback', timeout=30)

    dut.write('exit')
    dut.expect(r'Application cleanup completed', timeout=10)
