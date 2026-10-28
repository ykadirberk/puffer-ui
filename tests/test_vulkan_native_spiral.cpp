// Freeze/compare every actual Spiral frame; never updates an existing capture.
#define main native_spiral_original_main
#include "../examples/pui_showcase.cpp"
#undef main
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>

namespace
{
i32 failures = 0;
void diagnostic(i32 code, const char *text, void *)
{
    std::fprintf(stderr, "Vulkan %d: %s\n", code, text);
    ++failures;
}
void seed_scene(example_app &app, i32 count)
{
    app.selftest = true;
    focus_window(app.ctx, *app.win);
    mouse_move(*app.win, 800, 220);
    g_app = showcase{};
    seed(g_app);
    g_app.lab_mode = 4;
    g_app.lab_size = 64;
    while (g_app.tiles.size() < static_cast<usize>(count)) add_tile(g_app);
}
bool initialize(example_app &app, render_device *device, i32 count)
{
    app.device = device;
    app.surface = create_vulkan_surface(device, {.width = 1360, .height = 860});
    if (!app.surface) return false;
    app.ctx = create_context(device, app.surface);
    app.win = add_window(app.ctx, nullptr, app.surface, rect::make(0, 0, 1360, 860));
    app.font = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    app.font_bold = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
    app.font_oblique = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Oblique.ttf");
    app.width = 1360;
    app.height = 860;
    seed_scene(app, count);
    return app.font != FONT_INVALID && app.font_bold != FONT_INVALID;
}
void frame(example_app &app, i32 f)
{
    app.frame_index = f;
    app.now = f / 60.0;
    app.dt = 1.0 / 60.0;
    if (f >= 90 && (f - 90) % 30 == 0)
    {
        app.win->focus = "lab_add"_id;
        key_event(*app.win, key::ENTER, true);
    }
    if (f >= 91 && (f - 91) % 30 == 0) key_event(*app.win, key::ENTER, false);
    begin_frame(app.ctx, *app.win, app.now, app.dt);
    {
        ui u(app.ctx);
        showcase_frame(u, app);
    }
    end_frame(app.ctx);
}
} // namespace

int main(int argc, char **argv)
{
    const char *capture = nullptr, *reference = nullptr;
    bool bitmap = false, staged = false, cache = false, bench = false;
    bool windowed = false;
    i32 hz = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--capture") && i + 1 < argc)
            capture = argv[++i];
        else if (!std::strcmp(argv[i], "--reference") && i + 1 < argc)
            reference = argv[++i];
        else if (!std::strcmp(argv[i], "--bitmap"))
            bitmap = true;
        else if (!std::strcmp(argv[i], "--staged"))
            staged = true;
        else if (!std::strcmp(argv[i], "--cache"))
            cache = true;
        else if (!std::strcmp(argv[i], "--bench"))
            bench = true;
        else if (!std::strcmp(argv[i], "--windowed"))
            windowed = true;
        else if (!std::strcmp(argv[i], "--hz") && i + 1 < argc)
            hz = std::atoi(argv[++i]);
        else
            return 2;
    }
    if ((capture && reference) || (bench && (capture || reference))) return 2;
    if (hz < 0 || hz > 1000 || (windowed && !bench)) return 2;
    if (capture && !std::filesystem::create_directory(capture)) return 2;
    vulkan_device_opts opts;
    opts.validation = !bench;
    opts.error_callback = diagnostic;
    opts.tile_cache = cache;
    opts.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    opts.vector_text = !bitmap;
#else
    (void)bitmap;
