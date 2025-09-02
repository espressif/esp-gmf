idf_component_register(
    SRC_DIRS "."
    INCLUDE_DIRS "."
    REQUIRES esp_board_manager
)
set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY INTERFACE_LINK_LIBRARIES
            "-u g_esp_board_devices -u g_esp_board_device_handles -u g_esp_board_peripherals -u g_esp_board_periph_handles -u g_esp_board_info")
set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY INTERFACE_LINK_LIBRARIES "-u lcd_panel_factory_entry_t -u lcd_touch_factory_entry_t")
