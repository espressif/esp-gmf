set(extra_component_path "$ENV{IDF_PATH}/examples/common_components/protocol_examples_common")

set(EXTRA_SRCS "${extra_component_path}/stdin_out.c"
         "${extra_component_path}/addr_from_stdin.c"
         "${extra_component_path}/connect.c"
         "${extra_component_path}/wifi_connect.c"
         "${extra_component_path}/protocol_examples_utils.c")

if(CONFIG_EXAMPLE_PROVIDE_WIFI_CONSOLE_CMD)
    list(APPEND EXTRA_SRCS "${extra_component_path}/console_cmd.c")
endif()

if(CONFIG_EXAMPLE_CONNECT_ETHERNET)
    list(APPEND EXTRA_SRCS "${extra_component_path}/eth_connect.c")
endif()

if(CONFIG_EXAMPLE_CONNECT_THREAD)
    list(APPEND EXTRA_SRCS "${extra_component_path}/thread_connect.c")
endif()

if(CONFIG_EXAMPLE_CONNECT_PPP)
    list(APPEND EXTRA_SRCS "${extra_component_path}/ppp_connect.c")
endif()

file(GLOB_RECURSE APP_SRCS "src/*.c")

idf_component_register(
    SRCS ${APP_SRCS} ${EXTRA_SRCS}
    INCLUDE_DIRS "include"  "${extra_component_path}/include"
    REQUIRES "sdmmc" "fatfs" "nvs_flash" "esp_driver_i2c" "unity"
    PRIV_REQUIRES esp_netif esp_driver_gpio esp_driver_uart esp_wifi vfs console esp_eth openthread
)

if(CONFIG_EXAMPLE_PROVIDE_WIFI_CONSOLE_CMD)
    idf_component_optional_requires(PRIVATE console)
endif()

if(CONFIG_EXAMPLE_CONNECT_ETHERNET)
    idf_component_optional_requires(PUBLIC esp_eth)
endif()

if(CONFIG_EXAMPLE_CONNECT_THREAD)
    idf_component_optional_requires(PRIVATE openthread)
endif()

if(CONFIG_EXAMPLE_CONNECT_PPP)
    idf_component_optional_requires(PRIVATE esp_tinyusb espressif__esp_tinyusb)
endif()
