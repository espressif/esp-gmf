idf_build_get_property(build_components BUILD_COMPONENTS)
set(search_pattern "esp_codec_dev")
set(index -1)
foreach(item IN LISTS build_components)
    if("${item}" MATCHES "${search_pattern}.*")
        set(index ${item})
        break()
    endif()
endforeach()

list(APPEND io_srcs "esp_gmf_io_file.c" "esp_gmf_io_embed_flash.c" "esp_gmf_io_i2s_pdm.c" "esp_gmf_io_http.c" "http_lib/gzip/gzip_miniz.c")
if(index EQUAL -1)
    set(io_inc "")
else()
    message(STATUS "The esp_codec_dev is installed")
    list(APPEND io_srcs "codec_dev_io/esp_gmf_io_codec_dev.c")
    set(io_inc "codec_dev_io")
endif()

idf_component_register(SRCS ${io_srcs}
                       INCLUDE_DIRS ./include ${io_inc} "http_lib/gzip/" "http_lib/gzip/include"
                       PRIV_INCLUDE_DIRS ""
                       REQUIRES "driver" "esp_http_client")
