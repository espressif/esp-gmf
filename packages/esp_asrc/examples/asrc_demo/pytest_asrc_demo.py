# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut

@pytest.mark.parametrize('target', ['esp32s3', 'esp32s31'], indirect=True)
@pytest.mark.temp_skip_ci(targets=['esp32', 'esp32s3', 'esp32p4'], reason='No running in CI')
def test_audio_asrc(dut: Dut)-> None:
    dut.expect(r'ASRC process finished', timeout=60)
