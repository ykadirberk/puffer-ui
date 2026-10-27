#include "vulkan_test_options.h"
// Submitted-token ownership, fixed-capacity keys and final-alpha GPU parity.
#undef near
#undef far
#undef small
#include <cstdio>
using namespace pui;
namespace
{
u32 diagnostic_errors = 0;
void glyph_diagnostic(i32, const char *message, void *)
{
    ++diagnostic_errors;
    std::fprintf(stderr, "%s\n", message);
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
    vulkan_device_opts options = test_detail::reference_options();
    options.painter = vulkan_painter_mode::INTEGRATED;
    options.vector_text = true;
    options.validation = true;
    options.error_callback = glyph_diagnostic;
    options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    render_device *devices[] = {create_vulkan_device(options), nullptr};
    REQUIRE(devices[0]);
    auto &d = *static_cast<vk_detail::device *>(devices[0]);
    REQUIRE(d.glyph_source_capacity >= 8 && d.glyph_source_image->intrinsic);
    const auto native = vulkan_handles(devices[0]);
    options.native = &native;
    options.painter = vulkan_painter_mode::SDF_RASTER;
    devices[1] = create_vulkan_device(options);
    REQUIRE(devices[1]);
    constexpr i32 width = 192, height = 96;
    render_surface *surfaces[2];
    context *contexts[2];
    window *windows[2];
    font_handle fonts[2];
    std::vector<u8> pixels[2];
    for (i32 i = 0; i < 2; ++i)
    {
        surfaces[i] = create_vulkan_surface(devices[i], {.width = width, .height = height});
        REQUIRE(surfaces[i]);
        contexts[i] = create_context(devices[i], surfaces[i]);
        windows[i] = add_window(contexts[i], nullptr, surfaces[i], {0, 0, width, height});
        fonts[i] = load_font(contexts[i], PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
        REQUIRE(fonts[i] != FONT_INVALID);
        pixels[i].resize(width * height * 4);
    }
    for (i32 frame = 0; frame < 48; ++frame)
    {
        for (i32 i = 0; i < 2; ++i)
        {
            set_current_context(contexts[i]);
            begin_frame(contexts[i], *windows[i], 0, 1.0 / 60.0);
            devices[i]->clear({17, 43, 79, 113});
            ui u(contexts[i]);
            auto style = u.text_style(frame < 32 ? 19.5f : 23.75f, fonts[i]);
            auto theme = contexts[i]->active_theme;
            theme.text_gamma = frame < 24 ? 1.0f : 1.7f;
            contexts[i]->active_theme = theme;
            {
                auto disabled = u.disabled(frame >= 16 && frame < 20);
                u.text({7.375f + (frame >= 8 && frame < 12 ? 0.25f : 0), 5.75f, 180, 32},
                       "Ag Q 012345", {241, 217, 183, static_cast<u8>(frame < 40 ? 173 : 91)});
            }
            u.text({11.625f, 43.25f, 170, 34}, frame < 36 ? "curve Qjg" : "replacement Wf",
                   {107, 227, 163, 255});
            end_frame(contexts[i]);
            REQUIRE(!violation_count(contexts[i]));
            REQUIRE(devices[i]->read_pixels(surfaces[i]->scene_target(), 0, 0, width, height,
                                            pixels[i].data()));
        }
        REQUIRE(pixels[0] == pixels[1]);
    }
    REQUIRE(d.glyph_source_hits && d.glyph_source_misses && d.glyph_source_fills);
    vulkan_statistics(devices[0]);
    REQUIRE(d.glyph_source_gpu_hits > 0);
    REQUIRE(!d.stats.draw_memory_allocations && !d.stats.draw_resource_creations);
    REQUIRE(d.wait(d.next_serial - 1));
    // A token that was never submitted must not publish a ready key, including
    // a later presentation-only recording which reuses the same serial value.
    auto saved = d.glyph_sources[0];
    d.glyph_sources[0].ready = false;
    d.glyph_sources[0].fill_token = 0x12345678;
    d.submitted_glyph_sources(0x12345679);
    REQUIRE(!d.glyph_sources[0].ready);
    d.submitted_glyph_sources(0x12345678);
    REQUIRE(d.glyph_sources[0].ready);
    d.glyph_sources[0] = saved;
    for (auto &entry : d.glyph_sources) entry = {};
    u32 admitted = 0;
    for (u32 i = 0; i < 64; ++i)
    {
        vk_detail::sdf_shape key;
        key.bounds = {8, 8, 9, 17};
        key.radii = {8, 21, 19.5f, 0};
        const u32 outline = i * 8;
        std::memcpy(&key.radii.bl, &outline, sizeof(outline));
        key.reserved[0] = 1;
        key.reserved[1] = 1;
        d.cache_glyph_source(key, {1, 2, 3, 197});
        admitted += key.corners[0] != 0;
    }
    REQUIRE(admitted > std::min<u32>(32, d.glyph_source_capacity / 2));
    // Every key component remains significant; oversized glyphs use Slug.
    vk_detail::sdf_shape oversized;
    oversized.bounds = {0, 0, 64, 20};
    oversized.reserved[0] = 1;
    d.cache_glyph_source(oversized, {1, 2, 3, 255});
    REQUIRE(!oversized.corners[0]);
    const auto misses = d.glyph_source_misses;
    const auto capacity = d.glyph_source_capacity;
    for (u32 i = 0; i < capacity; ++i)
    {
        // Both populated and previously empty cells must represent live keys;
        // last_use alone does not occupy an empty generation-zero cell.
        if (!d.glyph_sources[i].generation)
            d.glyph_sources[i].generation = ++d.glyph_source_generation;
        d.glyph_sources[i].last_use = d.next_serial;
    }
    vk_detail::sdf_shape rejected;
    rejected.bounds = {73.125f, 11.875f, 9, 17};
    rejected.reserved[0] = 1;
    d.cache_glyph_source(rejected, {1, 2, 3, 197});
    REQUIRE(!rejected.corners[0] && d.glyph_source_misses == misses);
    for (i32 i = 0; i < 2; ++i)
    {
        destroy_context(contexts[i]);
        devices[i]->destroy_surface(surfaces[i]);
    }
    destroy_vulkan_device(devices[1]);
    destroy_vulkan_device(devices[0]);
    REQUIRE(!diagnostic_errors);
    std::puts("all Vulkan glyph source tests passed (48 exact frames)");
    return 0;
}
#undef REQUIRE
