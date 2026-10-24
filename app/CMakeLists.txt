cmake_minimum_required(VERSION 3.25)
project(sotc_viewer CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_ttf CONFIG REQUIRED)

# The S13 modules: the interpreter's (pure C++: the evaluator, the shared library, routes, terrain,
# Phaedra's registry; the dump tool links these), then the additive layer and Phaedra's glue (the
# viewer and the tests).
set(S13_SCR_SOURCES src/scr_cdef.cpp src/scr_library.cpp src/route.cpp src/terrain.cpp src/scr_kirin.cpp)
set(S13_SOURCES ${S13_SCR_SOURCES} src/additive.cpp src/kirin_fight.cpp)

add_executable(sotc-viewer
  src/main.cpp src/app.cpp src/encounter_rt.cpp src/capture.cpp src/audio_seam.cpp
  src/gamepad_input.cpp src/stage_ctx.cpp src/stage_setup.cpp
  src/selftest_s1.cpp src/selftest_s3.cpp src/selftest_s4.cpp src/selftest_s6.cpp src/selftest_s7.cpp src/selftest_s41.cpp
  src/selftest_s42.cpp src/selftest_s8.cpp src/selftest_s9.cpp src/selftest_s11.cpp src/interactive.cpp
  src/asset_io.cpp src/renderer.cpp src/camera.cpp src/anim.cpp
  src/manifest.cpp src/heightmap.cpp src/player.cpp src/collision.cpp src/grab.cpp
  src/player_state.cpp src/hud.cpp src/valus.cpp src/audio.cpp src/wav_io.cpp
  src/death.cpp src/transition.cpp src/door.cpp src/ride.cpp src/sword.cpp src/effect.cpp src/sword_fx.cpp src/gamepad_map.h
  src/behaviour.cpp src/locomotion.cpp src/run_flags.cpp src/quadratus_motion.cpp
  src/quadratus_fight.cpp src/scr_graph.cpp src/scr_predicates.cpp src/objanim_events.cpp
  src/knight_fight.cpp src/selftest_s12.cpp ${S13_SOURCES})
target_link_libraries(sotc-viewer PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf)
target_include_directories(sotc-viewer PRIVATE third_party)

# Plan ruling R2: the Metal shaders are compiled by SDL at runtime from MSL
# source, so the build only copies `mesh.metal` and `hud.metal` next to the
# binary (the renderer locates them via SDL_GetBasePath()). No xcrun Metal
# toolchain needed. LINK_DEPENDS makes a shader-only edit relink the target,
# which reruns the POST_BUILD copy — without it the build directory would keep
# a stale shader.
set_property(TARGET sotc-viewer APPEND PROPERTY
  LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.metal
               ${CMAKE_CURRENT_SOURCE_DIR}/shaders/hud.metal
               ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tendril.metal)
add_custom_command(TARGET sotc-viewer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.metal
          $<TARGET_FILE_DIR:sotc-viewer>/mesh.metal
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/shaders/hud.metal
          $<TARGET_FILE_DIR:sotc-viewer>/hud.metal
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tendril.metal
          $<TARGET_FILE_DIR:sotc-viewer>/tendril.metal)

enable_testing()
add_executable(sotc-app-tests tests/test_main.cpp tests/test_camera.cpp tests/test_asset_io.cpp
  tests/test_anim.cpp tests/test_manifest.cpp tests/test_heightmap.cpp tests/test_player.cpp
  tests/test_grab.cpp tests/test_player_state.cpp tests/test_hud.cpp tests/test_valus.cpp
  tests/test_gamepad.cpp tests/test_audio.cpp tests/test_audio_events.cpp tests/test_death.cpp tests/test_transition.cpp tests/test_door.cpp
  tests/test_effect.cpp tests/test_ride.cpp tests/test_sword.cpp tests/test_sword_fx.cpp
  tests/test_behaviour.cpp tests/test_locomotion.cpp tests/test_run_flags.cpp tests/test_encounter.cpp
  tests/test_quadratus.cpp
  tests/test_quadratus_motion.cpp
  tests/test_quadratus_fight.cpp
  tests/test_scr_graph.cpp
  tests/test_climb_window.cpp
  tests/test_knight.cpp
  tests/test_objanim_events.cpp
  tests/test_scr_predicates.cpp
  tests/test_knight_fight.cpp
  tests/test_scr_cdef.cpp
  tests/test_scr_library.cpp
  tests/test_additive.cpp
  tests/test_route.cpp
  tests/test_terrain.cpp
  tests/test_kirin.cpp
  tests/test_scr_kirin.cpp
  tests/test_kirin_fight.cpp
  src/encounter_rt.cpp src/renderer.cpp src/audio_seam.cpp
  src/asset_io.cpp src/camera.cpp src/anim.cpp src/manifest.cpp src/heightmap.cpp src/player.cpp
  src/collision.cpp src/grab.cpp src/player_state.cpp src/hud.cpp src/valus.cpp
  src/audio.cpp src/wav_io.cpp src/death.cpp src/transition.cpp src/door.cpp src/ride.cpp src/sword.cpp src/effect.cpp src/sword_fx.cpp src/gamepad_map.h
  src/behaviour.cpp src/locomotion.cpp src/run_flags.cpp src/quadratus_motion.cpp
  src/quadratus_fight.cpp src/scr_graph.cpp src/objanim_events.cpp src/scr_predicates.cpp
  src/knight_fight.cpp ${S13_SOURCES})
target_link_libraries(sotc-app-tests PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf)
target_include_directories(sotc-app-tests PRIVATE third_party src)
# test_scr_predicates.cpp generates its fixture (the KNIGHT graph export) here at test time.
target_compile_definitions(sotc-app-tests PRIVATE SOTC_TEST_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME unit_tests COMMAND sotc-app-tests)
add_test(NAME windowed_selftest COMMAND sotc-viewer --selftest)
set_tests_properties(windowed_selftest PROPERTIES
  PASS_REGULAR_EXPRESSION "s1-status windowed=1 relative_mouse=0 mouse_auto_capture=0")

# S13 D13: what each registry runs, for `make coverage` (tools/re/scr/coverage.py). No SDL.
add_executable(scr_registry_dump tools/scr_registry_dump.cpp src/scr_graph.cpp src/scr_predicates.cpp
  ${S13_SCR_SOURCES})
target_include_directories(scr_registry_dump PRIVATE src)
