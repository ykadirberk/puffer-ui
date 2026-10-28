// Independent scalar Slug replay is the batching oracle; capture mode also
// compares this text-only domain with the previous SDF renderer's Slug output.
#if !defined(PUFFERUI_OLD_SLUG_ORACLE)
#define PUFFERUI_IMPLEMENTATION
#endif
#include <pufferui/vulkan.h>
#include <filesystem>
#include <fstream>
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
struct text_scene
{
    render_device *device = nullptr;
    render_surface *surface = nullptr;
    context *ctx = nullptr;
    window *win = nullptr;
    font_handle regular = FONT_INVALID, bold = FONT_INVALID;
    bool initialize(vulkan_device_opts opts)
    {
        device = create_vulkan_device(opts);
        if (!device) return false;
        surface = create_vulkan_surface(device, {.width = 321, .height = 193});
        ctx = create_context(device, surface);
        win = add_window(ctx, nullptr, surface, {0, 0, 321, 193});
        regular = load_font(ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
        bold = load_font(ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
        return surface && ctx && win && regular != FONT_INVALID && bold != FONT_INVALID;
    }
    void close()
    {
        destroy_context(ctx);
        device->destroy_surface(surface);
        destroy_vulkan_device(device);
    }
    bool frame(i32 f, std::vector<u8> &pixels)
    {
        const f32 scale = f < 32 ? 1 : f < 64 ? 1.5f : 2;
        set_window_scale(*win, scale);
        auto style = default_dark();
        style.font = regular;
        style.text_size = 13 + static_cast<f32>(f % 5) * 5;
        style.text_gamma = f % 3 == 0 ? .8f : f % 3 == 1 ? 1 : 1.2f;
        set_theme(ctx, style);
        begin_frame(ctx, *win, f / 60.0, 1.0 / 60.0);
        {
            ui u(ctx);
            u.text({3.25f, 2.75f, 300, 42}, "office ffi a\xcc\x81 \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85",
                   {253, 187, 73, 217});
            {
                auto clip = u.region({17.5f, 34.25f, 196.75f, 61.5f}, "text_clip"_id);
                auto font = u.text_style(28, bold);
                u.text({4.5f, 38.75f, 260, 42}, "AV ffi overlapping", {41, 183, 247, 133});
                u.text({9.75f, 43.25f, 260, 42}, "AV ffi overlapping", {241, 53, 167, 81});
            }
            u.draw_rect({12, 84, 146, 12}, {71, 83, 127, 177});
            {
                auto disabled = u.disabled();
                u.text({8.5f, 86.5f, 280, 42}, "Disabled ffi", {213, 239, 251, 199});
            }
            // An oversized outline and a completely clipped run retain the
            // native Slug result without creating a bitmap glyph atlas.
            {
                auto font = u.text_style(f % 2 ? 192 : 72, regular);
                u.text({240, 5, 70, 180}, "Q", {255, 255, 255, 137});
                u.text({0, 500, 300, 240}, "clipped ", {255, 255, 255, 255});
            }
        }
        end_frame(ctx);
        return !violation_count(ctx) && vulkan_last_result(device) == VK_SUCCESS &&
               device->read_pixels(surface->scene_target(), 0, 0, 321, 193, pixels.data());
    }
};
} // namespace
#if !defined(PUFFERUI_OLD_SLUG_ORACLE)
#include "test_vulkan_native_stream.inl"
#endif
int main(int argc, char **argv)
{
    const char *capture = nullptr, *reference = nullptr;
    bool staged = false;
    for (i32 i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--capture") && i + 1 < argc)
            capture = argv[++i];
        else if (!std::strcmp(argv[i], "--reference") && i + 1 < argc)
            reference = argv[++i];
        else if (!std::strcmp(argv[i], "--staged"))
            staged = true;
        else
            return 2;
    }
    if (capture && reference) return 2;
    if (capture && !std::filesystem::create_directory(capture)) return 2;
    vulkan_device_opts opts;
    opts.validation = true;
    opts.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    opts.error_callback = diagnostic;
#if defined(PUFFERUI_OLD_SLUG_ORACLE)
    opts.painter = vulkan_painter_mode::SDF_RASTER;
#endif
    text_scene actual, scalar;
    if (!actual.initialize(opts)) return 1;
    const bool paired = !capture && !reference;
    if (paired)
    {
        const auto native = vulkan_handles(actual.device);
        opts.native = &native;
        opts.tile_cache = true;
        if (!scalar.initialize(opts)) return 1;
#if !defined(PUFFERUI_OLD_SLUG_ORACLE)
        static_cast<vk_detail::device *>(scalar.device)->text->batch_limit = 1;
#endif
    }
    std::vector<u8> pixels(321 * 193 * 4), expected(pixels.size());
    for (i32 f = 0; f < 96; ++f)
    {
        if (!actual.frame(f, pixels)) return 1;
        char name[32];
        std::snprintf(name, sizeof(name), "frame-%03d.rgba", f);
        if (capture)
        {
            std::ofstream file(std::filesystem::path(capture) / name, std::ios::binary);
            file.write(reinterpret_cast<const char *>(pixels.data()), pixels.size());
            if (!file) return 1;
        }
        if (reference)
        {
            std::ifstream file(std::filesystem::path(reference) / name, std::ios::binary);
            file.read(reinterpret_cast<char *>(expected.data()), expected.size());
            if (!file || file.peek() != std::char_traits<char>::eof()) return 1;
        }
        if (paired && !scalar.frame(f, expected)) return 1;
        if ((paired || reference) && pixels != expected)
        {
            std::fprintf(stderr, "Slug text RGBA mismatch frame %d\n", f);
            return 1;
        }
    }
    if (paired)
    {
        const auto a = vulkan_statistics(actual.device), b = vulkan_statistics(scalar.device);
#if !defined(PUFFERUI_OLD_SLUG_ORACLE)
        if (!a.glyph_upload_bytes || a.glyph_instances != b.glyph_instances ||
            a.glyph_draws >= b.glyph_draws || a.glyph_instances != b.glyph_draws ||
            a.draw_buffer_allocations || b.draw_buffer_allocations || b.viewport_sets >= b.draws ||
            b.pipeline_binds >= b.draws)
            return 1;
#endif
        if (text_atlas_pages(actual.ctx) || text_atlas_pages(scalar.ctx) || a.validation_errors ||
            b.validation_errors)
            return 1;
#if !defined(PUFFERUI_OLD_SLUG_ORACLE)
        if (!arena_growth_regression(actual.device) || !arena_growth_regression(scalar.device) ||
            !chunk_regression(actual.device, scalar.device))
        {
            std::fprintf(stderr, "native ordered chunk/reuse regression failed\n");
            return 1;
        }
        if (!rebind_regression(actual, scalar.device, pixels))
        {
            std::fprintf(stderr, "native Slug device rebind regression failed\n");
            return 1;
        }
#endif
        scalar.close();
    }
    if (errors || vulkan_statistics(actual.device).validation_errors) return 1;
    actual.close();
    std::puts("all 96 native Slug ligature, overlap, clip, DPI, gamma and scalar frames matched");
    return 0;
}
