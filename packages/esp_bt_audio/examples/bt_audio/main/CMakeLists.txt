set(ui_srcs "")
if(CONFIG_EXAMPLE_BT_UI_ENABLE)
    list(APPEND ui_srcs "bt_ui/bt_ui.c"
         "bt_ui/images/cis_stream_icon.c"
         "bt_ui/images/bis_stream_icon.c"
         "bt_ui/images/speakers_dark_dark_small.c"
         "bt_ui/images/speakers_dark_light_small.c"
         "bt_ui/images/speakers_light_dark_small.c"
         "bt_ui/fonts/lv_font_notosanssc_regular_28.c")
endif()

set(reqs "bt" "nvs_flash" "console" "esp_board_manager" "esp_codec_dev" "esp_lvgl_port" "lvgl" "esp_jpeg")

idf_component_register(SRCS "main.c" "pool_reg.c" "cmd_reg.c" "stream_proc.c"
                    ${ui_srcs}
                    INCLUDE_DIRS "." "bt_ui"
                    REQUIRES ${reqs}
                    )
