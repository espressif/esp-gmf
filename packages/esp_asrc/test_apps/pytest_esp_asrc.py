# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded import Dut

# Branch tests finish in seconds but need [leaks] reset path for Unity OK + esp_restart.
# SW matrix coverage can run ~10 minutes on P4.
ESP_ASRC_CASE_TIMEOUT_OVERRIDES = {
    'ASRC Branch Test': 120,
    'ASRC SW Basic Function Test': 900,
    'ASRC HW Basic Function Test': 600,
    'ASRC HW Function With Cache Stress Test': 600,
    'ASRC Multi Channel Test': 600,
}


@pytest.mark.parametrize('target', ['esp32s3', 'esp32p4', 'esp32s31'], indirect=True)
@pytest.mark.timeout(60000)
@pytest.mark.unity_case_timeout(900)
@pytest.mark.parametrize(
    'config',
    [
        'default',
    ],
    indirect=True,
)
def test_esp_asrc_test_app(dut: Dut, unity_case_timeout: float) -> None:
    for case in dut.test_menu:
        if case.is_ignored or case.type not in ('normal', 'multi_stage'):
            continue
        dut.run_single_board_case(
            case.name,
            timeout=ESP_ASRC_CASE_TIMEOUT_OVERRIDES.get(case.name, unity_case_timeout),
        )
