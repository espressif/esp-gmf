set(COMPONENT_ADD_INCLUDEDIRS "oal/include" "include" "data_bus/include" "helpers/include")
set(COMPONENT_SRCDIRS src oal data_bus helpers)
set(COMPONENT_ADD_LDFRAGMENTS "linker.lf")

set(COMPONENT_REQUIRES esp_mm)

register_component()
