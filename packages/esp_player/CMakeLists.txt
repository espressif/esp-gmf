set(srcs
        "src/core/esp_player.c"
        "src/core/esp_player_advance.c"
        "src/core/player_stream.c"
        "src/core/player_cfg.c"
        "src/core/player_events.c"
        "src/core/player_helper.c"
        "src/core/player_state.c"
        "src/utils/player_url.c"
        "src/sync/player_sync.c"
        "src/sync/player_data_bus.c"
        "src/extractor/player_extractor.c"
        "src/extractor/player_extractor_io.c"
        "src/pipeline/player_pipeline.c"
        "src/pipeline/player_pipe_events.c"
        "src/pipeline/player_ports.c"
        "src/pipeline/player_submit_frame.c"
        )

if(NOT CONFIG_ESP_PLAYER_ENABLE_AUDIO)
    list(APPEND srcs "src/audio/player_stubs_audio.c")
endif()

if(NOT CONFIG_ESP_PLAYER_ENABLE_VIDEO)
    list(APPEND srcs "src/video/player_stubs_video.c")
endif()

if(CONFIG_ESP_PLAYER_ENABLE_AUDIO)
    list(APPEND srcs
        "src/audio/player_glue_audio.c"
        "src/audio/player_pipeline_audio.c"
        "src/audio/player_state_audio.c"
        "src/audio/player_dec_cfg.c"
        "src/audio/player_audio_render.c"
        "src/audio/player_pipe_events_audio.c"
        "src/audio/player_ports_audio.c"
        )
endif()

if(CONFIG_ESP_PLAYER_ENABLE_VIDEO)
    list(APPEND srcs
        "src/video/player_glue_video.c"
        "src/video/player_pipeline_video.c"
        "src/video/player_state_video.c"
        "src/video/player_video_render.c"
        "src/video/player_pipe_events_video.c"
        "src/video/player_ports_video.c"
        )
endif()

set(requires
        gmf_core
)

set(priv_requires
        gmf_io
        esp_extractor
)

if(CONFIG_ESP_PLAYER_ENABLE_HLS_IO)
    list(APPEND priv_requires esp_hls_stream)
endif()

if(CONFIG_ESP_PLAYER_ENABLE_AUDIO)
    list(APPEND priv_requires gmf_audio esp_audio_render)
endif()

if(CONFIG_ESP_PLAYER_ENABLE_VIDEO)
    list(APPEND priv_requires gmf_video esp_video_codec esp_video_render)
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src/core/private_inc"
                                         "src/extractor/private_inc"
                                         "src/pipeline/private_inc"
                                         "src/sync/private_inc"
                                         "src/utils/private_inc"
                                         "src/audio/private_inc"
                                         "src/video/private_inc"
                       REQUIRES ${requires}
                       PRIV_REQUIRES ${priv_requires}
                       WHOLE_ARCHIVE TRUE)
