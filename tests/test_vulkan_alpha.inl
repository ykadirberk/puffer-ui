// Common opacity belongs to the completed local RGBA8 result, preserving
// children's order, text/images and legacy blur prefix. Native is independent
// of the candidate's GPU command binning, function lookup and tile cache.
#define PUFFERUI_OUTPUT_CACHE_HELPERS_ONLY
#include "test_vulkan_output_cache.inl"
#undef PUFFERUI_OUTPUT_CACHE_HELPERS_ONLY
namespace
{
u32 nested_guards = 0;
u32 frame_guards = 0;
void capture_alpha_guard(void *, const char *, const char *message, const char *, i32)
{
    if (std::strstr(message, "nested Vulkan alpha barrier"))
        ++nested_guards;
    else if (std::strstr(message, "Vulkan alpha barrier outlived frame"))
        ++frame_guards;
    else
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
    for (i32 i = 0; i < 2; ++i)
    {
        REQUIRE(fixtures[i].initialize(devices[i]));
        set_violation_handler(fixtures[i].ctx, capture_alpha_guard, nullptr);
    }
    for (u32 frame = 0; frame < 32; ++frame)
    {
        for (u32 i = 0; i < 2; ++i)
        {
            auto &f = fixtures[i];
            set_current_context(f.ctx);
            begin_frame(f.ctx, *f.win, 0, 1.0 / 60.0);
            devices[i]->clear({10, 20, 30, 255});
            if (i == 1)
            {
                auto &d = *static_cast<vk_detail::device *>(devices[i]);
                d.sdf_tape.valid = false;
                d.sdf_tape.command_limit = 0;
            }
            {
                ui u(f.ctx);
                const f32 opacity = frame == 12 ? 0 : frame == 13 ? 1 : 0.5f;
                auto alpha = vulkan_alpha(u, {.opacity = opacity});
                REQUIRE(alpha.active());
                if (frame == 31)
                {
                    auto rejected = vulkan_alpha(u, {.opacity = 0.25f});
                    REQUIRE(!rejected.active());
                }
                u.draw_rect({8, 8, 48, 24}, {100, 150, 200, 255});
                u.draw_rect({32, 8, 48, 24}, {100, 150, 200, 255});
                u.draw_rounded_rect({7.25f, 40.75f, 67, 24}, {231, 49, 107, 137}, 6.25f);
                if (frame >= 16) u.image({91, 43, 24, 24}, {f.image, 8, 8});
                if (frame >= 20) u.blur({24, 43, 28, 17}, 3, 4);
                auto style = u.text_style(15.75f, f.font);
                u.text({9.375f, 67.25f, 102, 24}, "group Ag", {241, 217, 183, 173});
            }
            end_frame(f.ctx);
            REQUIRE(violation_count(f.ctx) == (frame == 31 ? 1 : 0));
            REQUIRE(devices[i]->read_pixels(f.surface->scene_target(), 0, 0, WIDTH, HEIGHT,
                                            f.pixels.data()));
            if (frame == 0)
            {
                const auto *single = f.pixels.data() + (16 * WIDTH + 16) * 4;
                const auto *overlap = f.pixels.data() + (16 * WIDTH + 40) * 4;
                REQUIRE(!std::memcmp(single, overlap, 4));
                REQUIRE(single[0] == 55 && single[1] == 85 && single[2] == 115 && single[3] == 255);
            }
        }
        REQUIRE(fixtures[0].pixels == fixtures[1].pixels);
    }
    REQUIRE(nested_guards == 2);
    for (u32 i = 0; i < 2; ++i)
    {
        auto &f = fixtures[i];
        set_current_context(f.ctx);
        const auto before = violation_count(f.ctx);
        begin_frame(f.ctx, *f.win, 0, 1.0 / 60.0);
        {
            ui u(f.ctx);
            auto alpha = vulkan_alpha(u, {.opacity = 0.5f});
            REQUIRE(alpha.active());
            u.draw_rect({8, 8, 24, 24}, {31, 151, 211, 191});
            end_frame(f.ctx); // deliberate lifetime guard; end() closes the group
            REQUIRE(violation_count(f.ctx) == before + 1);
            REQUIRE(!static_cast<vk_detail::device *>(devices[i])->alpha_context);
        }
        REQUIRE(violation_count(f.ctx) == before + 1); // destructor must not double-report
    }
    REQUIRE(frame_guards == 2);
    const auto stats = vulkan_statistics(devices[0]);
    REQUIRE(!stats.draw_memory_allocations && !stats.draw_resource_creations);
    REQUIRE(static_cast<vk_detail::device *>(devices[0])->integrated_alpha_pixels > 0);
    for (i32 i = 0; i < 2; ++i) fixtures[i].release();
    destroy_vulkan_device(devices[1]);
    destroy_vulkan_device(devices[0]);
    REQUIRE(!errors);
    std::puts("all Vulkan alpha barrier tests passed (32 exact frames)");
    return 0;
}
#undef REQUIRE
