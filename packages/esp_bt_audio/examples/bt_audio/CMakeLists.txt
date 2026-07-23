# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "IDF_PATH not defined")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(bt_audio)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang"
   OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16))
    idf_build_set_property(CXX_COMPILE_OPTIONS "-Wno-error=non-c-typedef-for-linkage" APPEND)
endif()
