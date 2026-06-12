set(srcs
 "main.c"
 "test_data.c"
 "test_player.c"
 "render_common.c"
 "test_interface.c"
 "test_scenario.c"
 "test_custom_decoder.c"
)

set(priv_requires
        esp_player
        esp_audio_render
        esp_video_codec
        esp_video_render
        gmf_video
        gmf_app_utils
        esp_board_manager
        esp_lcd
        fatfs
        unity
        esp_netif
        esp_wifi
        heap
        media_lib_sal
)

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES ${priv_requires}
                       REQUIRES
                       WHOLE_ARCHIVE TRUE)

