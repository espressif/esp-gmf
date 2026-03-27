if((${IDF_TARGET} STREQUAL "esp32s3") OR (${IDF_TARGET} STREQUAL "esp32p4") OR (${IDF_TARGET} STREQUAL "esp32") OR (${IDF_TARGET} STREQUAL "esp32c3") OR (${IDF_TARGET} STREQUAL "esp32c5") OR (${IDF_TARGET} STREQUAL "esp32c6") OR  (${IDF_TARGET} STREQUAL "esp32s2"))
    set(srcs "./src/esp_gmf_wn.c" "./src/esp_gmf_aec.c" "./src/esp_gmf_afe_manager.c" "./src/esp_gmf_afe.c")

    idf_component_register(
        SRCS "${srcs}"
        INCLUDE_DIRS "./include"
        PRIV_INCLUDE_DIRS "./private_include"
        REQUIRES esp-sr esp_timer
    )
endif()