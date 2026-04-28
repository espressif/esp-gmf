cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED IDF_TARGET)
    set(IDF_TARGET esp32s3)
endif()

get_filename_component(GMF_FFT_REPO_ROOT "${CMAKE_SOURCE_DIR}/../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${GMF_FFT_REPO_ROOT}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fft_spectrum_print)
