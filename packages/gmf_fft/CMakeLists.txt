set(GMF_FFT_SRCS
    "src/common/fft_heap.c"
    "src/common/fft_power2_q15.c")

if("${IDF_TARGET}" STREQUAL "esp32s3" OR "${IDF_TARGET}" STREQUAL "esp32p4")
    set(GMF_FFT_TARGET_SRC "src/${IDF_TARGET}/fft_pie_radix2_dit_s16.S")
else()
    message(FATAL_ERROR "gmf_fft: ${IDF_TARGET} is not supported.")
endif()

list(APPEND GMF_FFT_SRCS "${GMF_FFT_TARGET_SRC}")

idf_component_register(
    SRCS ${GMF_FFT_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES heap
)
