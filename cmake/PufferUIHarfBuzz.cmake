# Optional shaping and analytic GPU outlines. Pin the upstream implementation;
# Vulkan defaults include shaping/Slug. Core/SDL and an explicit OFF remain
# independent and neither download nor link this dependency.
option(PUFFERUI_WITH_HARFBUZZ "Build HarfBuzz shaping and Slug GPU text support" ${PUFFERUI_WITH_VULKAN})
if(PUFFERUI_WITH_HARFBUZZ)
  include(FetchContent)
  set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
  set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
  set(HB_BUILD_RASTER OFF CACHE BOOL "" FORCE)
  set(HB_BUILD_VECTOR OFF CACHE BOOL "" FORCE)
  set(HB_BUILD_GPU ON CACHE BOOL "" FORCE)
  set(HB_BUILD_GPU_DEMO OFF CACHE STRING "" FORCE)
  FetchContent_Declare(pufferui_harfbuzz
    GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
    GIT_TAG 35f656a1a2bab548ea7f54a6dd3fba2b09680d14)
  FetchContent_MakeAvailable(pufferui_harfbuzz)
endif()
