# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut

@pytest.mark.parametrize('target', ['esp32', 'esp32s3', 'esp32p4', 'esp32s31'], indirect=True)
def test_pipeline_http_download_str_detect(dut: Dut)-> None:
    # ~2.6 MB HTTP + SD write can exceed 30s on CI Wi-Fi (see pipeline_loop_play_no_gap: 80s).
    dut.expect(r'sub:ESP_GMF_EVENT_STATE_FINISHED', timeout=120)
    dut.expect(r'Http download and write to sdcard speed', timeout=20)
