# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut

@pytest.mark.esp32
@pytest.mark.esp32s3
def test_bmgr_play_sdcard_music_str_detect(dut: Dut)-> None:
    dut.expect(r'Play WAV file completed.', timeout=30)
