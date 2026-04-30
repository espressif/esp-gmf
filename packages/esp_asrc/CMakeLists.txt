cmake_minimum_required(VERSION 3.16)

set(COMMON_INCLUDE ./include)
set(PRIV_INC ./private_inc ./sw/private_inc ./hw/private_inc)
set(COMMON_SRCS ./src ./sw/src ./hw/src)

set(PRIV_REQS "")
if(EXISTS "$ENV{IDF_PATH}/components/esp_asrc_adapter"
   AND EXISTS "$ENV{IDF_PATH}/components/esp_hal_asrc")
    list(APPEND PRIV_REQS esp_asrc_adapter esp_hal_asrc)
endif()
if(CONFIG_SOC_ASRC_SUPPORTED)
    list(APPEND PRIV_INC ./hw/driver ./hw/caps)
    list(APPEND COMMON_SRCS ./hw/driver ./hw/caps/${CONFIG_IDF_TARGET})
endif()

idf_component_register(SRC_DIRS ${COMMON_SRCS}
                       INCLUDE_DIRS ${COMMON_INCLUDE}
                       PRIV_INCLUDE_DIRS ${PRIV_INC}
                       PRIV_REQUIRES ${PRIV_REQS})
