# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut


@pytest.mark.parametrize('target', ['esp32', 'esp32s3', 'esp32p4', 'esp32s31'], indirect=True)
def test_pipeline_record_muxer_str_detect(dut: Dut)-> None:
    dut.expect(r'ESP_GMF_TASK: One times job is complete, del', timeout=20)
