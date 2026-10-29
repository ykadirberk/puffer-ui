// Reuse the actual, unchanged 180-frame showcase harness. Factory controls and
// telemetry stay outside the timed frame loop and never change example flows.
#include <pufferui/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace pui
{
render_device *optimization_device(const vulkan_device_opts & = {});
void optimization_destroy(render_device *);
} // namespace pui
#define create_vulkan_device optimization_device
#define destroy_vulkan_device optimization_destroy
#include "test_vulkan_native_spiral.cpp"
#undef create_vulkan_device
#undef destroy_vulkan_device
namespace pui
{
render_device *optimization_device(const vulkan_device_opts &original)
{
    auto options = original;
#if !defined(PUFFERUI_OPT3_BASELINE)
    if (const auto *value = std::getenv("PUFFERUI_TEST_LEGACY_VERTICES"))
        options.compact_vertices = std::strcmp(value, "1") != 0;
    if (const auto *value = std::getenv("PUFFERUI_TEST_COVERAGE_BYTES"))
        options.glyph_coverage_budget_bytes = std::strtoull(value, nullptr, 10);
#endif
    return create_vulkan_device(options);
}
void optimization_destroy(render_device *device)
{
#if !defined(PUFFERUI_OPT3_BASELINE)
    const auto stats = vulkan_statistics(device);
    std::fprintf(stderr,
                 "optimization_stats hits=%llu misses=%llu fills=%llu coverage_draws=%llu "
                 "coverage_bytes=%llu evictions=%llu image_bytes=%llu\n",
                 static_cast<unsigned long long>(stats.glyph_coverage_hits),
                 static_cast<unsigned long long>(stats.glyph_coverage_misses),
                 static_cast<unsigned long long>(stats.glyph_coverage_fills),
                 static_cast<unsigned long long>(stats.glyph_coverage_draws),
                 static_cast<unsigned long long>(stats.glyph_coverage_bytes),
                 static_cast<unsigned long long>(stats.glyph_coverage_evictions),
                 static_cast<unsigned long long>(stats.image_bytes));
#endif
    destroy_vulkan_device(device);
}
} // namespace pui
