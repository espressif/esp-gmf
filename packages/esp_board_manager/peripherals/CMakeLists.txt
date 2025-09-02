# Function to get peripheral sources
function(get_peripheral_sources sources includes)
    set(srcs "")
    set(incs "")

    if(CONFIG_ESP_BOARD_PERIPH_GPIO_SUPPORT)
        message(STATUS "GPIO_SUPPORT")
        list(APPEND srcs "${CMAKE_CURRENT_LIST_DIR}/peripherals/periph_gpio.c")
        list(APPEND incs "${CMAKE_CURRENT_LIST_DIR}/peripherals")
    endif()

    if(CONFIG_ESP_BOARD_PERIPH_I2C_SUPPORT)
        message(STATUS "I2C_SUPPORT")
        list(APPEND srcs "${CMAKE_CURRENT_LIST_DIR}/peripherals/periph_i2c.c")
        list(APPEND incs "${CMAKE_CURRENT_LIST_DIR}/peripherals")
    endif()

    if(CONFIG_ESP_BOARD_PERIPH_I2S_SUPPORT)
        message(STATUS "I2S_SUPPORT")
        list(APPEND srcs "${CMAKE_CURRENT_LIST_DIR}/peripherals/periph_i2s.c")
        list(APPEND incs "${CMAKE_CURRENT_LIST_DIR}/peripherals")
    endif()

    if(CONFIG_ESP_BOARD_PERIPH_SPI_SUPPORT)
        message(STATUS "LCD_SPI_SUPPORT")
        list(APPEND srcs "${CMAKE_CURRENT_LIST_DIR}/peripherals/periph_spi.c")
        list(APPEND incs "${CMAKE_CURRENT_LIST_DIR}/peripherals")
    endif()

    if(CONFIG_ESP_BOARD_PERIPH_LEDC_SUPPORT)
        message(STATUS "LEDC_SUPPORT")
        list(APPEND srcs "${CMAKE_CURRENT_LIST_DIR}/peripherals/periph_ledc.c")
        list(APPEND incs "${CMAKE_CURRENT_LIST_DIR}/peripherals")
    endif()

    # Set output variables in parent scope
    set(${sources} ${srcs} PARENT_SCOPE)
    set(${includes} ${incs} PARENT_SCOPE)
    # message(STATUS "sources: ${srcs}")
    # message(STATUS "includes: ${incs}")
endfunction()
