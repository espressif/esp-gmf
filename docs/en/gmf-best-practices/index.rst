ESP-GMF Best Practices
======================

:link_to_translation:`zh_CN:[中文]`

This chapter combines the example projects in ``gmf_examples`` to introduce the development workflow, typical usage, and advanced techniques for ESP-GMF in real projects, helping developers become familiar with GMF and integrate its functionality into product solutions.

`gmf_examples <https://github.com/espressif/esp-gmf/tree/main/gmf_examples>`__ is the officially maintained collection of GMF examples. The current basic examples are located in ``gmf_examples/basic_examples``\ , demonstrating fundamental usage such as pipeline construction, playback, recording, muxer container recording, audio effects, looping, multi-source switching, and HTTP download. All examples can be created directly via the ESP-IDF Component Manager:

.. code:: shell

    idf.py create-project-from-example "espressif/gmf_examples=<version>:<example_name>"

For example, to create a project based on ``pipeline_play_embed_music``:

.. code:: shell

    idf.py create-project-from-example "espressif/gmf_examples=0.8.0:pipeline_play_embed_music"

Basic feature examples include:

- `pipeline_play_embed_music <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_play_embed_music>`__: Play audio embedded in Flash
- `pipeline_play_sdcard_music <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_play_sdcard_music>`__: Play SD card music
- `pipeline_play_http_music <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_play_http_music>`__: Play HTTP/network music
- `pipeline_record_sdcard <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_record_sdcard>`__: Record and encode audio to SD card
- `pipeline_record_audio_muxer <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_record_audio_muxer>`__: Encode audio and mux into container formats, save to SD card
- `pipeline_record_http <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_record_http>`__: Record from microphone, encode and upload to HTTP server
- `pipeline_audio_effects <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_audio_effects>`__: Play audio with various effects and mixing
- `pipeline_howl <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_howl>`__: Decode backing track from SD card, apply howling suppression (``aud_howl``) on the mic path, mix and play to speaker
- `pipeline_loop_play_no_gap <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_loop_play_no_gap>`__: Seamless loop playback of multiple files
- `pipeline_play_multi_source_music <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_play_multi_source_music>`__: Multi-source audio player supporting playback from HTTP, SD card, and Flash
- `pipeline_http_download_to_sdcard <https://github.com/espressif/esp-gmf/tree/main/gmf_examples/basic_examples/pipeline_http_download_to_sdcard>`__: Download HTTP file and write to SD card with overall speed optimization

For component examples, see the ``test_apps/`` or ``examples/`` folder in each component directory, for example ``packages/esp_capture/examples``.

For more complete application examples, see `ESP-ADF examples <https://github.com/espressif/esp-adf/tree/master/adf_examples>`__. The ESP-ADF examples cover a broader range of audio application scenarios and can serve as a reference for ESP-GMF component composition and productization workflows.
