# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded import Dut

# AFE / WakeNet cases stream embedded PCM through the models; allow time for the
# [leaks] teardown to emit Unity OK on P4 USB-Serial-JTAG.
DEFAULT_CASE_TIMEOUT = 5 * 60


@pytest.mark.parametrize('target', ['esp32', 'esp32s3', 'esp32p4', 'esp32s31'], indirect=True)
@pytest.mark.timeout(4000)
@pytest.mark.unity_case_timeout(DEFAULT_CASE_TIMEOUT)
@pytest.mark.parametrize(
    'config',
    [
        'default',
    ],
    indirect=True,
)
def test_gmf_ai_audio(dut: Dut, unity_case_timeout: float) -> None:
    dut.run_all_single_board_cases(timeout=unity_case_timeout)
