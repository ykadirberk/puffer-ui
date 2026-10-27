#include "vulkan_test_options.h"
// GPU producer dirty certificates flow to later sampling passes. Updates and
// unqualified producers retain the literal resource revision dependency.
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
    auto &d = *static_cast<vk_detail::device *>(devices[0]);
    const auto native = vulkan_handles(devices[0]);
    options.native = &native;
    options.painter = vulkan_painter_mode::SDF_RASTER;
    options.tile_cache = false;
    devices[1] = create_vulkan_device(options);
    REQUIRE(devices[1]);
    fixture fixtures[2];
    texture_handle images[2];
    for (i32 i = 0; i < 2; ++i)
    {
        REQUIRE(fixtures[i].initialize(devices[i]));
        images[i] = devices[i]->create_target(WIDTH, HEIGHT);
        REQUIRE(images[i]);
    }
    u64 stable_reuse = 0;
    for (u32 frame = 0; frame < 32; ++frame)
    {
        const auto before = vulkan_statistics(devices[0]);
        for (u32 i = 0; i < 2; ++i)
        {
            auto &f = fixtures[i];
            set_current_context(f.ctx);
            begin_frame(f.ctx, *f.win, 0, 1.0 / 60.0);
            devices[i]->set_target(images[i]);
            devices[i]->clear({17, 43, 79, 255});
            if (i == 0) d.sdf_cache_faults = frame == 18 ? 2u : 0u;
            {
                ui u(f.ctx);
                u.draw_rounded_rect({8.25f, 7.5f, 69, 31},
                                    frame >= 10 && frame < 12 ? color{97, 211, 43, 163}
                                                              : color{231, 49, 107, 137},
                                    8.25f);
                auto style = u.text_style(17, f.font);
                u.text({11.375f, 47.75f, 100, 26}, "producer Ag", {241, 217, 183, 173});
            }
            detail::dl_flush(f.ctx);
            devices[i]->set_target(f.surface->scene_target());
            if (i == 0) d.sdf_cache_faults = 0;
            if (frame == 14)
            {
                const u8 changed[] = {13, 211, 71, 191};
                devices[i]->update_texture(images[i], 20, 20, 1, 1, changed);
                if (i == 0) REQUIRE(!d.live_producer(static_cast<vk_detail::texture *>(images[i])));
            }
            devices[i]->clear({31, 11, 57, 255});
            {
                ui u(f.ctx);
                u.image({0, 0, WIDTH, HEIGHT}, {images[i], WIDTH, HEIGHT});
                if (frame == 22)
                {
                    const u8 changed[] = {223, 19, 151, 231};
                    devices[i]->update_texture(images[i], 23, 22, 1, 1, changed);
                }
            }
            end_frame(f.ctx);
            REQUIRE(!violation_count(f.ctx));
            REQUIRE(devices[i]->read_pixels(f.surface->scene_target(), 0, 0, WIDTH, HEIGHT,
                                            f.pixels.data()));
        }
        REQUIRE(fixtures[0].pixels == fixtures[1].pixels);
        const auto after = vulkan_statistics(devices[0]);
        if (frame >= 5 && frame <= 8)
        {
            REQUIRE(after.gpu_sdf_cached_pixels - before.gpu_sdf_cached_pixels == PIXELS * 2);
            ++stable_reuse;
        }
    }
    REQUIRE(stable_reuse == 4 && !d.stats.draw_memory_allocations &&
            !d.stats.draw_resource_creations);
    for (i32 i = 0; i < 2; ++i)
    {
        devices[i]->destroy_texture(images[i]);
        fixtures[i].release();
    }
    destroy_vulkan_device(devices[1]);
    destroy_vulkan_device(devices[0]);
    REQUIRE(!errors);
    std::puts("all Vulkan producer dependency tests passed (32 exact frames)");
    return 0;
}
#undef REQUIRE
