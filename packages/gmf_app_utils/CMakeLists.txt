get_directory_property(defined_defs COMPILE_DEFINITIONS)
message("Defined preprocessor variables-USER: ${defined_defs}")

idf_component_register(SRC_DIRS "./src"
                       INCLUDE_DIRS "./include"
                       REQUIRES "sdmmc" "fatfs" "nvs_flash" "esp_wifi" "console" "esp_driver_i2c" "esp_netif" "unity")
