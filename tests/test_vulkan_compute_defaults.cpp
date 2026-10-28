// Public defaults must execute GPU binning/SDF/Slug from the first changed
// frame, with whole-program and tile reuse together. Native SDF is independent.
#define PUFFERUI_COMPUTE_SPIRAL_HELPERS_ONLY
#include "test_vulkan_compute_spiral.cpp"
#undef PUFFERUI_COMPUTE_SPIRAL_HELPERS_ONLY

constexpr vulkan_device_opts compute_defaults{};
static_assert(compute_defaults.painter == vulkan_painter_mode::INTEGRATED);
static_assert(compute_defaults.vector_text && compute_defaults.tile_cache &&
              compute_defaults.whole_program_cache && !compute_defaults.native_observation &&
              !compute_defaults.adaptive_compute);

#define REQUIRE(x)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "FAIL line%d %s\n", __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int alpha_scope_case(bool staged)
{
    vulkan_device_opts options;
    options.validation = true;
    options.error_callback = diagnostic;
    options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    render_device *devices[] = {create_vulkan_device(options), nullptr};
    REQUIRE(devices[0]);
    const auto native = vulkan_handles(devices[0]);
    options.native = &native;
    options.tile_cache = options.whole_program_cache = false;
    devices[1] = create_vulkan_device(options);
    REQUIRE(devices[1]);
    render_surface *surfaces[2]{};
    context *contexts[2]{};
    window *windows[2]{};
    std::vector<u8> pixels[2];
    for (u32 version = 0; version < 2; ++version)
    {
        surfaces[version] = create_vulkan_surface(devices[version], {.width = 160, .height = 100});
        REQUIRE(surfaces[version]);
        contexts[version] = create_context(devices[version], surfaces[version]);
        REQUIRE(contexts[version]);
        windows[version] =
            add_window(contexts[version], nullptr, surfaces[version], {0, 0, 160, 100});
        REQUIRE(windows[version]);
        auto appearance = default_dark();
        appearance.font = load_font(contexts[version], PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
        REQUIRE(appearance.font != FONT_INVALID);
        set_theme(contexts[version], appearance);
        pixels[version].resize(160 * 100 * 4);
    }
    for (u32 frame = 0; frame < 32; ++frame)
    {
        for (u32 version = 0; version < 2; ++version)
        {
            auto *ctx = contexts[version];
            set_current_context(ctx);
            begin_frame(ctx, *windows[version], 2, 0);
            {
                ui u(ctx);
                u.draw_rect(windows[version]->area, {17, 31, 53, 255});
                const f32 opacity = frame < 12 ? 0.5f : frame < 24 ? 0.75f : 0.25f;
                auto alpha = vulkan_alpha(u, {.opacity = opacity});
                REQUIRE(alpha.active());
                u.draw_rounded_rect({5.25f, 7.75f, 143, 80}, {47, 113, 179, 193}, 12.25f);
                u.draw_rect({19, 17, 74, 37}, {173, 67, 19, 157});
                if (frame >= 16) u.blur({23, 21, 38, 27}, 3, 4);
                u.text({11.25f, 56.5f, 138, 27}, "Office ffi Ag", {231, 213, 189, 181});
            }
            end_frame(ctx);
            REQUIRE(!violation_count(ctx));
            REQUIRE(devices[version]->read_pixels(surfaces[version]->scene_target(), 0, 0, 160, 100,
                                                  pixels[version].data()));
            REQUIRE(!vulkan_statistics(devices[version]).validation_errors);
        }
        REQUIRE(pixels[0] == pixels[1]);
    }
    const auto stats = vulkan_statistics(devices[0]);
    REQUIRE(stats.whole_program_hits && stats.gpu_sdf_pixels && stats.glyph_upload_bytes);
    REQUIRE(!stats.draw_memory_allocations && !stats.draw_resource_creations && !diagnostic_errors);
    for (u32 version = 0; version < 2; ++version)
    {
        destroy_context(contexts[version]);
        devices[version]->destroy_surface(surfaces[version]);
    }
    destroy_vulkan_device(devices[1]);
    destroy_vulkan_device(devices[0]);
    return 0;
}

int main(int argc, char **argv)
{
    bool staged = false, idle = false;
    for (i32 i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--staged"))
            staged = true;
        else if (!std::strcmp(argv[i], "--idle"))
            idle = true;
        else
            return 1;
    REQUIRE(alpha_scope_case(staged) == 0);
    g_start_page = PAGE_LAYOUT;
    g_no_hud = true;
    vulkan_device_opts options;
    options.validation = true;
    options.error_callback = diagnostic;
    options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    render_device *devices[] = {create_vulkan_device(options), nullptr};
    REQUIRE(devices[0]);
    const auto native = vulkan_handles(devices[0]);
    options.native = &native;
    options.painter = vulkan_painter_mode::SDF_RASTER;
    options.tile_cache = options.whole_program_cache = false;
    devices[1] = create_vulkan_device(options);
    REQUIRE(devices[1]);
    example_app apps[2];
    showcase states[2];
    std::vector<u8> pixels[2];
    for (i32 version = 0; version < 2; ++version)
    {
        pixels[version].resize(PIXEL_BYTES);
        REQUIRE(initialize(apps[version], devices[version], 5000));
        if (idle)
        {
            g_app.reduced_motion = true;
            g_app.ambient = false;
            g_app.page_since = 0;
        }
        std::swap(g_app, states[version]);
    }
    u64 active_frames = 0;
    for (i32 frame = 0; frame < FRAMES; ++frame)
    {
        const auto before = vulkan_statistics(devices[0]);
        for (i32 version = 0; version < 2; ++version)
        {
            auto &app = apps[version];
            set_current_context(app.ctx);
            std::swap(g_app, states[version]);
            if (idle)
            {
                app.frame_index = frame;
                app.now = 2;
                app.dt = 0;
                if (frame == 90 || frame == 120 || frame == 150) g_app.tiles[0].hue += 0.0625f;
                begin_frame(app.ctx, *app.win, app.now, app.dt);
                {
                    ui u(app.ctx);
                    showcase_frame(u, app);
                }
                end_frame(app.ctx);
                REQUIRE(!violation_count(app.ctx));
                REQUIRE(app.device->read_pixels(app.surface->scene_target(), 0, 0, WIDTH, HEIGHT,
                                                pixels[version].data()));
            }
            else
                REQUIRE(render(app, frame, pixels[version]));
            std::swap(g_app, states[version]);
        }
        REQUIRE(pixels[0] == pixels[1]);
        const auto after = vulkan_statistics(devices[0]);
        const bool active = after.gpu_sdf_batches > before.gpu_sdf_batches;
        active_frames += active;
        // The default must never wait for a failed LOAD certificate to admit
        // compute. A mutation must also leave whole-image reuse immediately.
        if (!frame || frame == 90 || frame == 120 || frame == 150) REQUIRE(active);
        REQUIRE(!after.draw_memory_allocations && !after.draw_resource_creations);
        REQUIRE(!after.validation_errors && !diagnostic_errors);
    }
    const auto stats = vulkan_statistics(devices[0]);
    REQUIRE(stats.gpu_sdf_pixels && stats.glyph_upload_bytes && stats.glyph_resident_batches);
    REQUIRE(idle ? stats.whole_program_hits > 0 : active_frames == FRAMES);
    REQUIRE(states[0].tiles.size() == (idle ? 5000u : 5003u));
    std::printf("compute default: %d exact 5000-Spiral frames, active=%llu cache_hits=%llu "
                "GPU_pixels=%llu Slug_bytes=%llu staged=%d idle=%d\n",
                FRAMES, static_cast<unsigned long long>(active_frames),
                static_cast<unsigned long long>(stats.whole_program_hits),
                static_cast<unsigned long long>(stats.gpu_sdf_pixels),
                static_cast<unsigned long long>(stats.glyph_upload_bytes), staged, idle);
    for (i32 version = 0; version < 2; ++version)
    {
        destroy_context(apps[version].ctx);
        devices[version]->destroy_surface(apps[version].surface);
    }
    destroy_vulkan_device(devices[1]);
    destroy_vulkan_device(devices[0]);
    return 0;
}
