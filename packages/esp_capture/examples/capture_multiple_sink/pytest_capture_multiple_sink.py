# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut


@pytest.mark.parametrize('target', ['esp32p4'], indirect=True)
@pytest.mark.temp_skip_ci(targets=['esp32', 'esp32s3', 'esp32p4'], reason='No running in CI')
def test_capture_multiple_sink(dut: Dut) -> None:
    # Four scenarios (stream / display / JPEG one-shot) can exceed 2 minutes on device.
    dut.expect(r'All scenarios finished', timeout=5 * 60)
