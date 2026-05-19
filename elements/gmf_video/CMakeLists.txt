if (${IDF_TARGET} STREQUAL "esp32p4" OR ${IDF_TARGET} STREQUAL "esp32s31")
list(APPEND COMPONENT_REQUIRES  esp_driver_ppa esp_hw_support esp_mm)
if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "6.0")
list(APPEND COMPONENT_REQUIRES esp_driver_dma)
endif()
endif()

idf_component_register(SRC_DIRS "./src"
                       INCLUDE_DIRS "./include"
                       REQUIRES ${COMPONENT_REQUIRES}
                       PRIV_INCLUDE_DIRS "./private_inc")
