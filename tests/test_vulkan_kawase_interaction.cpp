// Exercise the unchanged interaction fixture with the alternate native filter.
#include <pufferui/vulkan.h>
static pui::render_device *create_kawase_device(const pui::vulkan_device_opts &requested)
{
    auto options = requested;
    options.blur = pui::vulkan_blur_mode::KAWASE;
    return pui::create_vulkan_device(options);
}
#define create_vulkan_device create_kawase_device
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
#include "test_harfbuzz_interaction.cpp"
#else
#include "test_vulkan_interaction.cpp"
#endif
#undef create_vulkan_device
