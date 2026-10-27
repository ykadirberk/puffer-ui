// Compile with the same default-selection definition as the shipped showcase.
// Explicit diagnostic painters must retain their independent configuration.
#define main showcase_original_main
#include "../examples/pui_showcase.cpp"
#undef main
#include <cstdio>
#include <vector>

#define REQUIRE(x)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "FAIL line%d %s\n", __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main()
{
    g_no_hud = true;
    g_start_page = PAGE_OVERVIEW;
    std::vector<u8> expected;
    for (const char *mode : {"default", "reference", "sdf", "compute", "adaptive", "integrated",
                             "sdf-vector-reference"})
    {
        g_app = showcase{};
        seed(g_app);
        example_app app;
        char program[] = "unified-showcase-test", selftest[] = "--selftest",
             no_script[] = "--no-script", size[] = "--size", dimensions[] = "640x480",
             painter[] = "--vulkan-painter", vector_text[] = "--vector-text";
        std::vector<char *> arguments{program, selftest, no_script, size, dimensions};
        const bool defaults = !std::strcmp(mode, "default");
        const bool native_reference = !std::strcmp(mode, "sdf-vector-reference");
        if (!defaults)
        {
            arguments.push_back(painter);
            arguments.push_back(const_cast<char *>(native_reference ? "sdf" : mode));
        }
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
        if (native_reference) arguments.push_back(vector_text);
#endif
        REQUIRE(example_init(app, "Unified Vulkan showcase", 640, 480,
                             static_cast<i32>(arguments.size()), arguments.data()));
        const auto &options = app.boot.vulkan;
        const bool integrated = defaults || !std::strcmp(mode, "integrated");
        if (integrated)
        {
            REQUIRE(options.painter == vulkan_painter_mode::INTEGRATED);
            REQUIRE(options.tile_cache && options.whole_program_cache && !options.adaptive_compute);
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
            REQUIRE(options.vector_text);
#endif
        }
        else
        {
            const bool adaptive = !std::strcmp(mode, "adaptive");
            const auto selected = !std::strcmp(mode, "reference") ? vulkan_painter_mode::REFERENCE
                                  : !std::strcmp(mode, "sdf") || native_reference
                                      ? vulkan_painter_mode::SDF_RASTER
                                      : vulkan_painter_mode::COMPUTE_SDF;
            REQUIRE(options.painter == selected);
            REQUIRE(options.adaptive_compute == adaptive && options.tile_cache == adaptive);
            REQUIRE(options.whole_program_cache ==
                    (selected != vulkan_painter_mode::REFERENCE && !adaptive));
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
            REQUIRE(options.vector_text == native_reference);
#else
            REQUIRE(!options.vector_text);
#endif
        }
        for (u32 frame = 0; frame < 8; ++frame)
        {
            app.now = static_cast<f64>(frame) / 60.0;
            app.dt = 1.0 / 60.0;
            begin_frame(app.ctx, *app.win, app.now, app.dt);
            {
                ui u(app.ctx);
                showcase_frame(u, app);
            }
            end_frame(app.ctx);
            if (defaults || native_reference)
            {
                std::vector<u8> pixels(640 * 480 * 4);
                REQUIRE(app.device->read_pixels(app.surface->scene_target(), 0, 0, 640, 480,
                                                pixels.data()));
                if (defaults)
                    expected.insert(expected.end(), pixels.begin(), pixels.end());
                else
                    REQUIRE(std::equal(pixels.begin(), pixels.end(),
                                       expected.begin() + frame * pixels.size()));
            }
        }
        REQUIRE(!violation_count(app.ctx) && !vulkan_statistics(app.device).validation_errors);
        REQUIRE(!g_app.vsync && !g_app.vsync_applied); // the unchanged selftest policy
        example_shutdown(app);
    }
    std::puts("all unified Vulkan showcase tests passed (defaults, six overrides, 8 exact frames)");
    return 0;
}
#undef REQUIRE
