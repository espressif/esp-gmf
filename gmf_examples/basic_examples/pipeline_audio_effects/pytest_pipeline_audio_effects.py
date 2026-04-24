# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut

@pytest.mark.parametrize('target', ['esp32', 'esp32s3', 'esp32s31'], indirect=True)
def test_pipeline_audio_effects_demo(dut: Dut) -> None:
    dut.expect(r'PIPELINE_AUDIO_EFFECTS: Effect demo finished', timeout=120)
