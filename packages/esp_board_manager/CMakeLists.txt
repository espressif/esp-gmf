# Include peripheral and device sources functions
include(${CMAKE_CURRENT_LIST_DIR}/peripherals/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/devices/CMakeLists.txt)

# Get peripheral sources
get_peripheral_sources(PERIPH_SRCS PERIPH_INCLUDES)

# Get device sources
get_device_sources(DEVICE_SRCS DEVICE_INCLUDES)

set(BOARD_SETUP_SRCS
)

idf_component_register(
    SRCS
        "src/esp_board_manager.c"
        "src/esp_board_periph.c"
        "src/esp_board_device.c"
        "src/esp_board_manager_err.c"
        ${PERIPH_SRCS}  # Add peripheral sources
        ${DEVICE_SRCS}  # Add device sources
        ${BOARD_SETUP_SRCS}  # Add board-specific sources
    INCLUDE_DIRS
        "."
        "include"
        "private_inc"
        ${PERIPH_INCLUDES}  # Add peripheral includes
        ${DEVICE_INCLUDES}  # Add device includes
    REQUIRES
        driver
        esp_driver_i2s
        esp_common
        fatfs
        esp_lcd
        spiffs
        esp_adc
        esp_driver_uart
        esp_driver_rmt
    LDFRAGMENTS
        "linker.lf"
    WHOLE_ARCHIVE
)
