# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut

@pytest.mark.esp32
@pytest.mark.esp32s3
def test_pipeline_audio_effects_demo(dut: Dut) -> None:
    dut.expect(r'PIPELINE_AUDIO_EFFECTS: Effect demo finished', timeout=120)
