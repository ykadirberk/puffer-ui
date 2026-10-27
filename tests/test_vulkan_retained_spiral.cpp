// Exercise the unchanged all-frame showcase oracle with native program reuse.
#include <pufferui/vulkan.h>
namespace
{
pui::render_device *retained_spiral_device(const pui::vulkan_device_opts &options = {})
{
    auto configured = options;
    configured.whole_program_cache = true;
    return pui::create_vulkan_device(configured);
}
} // namespace
#define create_vulkan_device retained_spiral_device
#include "test_vulkan_compute_spiral.cpp"
#undef create_vulkan_device
