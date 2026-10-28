// The one translation unit that compiles the PufferUI implementation.
#define PUFFERUI_IMPLEMENTATION
#include <pufferui/pufferui.h>

#if defined(PUFFERUI_INTERNAL_TESTS)
#include "../tests/test_performance.inl"
#include "../tests/test_geometry_performance.inl"
#include "../tests/test_core_performance.inl"
#include "../tests/test_state_pool.inl"
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
#include "../tests/test_rect_preparation_culling.inl"
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

#if defined(PUFFERUI_VULKAN_SOURCE_TESTS)
#include "../tests/test_vulkan_source.inl"
#endif

#if defined(PUFFERUI_VULKAN_FUNCTION_TESTS)
#include "../tests/test_vulkan_function.inl"
#endif

#if defined(PUFFERUI_VULKAN_INTEGRATED_TESTS)
#include "../tests/test_vulkan_integrated.inl"
#endif

#if defined(PUFFERUI_VULKAN_OUTPUT_CACHE_TESTS)
#include "../tests/test_vulkan_output_cache.inl"
#endif

#if defined(PUFFERUI_VULKAN_GLYPH_SOURCE_TESTS)
#include "../tests/test_vulkan_glyph_source.inl"
#endif

#if defined(PUFFERUI_VULKAN_IDENTITY_TESTS)
#include "../tests/test_vulkan_identity.inl"
#endif

#if defined(PUFFERUI_VULKAN_PRODUCER_TESTS)
#include "../tests/test_vulkan_producer.inl"
#endif

#if defined(PUFFERUI_VULKAN_GROUP_TESTS)
#include "../tests/test_vulkan_groups.inl"
#endif

#if defined(PUFFERUI_VULKAN_ALPHA_TESTS)
#include "../tests/test_vulkan_alpha.inl"
#endif

#if defined(PUFFERUI_VULKAN_LOAD_TESTS)
#include "../tests/test_vulkan_load.inl"
#endif

#if defined(PUFFERUI_VULKAN_PREPARATION_TESTS)
#include "../tests/test_vulkan_preparation.inl"
#include "../tests/test_vulkan_glyph_residency.inl"
#include "../tests/test_vulkan_text_culling.inl"
#endif

#if defined(PUFFERUI_VULKAN_WORKGROUP_TESTS)
#include "../tests/test_vulkan_workgroup.inl"
#endif

#if defined(PUFFERUI_VULKAN_PROGRAM_TESTS)
#include "../tests/test_vulkan_program.inl"
#include "../tests/test_vulkan_program_admission.inl"
#include "../tests/test_vulkan_program_probe_preparation.inl"
#include "../tests/test_vulkan_graphics_state.inl"
#include "../tests/test_vulkan_color_upload.inl"
#endif

#if defined(PUFFERUI_VULKAN_MEMORY_ROLE_TESTS)
#include "../tests/test_vulkan_memory_roles.inl"
#endif

#if defined(PUFFERUI_VULKAN_LOCALITY_TESTS)
#include "../tests/test_vulkan_locality.inl"
#include "../tests/test_vulkan_painter_kernel.inl"
#endif
