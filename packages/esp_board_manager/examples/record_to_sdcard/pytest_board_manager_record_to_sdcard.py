# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut

@pytest.mark.esp32
@pytest.mark.esp32s3
def test_bmgr_record_to_sdcard_str_detect(dut: Dut)-> None:
    dut.expect(r'Record example finished.', timeout=30)
