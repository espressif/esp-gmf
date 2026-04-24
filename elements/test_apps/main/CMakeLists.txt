idf_component_register(SRC_DIRS "." "elements"
                       INCLUDE_DIRS "." "elements"
                       REQUIRES unity esp_codec_dev esp_gdbstub
                       EMBED_FILES "test_48000hz_16bit_2ch_5000ms.flac"
                       WHOLE_ARCHIVE)
