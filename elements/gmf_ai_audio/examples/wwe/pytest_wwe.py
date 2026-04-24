# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest

from pytest_embedded import Dut

@pytest.mark.parametrize('target', ['esp32', 'esp32s3', 'esp32s31'], indirect=True)
@pytest.mark.temp_skip_ci(targets=['esp32', 'esp32s3', 'esp32p4'], reason='No running in CI')
def test_wwe(dut: Dut) -> None:
    try:
        dut.expect(r'Audio >', timeout=20)
    except StopIteration as e:
        raise RuntimeError('DUT serial stream ended unexpectedly') from e
