# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest

from pytest_embedded import Dut

@pytest.mark.esp32
@pytest.mark.esp32s3
def test_bmgr_record_and_play_str_detect(dut: Dut) -> None:
    dut.expect(r'Audio loopback completed\.', timeout=40)
