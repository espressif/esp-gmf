# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut

@pytest.mark.esp32
@pytest.mark.esp32s3
@pytest.mark.esp32p4
def test_pipeline_http_download_str_detect(dut: Dut)-> None:
    dut.expect(r'sub:ESP_GMF_EVENT_STATE_FINISHED', timeout=30)
    dut.expect(r'Http download and write to sdcard speed', timeout=30)
