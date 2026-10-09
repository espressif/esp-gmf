set(ui_srcs "")
if(CONFIG_EXAMPLE_BT_UI_ENABLE)
    list(APPEND ui_srcs "bt_ui/bt_ui.c"
         "bt_ui/assets/bt_ui_assets.c")
endif()

set(reqs "bt" "nvs_flash" "console" "esp_board_manager" "esp_codec_dev" "esp_lvgl_port" "lvgl" "esp_jpeg")
if(CONFIG_EXAMPLE_BT_UI_ENABLE)
    list(APPEND reqs "esp_partition")
endif()

idf_component_register(SRCS "main.c" "pool_reg.c" "cmd_reg.c" "stream_proc.c"
                    ${ui_srcs}
                    INCLUDE_DIRS "." "bt_ui"
                    REQUIRES ${reqs}
                    )
