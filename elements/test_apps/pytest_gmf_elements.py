# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded import Dut
from pytest_embedded_idf.unity_tester import UnittestMenuCase

# Default per-case timeout: allow [leaks] teardown + Unity OK on P4 USB-Serial-JTAG.
DEFAULT_CASE_TIMEOUT = 5 * 60

# Recorder / video / if-check cases run late in the full menu and often need extra time
# for IIS+SDIO pipelines or post-PASS leak checks on CI runners.
SLOW_CASE_TIMEOUT = 10 * 60

# HTTP / network streaming cases may need WiFi and remote I/O on CI runners.
NETWORK_CASE_TIMEOUT = 20 * 60

# Local file pipelines that iterate multiple URLs without reopening the stream.
LONG_FILE_PIPELINE_CASE_TIMEOUT = 10 * 60

LONG_FILE_PIPELINE_CASES = {
    'Audio File Stream Play Different URL Without Close, One pipeline',
    'Audio File Stream Play Different URL Without Close, Two pipeline',
}

RECORDER_FILE_CASES = {
    'Recorder, One Pipe, [IIS->ENC->FILE]',
    'Recorder, One Pipe recoding multiple format, [IIS->ENC->FILE]',
}

SLOW_CASE_KEYWORDS = frozenset({
    'ESP_GMF_VIDEO',
    'ESP_GMF_IF_CHECK',
})


def _is_network_case(case: UnittestMenuCase) -> bool:
    name = case.name
    if 'IO_HTTP' in case.keywords:
        return True
    if 'HTTP' in name or 'Http Stream' in name:
        return True
    return False


def _is_slow_case(case: UnittestMenuCase) -> bool:
    if case.name in RECORDER_FILE_CASES:
        return True
    return any(kw in case.keywords for kw in SLOW_CASE_KEYWORDS)


def _resolve_case_timeout(case: UnittestMenuCase) -> float:
    if _is_network_case(case):
        return NETWORK_CASE_TIMEOUT
    if case.name in LONG_FILE_PIPELINE_CASES:
        return LONG_FILE_PIPELINE_CASE_TIMEOUT
    if _is_slow_case(case):
        return SLOW_CASE_TIMEOUT
    return DEFAULT_CASE_TIMEOUT


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
def test_gmf_elements(dut: Dut, unity_case_timeout: float) -> None:
    for case in dut.test_menu:
        if case.is_ignored or case.type not in ('normal', 'multi_stage'):
            continue
        dut.run_single_board_case(
            case.name,
            timeout=_resolve_case_timeout(case),
        )
