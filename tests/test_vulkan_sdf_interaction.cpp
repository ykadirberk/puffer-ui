// Reuse the original interaction workload and assertions without editing it.
// Only the factory selects the new painter; bitmap atlas semantics stay active.
#include <pufferui/vulkan.h>
static pui::render_device *sdf_interaction_device(const pui::vulkan_device_opts &input)
{
    auto opts = input;
    opts.painter = pui::vulkan_painter_mode::COMPUTE_SDF;
    return pui::create_vulkan_device(opts);
}
#define create_vulkan_device sdf_interaction_device
#define main original_interaction_main
#include "test_vulkan_interaction.cpp"
#undef main
#undef create_vulkan_device
int main(int argc, char **argv)
{
    return original_interaction_main(argc, argv);
}
