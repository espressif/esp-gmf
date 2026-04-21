idf_component_register(
    SRCS
        "main.c"
        "video_player_app.c"
        "video_player_view.c"
        "video_render_sys.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        console
)

get_property(COMPONENT_LIB_NAME TARGET ${COMPONENT_LIB} PROPERTY NAME)
target_add_binary_data(${COMPONENT_LIB_NAME} "../../../assets/DejaVuSans.ttf" TEXT)
