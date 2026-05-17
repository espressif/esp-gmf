# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest

from pytest_embedded import Dut


@pytest.mark.parametrize('target', ['esp32s3', 'esp32p4', 'esp32s31'], indirect=True)
def test_video_player_finished(dut: Dut) -> None:
    dut.expect(r'Video playback finished', timeout=180)
