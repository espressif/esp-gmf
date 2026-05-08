# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

function(bmgr_get_idf_compat_dirs out_var)
    set(current_major 5)
    if(DEFINED IDF_VERSION_MAJOR AND IDF_VERSION_MAJOR GREATER 5)
        set(current_major ${IDF_VERSION_MAJOR})
    endif()

    set(candidates "")
    while(current_major GREATER_EQUAL 5)
        list(APPEND candidates "idf${current_major}")
        math(EXPR current_major "${current_major} - 1")
    endwhile()

    set(${out_var} ${candidates} PARENT_SCOPE)
endfunction()

function(bmgr_resolve_component_file out_file out_dir base_dir component_name source_name)
    bmgr_get_idf_compat_dirs(candidates)

    foreach(compat_dir IN LISTS candidates)
        set(candidate_dir "${base_dir}/${component_name}/${compat_dir}")
        set(candidate_file "${candidate_dir}/${source_name}")
        if(EXISTS "${candidate_file}")
            set(${out_file} "${candidate_file}" PARENT_SCOPE)
            set(${out_dir} "${candidate_dir}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(default_dir "${base_dir}/${component_name}")
    set(default_file "${default_dir}/${source_name}")
    if(EXISTS "${default_file}")
        set(${out_file} "${default_file}" PARENT_SCOPE)
        set(${out_dir} "${default_dir}" PARENT_SCOPE)
        return()
    endif()

    message(FATAL_ERROR
        "No compatible implementation found for ${component_name}/${source_name}. "
        "Checked ${candidates} and default directory under ${base_dir}/${component_name}"
    )
endfunction()

function(bmgr_append_component_file sources includes base_dir component_name source_name)
    bmgr_resolve_component_file(component_src component_dir "${base_dir}" "${component_name}" "${source_name}")

    set(current_sources ${${sources}})
    list(APPEND current_sources "${component_src}")
    set(${sources} ${current_sources} PARENT_SCOPE)

    set(current_includes ${${includes}})
    list(APPEND current_includes "${component_dir}")
    list(REMOVE_DUPLICATES current_includes)
    set(${includes} ${current_includes} PARENT_SCOPE)
endfunction()
