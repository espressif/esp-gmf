set(MAIN_TEST_SRCS
    "gmf_fft_test_runner.c"
    "test_gmf_fft_api.c"
    "test_gmf_fft_forward_vs_float.c"
)

idf_component_register(
    SRCS "app_main.c" ${MAIN_TEST_SRCS}
    INCLUDE_DIRS "."
    PRIV_REQUIRES gmf_fft esp_timer unity
)
