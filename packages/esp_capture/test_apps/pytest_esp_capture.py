# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded import Dut

@pytest.mark.parametrize('target', ['esp32s3', 'esp32p4', 'esp32s31'], indirect=True)
@pytest.mark.timeout(60000)
@pytest.mark.parametrize(
    'config',
    [
        'default',
    ],
    indirect=True,
)
def test_esp_capture(dut: Dut, unity_case_timeout: float) -> None:
    dut.run_all_single_board_cases(timeout=unity_case_timeout)
