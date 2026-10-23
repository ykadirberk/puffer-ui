#include <pufferui/vulkan.h>
#include <cstdio>
using namespace pui;

static bool exercise(render_device *device)
{
    render_surface *surface = create_vulkan_surface(device, {.width = 32, .height = 32});
    if (!surface) return false;
    context *context = create_context(device, surface);
    begin_frame(context, 1, 1.0 / 60.0, rect::make(0, 0, 32, 32));
    {
        ui u(context);
        u.draw_rect(rect::make(0, 0, 16, 16), {255, 0, 0, 255});
    }
    end_frame(context);
    u8 pixel[4]{};
    const bool okay = device->read_pixels(surface->scene_target(), 4, 4, 1, 1, pixel) &&
                      pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0 &&
                      violation_count(context) == 0;
    destroy_context(context);
    device->destroy_surface(surface);
    return okay && vulkan_wait_idle(device);
}
int main()
{
    render_device *owner = create_vulkan_device();
    if (!owner) return 1;
    auto native = vulkan_handles(owner);
    vulkan_device_opts opts;
    opts.native = &native;
    render_device *borrowed = create_vulkan_device(opts);
    const bool child_okay = borrowed && exercise(borrowed);
    destroy_vulkan_device(borrowed);
    const bool parent_okay = exercise(owner);
    destroy_vulkan_device(owner);
    if (!child_okay || !parent_okay) return 1;
    std::puts("installed Vulkan consumer: borrowed and owned devices passed");
    return 0;
}
