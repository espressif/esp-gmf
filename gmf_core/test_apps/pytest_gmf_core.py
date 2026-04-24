# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0
import pytest
from pytest_embedded import Dut


GMF_CORE_CASE_TIMEOUT_OVERRIDES = {
    'Read and write with RANDOM SIZE + RANDOM DELAY on different task': 25 * 60,
}


@pytest.mark.parametrize('target', ['esp32', 'esp32c3', 'esp32s3', 'esp32p4', 'esp32s31'], indirect=True)
@pytest.mark.timeout(8000)
@pytest.mark.ESP_GMF_ELEMENT
@pytest.mark.ESP_GMF_TASK
@pytest.mark.ESP_GMF_IO
@pytest.mark.ESP_GMF_RINGBUF
@pytest.mark.ESP_GMF_PBUF
@pytest.mark.ESP_GMF_BLOCK
@pytest.mark.ELEMENT_POOL
@pytest.mark.ELEMENT_PORT
@pytest.mark.ESP_GMF_METHOD
@pytest.mark.parametrize(
    'config',
    [
        'default',
    ],
    indirect=True,
)
def test_gmf_core(dut: Dut, unity_case_timeout: float) -> None:
    for case in dut.test_menu:
        if case.is_ignored or case.type not in ('normal', 'multi_stage'):
            continue
        dut.run_single_board_case(
            case.name,
            timeout=GMF_CORE_CASE_TIMEOUT_OVERRIDES.get(case.name, unity_case_timeout),
        )
