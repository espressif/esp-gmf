# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut

@pytest.mark.esp32
@pytest.mark.esp32s3
def test_pipeline_play_http_music_str_detect(dut: Dut)-> None:
    dut.expect(r'PIPELINE_PLAY_HTTP_MUSIC\: \[ 6 \] Destroy all the resources', timeout=30)
