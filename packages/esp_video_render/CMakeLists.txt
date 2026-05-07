# Gather common sources
set(COMPONENT_SRC_DIRS "src" "src/impl" "src/vui")

set(COMPONENT_INCLUDE "./include" "include/vui")

set(COMPONENT_PRIV_INCLUDE "private_inc")

if(CONFIG_SOC_PPA_SUPPORTED)
    list(APPEND COMPONENT_PRIV_REQUIRES esp_driver_ppa esp_mm)
endif()

if(CONFIG_ESP_VIDEO_RENDER_WIDGET_SUPPORT)
if (CONFIG_ESP_VIDEO_RENDER_TEXT_WIDGET_SUPPORT)
    list(APPEND COMPONENT_SRC_DIRS "src/vui/text_widget")
endif()
    list(APPEND COMPONENT_SRC_DIRS "src/vui/image_widget")
endif()

idf_component_register(
    SRC_DIRS ${COMPONENT_SRC_DIRS}
    INCLUDE_DIRS ${COMPONENT_INCLUDE}
    PRIV_INCLUDE_DIRS ${COMPONENT_PRIV_INCLUDE}
    REQUIRES esp_timer esp_lcd
    PRIV_REQUIRES ${COMPONENT_PRIV_REQUIRES}
)
