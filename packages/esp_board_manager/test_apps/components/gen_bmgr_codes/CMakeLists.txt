idf_component_register(
    SRC_DIRS "."
    INCLUDE_DIRS "."
    REQUIRES
)

# This is equivalent to adding WHOLE_ARCHIVE option to the idf_component_register call above:
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
