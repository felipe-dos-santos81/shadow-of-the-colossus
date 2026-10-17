cmake_minimum_required(VERSION 3.25)
project(sotc_viewer CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(SDL3 CONFIG REQUIRED)

add_executable(sotc-viewer
  src/main.cpp src/app.cpp)
target_link_libraries(sotc-viewer PRIVATE SDL3::SDL3)
target_include_directories(sotc-viewer PRIVATE third_party)

enable_testing()
add_executable(sotc-app-tests tests/test_main.cpp tests/test_camera.cpp tests/test_asset_io.cpp)
target_link_libraries(sotc-app-tests PRIVATE SDL3::SDL3)
target_include_directories(sotc-app-tests PRIVATE third_party src)
add_test(NAME unit_tests COMMAND sotc-app-tests)
add_test(NAME windowed_selftest COMMAND sotc-viewer --selftest)
set_tests_properties(windowed_selftest PROPERTIES
  PASS_REGULAR_EXPRESSION "s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0")
