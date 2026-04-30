# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut

@pytest.mark.esp32s3
@pytest.mark.esp32p4
def test_fft_spectrum_print(dut: Dut) -> None:
    dut.expect(r'Round-trip peak error after scaling by N/4', timeout=120)
