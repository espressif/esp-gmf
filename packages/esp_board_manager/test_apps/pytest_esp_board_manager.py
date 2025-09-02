# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded import Dut

@pytest.mark.esp32s3
@pytest.mark.timeout(60000)
@pytest.mark.parametrize(
    'config',
    [
        'default',
    ],
    indirect=True,
)
def test_esp_board_manager(dut: Dut) -> None:
    dut.run_all_single_board_cases(timeout=3000)
