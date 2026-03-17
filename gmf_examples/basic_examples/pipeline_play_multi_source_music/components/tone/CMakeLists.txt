include( ${PROJECT_DIR}/components/tone/esp_embed_tone.cmake )
idf_component_register( INCLUDE_DIRS "./"
                        EMBED_FILES ${COMPONENT_EMBED_TXTFILES})
