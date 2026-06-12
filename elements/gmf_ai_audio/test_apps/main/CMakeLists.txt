idf_component_register(SRC_DIRS "."
                      INCLUDE_DIRS "."
                      REQUIRES unity esp_gdbstub
                      EMBED_FILES "ns_input_sample.pcm" "hi_lexin.pcm" "doa.pcm"
                      WHOLE_ARCHIVE)