#endif
    render_device *device = nullptr;
    example_app app;
    g_start_page = PAGE_LAYOUT;
    g_no_hud = true;
    if (windowed)
    {
        app.boot.vulkan = opts;
        char program[] = "native-Spiral-bench", selftest[] = "--selftest",
             no_script[] = "--no-script";
        char *arguments[] = {program, selftest, no_script};
        if (!example_init(app, "Native Spiral benchmark", 1360, 860, 3, arguments)) return 1;
        device = app.device;
        device->set_vsync(false);
        seed_scene(app, 5000);
    }
    else
    {
        device = create_vulkan_device(opts);
        if (!device || !initialize(app, device, 5000)) return 1;
    }
    std::vector<u8> actual(1360 * 860 * 4), expected(actual.size());
    const bool paired = !bench && !capture && !reference;
    render_device *other = nullptr;
    example_app oracle;
    showcase states[2];
    if (paired)
    {
        std::swap(g_app, states[0]);
        const auto native = vulkan_handles(device);
        auto second = opts;
        second.native = &native;
        second.tile_cache = !cache;
        second.upload_mode = staged ? vulkan_upload_mode::DIRECT : vulkan_upload_mode::STAGED;
        other = create_vulkan_device(second);
        if (!other || !initialize(oracle, other, 5000)) return 1;
        std::swap(g_app, states[1]);
        std::swap(g_app, states[0]);
    }
    if (bench)
    {
        std::printf("frame,total_ms,upload_bytes,resident_bytes,gpu_samples,gpu_ms");
#if defined(PUFFERUI_NATIVE_TELEMETRY)
        std::printf(",draw_buffer_allocations,glyph_upload_bytes,glyph_instances,glyph_draws,"
                    "pipeline_binds,viewport_sets,scissor_sets,descriptor_binds");
#endif
        std::puts("");
    }
    if (bench && !vulkan_wait_idle(device)) return 1;
    const auto initial_stats = vulkan_statistics(device);
    const auto epoch = std::chrono::steady_clock::now();
    for (i32 f = 0; f < 180; ++f)
    {
        const auto before = vulkan_statistics(device);
        const auto start = std::chrono::steady_clock::now();
        frame(app, f);
        const auto after = vulkan_statistics(device);
#if defined(PUFFERUI_NATIVE_TELEMETRY)
        if (after.draw_buffer_allocations != before.draw_buffer_allocations)
        {
            std::fprintf(stderr, "native draw allocation frame %d\n", f);
            return 1;
        }
#endif
        if (vulkan_last_result(device) != VK_SUCCESS || violation_count(app.ctx)) return 1;
        if (bench)
        {
            std::printf(
                "%d,%.6f,%llu,%llu,%llu,%.6f", f,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count(),
                static_cast<unsigned long long>(after.upload_bytes - before.upload_bytes),
                static_cast<unsigned long long>(after.upload_allocated_bytes),
                static_cast<unsigned long long>(after.gpu_samples - before.gpu_samples),
                after.completed_gpu_ms - before.completed_gpu_ms);
#if defined(PUFFERUI_NATIVE_TELEMETRY)
            const auto U = [](u64 x) { return static_cast<unsigned long long>(x); };
            std::printf(",%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu",
                        U(after.draw_buffer_allocations - before.draw_buffer_allocations),
                        U(after.glyph_upload_bytes - before.glyph_upload_bytes),
                        U(after.glyph_instances - before.glyph_instances),
                        U(after.glyph_draws - before.glyph_draws),
                        U(after.pipeline_binds - before.pipeline_binds),
                        U(after.viewport_sets - before.viewport_sets),
                        U(after.scissor_sets - before.scissor_sets),
                        U(after.descriptor_binds - before.descriptor_binds));
#endif
            std::puts("");
        }
        else
        {
            if (!device->read_pixels(app.surface->scene_target(), 0, 0, 1360, 860, actual.data()))
                return 1;
            char name[32];
            std::snprintf(name, sizeof(name), "frame-%03d.rgba", f);
            if (capture)
            {
                const auto path = std::filesystem::path(capture) / name;
                std::ofstream file(path, std::ios::binary);
                file.write(reinterpret_cast<const char *>(actual.data()), actual.size());
                if (!file) return 1;
            }
            if (reference)
            {
                std::ifstream file(std::filesystem::path(reference) / name, std::ios::binary);
                file.read(reinterpret_cast<char *>(expected.data()), expected.size());
                if (!file || file.peek() != std::char_traits<char>::eof() || actual != expected)
                {
                    std::fprintf(stderr, "native Spiral RGBA mismatch at frame %d\n", f);
                    return 1;
                }
            }
            if (paired)
            {
                std::swap(g_app, states[0]);
                std::swap(g_app, states[1]);
                frame(oracle, f);
                if (!other->read_pixels(oracle.surface->scene_target(), 0, 0, 1360, 860,
                                        expected.data()) ||
                    actual != expected || violation_count(oracle.ctx) ||
                    vulkan_last_result(other) != VK_SUCCESS)
                {
                    std::fprintf(stderr, "native text direct/staged/cache mismatch frame %d\n", f);
                    return 1;
                }
                std::swap(g_app, states[1]);
                std::swap(g_app, states[0]);
            }
        }
        if (hz)
            std::this_thread::sleep_until(
                epoch + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                            std::chrono::duration<double>(static_cast<double>(f + 1) / hz)));
    }
    if (!vulkan_wait_idle(device) || failures || g_app.tiles.size() != 5003) return 1;
    if (bench)
    {
        const auto stats = vulkan_statistics(device);
        std::fprintf(
            stderr, "gpu_total_ms=%.9f gpu_samples=%llu frames=180\n",
            stats.completed_gpu_ms - initial_stats.completed_gpu_ms,
            static_cast<unsigned long long>(stats.gpu_samples - initial_stats.gpu_samples));
    }
    if (paired)
    {
        if (vulkan_statistics(other).validation_errors || !vulkan_wait_idle(other)) return 1;
        destroy_context(oracle.ctx);
        other->destroy_surface(oracle.surface);
        destroy_vulkan_device(other);
    }
    if (vulkan_statistics(device).validation_errors) return 1;
    if (windowed)
        example_shutdown(app);
    else
    {
        destroy_context(app.ctx);
        device->destroy_surface(app.surface);
        destroy_vulkan_device(device);
    }
    std::fprintf(stderr, "native Spiral: 180 frames, 5000 -> 5003 items, %s\n",
                 bench     ? "readback-free timings completed"
                 : capture ? "RGBA captured"
                           : "exact RGBA passed");
    return 0;
}
