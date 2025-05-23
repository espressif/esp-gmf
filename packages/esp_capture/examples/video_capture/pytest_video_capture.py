# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut

@pytest.mark.esp32s3
@pytest.mark.temp_skip_ci(targets=['esp32', 'esp32s3', 'esp32p4'], reason='No running in CI')
def test_video_capture(dut: Dut)-> None:
    dut.expect(r'All case finished', timeout=60)
