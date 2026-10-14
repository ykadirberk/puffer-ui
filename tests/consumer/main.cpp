#include <pufferui/pufferui.h>

int main()
{
    pui::null_device device;
    pui::context *ctx = pui::create_context(&device, device.create_surface());
    pui::begin_frame(ctx, 0, 0.016, {0, 0, 100, 100});
    {
        pui::ui u(ctx);
        pui::field_opts opts{.enabled = false, .read_only = true};
        auto disabled = u.disabled(!opts.enabled);
        u.draw_rect({0, 0, 10, 10}, {255, 255, 255, 255});
    }
    pui::end_frame(ctx);
    const bool ok = pui::violation_count(ctx) == 0;
    pui::destroy_context(ctx);
    return ok ? 0 : 1;
}
