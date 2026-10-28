// Cached glyphs and native Slug misses must agree at every route-word boundary.
#include <pufferui/vulkan.h>
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
} // namespace

#define REQUIRE(x)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "FAIL line%d %s\n", __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(int argc, char **argv)
{
    bool staged = false;
    for (i32 i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--staged"))
            staged = true;
        else
            return 1;
    vulkan_device_opts options;
    options.validation = true;
    options.error_callback = diagnostic;
    options.whole_program_cache = false; // exercise the painter on every frame
    options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    render_device *devices[] = {create_vulkan_device(options), nullptr};
    REQUIRE(devices[0]);
    const auto native = vulkan_handles(devices[0]);
    options.native = &native;
    options.painter = vulkan_painter_mode::SDF_RASTER;
    options.tile_cache = false;
    devices[1] = create_vulkan_device(options);
    REQUIRE(devices[1]);
    constexpr i32 extents[][2] = {{31, 37}, {33, 65}, {65, 67}, {257, 161}};
    constexpr i32 FRAMES = 48;
    for (const auto &extent : extents)
    {
        const i32 width = extent[0], height = extent[1];
        render_surface *surfaces[2]{};
        context *contexts[2]{};
        window *windows[2]{};
        font_handle fonts[2]{};
        std::vector<u8> pixels[2];
        for (u32 version = 0; version < 2; ++version)
        {
            surfaces[version] =
                create_vulkan_surface(devices[version], {.width = width, .height = height});
            REQUIRE(surfaces[version]);
            contexts[version] = create_context(devices[version], surfaces[version]);
            REQUIRE(contexts[version]);
            windows[version] = add_window(contexts[version], nullptr, surfaces[version],
                                          rect::make(0, 0, width, height));
            fonts[version] =
                load_font(contexts[version], PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
            REQUIRE(windows[version] && fonts[version] != FONT_INVALID);
            pixels[version].resize(static_cast<usize>(width) * height * 4);
        }
        for (i32 frame = 0; frame < FRAMES; ++frame)
        {
            for (u32 version = 0; version < 2; ++version)
            {
                auto *ctx = contexts[version];
                set_current_context(ctx);
                auto theme = default_dark();
                theme.font = fonts[version];
                theme.text_gamma = frame < 24 ? 1.0f : 1.7f;
                set_theme(ctx, theme);
                begin_frame(ctx, *windows[version], frame / 60.0, 1.0 / 60.0);
                devices[version]->clear({17, 43, 79, 113});
                {
                    ui u(ctx);
                    u.draw_rect(windows[version]->area, {11, 29, 53, 193});
                    const vertex triangle[] = {
                        {0.499f, 0.501f, 0, 0, {251, 19, 71, 127}},
                        {static_cast<f32>(width), 1, 0, 0, {37, 229, 91, 201}},
                        {3, static_cast<f32>(height), 0, 0, {43, 89, 233, 255}}};
                    const i32 indices[] = {0, 1, 2};
                    u.draw_triangles(nullptr, triangle, 3, indices, 3);
                    u.draw_rounded_rect(
                        {5.25f, 9.75f, static_cast<f32>(width - 7), static_cast<f32>(height - 13)},
                        {73, 137, 211, static_cast<u8>(frame % 3 == 0 ? 255 : 173)}, 7.5f);
                    // Repeated keys, fractional-position churn and glyphs too
                    // large for the 64x64 residency all need identical output.
                    auto style = u.text_style(frame < 32 ? 19.5f : 112.0f, fonts[version]);
                    const f32 shift = frame >= 16 && frame < 32 ? (frame - 16) * 0.125f : 0.0f;
                    auto disabled = u.disabled(frame >= 12 && frame < 16);
                    u.text(
                        {1.375f + shift, 2.75f, static_cast<f32>(width), static_cast<f32>(height)},
                        frame < 40 ? "WQgj Office ffi" : "Ag curve", {239, 213, 181, 197});
                    u.draw_rect({16.499f, 30.501f, 32.002f, 21.999f}, {197, 83, 47, 153});
                }
                end_frame(ctx);
                REQUIRE(!violation_count(ctx));
                REQUIRE(devices[version]->read_pixels(surfaces[version]->scene_target(), 0, 0,
                                                      width, height, pixels[version].data()));
            }
            REQUIRE(pixels[0] == pixels[1]);
        }
        for (u32 version = 0; version < 2; ++version)
        {
            destroy_context(contexts[version]);
            devices[version]->destroy_surface(surfaces[version]);
        }
    }
    const auto stats = vulkan_statistics(devices[0]);
    REQUIRE(stats.gpu_sdf_batches && stats.gpu_sdf_pixels && stats.glyph_upload_bytes);
    REQUIRE(!stats.draw_memory_allocations && !stats.draw_resource_creations);
    REQUIRE(!stats.validation_errors && !errors);
    destroy_vulkan_device(devices[1]);
    destroy_vulkan_device(devices[0]);
    std::puts("all painter-work tests passed (192 exact cached/missing/oversized glyph and "
              "odd-extent frames)");
    return 0;
}
#undef REQUIRE
