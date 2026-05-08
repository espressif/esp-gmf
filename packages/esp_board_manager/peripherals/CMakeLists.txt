# Function to get peripheral sources
function(get_peripheral_sources sources includes)
    set(srcs "")
    set(incs "")
    set(peripherals_dir "${CMAKE_CURRENT_LIST_DIR}/peripherals")

    if(CONFIG_ESP_BOARD_PERIPH_GPIO_SUPPORT)
        message(STATUS "GPIO_SUPPORT")
        bmgr_append_component_file(srcs incs "${peripherals_dir}" "periph_gpio" "periph_gpio.c")
    endif()

    if(CONFIG_ESP_BOARD_PERIPH_I2C_SUPPORT)
        message(STATUS "I2C_SUPPORT")
        bmgr_append_component_file(srcs incs "${peripherals_dir}" "periph_i2c" "periph_i2c.c")
    endif()

    if(CONFIG_ESP_BOARD_PERIPH_I2S_SUPPORT)
        message(STATUS "I2S_SUPPORT")
        bmgr_append_component_file(srcs incs "${peripherals_dir}" "periph_i2s" "periph_i2s.c")
    endif()

    if(CONFIG_ESP_BOARD_PERIPH_SPI_SUPPORT)
        message(STATUS "LCD_SPI_SUPPORT")
        bmgr_append_component_file(srcs incs "${peripherals_dir}" "periph_spi" "periph_spi.c")
    endif()

    if(CONFIG_ESP_BOARD_PERIPH_LEDC_SUPPORT)
        message(STATUS "LEDC_SUPPORT")
        bmgr_append_component_file(srcs incs "${peripherals_dir}" "periph_ledc" "periph_ledc.c")
    endif()

    if(CONFIG_ESP_BOARD_PERIPH_UART_SUPPORT)
        message(STATUS "UART_SUPPOR")
        bmgr_append_component_file(srcs incs "${peripherals_dir}" "periph_uart" "periph_uart.c")
    endif()

    if(CONFIG_ESP_BOARD_PERIPH_ADC_SUPPORT)
        message(STATUS "ADC_SUPPORT")
        bmgr_append_component_file(srcs incs "${peripherals_dir}" "periph_adc" "periph_adc.c")
    endif()

    if(CONFIG_ESP_BOARD_PERIPH_RMT_SUPPORT)
        message(STATUS "RMT_SUPPORT")
        bmgr_append_component_file(srcs incs "${peripherals_dir}" "periph_rmt" "periph_rmt.c")
    endif()

    if(CONFIG_ESP_BOARD_PERIPH_PCNT_SUPPORT)
        message(STATUS "PCNT_SUPPORT")
        bmgr_append_component_file(srcs incs "${peripherals_dir}" "periph_pcnt" "periph_pcnt.c")
    endif()

    if(CONFIG_ESP_BOARD_PERIPH_DAC_SUPPORT)
        message(STATUS "DAC_SUPPORT")
        bmgr_append_component_file(srcs incs "${peripherals_dir}" "periph_dac" "periph_dac.c")
    endif()

    if(CONFIG_ESP_BOARD_PERIPH_SDM_SUPPORT)
        message(STATUS "SDM_SUPPORT")
        bmgr_append_component_file(srcs incs "${peripherals_dir}" "periph_sdm" "periph_sdm.c")
    endif()

    if(CONFIG_ESP_BOARD_PERIPH_MCPWM_SUPPORT)
        message(STATUS "MCPWM_SUPPORT")
        bmgr_append_component_file(srcs incs "${peripherals_dir}" "periph_mcpwm" "periph_mcpwm.c")
    endif()

    if(CONFIG_ESP_BOARD_PERIPH_ANACMPR_SUPPORT)
        message(STATUS "ANACMPR_SUPPORT")
        bmgr_append_component_file(srcs incs "${peripherals_dir}" "periph_anacmpr" "periph_anacmpr.c")
    endif()

    if(CONFIG_ESP_BOARD_PERIPH_DSI_SUPPORT)
        message(STATUS "DSI_SUPPORT")
        bmgr_append_component_file(srcs incs "${peripherals_dir}" "periph_dsi" "periph_dsi.c")
    endif()

    if(CONFIG_ESP_BOARD_PERIPH_LDO_SUPPORT)
        message(STATUS "LDO_SUPPORT")
        bmgr_append_component_file(srcs incs "${peripherals_dir}" "periph_ldo" "periph_ldo.c")
    endif()

    # Set output variables in parent scope
    set(${sources} ${srcs} PARENT_SCOPE)
    set(${includes} ${incs} PARENT_SCOPE)
    # message(STATUS "sources: ${srcs}")
    # message(STATUS "includes: ${incs}")
endfunction()
