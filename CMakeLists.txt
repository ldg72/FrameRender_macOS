cmake_minimum_required(VERSION 3.25)
project(FrameRender VERSION 0.1.0 LANGUAGES CXX OBJCXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT APPLE)
  message(FATAL_ERROR "This first FrameRender build targets macOS.")
endif()
list(APPEND CMAKE_PREFIX_PATH /opt/homebrew /usr/local)
find_package(SDL3 CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(AV REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
find_program(FFMPEG_EXECUTABLE ffmpeg REQUIRED)
find_package(Threads REQUIRED)

add_library(fr_core src/core/Project.cpp)
target_include_directories(fr_core PUBLIC src)
target_compile_options(fr_core PRIVATE -Wall -Wextra -Wpedantic)
add_library(fr_media src/media/Media.cpp src/media/Process.cpp src/media/Render.cpp src/media/Transport.cpp)
target_include_directories(fr_media PUBLIC src)
target_link_libraries(fr_media PUBLIC fr_core PkgConfig::AV SDL3::SDL3 Threads::Threads)
target_compile_definitions(fr_media PRIVATE FR_FFMPEG_PATH="${FFMPEG_EXECUTABLE}")
target_compile_options(fr_media PRIVATE -Wall -Wextra -Wpedantic)

set(IMGUI_DIR external/imgui)
add_library(fr_imgui STATIC
  ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
  external/imnodes/imnodes.cpp)
target_include_directories(fr_imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends external/imnodes)
target_link_libraries(fr_imgui PUBLIC SDL3::SDL3)

add_executable(FrameRender MACOSX_BUNDLE src/main.cpp src/ui/Editor.cpp src/platform/Mac.mm)
target_link_libraries(FrameRender PRIVATE fr_media fr_imgui "-framework Cocoa" "-framework UniformTypeIdentifiers")
target_compile_options(FrameRender PRIVATE -Wall -Wextra -Wpedantic "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
set_target_properties(FrameRender PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/assets/Info.plist.in"
  MACOSX_BUNDLE_GUI_IDENTIFIER net.lucadegiovanni.framerender
  MACOSX_BUNDLE_BUNDLE_NAME FrameRender
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})

include(CTest)
if(BUILD_TESTING)
  add_executable(fr_core_tests tests/CoreTests.cpp)
  target_link_libraries(fr_core_tests PRIVATE fr_core)
  add_test(NAME project_and_editing COMMAND fr_core_tests)
  add_executable(fr_cut_overview_tests tests/CutOverviewTests.cpp)
  target_link_libraries(fr_cut_overview_tests PRIVATE fr_core)
  add_test(NAME cut_overview COMMAND fr_cut_overview_tests)
  add_executable(fr_transitions_check tests/TransitionsCheck.cpp)
  target_link_libraries(fr_transitions_check PRIVATE fr_media)
  add_test(NAME video_transitions COMMAND fr_transitions_check "${CMAKE_BINARY_DIR}/checks/transitions")
  set_tests_properties(video_transitions PROPERTIES TIMEOUT 120)
  add_executable(fr_media_check tests/MediaCheck.cpp)
  target_link_libraries(fr_media_check PRIVATE fr_media)
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/checks")
  add_test(NAME generate_media_fixture COMMAND "${FFMPEG_EXECUTABLE}" -hide_banner -loglevel error
    -f lavfi -i testsrc2=size=960x540:rate=25:duration=3
    -f lavfi -i sine=frequency=440:sample_rate=48000:duration=3
    -shortest -c:v libx264 -pix_fmt yuv420p -c:a aac -y "${CMAKE_BINARY_DIR}/checks/Video 01 test.mp4")
  set_tests_properties(generate_media_fixture PROPERTIES FIXTURES_SETUP video_fixture TIMEOUT 30)
  add_test(NAME media_pipeline COMMAND fr_media_check "${CMAKE_BINARY_DIR}/checks/Video 01 test.mp4" "${CMAKE_BINARY_DIR}/checks/Export.mp4")
  set_tests_properties(media_pipeline PROPERTIES FIXTURES_REQUIRED video_fixture TIMEOUT 90)
  add_executable(fr_export_formats tests/ExportFormats.cpp)
  target_link_libraries(fr_export_formats PRIVATE fr_media)
  add_test(NAME export_formats COMMAND fr_export_formats "${CMAKE_BINARY_DIR}/checks/Video 01 test.mp4" "${CMAKE_BINARY_DIR}/checks/export-formats")
  set_tests_properties(export_formats PROPERTIES FIXTURES_REQUIRED video_fixture TIMEOUT 120)
endif()
