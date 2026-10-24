// Same actual showcase input stream as the frozen R0 benchmark, without readback.
#define PUFFERUI_COMPUTE_SPIRAL_HELPERS_ONLY
#include "test_vulkan_compute_spiral.cpp"
#include <chrono>

int main(int argc, char **argv)
{
    vulkan_device_opts options;
    options.error_callback = diagnostic;
    i32 tiles = 5000;
    for (i32 i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--sdf")
            options.painter = vulkan_painter_mode::SDF_RASTER;
        else if (arg == "--compute")
            options.painter = vulkan_painter_mode::COMPUTE_SDF;
        else if (arg == "--vector-text")
            options.vector_text = true;
        else if (arg == "--cache")
            options.tile_cache = true;
        else if (arg == "--adaptive")
        {
            options.painter = vulkan_painter_mode::COMPUTE_SDF;
            options.tile_cache = options.adaptive_compute = true;
        }
        else if (arg == "--staged")
            options.upload_mode = vulkan_upload_mode::STAGED;
        else if (arg == "--tiles" && i + 1 < argc)
            tiles = std::atoi(argv[++i]);
        else
            return 1;
    }
    if (tiles != 1150 && tiles != 5000) return 1;
    g_start_page = PAGE_LAYOUT;
    g_no_hud = true;
    example_app app;
    app.boot.hidden = true;
    app.boot.asset_dir = PUFFERUI_ASSET_DIR;
    app.boot.use_vulkan = true;
    app.boot.vulkan = options;
    if (!sdl3_app_init(app.boot, "Vulkan painter benchmark", WIDTH, HEIGHT, 0, nullptr)) return 1;
    app.device = app.boot.device;
    app.surface = app.boot.surface;
    app.ctx = app.boot.ctx;
    app.win = app.boot.win;
    app.sdl_window = static_cast<SDL_Window *>(app.boot.sdl_window);
    app.font = app.boot.font;
    app.font_bold = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
    app.font_oblique = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Oblique.ttf");
    app.width = WIDTH;
    app.height = HEIGHT;
    app.selftest = true;
    focus_window(app.ctx, *app.win);
    mouse_move(*app.win, 800, 220);
    g_app = showcase{};
    seed(g_app);
    g_app.lab_mode = 4;
    g_app.lab_size = 64;
    while (g_app.tiles.size() < static_cast<usize>(tiles)) add_tile(g_app);
    app.device->set_vsync(false); // benchmark only; interactive startup policy is unchanged
    const auto native = vulkan_handles(app.device);
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(native.physical_device, &properties);
    std::fprintf(stderr, "calibration_ms=%.6f\n",
                 vulkan_statistics(app.device).gpu_sdf_calibration_ms);
    std::fprintf(stderr, "device=%s driver=%u mode=%u cache=%d vector=%d hidden_wsi=1\n",
                 properties.deviceName, properties.driverVersion, static_cast<u32>(options.painter),
                 options.tile_cache, options.vector_text);
    std::puts("frame,count,total_ms,draws,vertices,indices,upload_bytes,slot_wait_ms,present_ms,"
              "completed_gpu_ms,gpu_samples,compute_pixels,cached_pixels,metadata_bytes,readbacks,"
              "begin_ms,build_ms,end_ms,sdf_prepare_ms,sdf_record_ms,sdf_new_jobs,sdf_batches,sdf_"
              "bypasses");
    std::vector<u8> unused;
    for (i32 f = 0; f < FRAMES; ++f)
    {
        const auto before = vulkan_statistics(app.device);
        frame_times timing;
        if (!render(app, f, unused, false, &timing)) return 1;
        const f64 elapsed = timing.begin + timing.build + timing.end;
        const auto after = vulkan_statistics(app.device);
        const auto U = [](u64 n) { return static_cast<unsigned long long>(n); };
        std::printf(
            "%d,%zu,%.6f,%llu,%llu,%llu,%llu,%.6f,%.6f,%.6f,%llu,%llu,%llu,%llu,%llu,"
            "%.6f,%.6f,%.6f,%.6f,%.6f,%llu,%llu,%llu\n",
            f, g_app.tiles.size(), elapsed, U(after.requested_draws - before.requested_draws),
            U(after.requested_vertices - before.requested_vertices),
            U(after.requested_indices - before.requested_indices),
            U(after.upload_bytes - before.upload_bytes), after.slot_wait_ms - before.slot_wait_ms,
            after.present_ms - before.present_ms, after.completed_gpu_ms - before.completed_gpu_ms,
            U(after.gpu_samples - before.gpu_samples),
            U(after.gpu_sdf_pixels - before.gpu_sdf_pixels),
            U(after.gpu_sdf_cached_pixels - before.gpu_sdf_cached_pixels),
            U(after.gpu_metadata_allocated_bytes), U(after.readbacks - before.readbacks),
            timing.begin, timing.build, timing.end, after.sdf_prepare_ms - before.sdf_prepare_ms,
            after.sdf_record_ms - before.sdf_record_ms,
            U(after.sdf_job_creations - before.sdf_job_creations),
            U(after.gpu_sdf_batches - before.gpu_sdf_batches),
            U(after.gpu_sdf_bypasses - before.gpu_sdf_bypasses));
    }
    if (g_app.tiles.size() != static_cast<usize>(tiles + 3) || !vulkan_wait_idle(app.device))
        return 1;
    const bool clean = !diagnostic_errors && !vulkan_statistics(app.device).validation_errors;
    sdl3_app_shutdown(app.boot);
    return clean ? 0 : 1;
}
