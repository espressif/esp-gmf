# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut


@pytest.mark.esp32
@pytest.mark.esp32s3
@pytest.mark.esp32p4
def test_pipeline_record_muxer_str_detect(dut: Dut)-> None:
    dut.expect(r'ESP_GMF_TASK: One times job is complete, del', timeout=20)
