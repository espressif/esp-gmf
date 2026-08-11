# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "IDF_PATH not defined")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-Wno-stringop-truncation)

project(bt_audio)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang"
   OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16))
    idf_build_set_property(CXX_COMPILE_OPTIONS "-Wno-error=non-c-typedef-for-linkage" APPEND)
endif()

if("${IDF_TARGET}" STREQUAL "esp32s31")
    set(BT_UI_ASSETS_SOURCE "${CMAKE_SOURCE_DIR}/assets/assets_image.bin")
    set(BT_UI_ASSETS_IMAGE "${CMAKE_BINARY_DIR}/assets_image.bin")
    if(NOT EXISTS "${BT_UI_ASSETS_SOURCE}")
        message(FATAL_ERROR
            "Missing ${BT_UI_ASSETS_SOURCE}. "
            "Generate it with ~/esp/tools/bt_audio_assets/gen_assets.sh")
    endif()

    add_custom_command(
        OUTPUT "${BT_UI_ASSETS_IMAGE}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${BT_UI_ASSETS_SOURCE}" "${BT_UI_ASSETS_IMAGE}"
        DEPENDS "${BT_UI_ASSETS_SOURCE}"
        COMMENT "Copying assets partition image"
        VERBATIM
    )
    add_custom_target(bt_ui_assets_image ALL DEPENDS "${BT_UI_ASSETS_IMAGE}")
    esptool_py_flash_to_partition(flash "assets" "${BT_UI_ASSETS_IMAGE}")
endif()
