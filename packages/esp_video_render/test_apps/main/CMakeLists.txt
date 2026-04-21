idf_component_register(SRCS "video_render_test.c" "main.c" "video_render_sys.c"
                       "video_pattern.c" "clock_widget.c" "xiaozhi_panel_widget.c"
                       "dual_eyes_test.c" "fake_backend.c" "video_compose_test.c" "video_proc_test.c"
                       INCLUDE_DIRS "." "../../private_inc/"
                       PRIV_REQUIRES esp_timer
                       WHOLE_ARCHIVE)

get_property(COMPONENT_LIB_NAME TARGET ${COMPONENT_LIB} PROPERTY NAME)
target_add_binary_data(${COMPONENT_LIB_NAME} "../../assets/DejaVuSans.ttf" TEXT)
target_add_binary_data(${COMPONENT_LIB_NAME} "../../assets/NotoEmoji-Regular.ttf" TEXT)
target_add_binary_data(${COMPONENT_LIB_NAME} "../../assets/NotoColorEmojiBitmap-Subset.ttf" TEXT)
