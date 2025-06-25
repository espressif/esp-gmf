# Gather common sources
set(COMPONENT_SRC_DIRS
    "src"
    "src/utils"
    "impl/capture_gmf_path/src"
    "impl/capture_gmf_path/src/elements"
    "impl/capture_file_src"
)

set(COMPONENT_INCLUDE
  "./include" ./include/impl "./interface"
  "impl/capture_gmf_path/include"
)

set(COMPONENT_PRIV_INCLUDE
    "private_inc"
    "impl/capture_gmf_path/include/elements"
)

if(CONFIG_ESP_CAPTURE_ENABLE_AUDIO)
    list(APPEND COMPONENT_SRC_DIRS "impl/capture_audio_src")
endif()

if(CONFIG_ESP_CAPTURE_ENABLE_VIDEO)
    list(APPEND COMPONENT_SRC_DIRS
        "impl/capture_video_src"
        "impl/capture_text_overlay"
        "impl/capture_text_overlay/font"
    )
    list(APPEND COMPONENT_PRIV_INCLUDE
        "impl/capture_text_overlay"
    )
endif()

idf_component_register(
    SRC_DIRS ${COMPONENT_SRC_DIRS}
    INCLUDE_DIRS ${COMPONENT_INCLUDE}
    PRIV_INCLUDE_DIRS ${COMPONENT_PRIV_INCLUDE}
    REQUIRES esp_timer
)

include(package_manager)
cu_pkg_define_version(${CMAKE_CURRENT_LIST_DIR})
