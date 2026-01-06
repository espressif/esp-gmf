# Changelog

## v0.7.2~1

### Bug Fixes

- Removed CONFIG_AUDIO_BOARD and CODEC_I2C_BACKWARD_COMPATIBLE in sdkconfig.defaults

## v0.7.2

### Features

- Added API `esp_audio_render_set_solo_stream` for solo stream playback

## v0.7.1

### Features

- Added API `esp_audio_render_task_reconfigure` for reconfiguration of render task
- Added `process_buf_align` to allow use special aligned buffer for audio processor
- Added API `esp_audio_render_stream_set_mixer_gain` for changing of stream mixer gain
- Added API `esp_audio_render_stream_set_fade` to do fade in/out when stream is mixing
- Changed `ESP_AUDIO_RENDER_MIXER_THREAD_PRIORITY` default value to 20

### Bug Fixes

- Fixed double free caused by pipeline failed to create
- Fixed mixer thread can not restart when all stream close and reopen
- Warning for writing to ringfifo if timeout to prevent from resisting for further write


## v0.7.0

### Features

- Initial version of `esp_audio_render`
