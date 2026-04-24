# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut

@pytest.mark.parametrize('target', ['esp32s3', 'esp32s31'], indirect=True)
@pytest.mark.temp_skip_ci(targets=['esp32', 'esp32s3', 'esp32p4', 'esp32s31'], reason='No running in CI')
def test_audio_render(dut: Dut)-> None:
    dut.expect(r'Audio render test finished', timeout=120)
