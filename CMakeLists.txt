# CMakeList.txt : CMake project for PufferUI
cmake_minimum_required (VERSION 3.21)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("PufferUI" VERSION 0.1.0.1)

# Link-time optimization for release builds. No /fp:fast: the golden-image
# tests compare rendered pixels across compilers, and fast-math changes
# floating-point rounding (layout math and edge antialiasing are float).
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

# Sanitizers (gcc/clang only): -DPUFFERUI_SANITIZERS=ON
option(PUFFERUI_SANITIZERS "enable ASan + UBSan (non-MSVC)" OFF)
if(PUFFERUI_SANITIZERS AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# Compiler warnings for our targets (INTERFACE so vendored SDL stays quiet).
add_library(pufferui_warnings INTERFACE)
if(NOT MSVC)
  target_compile_options(pufferui_warnings INTERFACE -Wall -Wextra)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

set(BUILD_SHARED_LIBS OFF)
add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# PufferUI libraries. The implementation TU is compiled ONCE per variant and
# everything links the prebuilt library — one header edit no longer
# recompiles the ~6k-line implementation once per executable.
#   pufferui        core only (backend-agnostic: tests, headless, engines)
#   pufferui_sdl3   core + the SDL3 renderer backend (PUBLIC define + SDL3)
# The single-header + single-TU distribution is unchanged: consumers may
# still ignore these targets, add pufferui_impl.cpp themselves and define
# PUFFERUI_IMPLEMENTATION in exactly one TU.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(PUFFERUI_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")
set(PUFFERUI_CORE_SOURCES "src/pufferui_impl.cpp")

# pui_add_library(<name> [SDL3]) — the core, optionally with the SDL3 backend.
function(pui_add_library name)
  cmake_parse_arguments(A "SDL3" "" "" ${ARGN})
  add_library(${name} STATIC ${PUFFERUI_CORE_SOURCES})
  if(NOT MSVC)
    target_compile_options(${name} PRIVATE -Wall -Wextra)
  endif()
  target_include_directories(${name} PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendored/stb>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  if(A_SDL3)
    target_compile_definitions(${name} PUBLIC PUFFERUI_ENABLE_SDL3)
    target_link_libraries(${name} PUBLIC SDL3-static)
  endif()
endfunction()

pui_add_library(pufferui)
add_library(pufferui::pufferui ALIAS pufferui)
pui_add_library(pufferui_sdl3 SDL3)
add_library(pufferui::sdl3 ALIAS pufferui_sdl3)

# pui_add_exe(<name> SOURCES <files...> [SDL3] [DEFS <defines...>])
# One executable on the prebuilt library (SDL3 variant with SDL3), warnings
# on, and PUFFERUI_ASSET_DIR set so it can load the bundled fonts.
function(pui_add_exe name)
  cmake_parse_arguments(A "SDL3" "" "SOURCES;DEFS" ${ARGN})
  add_executable(${name} ${A_SOURCES})
  if(A_SDL3)
    target_link_libraries(${name} PRIVATE pufferui_sdl3 pufferui_warnings)
  else()
    target_link_libraries(${name} PRIVATE pufferui pufferui_warnings)
  endif()
  target_compile_definitions(${name} PRIVATE
      PUFFERUI_ASSET_DIR="${PUFFERUI_ASSET_DIR}" ${A_DEFS})
endfunction()

# pui_add_selftest_exe(<name> ...) — pui_add_exe plus a place in the
# `examples_selftest` target (every example runs offscreen with --selftest).
function(pui_add_selftest_exe name)
  pui_add_exe(${name} ${ARGN})
  set_property(GLOBAL APPEND PROPERTY PUFFERUI_SELFTESTS ${name})
endfunction()

# ---------------------------------------------------------------------------
# Tests and benchmarks.
# ---------------------------------------------------------------------------
# Headless core tests (no SDL, no window). Tests register themselves
# (PUI_TEST in tests/test_util.h): add a test by writing it in an area file.
pui_add_exe(pui_core_tests SOURCES
    tests/test_core.cpp
    tests/test_layout.cpp
    tests/test_input.cpp
    tests/test_text.cpp
    tests/test_widgets.cpp
    tests/test_dock.cpp
    tests/test_render.cpp
    tests/test_components.cpp
    tests/test_motion.cpp)

# Headless frame-cost measurement of the core (layout + interact + draw-list
# recording/flush on null_device). Machine-relative times; the stable metrics
# are draw_calls/vertices. Baseline numbers live in docs/perf.md.
pui_add_exe(pui_bench SOURCES tests/bench.cpp)

# SDL event-pump tests: SDL_PushEvent-driven routing through the offscreen
# driver. Same exit contract as the core tests (exit 0 = all passed).
pui_add_exe(pui_pump_tests SDL3 SOURCES tests/test_pump.cpp)

# Golden-image tests: offscreen SDL + software renderer, hash + BMP compare.
pui_add_exe(pui_golden_tests SDL3 SOURCES tests/golden/test_golden.cpp
    DEFS PUFFERUI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")

# ---------------------------------------------------------------------------
# Apps: the small model/view example (docs/model_view.md) and the two-window
# demo (docking, floating panels, HUD).
# ---------------------------------------------------------------------------
pui_add_exe(pui_counter SDL3 SOURCES examples/pui_counter.cpp)
pui_add_exe(pui_demo SDL3 SOURCES examples/pui_demo.cpp)

# ---------------------------------------------------------------------------
# Examples: one standalone program per capability (examples/atomic/<name>.cpp),
# plus the guided tour. `examples_selftest` builds nothing itself; it runs every
# example offscreen (`--selftest`) and fails on any core violation. Add a new
# example by dropping the file in and appending its name to the list.
# ---------------------------------------------------------------------------
set(PUFFERUI_EXAMPLE_NAMES
    hello bootstrap rect cursors tracks grid ids text buttons inputs widgets
    panels scroll popups dock windows drawing blur animation theme custom
    patterns expander combo keyboard components vlist overflow relayout)

# Headless examples: no SDL, no window (they still get the bundled font path).
set(PUFFERUI_HEADLESS_EXAMPLES headless renderer)

foreach(name IN LISTS PUFFERUI_EXAMPLE_NAMES)
  pui_add_selftest_exe(pui_ex_${name} SDL3 SOURCES examples/atomic/${name}.cpp)
endforeach()
foreach(name IN LISTS PUFFERUI_HEADLESS_EXAMPLES)
  pui_add_selftest_exe(pui_ex_${name} SOURCES examples/atomic/${name}.cpp)
endforeach()

# The guided tour: every capability in one app.
pui_add_selftest_exe(pui_tour SDL3 SOURCES examples/pui_tour.cpp)
# The motion showcase: animated layout (FLIP), blur, particles, components.
pui_add_selftest_exe(pui_showcase SDL3 SOURCES examples/pui_showcase.cpp)

# ---------------------------------------------------------------------------
# The tutorial (docs/tutorial/): every chapter's program is built and, apart
# from the window-only first step, self-tested here, so the lessons cannot rot.
# ---------------------------------------------------------------------------
pui_add_exe(pui_tut_step01_window SDL3 SOURCES examples/tutorial/step01_window.cpp)
pui_add_selftest_exe(pui_tut_step01_shot SDL3 SOURCES examples/tutorial/step01_shot.cpp)
pui_add_selftest_exe(pui_tut_button_states SDL3 SOURCES examples/tutorial/button_states.cpp)
foreach(step step02_layout step03_tasks step04_filters step05_glass step06_motion todo)
  pui_add_selftest_exe(pui_tut_${step} SDL3 SOURCES examples/tutorial/${step}.cpp)
endforeach()
# the headless test: the todo screen on a renderer that draws nothing (no SDL)
pui_add_selftest_exe(pui_tut_todo_test SOURCES examples/tutorial/todo_test.cpp)

get_property(PUFFERUI_SELFTESTS GLOBAL PROPERTY PUFFERUI_SELFTESTS)
set(PUFFERUI_SELFTEST_COMMANDS)
foreach(name IN LISTS PUFFERUI_SELFTESTS)
  list(APPEND PUFFERUI_SELFTEST_COMMANDS COMMAND $<TARGET_FILE:${name}> --selftest)
endforeach()
add_custom_target(examples_selftest ${PUFFERUI_SELFTEST_COMMANDS}
    DEPENDS ${PUFFERUI_SELFTESTS} VERBATIM)

# ---------------------------------------------------------------------------
# Install / export: `find_package(pufferui)` gives `pufferui::pufferui` (core)
# and `pufferui::sdl3` (core + SDL3 backend), plus ${PUFFERUI_IMPL} — the
# implementation TU for consumers who prefer the single-TU distribution (add
# it to exactly one target and define PUFFERUI_IMPLEMENTATION there).
# ---------------------------------------------------------------------------
include(CMakePackageConfigHelpers)

install(TARGETS pufferui EXPORT pufferuiTargets)
# pufferui_sdl3 is a build-tree convenience (it depends on SDL3-static, which
# is not ours to export): package consumers get pufferui::pufferui and either
# provide the SDL3 backend via the single-TU path or link their own SDL3.
install(TARGETS pufferui_sdl3)
file(GLOB PUFFERUI_IMPL_SLICES "include/pufferui/impl/*.inl")
install(FILES
    "include/pufferui/pufferui.h"
    "src/pufferui_impl.cpp"
    "vendored/stb/stb_truetype.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pufferui")
install(FILES ${PUFFERUI_IMPL_SLICES}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pufferui/impl")

install(EXPORT pufferuiTargets
    NAMESPACE pufferui::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pufferui")

configure_package_config_file(
    "cmake/pufferuiConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/pufferuiConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pufferui")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/pufferuiConfigVersion.cmake"
    VERSION 0.1.0.1
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/pufferuiConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/pufferuiConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pufferui")