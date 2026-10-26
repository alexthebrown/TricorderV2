cmake_minimum_required(VERSION 3.16)
project(tricorder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(SDL REQUIRED IMPORTED_TARGET sdl2 SDL2_ttf)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
pkg_check_modules(CURL IMPORTED_TARGET libcurl)

add_library(tricorder_core STATIC
  src/config.cpp
  src/json.cpp
  src/weather.cpp
  src/sysinfo.cpp
  src/astro.cpp
  src/subspace.cpp
  src/orbit.cpp
  src/relay.cpp
)
target_include_directories(tricorder_core PUBLIC src)
target_link_libraries(tricorder_core PUBLIC Threads::Threads)
if(CURL_FOUND)
  target_compile_definitions(tricorder_core PUBLIC HAVE_CURL)
  target_link_libraries(tricorder_core PUBLIC PkgConfig::CURL)
else()
  message(WARNING "libcurl not found: weather will be simulated (apt install libcurl4-openssl-dev)")
endif()

file(GLOB SCREENS CONFIGURE_DEPENDS src/screens/*.cpp)
add_executable(tricorder
  src/main.cpp
  src/app.cpp
  src/gfx.cpp
  src/input.cpp
  src/widgets.cpp
  src/video_player.cpp
  src/media_library.cpp
  src/overlays.cpp
  src/third_party/qrcodegen.cpp
  ${SCREENS}
)
target_link_libraries(tricorder PRIVATE tricorder_core PkgConfig::SDL PkgConfig::FFMPEG)
target_compile_options(tricorder PRIVATE -Wall -Wextra)

# Unit tests for the non-graphical pieces: `ctest --test-dir build`
enable_testing()
add_executable(tricorder_tests tests/tests.cpp)
target_link_libraries(tricorder_tests PRIVATE tricorder_core)
add_test(NAME tricorder_tests COMMAND tricorder_tests)

install(TARGETS tricorder RUNTIME DESTINATION bin)
