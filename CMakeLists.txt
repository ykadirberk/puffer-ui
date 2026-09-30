# CMakeList.txt : CMake project for PufferUI
cmake_minimum_required (VERSION 3.21)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("PufferUI" VERSION 0.1.0.1)

# Link-time optimization + fast math for release builds
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
if (MSVC)
  add_compile_options("$<$<CONFIG:Release>:/fp:fast>")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

set(BUILD_SHARED_LIBS OFF)
add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# PufferUI core: one header + one implementation TU (src/pufferui_impl.cpp).
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
add_library(pufferui INTERFACE)
add_library(pufferui::pufferui ALIAS pufferui)
target_include_directories(pufferui INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendored/stb>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# ---------------------------------------------------------------------------
# Headless core tests (no SDL, no window).
# ---------------------------------------------------------------------------
add_executable(pui_core_tests
  "tests/test_core.cpp"
  "src/pufferui_impl.cpp"
)
target_link_libraries(pui_core_tests PRIVATE pufferui)

set_property(TARGET pui_core_tests PROPERTY CXX_STANDARD 20)
set_property(TARGET pui_core_tests PROPERTY CXX_STANDARD_REQUIRED ON)
target_compile_definitions(pui_core_tests PRIVATE
    PUFFERUI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

# SDL event-pump tests: SDL_PushEvent-driven routing through the offscreen
# driver. Same exit contract as the core tests (exit 0 = all passed).
add_executable(pui_pump_tests
  "tests/test_pump.cpp"
  "src/pufferui_impl.cpp"
)
target_compile_definitions(pui_pump_tests PRIVATE PUFFERUI_ENABLE_SDL3
    PUFFERUI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_link_libraries(pui_pump_tests PRIVATE pufferui SDL3-static)
set_property(TARGET pui_pump_tests PROPERTY CXX_STANDARD 20)
set_property(TARGET pui_pump_tests PROPERTY CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# PufferUI "next" small example: model/view/dispatch (docs/model_view.md).
# ---------------------------------------------------------------------------
add_executable(pui_counter
  "examples/pui_counter.cpp"
  "src/pufferui_impl.cpp"
)

target_compile_definitions(pui_counter PRIVATE PUFFERUI_ENABLE_SDL3
    PUFFERUI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_link_libraries(pui_counter PRIVATE pufferui SDL3-static)

set_property(TARGET pui_counter PROPERTY CXX_STANDARD 20)
set_property(TARGET pui_counter PROPERTY CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# PufferUI "next" Phase 1 demo: SDL3 window using the redesigned core.
# ---------------------------------------------------------------------------
add_executable(pui_demo
  "examples/pui_demo.cpp"
  "src/pufferui_impl.cpp"
)

target_compile_definitions(pui_demo PRIVATE PUFFERUI_ENABLE_SDL3
    PUFFERUI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_link_libraries(pui_demo PRIVATE pufferui SDL3-static)

set_property(TARGET pui_demo PROPERTY CXX_STANDARD 20)
set_property(TARGET pui_demo PROPERTY CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Golden-image tests: offscreen SDL + software renderer, hash + BMP compare.
# ---------------------------------------------------------------------------
add_executable(pui_golden_tests
  "tests/golden/test_golden.cpp"
  "src/pufferui_impl.cpp"
)

target_compile_definitions(pui_golden_tests PRIVATE PUFFERUI_ENABLE_SDL3
    PUFFERUI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    PUFFERUI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
target_link_libraries(pui_golden_tests PRIVATE pufferui SDL3-static)

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
  add_executable(pui_ex_${name} "examples/atomic/${name}.cpp" "src/pufferui_impl.cpp")
  target_compile_definitions(pui_ex_${name} PRIVATE PUFFERUI_ENABLE_SDL3
      PUFFERUI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
  target_link_libraries(pui_ex_${name} PRIVATE pufferui SDL3-static)
  set_property(TARGET pui_ex_${name} PROPERTY CXX_STANDARD 20)
  set_property(TARGET pui_ex_${name} PROPERTY CXX_STANDARD_REQUIRED ON)
  list(APPEND PUFFERUI_SELFTEST_COMMANDS COMMAND $<TARGET_FILE:pui_ex_${name}> --selftest)
  list(APPEND PUFFERUI_SELFTEST_DEPENDS pui_ex_${name})
endforeach()

foreach(name IN LISTS PUFFERUI_HEADLESS_EXAMPLES)
  add_executable(pui_ex_${name} "examples/atomic/${name}.cpp" "src/pufferui_impl.cpp")
  target_compile_definitions(pui_ex_${name} PRIVATE
      PUFFERUI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
  target_link_libraries(pui_ex_${name} PRIVATE pufferui)
  set_property(TARGET pui_ex_${name} PROPERTY CXX_STANDARD 20)
  set_property(TARGET pui_ex_${name} PROPERTY CXX_STANDARD_REQUIRED ON)
  list(APPEND PUFFERUI_SELFTEST_COMMANDS COMMAND $<TARGET_FILE:pui_ex_${name}> --selftest)
  list(APPEND PUFFERUI_SELFTEST_DEPENDS pui_ex_${name})
endforeach()

# The guided tour: every capability in one app.
add_executable(pui_tour "examples/pui_tour.cpp" "src/pufferui_impl.cpp")
target_compile_definitions(pui_tour PRIVATE PUFFERUI_ENABLE_SDL3
    PUFFERUI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_link_libraries(pui_tour PRIVATE pufferui SDL3-static)
set_property(TARGET pui_tour PROPERTY CXX_STANDARD 20)
set_property(TARGET pui_tour PROPERTY CXX_STANDARD_REQUIRED ON)
list(APPEND PUFFERUI_SELFTEST_COMMANDS COMMAND $<TARGET_FILE:pui_tour> --selftest)
list(APPEND PUFFERUI_SELFTEST_DEPENDS pui_tour)

add_custom_target(examples_selftest ${PUFFERUI_SELFTEST_COMMANDS}
    DEPENDS ${PUFFERUI_SELFTEST_DEPENDS} VERBATIM)

# ---------------------------------------------------------------------------
# Install / export: `find_package(pufferui)` gives pufferui::pufferui plus
# ${PUFFERUI_IMPL}, the implementation TU to add to exactly one target.
# ---------------------------------------------------------------------------
include(CMakePackageConfigHelpers)

install(TARGETS pufferui EXPORT pufferuiTargets)
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