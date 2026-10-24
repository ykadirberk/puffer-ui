// The one translation unit that compiles the PufferUI implementation.
#define PUFFERUI_IMPLEMENTATION
#include <pufferui/pufferui.h>

#if defined(PUFFERUI_INTERNAL_TESTS)
#include "../tests/test_performance.inl"
#include "../tests/test_geometry_performance.inl"
#include "../tests/test_core_performance.inl"
#endif

#if defined(PUFFERUI_MEMORY_TESTS)
#include "../tests/test_memory_performance.inl"
#endif

#if defined(PUFFERUI_CORE_PERF_BENCH)
#include "../tests/bench_core_performance.inl"
#endif

#if defined(PUFFERUI_PREPARATION_TESTS)
#include "../tests/test_geometry_preparation.inl"
#endif

#if defined(PUFFERUI_VULKAN_INTERNAL_TESTS)
#include "../tests/test_vulkan_internal.inl"
#endif

#if defined(PUFFERUI_VULKAN_COMMAND_TESTS)
#include "../tests/test_vulkan_commands.inl"
#endif

#if defined(PUFFERUI_VULKAN_BINNING_TESTS)
#include "../tests/test_vulkan_binning.inl"
#endif

#if defined(PUFFERUI_VULKAN_SPIRAL_BINNING_TESTS)
#include "../tests/test_vulkan_spiral_binning.inl"
#endif

#if defined(PUFFERUI_VULKAN_NUMERIC_PROBE)
#include "../tests/test_vulkan_numeric_probe.inl"
#endif

#if defined(PUFFERUI_VULKAN_COMPUTE_TESTS)
#include "../tests/test_vulkan_compute.inl"
#endif

#if defined(PUFFERUI_VULKAN_SDF_CACHE_TESTS)
#include "../tests/test_vulkan_sdf_cache.inl"
#endif

#if defined(PUFFERUI_VULKAN_PREPARATION_TESTS)
#include "../tests/test_vulkan_preparation.inl"
#endif
