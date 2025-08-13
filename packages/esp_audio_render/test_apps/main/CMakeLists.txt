idf_component_register(SRCS "audio_render_test.c" "main.c"
                       INCLUDE_DIRS "." "../../private_inc/"
                       PRIV_REQUIRES esp_timer
                       WHOLE_ARCHIVE)
