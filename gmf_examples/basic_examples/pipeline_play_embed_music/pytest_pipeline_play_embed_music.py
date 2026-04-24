# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut
from audio_test_package.audio_recorder.recorder_stream import Recorder

@pytest.mark.parametrize('target', ['esp32'], indirect=True)
@pytest.mark.AUDIO_LOOPBACK_ENV
def test_pipeline_play_embed_music_similarity_compare(dut: Dut)-> None:
    script_path = os.path.abspath(__file__)
    script_dir = os.path.dirname(script_path)
    filename = os.path.join(script_dir, 'output.wav')
    pipeline_id = os.getenv('CI_PIPELINE_ID')
    job_id = os.getenv('CI_JOB_ID')
    media_ci_server = os.getenv('MEDIA_CI_SERVER')

    recorder = Recorder(filename=filename, device='plughw:G3,0', channels=1, sample_rate=44100)
    dut.expect(r'Wait stop event to the pipeline and stop all the pipeline')
    # Start the Runner recording thread
    recorder.start()

    dut.expect(r'ESP_GMF_EMBED_FLASH: Closed, pos: 231725/231725')
    # Stop the Runner recording thread
    recorder.stop()

    # Upload audio to the server to calculate audio similarity
    url = f'{media_ci_server}/upload/?pipeline_id={pipeline_id}&job_id={job_id}&target_file=ff-16b-1c-44100hz.mp3'
    recorder.request_audio_similarity(url)

@pytest.mark.parametrize('target', ['esp32s3', 'esp32s31'], indirect=True)
def test_pipeline_play_embed_music_str_detect(dut: Dut)-> None:
    dut.expect(r'ESP_GMF_EMBED_FLASH: Closed, pos: 231725/231725', timeout=30)
