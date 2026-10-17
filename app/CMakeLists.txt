cmake_minimum_required(VERSION 3.25)
project(sotc_viewer CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(SDL3 CONFIG REQUIRED)

add_executable(sotc-viewer
  src/main.cpp src/app.cpp src/asset_io.cpp src/renderer.cpp src/camera.cpp)
target_link_libraries(sotc-viewer PRIVATE SDL3::SDL3)
target_include_directories(sotc-viewer PRIVATE third_party)

# Plan ruling R2: the Metal shader is compiled by SDL at runtime from MSL
# source, so the build only copies `mesh.metal` next to the binary (the
# renderer locates it via SDL_GetBasePath()). No xcrun Metal toolchain needed.
# LINK_DEPENDS makes a shader-only edit relink the target, which reruns the
# POST_BUILD copy — without it the build directory would keep a stale shader.
set_property(TARGET sotc-viewer APPEND PROPERTY
  LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.metal)
add_custom_command(TARGET sotc-viewer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.metal
          $<TARGET_FILE_DIR:sotc-viewer>/mesh.metal)

enable_testing()
add_executable(sotc-app-tests tests/test_main.cpp tests/test_camera.cpp tests/test_asset_io.cpp
  src/asset_io.cpp src/camera.cpp)
target_link_libraries(sotc-app-tests PRIVATE SDL3::SDL3)
target_include_directories(sotc-app-tests PRIVATE third_party src)
add_test(NAME unit_tests COMMAND sotc-app-tests)
add_test(NAME windowed_selftest COMMAND sotc-viewer --selftest)
set_tests_properties(windowed_selftest PROPERTIES
  PASS_REGULAR_EXPRESSION "s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0")
