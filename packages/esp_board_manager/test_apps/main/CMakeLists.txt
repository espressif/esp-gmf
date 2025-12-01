idf_component_register(
    SRCS
        # Main application
        "main.c"

        # Peripheral tests (always included)
        "test_periph_i2c.c"
        "test_periph_gpio.c"

        # Device tests (conditionally included based on Kconfig)
        $<$<BOOL:${CONFIG_ESP_BOARD_DEV_AUDIO_CODEC_SUPPORT}>:test_dev_audio_codec.c>
        $<$<OR:$<BOOL:${CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SUPPORT}>,$<BOOL:${CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SPI_SUPPORT}>>:test_dev_fatfs_sdcard.c>
        $<$<BOOL:${CONFIG_ESP_BOARD_DEV_FS_SPIFFS_SUPPORT}>:test_dev_fs_spiffs.c>
        $<$<BOOL:${CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SPI_SUPPORT}>:test_dev_lcd_lvgl.c>
        $<$<BOOL:${CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SPI_SUPPORT}>:test_dev_lcd_init.c>
        $<$<BOOL:${CONFIG_ESP_BOARD_DEV_GPIO_CTRL_SUPPORT}>:test_dev_pwr_ctrl.c>
        $<$<BOOL:${CONFIG_ESP_BOARD_DEV_LEDC_CTRL_SUPPORT}>:test_dev_ledc.c>
        $<$<BOOL:${CONFIG_ESP_BOARD_DEV_CUSTOM_SUPPORT}>:test_dev_custom.c>
        $<$<BOOL:${CONFIG_ESP_BOARD_DEV_GPIO_EXPANDER_SUPPORT}>:test_dev_gpio_expander.c>
        $<$<BOOL:${CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT}>:test_dev_camera.c>

        # Audio board manager test (conditionally included based on SD card support)
        $<$<AND:$<BOOL:${CONFIG_ESP_BOARD_DEV_AUDIO_CODEC_SUPPORT}>,$<OR:$<BOOL:${CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SUPPORT}>,$<BOOL:${CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SPI_SUPPORT}>>>:test_board_mgr_audio_sdcard.c>
        $<$<AND:$<BOOL:${CONFIG_ESP_BOARD_DEV_AUDIO_CODEC_SUPPORT}>,$<NOT:$<OR:$<BOOL:${CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SUPPORT}>,$<BOOL:${CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SPI_SUPPORT}>>>>:test_board_mgr_audio_embed.c>

        # Utility files (conditionally included)
        $<$<BOOL:${CONFIG_ESP_BOARD_DEV_AUDIO_CODEC_SUPPORT}>:wav_header.c>
    INCLUDE_DIRS "."
    REQUIRES esp_board_manager esp_lcd esp_timer esp_partition spiffs gen_bmgr_codes
    EMBED_TXTFILES audio_files/test.wav
)

# Dynamically link LVGL components when LCD SPI support is enabled
idf_build_get_property(build_components BUILD_COMPONENTS)

# Check if lvgl component is available (using string matching)
foreach(comp ${build_components})
    if(comp MATCHES "lvgl")
        message(STATUS, "Found LVGL component: ${comp}")
        idf_component_get_property(lvgl_lib ${comp} COMPONENT_LIB)
        target_link_libraries(${COMPONENT_LIB} PRIVATE ${lvgl_lib})
        break()
    endif()
endforeach()

# Check if esp_lvgl_port component is available (using string matching)
foreach(comp ${build_components})
    if(comp MATCHES "esp_lvgl_port")
        idf_component_get_property(esp_lvgl_port_lib ${comp} COMPONENT_LIB)
        message(STATUS, "Found esp_lvgl_port component: ${esp_lvgl_port_lib}")
        target_link_libraries(${COMPONENT_LIB} PRIVATE ${esp_lvgl_port_lib})
        break()
    endif()
endforeach()


spiffs_create_partition_image(storage ../spiffs_image FLASH_IN_PROJECT)
