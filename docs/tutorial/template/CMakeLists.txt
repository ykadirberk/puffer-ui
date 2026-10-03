cmake_minimum_required(VERSION 3.21)
project(glass_todo CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# PufferUI lives in ./pufferui (a clone or a git submodule). EXCLUDE_FROM_ALL keeps
# its examples and tests out of your build: only what you link gets compiled.
add_subdirectory(pufferui EXCLUDE_FROM_ALL)

add_executable(glass_todo main.cpp)
target_link_libraries(glass_todo PRIVATE pufferui::sdl3) # core + the SDL3 backend

# Where the bundled fonts live: the directory that contains fonts/.
target_compile_definitions(glass_todo PRIVATE
    PUFFERUI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/pufferui/assets")
