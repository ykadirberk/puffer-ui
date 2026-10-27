#include "vulkan_test_options.h"
// General ordered compute starts from actual RGBA8 LOAD, without requiring an
// opaque suffix. Different destination bytes every frame exercise the native
// function cache and fully computed packets must skip native vertex execution.
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
    options.validation = true;
    options.error_callback = diagnostic;
    options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    render_device *devices[] = {create_vulkan_device(options), nullptr};
    REQUIRE(devices[0]);
    const auto native = vulkan_handles(devices[0]);
    options.native = &native;
    options.painter = vulkan_painter_mode::SDF_RASTER;
    devices[1] = create_vulkan_device(options);
    REQUIRE(devices[1]);
    fixture fixtures[2];
    for (i32 i = 0; i < 2; ++i) REQUIRE(fixtures[i].initialize(devices[i]));
    auto &d = *static_cast<vk_detail::device *>(devices[0]);
    for (u32 frame = 0; frame < 32; ++frame)
    {
        for (u32 i = 0; i < 2; ++i)
        {
            auto &f = fixtures[i];
            set_current_context(f.ctx);
            begin_frame(f.ctx, *f.win, 0, 1.0 / 60.0);
            devices[i]->clear({static_cast<u8>(frame * 7), static_cast<u8>(frame * 13),
                               static_cast<u8>(frame * 17), static_cast<u8>(31 + frame * 5)});
            {
                ui u(f.ctx);
                u.draw_rect({-2, -2, WIDTH + 4, HEIGHT + 4}, {231, 49, 107, 137});
                if (frame >= 12)
                {
                    u.draw_rounded_rect({8.25f, 9.5f, 71, 37}, {97, 211, 43, 163}, 8.25f);
                    u.draw_rounded_rect({28.5f, 21.75f, 67, 29}, {73, 59, 227, 113}, 6.5f);
                }
            }
            end_frame(f.ctx);
            REQUIRE(!violation_count(f.ctx));
            REQUIRE(devices[i]->read_pixels(f.surface->scene_target(), 0, 0, WIDTH, HEIGHT,
                                            f.pixels.data()));
        }
        REQUIRE(fixtures[0].pixels == fixtures[1].pixels);
    }
    const auto stats = vulkan_statistics(devices[0]);
    REQUIRE(d.integrated_counts[3] > 0 && d.native_replay_counts[1] > 0);
    REQUIRE(!stats.draw_memory_allocations && !stats.draw_resource_creations);
    for (i32 i = 0; i < 2; ++i) fixtures[i].release();
    destroy_vulkan_device(devices[1]);
    destroy_vulkan_device(devices[0]);
    REQUIRE(!errors);
    std::puts("all Vulkan RGBA8 LOAD tests passed (32 exact frames)");
    return 0;
}
#undef REQUIRE
