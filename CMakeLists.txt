# CMakeList.txt : CMake project for PufferUI
cmake_minimum_required (VERSION 3.21)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  if(NOT DEFINED CMAKE_MSVC_DEBUG_INFORMATION_FORMAT)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>")
  endif()
endif()

# The version lives in the header (PUFFERUI_VERSION_*): read it from there.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/include/pufferui/pufferui.h" PUI_VERSION_LINES
     REGEX "^#define PUFFERUI_VERSION_(MAJOR|MINOR|PATCH|TWEAK) [0-9]+$")
foreach(part MAJOR MINOR PATCH TWEAK)
  string(REGEX MATCH "PUFFERUI_VERSION_${part} ([0-9]+)" _m "${PUI_VERSION_LINES}")
  set(PUI_VERSION_${part} "${CMAKE_MATCH_1}")
endforeach()
project ("PufferUI" VERSION
         ${PUI_VERSION_MAJOR}.${PUI_VERSION_MINOR}.${PUI_VERSION_PATCH}.${PUI_VERSION_TWEAK})
set(PUFFERUI_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(PUFFERUI_TOP_LEVEL ON)
endif()
option(PUFFERUI_BUILD_TESTS "Build tests and benchmarks" ${PUFFERUI_TOP_LEVEL})
option(PUFFERUI_BUILD_EXAMPLES "Build examples and tutorials" ${PUFFERUI_TOP_LEVEL})
option(PUFFERUI_WITH_SDL3 "Build the SDL3 backend" ON)
option(PUFFERUI_USE_SYSTEM_SDL3 "Use find_package(SDL3) instead of vendored SDL" OFF)

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

if(PUFFERUI_WITH_SDL3)
  if(PUFFERUI_USE_SYSTEM_SDL3)
    find_package(SDL3 CONFIG REQUIRED)
  else()
    set(SDL_SHARED OFF CACHE BOOL "Build SDL shared library")
    set(SDL_STATIC ON CACHE BOOL "Build SDL static library")
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL's own test support library")
    set(SDL_INSTALL ON CACHE BOOL "Install the SDL dependency with PufferUI")
    add_subdirectory(vendored/SDL)
    include(cmake/PufferUISdlD3d11.cmake)
    pui_sdl_d3d11_buffer_overlay()
  endif()
endif()

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
  target_compile_features(${name} PUBLIC cxx_std_20)
  if(NOT MSVC)
    target_compile_options(${name} PRIVATE -Wall -Wextra)
  endif()
  target_include_directories(${name} PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendored/stb>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  if(A_SDL3)
    target_compile_definitions(${name} PUBLIC PUFFERUI_ENABLE_SDL3)
    target_link_libraries(${name} PUBLIC $<BUILD_INTERFACE:SDL3::SDL3> $<INSTALL_INTERFACE:SDL3::SDL3>)
    if(WIN32) # the UI Automation adapter (impl/uia_windows.inl)
      target_link_libraries(${name} PUBLIC uiautomationcore comctl32 ole32 oleaut32)
    endif()
  endif()
endfunction()

pui_add_library(pufferui)
add_library(pufferui::pufferui ALIAS pufferui)
if(PUFFERUI_WITH_SDL3)
  pui_add_library(pufferui_sdl3 SDL3)
  add_library(pufferui::sdl3 ALIAS pufferui_sdl3)
  set_target_properties(pufferui_sdl3 PROPERTIES EXPORT_NAME sdl3)
endif()

# pui_add_exe(<name> SOURCES <files...> [SDL3] [DEFS <defines...>])
include(cmake/PufferUIHarfBuzz.cmake)
include(cmake/PufferUIVulkan.cmake)
# One executable on the prebuilt library (SDL3 variant with SDL3), warnings
# on, and PUFFERUI_ASSET_DIR set so it can load the bundled fonts.
function(pui_add_exe name)
  cmake_parse_arguments(A "SDL3;VULKAN" "" "SOURCES;DEFS" ${ARGN})
  add_executable(${name} ${A_SOURCES})
  if(A_VULKAN)
    target_link_libraries(${name} PRIVATE pufferui_vulkan pufferui_warnings)
  elseif(A_SDL3)
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
if(PUFFERUI_BUILD_TESTS)
if(PUFFERUI_WITH_VULKAN)
  # One registration owns the executable, runner cases and CI selftest commands.
  function(pui_vulkan_case name full_only)
    set(case "{}")
    string(JSON case SET "${case}" target "\"${name}\"")
    string(JSON case SET "${case}" full_only "${full_only}")
    string(JSON case SET "${case}" args "[]")
    set(index 0)
    foreach(arg IN LISTS ARGN)
      string(REPLACE "\\" "\\\\" escaped "${arg}")
      string(REPLACE "\"" "\\\"" escaped "${escaped}")
      string(JSON case SET "${case}" args ${index} "\"${escaped}\"")
      math(EXPR index "${index} + 1")
    endforeach()
    set_property(GLOBAL APPEND PROPERTY PUFFERUI_VULKAN_CASES "${case}")
    set_property(GLOBAL APPEND PROPERTY PUFFERUI_VULKAN_COMMANDS
        COMMAND "$<TARGET_FILE:${name}>" ${ARGN})
  endfunction()
  function(pui_add_vulkan_test name)
    cmake_parse_arguments(V "FULL_ONLY" "" "SOURCES;DEFS;RUNS" ${ARGN})
    if(V_UNPARSED_ARGUMENTS)
      message(FATAL_ERROR "Unknown Vulkan test options: ${V_UNPARSED_ARGUMENTS}")
    endif()
    pui_add_exe(${name} VULKAN SOURCES ${V_SOURCES} DEFS ${V_DEFS})
    set_property(GLOBAL APPEND PROPERTY PUFFERUI_VULKAN_TARGETS ${name})
    set(full_only false)
    if(V_FULL_ONLY)
      set(full_only true)
    endif()
    pui_vulkan_case(${name} ${full_only})
    foreach(run IN LISTS V_RUNS)
      separate_arguments(run_args UNIX_COMMAND "${run}")
      pui_vulkan_case(${name} ${full_only} ${run_args})
    endforeach()
  endfunction()
  pui_add_vulkan_test(pui_vulkan_tests SOURCES tests/test_vulkan.cpp)
  pui_add_vulkan_test(pui_vulkan_external_tests SOURCES tests/test_vulkan_external.cpp)
  pui_add_vulkan_test(pui_vulkan_cache_tests SOURCES tests/test_vulkan_cache.cpp)
  pui_add_vulkan_test(pui_vulkan_interaction_tests SOURCES tests/test_vulkan_interaction.cpp RUNS "--staged")
  pui_add_vulkan_test(pui_vulkan_fault_tests SOURCES tests/test_vulkan_faults.cpp)
  pui_add_vulkan_test(pui_vulkan_internal_tests SOURCES src/pufferui_impl.cpp DEFS PUFFERUI_VULKAN_INTERNAL_TESTS)
  pui_add_vulkan_test(pui_vulkan_command_tests SOURCES src/pufferui_impl.cpp DEFS PUFFERUI_VULKAN_COMMAND_TESTS)
  pui_add_vulkan_test(pui_vulkan_binning_tests SOURCES src/pufferui_impl.cpp DEFS PUFFERUI_VULKAN_BINNING_TESTS)
  pui_add_exe(pui_vulkan_numeric_probe VULKAN SOURCES src/pufferui_impl.cpp DEFS PUFFERUI_VULKAN_NUMERIC_PROBE)
  pui_add_vulkan_test(pui_vulkan_compute_tests SOURCES src/pufferui_impl.cpp DEFS PUFFERUI_VULKAN_COMPUTE_TESTS)
  pui_add_vulkan_test(pui_vulkan_sdf_tests SOURCES tests/test_vulkan_sdf.cpp)
  pui_add_vulkan_test(pui_vulkan_sdf_cache_tests SOURCES src/pufferui_impl.cpp DEFS PUFFERUI_VULKAN_SDF_CACHE_TESTS)
  pui_add_vulkan_test(pui_vulkan_sdf_interaction_tests FULL_ONLY SOURCES tests/test_vulkan_sdf_interaction.cpp RUNS "--staged")
  if(PUFFERUI_WITH_HARFBUZZ)
    pui_add_vulkan_test(pui_harfbuzz_tests SOURCES tests/test_harfbuzz.cpp)
    pui_add_vulkan_test(pui_harfbuzz_interaction_tests FULL_ONLY SOURCES tests/test_harfbuzz_interaction.cpp RUNS "--staged")
  endif()
  if(PUFFERUI_WITH_SDL3)
    pui_add_vulkan_test(pui_vulkan_kawase_interaction_tests FULL_ONLY SOURCES tests/test_vulkan_kawase_interaction.cpp RUNS "--staged")
    pui_add_vulkan_test(pui_vulkan_preparation_tests FULL_ONLY SOURCES src/pufferui_impl.cpp DEFS PUFFERUI_VULKAN_PREPARATION_TESTS)
    if(PUFFERUI_WITH_HARFBUZZ)
      pui_add_vulkan_test(pui_vulkan_kawase_spiral_tests FULL_ONLY SOURCES tests/test_vulkan_kawase_spiral.cpp RUNS "--staged")
    endif()
    pui_add_vulkan_test(pui_vulkan_spiral_binning_tests FULL_ONLY
        SOURCES src/pufferui_impl.cpp DEFS PUFFERUI_VULKAN_SPIRAL_BINNING_TESTS)
    pui_add_vulkan_test(pui_vulkan_wsi_tests SOURCES tests/test_vulkan_wsi.cpp)
    pui_add_vulkan_test(pui_vulkan_showcase_tests SOURCES tests/test_vulkan_showcase.cpp)
    pui_add_vulkan_test(pui_vulkan_spiral_tests FULL_ONLY SOURCES tests/test_vulkan_spiral.cpp
        RUNS "--tiles 5000" "--tiles 1150 --upload-reference" "--tiles 5000 --upload-reference"
             "--tiles 1150 --staged" "--tiles 5000 --staged")
    pui_add_vulkan_test(pui_vulkan_compute_spiral_tests FULL_ONLY
        SOURCES tests/test_vulkan_compute_spiral.cpp RUNS "--tiles 1150" "--compute --cache")
    pui_add_exe(pui_vulkan_painter_bench VULKAN SOURCES tests/bench_vulkan_painter.cpp)
    pui_add_exe(pui_vulkan_kawase_bench VULKAN SOURCES tests/bench_vulkan_kawase.cpp)
    if(PUFFERUI_WITH_HARFBUZZ)
      pui_vulkan_case(pui_vulkan_compute_spiral_tests true --compute --vector-text)
      pui_vulkan_case(pui_vulkan_compute_spiral_tests true --compute --vector-text --cache)
      pui_vulkan_case(pui_vulkan_compute_spiral_tests true --compute --vector-text --cache --staged --tiles 1150)
      pui_vulkan_case(pui_vulkan_compute_spiral_tests true --adaptive --vector-text)
      pui_vulkan_case(pui_vulkan_compute_spiral_tests true --compute --vector-text --cache --growth)
    endif()
  endif()
  get_property(cases GLOBAL PROPERTY PUFFERUI_VULKAN_CASES)
  list(JOIN cases ",\n" cases_json)
  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/pufferui-vulkan-tests-$<CONFIG>.json"
      CONTENT "[\n${cases_json}\n]\n")
  get_property(targets GLOBAL PROPERTY PUFFERUI_VULKAN_TARGETS)
  get_property(commands GLOBAL PROPERTY PUFFERUI_VULKAN_COMMANDS)
  add_custom_target(vulkan_tests DEPENDS ${targets})
  add_custom_target(vulkan_selftest ${commands} DEPENDS ${targets} VERBATIM)
endif()
pui_add_exe(pui_core_tests SOURCES
    tests/test_core.cpp
    tests/test_layout.cpp
    tests/test_input.cpp
    tests/test_text.cpp
    tests/test_widgets.cpp
    tests/test_dock.cpp
    tests/test_render.cpp
    tests/test_components.cpp
    tests/test_ergonomics.cpp
    tests/test_platform.cpp
    tests/test_textarea.cpp
    tests/test_motion.cpp)

# Headless frame-cost measurement of the core (layout + interact + draw-list
# recording/flush on null_device). Machine-relative times; the stable metrics
# are draw_calls/vertices. Baseline numbers live in docs/perf.md.
pui_add_exe(pui_bench SOURCES tests/bench.cpp)
pui_add_exe(pui_core_bench SOURCES src/pufferui_impl.cpp DEFS PUFFERUI_CORE_PERF_BENCH)

# White-box regressions use the same implementation TU, with private test code
# included at its end. The core archive is not pulled in (no unresolved symbols).
pui_add_exe(pui_perf_tests SOURCES src/pufferui_impl.cpp DEFS PUFFERUI_INTERNAL_TESTS)

# SDL event-pump tests: SDL_PushEvent-driven routing through the offscreen
# driver. Same exit contract as the core tests (exit 0 = all passed).
if(PUFFERUI_WITH_SDL3)
pui_add_exe(pui_memory_tests SDL3 SOURCES src/pufferui_impl.cpp tests/test_memory_upload.cpp DEFS PUFFERUI_MEMORY_TESTS)
pui_add_exe(pui_preparation_tests SDL3 SOURCES src/pufferui_impl.cpp DEFS PUFFERUI_PREPARATION_TESTS)
pui_add_exe(pui_pump_tests SDL3 SOURCES tests/test_pump.cpp)
pui_add_exe(pui_perf_pixels SDL3 SOURCES tests/test_perf_pixels.cpp)
pui_add_exe(pui_core_perf_pixels SDL3 SOURCES tests/test_core_perf_pixels.cpp)
pui_add_exe(pui_showcase_tests SDL3 SOURCES tests/test_showcase.cpp tests/test_sdl_capacity.cpp tests/test_sdl_upload.cpp tests/test_sdl_geometry.cpp)

# Golden-image tests: offscreen SDL + software renderer, hash + BMP compare.
pui_add_exe(pui_golden_tests SDL3 SOURCES tests/golden/test_golden.cpp
    DEFS PUFFERUI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
endif()
endif()

# ---------------------------------------------------------------------------
# Apps: the small model/view example (docs/model_view.md) and the two-window
# demo (docking, floating panels, HUD).
# ---------------------------------------------------------------------------
if(PUFFERUI_BUILD_EXAMPLES AND PUFFERUI_WITH_SDL3)
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
    patterns expander combo keyboard components vlist overflow relayout
    controls tables tree numeric colors dpi files textarea a11y pickers)

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
if(PUFFERUI_WITH_VULKAN)
pui_add_selftest_exe(pui_showcase_vulkan VULKAN SOURCES examples/pui_showcase.cpp)
pui_add_selftest_exe(pui_ex_vulkan VULKAN SOURCES examples/atomic/vulkan.cpp)
pui_add_selftest_exe(pui_ex_vulkan_painter VULKAN SOURCES examples/atomic/vulkan_painter.cpp)
pui_add_selftest_exe(pui_ex_vulkan_blur VULKAN SOURCES examples/atomic/vulkan_blur.cpp)
endif()

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
endif()

# ---------------------------------------------------------------------------
# Install / export: `find_package(pufferui)` gives `pufferui::pufferui` (core)
# and `pufferui::sdl3` (core + SDL3 backend), plus ${PUFFERUI_IMPL} — the
# implementation TU for consumers who prefer the single-TU distribution (add
# it to exactly one target and define PUFFERUI_IMPLEMENTATION there).
# ---------------------------------------------------------------------------
include(CMakePackageConfigHelpers)

install(TARGETS pufferui EXPORT pufferuiTargets)
# The SDL variant has the same target name in the build and installed package.
if(PUFFERUI_WITH_SDL3)
  install(TARGETS pufferui_sdl3 EXPORT pufferuiTargets)
endif()
file(GLOB PUFFERUI_IMPL_SLICES "include/pufferui/impl/*.inl")
install(FILES
    "include/pufferui/pufferui.h"
    "include/pufferui/vulkan.h"
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
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/pufferuiConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/pufferuiConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pufferui")
