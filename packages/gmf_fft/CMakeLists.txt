set(GMF_FFT_SRCS
    "src/common/fft_heap.c"
    "src/common/fft_power2_q15.c")

set(GMF_FFT_USE_PIE_ASM OFF)
if(CONFIG_IDF_TARGET STREQUAL "esp32s3")
    set(GMF_FFT_USE_PIE_ASM ON)
    set(GMF_FFT_PIE_DIR "src/esp32s3")
elseif(CONFIG_IDF_TARGET STREQUAL "esp32p4")
    set(GMF_FFT_USE_PIE_ASM ON)
    set(GMF_FFT_PIE_DIR "src/riscv_pie")
elseif(CONFIG_IDF_TARGET STREQUAL "esp32s31" AND CONFIG_GMF_FFT_S31_USE_ASM)
    set(GMF_FFT_USE_PIE_ASM ON)
    set(GMF_FFT_PIE_DIR "src/riscv_pie")
endif()

if(GMF_FFT_USE_PIE_ASM)
    list(APPEND GMF_FFT_SRCS
        "${GMF_FFT_PIE_DIR}/fft_pie_radix2_dit_s16.S"
        "${GMF_FFT_PIE_DIR}/fft_pie_radix2_hp_s16.S"
        "${GMF_FFT_PIE_DIR}/fft_bitrev_s16.c")
else()
    list(APPEND GMF_FFT_SRCS
        "src/esp_c/gmf_fft_radix2_dit_s16.c"
        "src/esp_c/gmf_fft_radix2_hp_s16.c"
        "src/esp_c/fft_hp_pack.c"
        "src/esp_c/fft_hp_scale_s16.c")
endif()

idf_component_register(
    SRCS ${GMF_FFT_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES heap
)

if("src/esp_c/fft_hp_pack.c" IN_LIST GMF_FFT_SRCS)
    set_source_files_properties(
        "src/esp_c/fft_hp_pack.c"
        "src/esp_c/fft_hp_scale_s16.c"
        PROPERTIES COMPILE_FLAGS "-O3 -funroll-loops")
endif()
