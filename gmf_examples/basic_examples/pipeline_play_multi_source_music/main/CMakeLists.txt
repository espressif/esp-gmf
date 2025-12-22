set(SRCS
        "play_multi_source_music.c"
        "command_interface/audio_commands.c"
        "audio_player/audio_multi_source_player.c"
    )
set(INCLUDE_DIRS
        "."
        "common"
        "audio_player"
        "command_interface"
    )

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS ${INCLUDE_DIRS}
)
