include( ${PROJECT_DIR}/main/esp_embed_tone.cmake )
idf_component_register(SRC_DIRS "./"
                       INCLUDE_DIRS "./"
                       REQUIRES unity esp_hw_support fatfs vfs sdmmc driver esp_wifi
                                nvs_flash
                       EMBED_FILES ${COMPONENT_EMBED_TXTFILES}
                       WHOLE_ARCHIVE )
