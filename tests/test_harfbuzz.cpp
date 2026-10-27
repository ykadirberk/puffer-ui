#include <pufferui/vulkan.h>
#include "vulkan_test_options.h"
#include <cstdio>
#include <vector>
using namespace pui;
namespace
{
i32 errors = 0;
void diagnostic(i32 code, const char *message, void *)
{
    std::fprintf(stderr, "Vulkan %d: %s\n", code, message);
    ++errors;
}
} // namespace
int main()
{
    vulkan_device_opts opts = test_detail::reference_options();
    opts.painter = vulkan_painter_mode::SDF_RASTER;
    opts.vector_text = true;
    opts.validation = true;
    opts.error_callback = diagnostic;
    auto *device = create_vulkan_device(opts);
    if (!device) return 1;
    auto *surface = create_vulkan_surface(device, {.width = 256, .height = 128});
    auto *ctx = create_context(device, surface);
    auto *window = add_window(ctx, nullptr, surface, {0, 0, 256, 128});
    auto font = load_font(ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    auto theme = default_dark();
    theme.font = font;
    theme.text_size = 32;
    set_theme(ctx, theme);
    std::vector<u8> previous, pixels(256 * 128 * 4);
    for (i32 frame = 0; frame < 6; ++frame)
    {
        const auto before = vulkan_statistics(device);
        begin_frame(ctx, *window, 0, 1.0 / 60.0);
        {
            ui u(ctx);
            const float width = u.text_width("ffi");
            if (!(width > 15 && width < 40)) ++errors;
            u.draw_rect({0, 0, 256, 128}, {0, 0, 0, 255});
            u.text({8, 8, 200, 48}, "ffi", {255, 255, 255, 255});
            // One connected Arabic run and a combining-mark cluster.
            u.text({8, 64, 230, 48}, "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85  a\xcc\x81",
                   {255, 255, 255, 255});
        }
        end_frame(ctx);
        const auto after = vulkan_statistics(device);
        // 1 background quad + 1 ffi ligature + 3 Arabic glyphs + spaces + a-acute.
        // The old codepoint path emits 3 ffi and 2 combining-cluster glyphs.
        if (after.requested_vertices - before.requested_vertices > 28 || after.live_textures != 2)
            ++errors; // white + scene, no bitmap glyph atlas
        if (!device->read_pixels(surface->scene_target(), 0, 0, 256, 128, pixels.data())) ++errors;
        if (frame && pixels != previous) ++errors;
        previous = pixels;
        if (frame == 0)
        {
            if (load_font(ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf") == FONT_INVALID ||
                load_font(ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Oblique.ttf") == FONT_INVALID)
                ++errors;
        }
    }
    usize lit = 0;
    for (usize i = 0; i < pixels.size(); i += 4)
        if (pixels[i]) ++lit;
    if (lit < 200 || lit > 6000 || violation_count(ctx)) ++errors;
    errors += static_cast<i32>(vulkan_statistics(device).validation_errors);
    destroy_context(ctx);
    device->destroy_surface(surface);
    if (!vulkan_wait_idle(device)) ++errors;
    destroy_vulkan_device(device);
    if (!errors)
        std::puts("all HarfBuzz shaping, ligature, Arabic, Slug GPU and reuse regressions passed");
    else
        std::fprintf(stderr, "HarfBuzz regression errors=%d, lit pixels=%zu\n", errors, lit);
    return errors ? 1 : 0;
}
