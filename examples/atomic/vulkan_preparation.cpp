// Prepare a known scene without advancing any UI/animation frame.
#include "../example_common.h"
#include <pufferui/vulkan.h>

struct prepared_scene
{
    bool ready = false;
    void prepare(example_app &app)
    {
        const geometry_capacity capacity{4096, 8192};
        ready = ui(app.ctx).prepare_geometry(capacity);
        if (vulkan_wait_idle(app.device))
            ready = vulkan_prepare_surface_geometry(app.surface, capacity) && ready;
        else
            ready = false;
    }
    void operator()(ui &u, example_app &app) const
    {
        auto page = example_begin_page(u, app, "Vulkan scene preparation",
                                       "Resource setup completes before the first UI frame.");
        rect area = page.content.cut_top(u.control_h() * 8 + u.spacing() * 4);
        rect content = example_section(u, area, "Known scene capacity",
                                       "CPU storage and bounded upload pages are renderer-owned.",
                                       app.font_bold);
        u.text_wrapped(content,
                       "prepare_geometry commits CPU storage. vulkan_prepare_surface_geometry "
                       "prepares every completed surface slot without a draw, submit or wait. "
                       "The normal frame path still handles later growth.",
                       u.th().text);
    }
};
int main(int argc, char **argv)
{
    bool help = false;
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) help = true;
    prepared_scene scene;
    const int result = example_run("Vulkan scene preparation", 720, 420, argc, argv, scene);
    return result ? result : (help || scene.ready ? 0 : 1);
}
