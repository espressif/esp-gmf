# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest

from pytest_embedded import Dut

@pytest.mark.parametrize('target', ['esp32', 'esp32s31'], indirect=True)
def test_bt_audio(dut: Dut) -> None:
    dut.expect(r'Returned from app_main()', timeout=60)
