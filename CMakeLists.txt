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

set(PUFFERUI_CORE_SOURCES "src/pufferui_impl.cpp")

add_library(pufferui STATIC ${PUFFERUI_CORE_SOURCES})
add_library(pufferui::pufferui ALIAS pufferui)
if(NOT MSVC)
  target_compile_options(pufferui PRIVATE -Wall -Wextra)
endif()
target_include_directories(pufferui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendored/stb>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_property(TARGET pufferui PROPERTY CXX_STANDARD 20)
set_property(TARGET pufferui PROPERTY CXX_STANDARD_REQUIRED ON)

add_library(pufferui_sdl3 STATIC ${PUFFERUI_CORE_SOURCES})
add_library(pufferui::sdl3 ALIAS pufferui_sdl3)
target_compile_definitions(pufferui_sdl3 PUBLIC PUFFERUI_ENABLE_SDL3)
target_link_libraries(pufferui_sdl3 PUBLIC SDL3-static)
if(NOT MSVC)
  target_compile_options(pufferui_sdl3 PRIVATE -Wall -Wextra)
endif()
target_include_directories(pufferui_sdl3 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendored/stb>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_property(TARGET pufferui_sdl3 PROPERTY CXX_STANDARD 20)
set_property(TARGET pufferui_sdl3 PROPERTY CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Headless core tests (no SDL, no window).
# ---------------------------------------------------------------------------
set(PUFFERUI_CORE_TEST_SOURCES
    tests/test_core.cpp
    tests/test_layout.cpp
    tests/test_input.cpp
    tests/test_text.cpp
    tests/test_widgets.cpp
    tests/test_dock.cpp
    tests/test_render.cpp
)
add_executable(pui_core_tests ${PUFFERUI_CORE_TEST_SOURCES})
target_link_libraries(pui_core_tests PRIVATE pufferui pufferui_warnings)

set_property(TARGET pui_core_tests PROPERTY CXX_STANDARD 20)
set_property(TARGET pui_core_tests PROPERTY CXX_STANDARD_REQUIRED ON)
target_compile_definitions(pui_core_tests PRIVATE
    PUFFERUI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

# ---------------------------------------------------------------------------
# Benchmarks: headless frame-cost measurement of the core (layout + interact +
# draw-list recording/flush on null_device). Machine-relative times; the
# stable metrics are draw_calls/vertices. Baseline numbers live in docs/perf.md.
# ---------------------------------------------------------------------------
add_executable(pui_bench "tests/bench.cpp")
target_link_libraries(pui_bench PRIVATE pufferui pufferui_warnings)
set_property(TARGET pui_bench PROPERTY CXX_STANDARD 20)
set_property(TARGET pui_bench PROPERTY CXX_STANDARD_REQUIRED ON)
target_compile_definitions(pui_bench PRIVATE
    PUFFERUI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

# SDL event-pump tests: SDL_PushEvent-driven routing through the offscreen
# driver. Same exit contract as the core tests (exit 0 = all passed).
add_executable(pui_pump_tests
  "tests/test_pump.cpp"
)
target_compile_definitions(pui_pump_tests PRIVATE
    PUFFERUI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_link_libraries(pui_pump_tests PRIVATE pufferui_sdl3 pufferui_warnings)
set_property(TARGET pui_pump_tests PROPERTY CXX_STANDARD 20)
set_property(TARGET pui_pump_tests PROPERTY CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# PufferUI "next" small example: model/view/dispatch (docs/model_view.md).
# ---------------------------------------------------------------------------
add_executable(pui_counter
  "examples/pui_counter.cpp"
)

target_compile_definitions(pui_counter PRIVATE
    PUFFERUI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_link_libraries(pui_counter PRIVATE pufferui_sdl3 pufferui_warnings)

set_property(TARGET pui_counter PROPERTY CXX_STANDARD 20)
set_property(TARGET pui_counter PROPERTY CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# PufferUI "next" Phase 1 demo: SDL3 window using the redesigned core.
# ---------------------------------------------------------------------------
add_executable(pui_demo
  "examples/pui_demo.cpp"
)

target_compile_definitions(pui_demo PRIVATE
    PUFFERUI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_link_libraries(pui_demo PRIVATE pufferui_sdl3 pufferui_warnings)

set_property(TARGET pui_demo PROPERTY CXX_STANDARD 20)
set_property(TARGET pui_demo PROPERTY CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Golden-image tests: offscreen SDL + software renderer, hash + BMP compare.
# ---------------------------------------------------------------------------
add_executable(pui_golden_tests
  "tests/golden/test_golden.cpp"
)

target_compile_definitions(pui_golden_tests PRIVATE
    PUFFERUI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    PUFFERUI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
target_link_libraries(pui_golden_tests PRIVATE pufferui_sdl3 pufferui_warnings)

set_property(TARGET pui_golden_tests PROPERTY CXX_STANDARD 20)
set_property(TARGET pui_golden_tests PROPERTY CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Examples: one standalone program per capability (examples/atomic/<name>.cpp),
# plus the guided tour. `examples_selftest` builds nothing itself; it runs every
# example offscreen (`--selftest`) and fails on any core violation. Add a new
# example by dropping the file in and appending its name to the list.
# ---------------------------------------------------------------------------
set(PUFFERUI_EXAMPLE_NAMES
    hello
    bootstrap
    rect
    cursors
    tracks
    grid
    ids
    text
    buttons
    inputs
    widgets
    panels
    scroll
    popups
    dock
    windows
    drawing
    blur
    animation
    theme
    custom
    patterns
    expander
    combo
    keyboard
    vlist
overflow
)

# Headless examples: no SDL, no window (they still get the bundled font path).
set(PUFFERUI_HEADLESS_EXAMPLES
    headless
    renderer
)

set(PUFFERUI_SELFTEST_COMMANDS)
set(PUFFERUI_SELFTEST_DEPENDS)
foreach(name IN LISTS PUFFERUI_EXAMPLE_NAMES)
  add_executable(pui_ex_${name} "examples/atomic/${name}.cpp")
  target_compile_definitions(pui_ex_${name} PRIVATE
      PUFFERUI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
  target_link_libraries(pui_ex_${name} PRIVATE pufferui_sdl3 pufferui_warnings)
  set_property(TARGET pui_ex_${name} PROPERTY CXX_STANDARD 20)
  set_property(TARGET pui_ex_${name} PROPERTY CXX_STANDARD_REQUIRED ON)
  list(APPEND PUFFERUI_SELFTEST_COMMANDS COMMAND $<TARGET_FILE:pui_ex_${name}> --selftest)
  list(APPEND PUFFERUI_SELFTEST_DEPENDS pui_ex_${name})
endforeach()

foreach(name IN LISTS PUFFERUI_HEADLESS_EXAMPLES)
  add_executable(pui_ex_${name} "examples/atomic/${name}.cpp")
  target_compile_definitions(pui_ex_${name} PRIVATE
      PUFFERUI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
  target_link_libraries(pui_ex_${name} PRIVATE pufferui pufferui_warnings)
  set_property(TARGET pui_ex_${name} PROPERTY CXX_STANDARD 20)
  set_property(TARGET pui_ex_${name} PROPERTY CXX_STANDARD_REQUIRED ON)
  list(APPEND PUFFERUI_SELFTEST_COMMANDS COMMAND $<TARGET_FILE:pui_ex_${name}> --selftest)
  list(APPEND PUFFERUI_SELFTEST_DEPENDS pui_ex_${name})
endforeach()

# The guided tour: every capability in one app.
add_executable(pui_tour "examples/pui_tour.cpp")
target_compile_definitions(pui_tour PRIVATE
    PUFFERUI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_link_libraries(pui_tour PRIVATE pufferui_sdl3 pufferui_warnings)
set_property(TARGET pui_tour PROPERTY CXX_STANDARD 20)
set_property(TARGET pui_tour PROPERTY CXX_STANDARD_REQUIRED ON)
list(APPEND PUFFERUI_SELFTEST_COMMANDS COMMAND $<TARGET_FILE:pui_tour> --selftest)
list(APPEND PUFFERUI_SELFTEST_DEPENDS pui_tour)

add_custom_target(examples_selftest ${PUFFERUI_SELFTEST_COMMANDS}
    DEPENDS ${PUFFERUI_SELFTEST_DEPENDS} VERBATIM)

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
install(FILES
    "include/pufferui/pufferui.h"
    "src/pufferui_impl.cpp"
    "vendored/stb/stb_truetype.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pufferui")

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