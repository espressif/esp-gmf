idf_component_register(SRCS "main.c" "capture_test.c" "capture_builder.c"
                       "capture_fake_aud_src.c"  "capture_fake_vid_src.c"
                       "test_cxx_build.cpp"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES  esp_timer esp_muxer
                       WHOLE_ARCHIVE)
