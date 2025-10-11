# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut

@pytest.mark.esp32
@pytest.mark.esp32s3
def test_pipeline_record_http_str_detect(dut: Dut)-> None:
    dut.expect(r'REC_HTTP: Got HTTP Response =', timeout=30)
