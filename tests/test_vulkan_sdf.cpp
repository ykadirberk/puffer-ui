#include <pufferui/vulkan.h>
#include "vulkan_test_options.h"
#include <cstdio>
#include <cstring>
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
struct capture
{
    std::vector<u8> pixels;
    u64 vertices = 0, indices = 0;
};
capture render(vulkan_painter_mode mode, bool rounded, bool staged, bool cache = false)
{
    vulkan_device_opts opts = test_detail::reference_options();
    opts.validation = true;
    opts.painter = mode;
    opts.tile_cache = cache;
    opts.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    opts.error_callback = diagnostic;
    auto *device = create_vulkan_device(opts);
    capture result;
    if (!device)
    {
        ++errors;
        return result;
    }
    auto *surface = create_vulkan_surface(device, {.width = 128, .height = 96});
    auto *ctx = create_context(device, surface);
    auto *window = add_window(ctx, nullptr, surface, {0, 0, 128, 96});
    auto font = load_font(ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (font == FONT_INVALID) ++errors;
    auto theme = default_dark();
    theme.font = font;
    set_theme(ctx, theme);
    set_violation_handler(
        ctx,
        [](void *, const char *condition, const char *message, const char *, i32)
        {
            std::fprintf(stderr, "Violation %s: %s\n", condition, message);
            ++errors;
        },
        nullptr);
    // Pool reset and slot reuse must retain the same picture for identical input.
    for (i32 frame = 0; frame < 8; ++frame)
    {
        begin_frame(ctx, *window, 0, 1.0 / 60.0);
        {
            ui u(ctx);
            u.draw_rect({0, 0, 128, 96}, {11, 22, 33, 255});
            u.text({4, 2, 100, 20}, "Atlas text", {240, 220, 200, 255});
            if (rounded)
            {
                u.draw_rounded_rect({16, 28, 48, 48}, {255, 0, 0, 255}, 12);
                u.draw_rounded_rect({40, 40, 48, 48}, {0, 255, 0, 128}, corner_radii{2, 10, 16, 4});
            }
            else
            {
                const vertex vertices[] = {{5, 25, 0, 0, {255, 0, 0, 127}},
                                           {100, 29, 0, 0, {0, 255, 0, 200}},
                                           {52, 92, 0, 0, {0, 0, 255, 255}}};
                const i32 indices[] = {0, 1, 2};
                u.draw_triangles(nullptr, vertices, 3, indices, 3);
            }
            u.draw_rect({110, 70, 8, 8}, {1, 2, 3, 255});
            // Fractional edges straddle tile/sample boundaries. Unknown legacy
            // geometry must invalidate qualification even after raster snapping.
            u.draw_rect({31.499f, 30.501f, 32.002f, 20.999f}, {41, 122, 203, 128});
            const vertex edge[] = {{31.499f, 32.499f, 0, 0, {250, 10, 50, 90}},
                                   {64.501f, 32.499f, 0, 0, {10, 250, 50, 200}},
                                   {31.499f, 63.501f, 0, 0, {50, 10, 250, 255}}};
            const i32 edge_indices[] = {0, 1, 2};
            u.draw_triangles(nullptr, edge, 3, edge_indices, 3);
        }
        end_frame(ctx);
        std::vector<u8> pixels(128 * 96 * 4);
        if (!device->read_pixels(surface->scene_target(), 0, 0, 128, 96, pixels.data())) ++errors;
        if (frame && result.pixels != pixels) ++errors;
        result.pixels = pixels;
    }
    const auto stats = vulkan_statistics(device);
    if (cache && (!stats.gpu_sdf_cache_hits || !stats.gpu_sdf_cached_pixels))
    {
        std::fprintf(stderr,
                     "GPU cache did not reuse pixels: batches=%llu pixels=%llu hits=%llu "
                     "misses=%llu saved=%llu\n",
                     static_cast<unsigned long long>(stats.gpu_sdf_batches),
                     static_cast<unsigned long long>(stats.gpu_sdf_pixels),
                     static_cast<unsigned long long>(stats.gpu_sdf_cache_hits),
                     static_cast<unsigned long long>(stats.gpu_sdf_cache_misses),
                     static_cast<unsigned long long>(stats.gpu_sdf_cache_image_bytes));
        ++errors;
    }
    if (mode == vulkan_painter_mode::COMPUTE_SDF &&
        (!stats.gpu_sdf_batches || !stats.gpu_sdf_pixels))
    {
        std::fprintf(stderr, "compute did not write scene pixels\n");
        ++errors;
    }
    result.vertices = stats.requested_vertices;
    result.indices = stats.requested_indices;
    errors += static_cast<i32>(stats.validation_errors);
    destroy_context(ctx);
    device->destroy_surface(surface);
    if (!vulkan_wait_idle(device)) ++errors;
    destroy_vulkan_device(device);
    return result;
}
} // namespace
int main()
{
#if !defined(PUFFERUI_ENABLE_HARFBUZZ)
    i32 rejected = 0;
    vulkan_device_opts unavailable = test_detail::reference_options();
    unavailable.painter = vulkan_painter_mode::SDF_RASTER;
    unavailable.vector_text = true;
    unavailable.user = &rejected;
    unavailable.error_callback = [](i32, const char *message, void *user)
    {
        if (std::strstr(message, "HarfBuzz")) ++*static_cast<i32 *>(user);
    };
    auto *unsupported = create_vulkan_device(unavailable);
    if (unsupported)
    {
        destroy_vulkan_device(unsupported);
        ++errors;
    }
    if (rejected != 1) ++errors;
#endif
    const auto reference = render(vulkan_painter_mode::REFERENCE, false, false);
    const auto legacy = render(vulkan_painter_mode::SDF_RASTER, false, false);
    if (reference.pixels.empty() || reference.pixels != legacy.pixels)
    {
        std::fprintf(stderr, "legacy-only pixels changed\n");
        ++errors;
    }
    const auto old_rounded = render(vulkan_painter_mode::REFERENCE, true, false);
    const auto sdf = render(vulkan_painter_mode::SDF_RASTER, true, false);
    const auto staged = render(vulkan_painter_mode::SDF_RASTER, true, true);
    const auto compute = render(vulkan_painter_mode::COMPUTE_SDF, true, false);
    const auto cached = render(vulkan_painter_mode::COMPUTE_SDF, true, false, true);
    if (cached.pixels != sdf.pixels)
    {
        std::fprintf(stderr, "cached/SDF raster mismatch\n");
        ++errors;
    }
    if (compute.pixels != sdf.pixels)
    {
        std::fprintf(stderr, "compute/SDF raster mismatch\n");
        ++errors;
    }
    if (sdf.pixels.empty() || sdf.pixels != staged.pixels ||
        sdf.vertices + 1000 >= old_rounded.vertices || sdf.indices >= old_rounded.indices)
    {
        std::fprintf(stderr, "SDF counts legacy=%llu/%llu sdf=%llu/%llu staged_equal=%d\n",
                     static_cast<unsigned long long>(old_rounded.vertices),
                     static_cast<unsigned long long>(old_rounded.indices),
                     static_cast<unsigned long long>(sdf.vertices),
                     static_cast<unsigned long long>(sdf.indices), sdf.pixels == staged.pixels);
        ++errors;
    }
    // SDF coverage may change only the rounded region; text and distant primitives stay exact.
    for (usize y = 0; y < 96; ++y)
        for (usize x = 0; x < 128; ++x)
            if (y < 26 || x > 90)
                for (usize channel = 0; channel < 4; ++channel)
                {
                    const usize at = (y * 128 + x) * 4 + channel;
                    if (sdf.pixels[at] != old_rounded.pixels[at])
                    {
                        if (!errors)
                            std::fprintf(stderr, "outside SDF difference x%zu y%zu c%zu: %u/%u\n",
                                         x, y, channel, sdf.pixels[at], old_rounded.pixels[at]);
                        ++errors;
                    }
                }
    if (!errors)
        std::puts("all SDF batching, legacy isolation and slot regressions passed");
    else
        std::fprintf(stderr, "SDF regressions: %d errors\n", errors);
    return errors ? 1 : 0;
}
