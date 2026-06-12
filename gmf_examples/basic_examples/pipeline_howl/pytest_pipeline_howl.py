# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded import Dut


@pytest.mark.parametrize('target', ['esp32s3', 'esp32s31'], indirect=True)
def test_pipeline_howl_finish_log(dut: Dut) -> None:
    dut.expect(r'PIPELINE_HOWL: Howl demo finished', timeout=120)
