cmake_minimum_required(VERSION 3.16)
project(esp_video_render_emulation C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(FREETYPE2 REQUIRED freetype2)

option(EMU_WITH_GSTREAMER "Enable GStreamer via gst-launch subprocess (no dev packages required)" OFF)
find_program(GST_LAUNCH gst-launch-1.0)
find_program(GST_INSPECT gst-inspect-1.0)
set(EMU_HAS_GST_TOOLS OFF)
if(GST_LAUNCH AND GST_INSPECT)
  set(EMU_HAS_GST_TOOLS ON)
endif()
if(EMU_WITH_GSTREAMER AND NOT EMU_HAS_GST_TOOLS)
  message(WARNING "EMU_WITH_GSTREAMER=ON but gst tools not found; forcing OFF. Need gst-launch-1.0 + gst-inspect-1.0.")
  set(EMU_WITH_GSTREAMER OFF CACHE BOOL "" FORCE)
endif()

add_library(video_render_emu STATIC
    src/backend_sdl.c
    src/assets_path.c
    $<$<BOOL:${EMU_WITH_GSTREAMER}>:src/proc_gstcli.c>
    $<$<NOT:$<BOOL:${EMU_WITH_GSTREAMER}>>:src/proc_emulation.c>
    src/stubs/esp_timer.c
    src/stubs/esp_gmf_video_format.c
    src/stubs/esp_gmf_oal_mem.c
    src/stubs/esp_gmf_oal_mutex.c
    src/stubs/esp_gmf_oal_thread.c
    src/stubs/esp_random.c

    # Reuse esp_video_render core (subset; exclude ESP-IDF/LCD/LVGL/GMF-heavy parts)
    ../src/esp_video_render.c
    ../src/esp_video_render_proc.c
    ../src/video_render_blend_flow.c
    ../src/video_render_blender.c
    ../src/video_blend_sw.c
    ../src/video_blend_hw.c
    ../src/video_render_compose.c
    ../src/video_render_utils.c
    ../src/video_render_decode.c
     ../src/video_render_measure.c
    ../src/video_render_log.c
    ../src/data_queue.c
    ../src/video_render_dual_stream.c
    ../src/vui/vui_overlay.c
    ../src/vui/vui_container.c
    ../src/vui/vui_widget.c
    ../src/vui/image_widget/image_widget.c
    ../src/vui/text_widget/text_font_resource.c
    ../src/vui/text_widget/text_render.c
    ../src/vui/text_widget/text_widget.c
    ../test_apps/main/xiaozhi_panel_widget.c
)

add_executable(video_render_emulator
    src/main.c
)

target_link_libraries(video_render_emulator PRIVATE video_render_emu)

target_include_directories(video_render_emu PUBLIC
    ${SDL2_INCLUDE_DIRS}
    ${FREETYPE2_INCLUDE_DIRS}

    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/stubs
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vui
    ${CMAKE_CURRENT_SOURCE_DIR}/../private_inc
)

target_compile_options(video_render_emu PRIVATE
    ${SDL2_CFLAGS_OTHER}
    ${FREETYPE2_CFLAGS_OTHER}
)

target_link_libraries(video_render_emu PRIVATE
    ${SDL2_LIBRARIES}
    ${FREETYPE2_LIBRARIES}
    pthread
    m
)

target_compile_definitions(video_render_emu PRIVATE
    CONFIG_FREETYPE_ENABLED=1
    CONFIG_ESP_VIDEO_RENDER_BLEND_THREAD_STACK_SIZE=32768
    CONFIG_ESP_VIDEO_RENDER_BLEND_THREAD_PRIORITY=5
    CONFIG_ESP_VIDEO_RENDER_BLEND_THREAD_CORE_ID=0
    $<$<BOOL:${EMU_WITH_GSTREAMER}>:EMU_WITH_GSTREAMER=1>
    __EMU__=1
)

include(CTest)
option(EMU_BUILD_TESTS "Build emulator unit tests" ON)
if(BUILD_TESTING AND EMU_BUILD_TESTS)
  add_executable(emu_test_backend_sdl
    tests/test_backend_sdl.c
  )
  target_link_libraries(emu_test_backend_sdl PRIVATE video_render_emu)
  add_test(NAME emu_backend_sdl COMMAND emu_test_backend_sdl)

  add_executable(emu_test_widgets_text
    tests/test_widgets_text.c
  )
  target_link_libraries(emu_test_widgets_text PRIVATE video_render_emu)
  add_test(NAME emu_widgets_text COMMAND emu_test_widgets_text)

  add_executable(emu_test_widgets_image
    tests/test_widgets_image.c
  )
  target_link_libraries(emu_test_widgets_image PRIVATE video_render_emu)
  add_test(NAME emu_widgets_image COMMAND emu_test_widgets_image)

  add_executable(emu_test_dual_eyes
    tests/test_dual_eyes.c
  )
  target_link_libraries(emu_test_dual_eyes PRIVATE video_render_emu)
  add_test(NAME emu_dual_eyes COMMAND emu_test_dual_eyes)
  set_tests_properties(emu_dual_eyes PROPERTIES SKIP_RETURN_CODE 0)

  add_executable(emu_test_xiaozhi_panel
    tests/test_xiaozhi_panel.c
  )
  target_link_libraries(emu_test_xiaozhi_panel PRIVATE video_render_emu)
  add_test(NAME emu_xiaozhi_panel COMMAND emu_test_xiaozhi_panel)

  add_executable(emu_test_player_view
    tests/test_player_view_entry.c
    ../examples/video_player/main/video_player_view.c
  )
  target_link_libraries(emu_test_player_view PRIVATE video_render_emu)
  add_test(NAME emu_player_view COMMAND emu_test_player_view)

  add_executable(emu_test_intercom_ui
    tests/test_intercom_ui.c
  )
  target_link_libraries(emu_test_intercom_ui PRIVATE video_render_emu)
  add_test(NAME emu_intercom_ui COMMAND emu_test_intercom_ui)

  add_executable(emu_test_video_compose
    tests/test_video_compose.c
    ../test_apps/main/video_compose_test.c
    ../test_apps/main/fake_backend.c
  )
  target_link_libraries(emu_test_video_compose PRIVATE video_render_emu)
  add_test(NAME emu_video_compose COMMAND emu_test_video_compose)

  add_executable(emu_test_esp_video_render_proc
    tests/test_esp_video_render_proc.c
    ../test_apps/main/video_proc_test.c
  )
  target_link_libraries(emu_test_esp_video_render_proc PRIVATE video_render_emu)
  target_compile_definitions(emu_test_esp_video_render_proc PRIVATE __EMU__=1)
  add_test(NAME emu_esp_video_render_proc COMMAND emu_test_esp_video_render_proc)

  if(EMU_WITH_GSTREAMER)
    add_executable(emu_test_proc_matrix
      tests/test_proc_matrix.c
    )
    target_link_libraries(emu_test_proc_matrix PRIVATE video_render_emu)
    add_test(NAME emu_proc_matrix COMMAND emu_test_proc_matrix)
    set_tests_properties(emu_proc_matrix PROPERTIES SKIP_RETURN_CODE 77)
  endif()
endif()

