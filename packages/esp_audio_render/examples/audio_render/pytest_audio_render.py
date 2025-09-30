# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut

@pytest.mark.esp32s3
@pytest.mark.temp_skip_ci(targets=['esp32', 'esp32s3', 'esp32p4'], reason='No running in CI')
def test_audio_render(dut: Dut)-> None:
    dut.expect(r'Audio render test finished', timeout=60)
