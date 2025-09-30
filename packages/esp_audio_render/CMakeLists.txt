# Gather common sources
set(COMPONENT_SRC_DIRS "src")

set(COMPONENT_INCLUDE "./include")

set(COMPONENT_PRIV_INCLUDE "private_inc")

idf_component_register(
    SRC_DIRS ${COMPONENT_SRC_DIRS}
    INCLUDE_DIRS ${COMPONENT_INCLUDE}
    PRIV_INCLUDE_DIRS ${COMPONENT_PRIV_INCLUDE}
    REQUIRES esp_timer
)
