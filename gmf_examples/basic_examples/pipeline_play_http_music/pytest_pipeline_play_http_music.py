# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut

@pytest.mark.parametrize('target', ['esp32', 'esp32s3', 'esp32s31'], indirect=True)
def test_pipeline_play_http_music_str_detect(dut: Dut) -> None:
    try:
        dut.expect(r'PIPELINE_PLAY_HTTP_MUSIC\: \[ 6 \] Destroy all the resources', timeout=60)
    except StopIteration as e:
        raise RuntimeError('DUT serial stream ended unexpectedly') from e
