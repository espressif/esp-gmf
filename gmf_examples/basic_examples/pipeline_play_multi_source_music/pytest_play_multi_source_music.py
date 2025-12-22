# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded import Dut

@pytest.mark.esp32
@pytest.mark.esp32s3
@pytest.mark.esp32p4
def test_cli(dut: Dut) -> None:
    dut.expect(r'Entering main application loop', timeout=15)

    dut.write('help')
    dut.expect(r'Exit the application')

    dut.write('tone')
    dut.expect(r'Starting flash tone playback', timeout=15)

    dut.write('exit')
    dut.expect(r'Application cleanup completed', timeout=10)
