# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded import Dut

# Most cases finish quickly; multitask pause/stop/run waits on tasks + codec on CI.
DEFAULT_CASE_TIMEOUT = 5 * 60

SIMPLE_PLAYER_CASE_TIMEOUT_OVERRIDES = {
    'Pause, Stop, and Run APIs for Multi-task Execution': 10 * 60,
}


@pytest.mark.parametrize('target', ['esp32', 'esp32c3', 'esp32s3', 'esp32p4', 'esp32s31'], indirect=True)
@pytest.mark.timeout(8000)
@pytest.mark.unity_case_timeout(DEFAULT_CASE_TIMEOUT)
@pytest.mark.parametrize(
    'config',
    [
        'default',
    ],
    indirect=True,
)
def test_esp_audio_simple_player(dut: Dut, unity_case_timeout: float) -> None:
    for case in dut.test_menu:
        if case.is_ignored or case.type not in ('normal', 'multi_stage'):
            continue
        dut.run_single_board_case(
            case.name,
            timeout=SIMPLE_PLAYER_CASE_TIMEOUT_OVERRIDES.get(case.name, unity_case_timeout),
        )
