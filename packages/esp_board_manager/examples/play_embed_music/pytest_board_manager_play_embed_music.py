# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut

@pytest.mark.esp32
@pytest.mark.esp32s3
def test_bmgr_play_embed_music_str_detect(dut: Dut)-> None:
    dut.expect(r'Embedded WAV file playback completed', timeout=30)
