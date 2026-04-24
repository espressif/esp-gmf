set(srcs "memory_checks.c"
         "test_runner.c"
         "test_utils.c")

if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    set(libc_component esp_libc)
else()
    set(libc_component newlib)
endif()

if(IDF_TARGET STREQUAL "esp32" OR
   IDF_TARGET STREQUAL "esp32s2" OR
   IDF_TARGET STREQUAL "esp32s3")
    set(perfmon_component perfmon)
else()
    set(perfmon_component "")
endif()


idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS include
                    REQUIRES esp_partition cmock ${libc_component}
                    PRIV_REQUIRES ${perfmon_component} esp_netif)
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")
