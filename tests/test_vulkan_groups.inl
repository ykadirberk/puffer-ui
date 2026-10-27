#include "vulkan_test_options.h"
// Distance union styles once; child payload changes invalidate full tile output
// even when the common quad, colors and union bounds remain byte-identical.
#define PUFFERUI_OUTPUT_CACHE_HELPERS_ONLY
#include "test_vulkan_output_cache.inl"
#undef PUFFERUI_OUTPUT_CACHE_HELPERS_ONLY
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
    options.tile_cache = true;
    options.validation = true;
    options.error_callback = diagnostic;
    options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    render_device *devices[] = {create_vulkan_device(options), nullptr};
    REQUIRE(devices[0]);
    const auto native = vulkan_handles(devices[0]);
    options.native = &native;
    options.tile_cache = false;
    devices[1] = create_vulkan_device(options);
    REQUIRE(devices[1]);
    fixture fixtures[2];
    for (i32 i = 0; i < 2; ++i) REQUIRE(fixtures[i].initialize(devices[i]));
    std::vector<u8> previous;
    for (u32 frame = 0; frame < 32; ++frame)
    {
        for (u32 i = 0; i < 2; ++i)
        {
            auto &f = fixtures[i];
            set_current_context(f.ctx);
            begin_frame(f.ctx, *f.win, 0, 1.0 / 60.0);
            devices[i]->clear({17, 43, 79, 255});
            if (i == 1)
            {
                static_cast<vk_detail::device *>(devices[i])->sdf_tape.valid = false;
                static_cast<vk_detail::device *>(devices[i])->sdf_tape.command_limit = 0;
            }
            {
                ui u(f.ctx);
                const vulkan_group_shape shapes[] = {
                    {{8.25f, 12.5f, 25, 20}, corner_radii::all(7)},
                    {{frame < 8 ? 38.125f : 48.125f, 20.75f, 15, 35}, corner_radii::all(5)},
                    {{70.375f, 45.5f, 30, 26}, corner_radii::all(8)}};
                REQUIRE(vulkan_shape_group(u, {.shapes = shapes,
                                               .fill = {173, 91, 227, 151},
                                               .outline = {241, 211, 139, 193},
                                               .glow = {89, 193, 211, 107},
                                               .outline_width = 2.25f,
                                               .glow_radius = 6.5f}));
                const auto before = f.ctx->dl->verts.size();
                REQUIRE(!vulkan_shape_group(u, {}));
                REQUIRE(!vulkan_shape_group(u, {.shapes = shapes, .outline_width = -1}));
                REQUIRE(f.ctx->dl->verts.size() == before);
                if (frame >= 16)
                {
                    const vulkan_group_shape duplicates[] = {shapes[0], shapes[0]};
                    REQUIRE(vulkan_shape_group(u, {.shapes = duplicates,
                                                   .fill = {197, 53, 19, 117},
                                                   .outline = {71, 211, 31, 211},
                                                   .outline_width = 1.5f}));
                }
            }
            end_frame(f.ctx);
            REQUIRE(!violation_count(f.ctx));
            REQUIRE(devices[i]->read_pixels(f.surface->scene_target(), 0, 0, WIDTH, HEIGHT,
                                            f.pixels.data()));
        }
        REQUIRE(fixtures[0].pixels == fixtures[1].pixels);
        if (frame == 8 || frame == 16) REQUIRE(previous != fixtures[0].pixels);
        previous = fixtures[0].pixels;
    }
    const auto stats = vulkan_statistics(devices[0]);
    REQUIRE(stats.gpu_sdf_cached_pixels && !stats.draw_memory_allocations &&
            !stats.draw_resource_creations);
    for (i32 i = 0; i < 2; ++i) fixtures[i].release();
    destroy_vulkan_device(devices[1]);
    destroy_vulkan_device(devices[0]);
    REQUIRE(!errors);
    std::puts("all Vulkan distance group tests passed (32 exact frames)");
    return 0;
}
#undef REQUIRE
