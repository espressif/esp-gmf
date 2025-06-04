idf_component_register(SRC_DIRS "." "elements"
                       INCLUDE_DIRS "." "elements"
                       REQUIRES unity esp_codec_dev esp_gdbstub
                       EMBED_FILES "hi_lexin.pcm"
                       WHOLE_ARCHIVE)
