set(COMMON_SRCS "test_asrc_main.c" "asrc_quality_test.c" "asrc_branch_test.c" "asrc_test_sw.c")

if(CONFIG_SOC_ASRC_SUPPORTED)
    list(APPEND COMMON_SRCS "asrc_test_hw.c")
endif()

idf_component_register(SRCS ${COMMON_SRCS}
                       INCLUDE_DIRS "."
                       REQUIRES unity esp_mm
                       WHOLE_ARCHIVE
                       EMBED_TXTFILES test_16000_2_16.pcm 16_to_48_21.pcm thetest32_1.pcm)
