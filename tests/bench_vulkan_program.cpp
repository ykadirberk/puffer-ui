// Real showcase workload: normal animation, settled output and local changes.
// Golden readback is optional and explicitly outside the measured frame.
#define PUFFERUI_COMPUTE_SPIRAL_HELPERS_ONLY
#ifndef PUFFERUI_PROGRAM_HELPERS
#define PUFFERUI_PROGRAM_HELPERS "test_vulkan_compute_spiral.cpp"
#endif
#include PUFFERUI_PROGRAM_HELPERS
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
struct process_cpu_stamp
{
    u64 ticks = 0, cycles = 0;
};
process_cpu_stamp cpu_stamp(bool thread = false)
{
    process_cpu_stamp result;
#if defined(_WIN32)
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (thread)
    {
        if (!GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user) ||
            !QueryThreadCycleTime(GetCurrentThread(), &result.cycles))
            std::abort();
    }
    else if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) ||
             !QueryProcessCycleTime(GetCurrentProcess(), &result.cycles))
        std::abort();
    result.ticks = (static_cast<u64>(kernel.dwHighDateTime) << 32) + kernel.dwLowDateTime +
                   (static_cast<u64>(user.dwHighDateTime) << 32) + user.dwLowDateTime;
#endif
    return result;
}
} // namespace

int main(int argc, char **argv)
{
    vulkan_device_opts options;
    options.error_callback = diagnostic;
    i32 tiles = 5000, frames = FRAMES, hz = 0;
    bool idle = false, local = false, visible = false, vsync = false, program_cache = true;
    bool hud = false, additions_enabled = true, tour = false, mouse_tour = false;
    bool scroll_tour = false;
    bool tooltip_tour = false;
    bool overlay_tour = false;
    i32 grow_until = 0, add_every = 2, lab_mode = -1;
    f32 lab_size = 0, lab_gap = -1;
    i32 lab_selected = 0;
    bool compact_hud = false, live_clock = false;
    fs::path capture, reference;
    for (i32 i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--sdf")
            options.painter = vulkan_painter_mode::SDF_RASTER;
        else if (arg == "--compute")
            options.painter = vulkan_painter_mode::COMPUTE_SDF;
        else if (arg == "--integrated")
        {
            options.painter = vulkan_painter_mode::INTEGRATED;
            options.tile_cache = true;
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
            options.vector_text = true;
#endif
        }
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
        else if (arg == "--idle")
            idle = true;
        else if (arg == "--local-change")
            idle = local = true;
        else if (arg == "--visible")
            visible = true;
        else if (arg == "--vsync")
            vsync = true;
        else if (arg == "--hud")
            hud = true;
        else if (arg == "--compact-hud")
            hud = compact_hud = true;
        else if (arg == "--live-clock")
            live_clock = true;
        else if (arg == "--no-add")
            additions_enabled = false;
        else if (arg == "--tour")
            tour = true;
        else if (arg == "--mouse-tour")
            mouse_tour = true;
        else if (arg == "--scroll-tour")
            scroll_tour = mouse_tour = true;
        else if (arg == "--tooltip-tour")
            tooltip_tour = true;
        else if (arg == "--overlay-tour")
            overlay_tour = true;
        else if (arg == "--no-program-cache")
            program_cache = false;
        else if (arg == "--tiles" && i + 1 < argc)
            tiles = std::atoi(argv[++i]);
        else if (arg == "--frames" && i + 1 < argc)
            frames = std::atoi(argv[++i]);
        else if (arg == "--grow-until" && i + 1 < argc)
            grow_until = std::atoi(argv[++i]);
        else if (arg == "--add-every" && i + 1 < argc)
            add_every = std::atoi(argv[++i]);
        else if (arg == "--lab-mode" && i + 1 < argc)
            lab_mode = std::atoi(argv[++i]);
        else if (arg == "--lab-size" && i + 1 < argc)
            lab_size = static_cast<f32>(std::atof(argv[++i]));
        else if (arg == "--lab-gap" && i + 1 < argc)
            lab_gap = static_cast<f32>(std::atof(argv[++i]));
        else if (arg == "--lab-selected" && i + 1 < argc)
            lab_selected = std::atoi(argv[++i]);
        else if (arg == "--hz" && i + 1 < argc)
            hz = std::atoi(argv[++i]);
        else if (arg == "--capture" && i + 1 < argc)
            capture = argv[++i];
        else if (arg == "--reference" && i + 1 < argc)
            reference = argv[++i];
        else
            return 1;
    }
    if (tiles < 14 || tiles > 5000 || frames < 1 ||
        frames > (live_clock   ? 72000
                  : grow_until ? 12000
                               : 3600) ||
        hz < 0 || hz > 1000 || grow_until < 0 || grow_until > 5000 ||
        (grow_until && (grow_until < tiles || !additions_enabled || idle || tour || mouse_tour ||
                        tooltip_tour || overlay_tour)) ||
        add_every < 2 || lab_mode < -1 || lab_mode > 4 || lab_size < 0 || lab_size > 180 ||
        lab_gap < -1 || lab_gap > 32 || lab_selected < 0 || lab_selected > tiles ||
        (live_clock && (idle || !capture.empty() || !reference.empty())) ||
        (!capture.empty() && !reference.empty()))
        return 1;
    if (!capture.empty() && (fs::exists(capture) || !fs::create_directories(capture))) return 1;
    if (!reference.empty() && !fs::is_directory(reference)) return 1;
#if !defined(PUFFERUI_PROGRAM_BASELINE)
    options.whole_program_cache = program_cache && !options.adaptive_compute &&
                                  (options.painter == vulkan_painter_mode::SDF_RASTER ||
                                   options.painter == vulkan_painter_mode::COMPUTE_SDF ||
                                   options.painter == vulkan_painter_mode::INTEGRATED);
#else
    (void)program_cache;
#endif
    g_start_page = tour || mouse_tour || tooltip_tour || overlay_tour ? PAGE_OVERVIEW : PAGE_LAYOUT;
    g_no_hud = !hud;
    example_app app;
    app.boot.hidden = !visible;
    app.boot.asset_dir = PUFFERUI_ASSET_DIR;
    app.boot.use_vulkan = true;
    app.boot.vulkan = options;
    if (!sdl3_app_init(app.boot, "PufferUI native program measurement", WIDTH, HEIGHT, 0, nullptr))
        return 1;
    app.device = app.boot.device;
    app.surface = app.boot.surface;
    app.ctx = app.boot.ctx;
    app.win = app.boot.win;
    app.sdl_window = static_cast<SDL_Window *>(app.boot.sdl_window);
    if (visible) SDL_RaiseWindow(app.sdl_window);
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
    if (idle || !additions_enabled) first_add = 100000;
    if (!mouse_tour)
    {
        g_app.lab_mode = 4;
        g_app.lab_size = 64;
    }
    if (lab_mode >= 0) g_app.lab_mode = lab_mode;
    if (lab_size > 0) g_app.lab_size = lab_size;
    if (lab_gap >= 0) g_app.lab_gap = lab_gap;
    g_app.lab_selected = static_cast<u32>(lab_selected);
    g_app.hud_compact = compact_hud;
    if (grow_until)
    {
        first_add = 180; // preserve a settled initial window before continuous input
        add_period = add_every;
    }
    while (g_app.tiles.size() < static_cast<usize>(tiles)) add_tile(g_app);
    // The benchmark owns its initial policy. The normal application's first
    // frame would otherwise replace --vsync with the selftest default.
    g_app.font = app.font;
    g_app.bold = app.font_bold;
    g_app.analytic_shapes = options.painter == vulkan_painter_mode::SDF_RASTER ||
                            options.painter == vulkan_painter_mode::COMPUTE_SDF ||
                            options.painter == vulkan_painter_mode::INTEGRATED;
    set_page(g_app, g_start_page, 0);
    g_app.vsync = g_app.vsync_applied = vsync;
    app.device->set_vsync(vsync);
    const auto native = vulkan_handles(app.device);
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(native.physical_device, &properties);
    std::fprintf(stderr, "device=%s driver=%u mode=%u cache=%d vector=%d hidden_wsi=%d\n",
                 properties.deviceName, properties.driverVersion, static_cast<u32>(options.painter),
                 options.tile_cache, options.vector_text, !visible);
    std::fprintf(stderr, "idle=%d local=%d vsync=%d calibration_ms=%.6f\n", idle, local, vsync,
                 vulkan_statistics(app.device).gpu_sdf_calibration_ms);
    if (grow_until)
        std::fprintf(stderr, "continuous_add from=%d until=%d period=%d mode=%d size=%.3f\n", tiles,
                     grow_until, add_every, g_app.lab_mode, g_app.lab_size);
    std::puts(
        "frame,count,total_ms,draws,vertices,indices,upload_bytes,slot_wait_ms,present_ms,"
        "completed_gpu_ms,gpu_samples,compute_pixels,cached_pixels,metadata_bytes,readbacks,"
        "begin_ms,build_ms,end_ms,sdf_prepare_ms,sdf_record_ms,sdf_new_jobs,sdf_batches,"
        "sdf_bypasses,actual_draws,program_hits,focused,visible,image_bytes,live_textures,"
        "descriptor_sets,upload_resident_bytes,draw_memory_allocations,draw_resource_creations,"
        "glyph_upload_bytes,program_probes,program_probe_pixels");
    std::vector<u8> pixels, expected;
    const bool proof = !capture.empty() || !reference.empty();
    if (proof)
    {
        pixels.resize(PIXEL_BYTES);
        expected.resize(PIXEL_BYTES);
    }
    const auto U = [](u64 n) { return static_cast<unsigned long long>(n); };
    const auto cpu_before = cpu_stamp();
    const auto main_before = cpu_stamp(true);
    u64 previous_start_ns = 0;
    f64 live_now = 0, live_dt = 1.0 / 60.0;
    for (i32 f = 0; f < frames; ++f)
    {
        const u64 frame_start_ns = hz || live_clock ? SDL_GetTicksNS() : 0;
        if (live_clock && f)
        {
            const f64 elapsed = static_cast<f64>(frame_start_ns - previous_start_ns) / 1.0e9;
            live_dt = elapsed > 0 ? std::min(elapsed, 0.25) : 1.0 / 60.0;
            live_now += live_dt;
        }
        previous_start_ns = frame_start_ns;
        SDL_PumpEvents();
        if (grow_until && first_add != 100000 &&
            g_app.tiles.size() >= static_cast<usize>(grow_until))
        {
            key_event(*app.win, key::ENTER, false);
            first_add = 100000;
        }
        if (tour && f >= 120 && f <= 600 && f % 120 == 0)
            set_page(g_app, (f / 120) % PAGE_COUNT, live_clock ? live_now : idle ? 2.0 : f / 60.0);
        if (mouse_tour && f >= 120 && f <= 601 && (f % 120 == 0 || f % 120 == 1))
        {
            // Drive the actual sidebar interaction/capture path, including its
            // click pulse, focus and hover state. This is synthetic core input
            // in our own benchmark, not desktop input or a direct model edit.
            const i32 page = (f / 120) % PAGE_COUNT;
            mouse_move(*app.win, 100, 142.0f + page * 48.0f);
            mouse_button(*app.win, f % 120 == 0);
        }
        if (scroll_tour && f < 600 && (f % 120 == 60 || f % 120 == 90))
        {
            // Traverse the offscreen portions as the scroll spring advances,
            // then restore the page before moving on. Keep the item count fixed.
            mouse_move(*app.win, 800, 500);
            mouse_wheel(*app.win, 0, f % 120 == 60 ? -100.0f : 100.0f);
        }
        if (tooltip_tour && (f == 60 || f == 240))
            mouse_move(*app.win, f == 60 ? 1296.0f : 800.0f, f == 60 ? 828.0f : 220.0f);
        const auto before = vulkan_statistics(app.device);
        frame_times timing;
        if (!idle && !overlay_tour && !live_clock)
        {
            if (!render(app, f, pixels, false, &timing)) return 1;
        }
        else
        {
            app.frame_index = f;
            app.now = live_clock ? live_now : idle ? 2.0 : f / 60.0;
            app.dt = live_clock ? live_dt : idle ? 0 : 1.0 / 60.0;
            if (idle)
            {
                g_app.reduced_motion = true;
                g_app.ambient = false;
                g_app.page_since = 0;
            }
            if (local && (f == 90 || f == 120 || f == 150)) g_app.tiles[0].hue += 0.0625f;
            if (f >= first_add && (f - first_add) % add_period == 0)
            {
                app.win->focus = "lab_add"_id;
                key_event(*app.win, key::ENTER, true);
            }
            if (f >= first_add + 1 && (f - first_add - 1) % add_period == 0)
                key_event(*app.win, key::ENTER, false);
            using clock = std::chrono::steady_clock;
            auto start = clock::now();
            const auto elapsed = [&]()
            { return std::chrono::duration<f64, std::milli>(clock::now() - start).count(); };
            begin_frame(app.ctx, *app.win, app.now, app.dt);
            timing.begin = elapsed();
            start = clock::now();
            {
                ui u(app.ctx);
                showcase_frame(u, app);
                if (overlay_tour && f >= 60 && f < 240)
                {
                    auto popup = u.popup("native_overlay_probe"_id, {1320, 840, 180, 70});
                    if (f == 60) std::fprintf(stderr, "native_overlay_probe=%d\n", popup.overlay_);
                    u.draw_rounded_rect(popup.content(), {37, 71, 113, 247}, 7);
                }
            }
            timing.build = elapsed();
            start = clock::now();
            end_frame(app.ctx);
            timing.end = elapsed();
        }
        const auto after = vulkan_statistics(app.device);
        if (f == 0)
        {
            const auto mode = vulkan_present_mode(app.surface);
            std::fprintf(stderr, "actual_present_mode=%u model_vsync=%d applied_vsync=%d\n",
                         static_cast<u32>(mode), g_app.vsync, g_app.vsync_applied);
            if (g_app.vsync != vsync || g_app.vsync_applied != vsync ||
                (vsync && mode != VK_PRESENT_MODE_FIFO_KHR))
                return 1;
        }
        if (mouse_tour && f >= 122 && f <= 602 && f % 120 == 2 &&
            g_app.page != (f / 120) % PAGE_COUNT)
        {
            std::fprintf(stderr, "sidebar mouse tour did not activate expected page at frame=%d\n",
                         f);
            return 1;
        }
        u64 hits = 0, probes = 0, probe_pixels = 0;
#if !defined(PUFFERUI_PROGRAM_BASELINE)
        hits = after.whole_program_hits - before.whole_program_hits;
        probes = after.whole_program_probes - before.whole_program_probes;
        probe_pixels = after.whole_program_probe_pixels - before.whole_program_probe_pixels;
#endif
        const auto flags = SDL_GetWindowFlags(app.sdl_window);
        std::printf(
            "%d,%zu,%.6f,%llu,%llu,%llu,%llu,%.6f,%.6f,%.6f,%llu,%llu,%llu,%llu,%llu,"
            "%.6f,%.6f,%.6f,%.6f,%.6f,%llu,%llu,%llu,%llu,%llu,%d,%d,"
            "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
            f, g_app.tiles.size(), timing.begin + timing.build + timing.end,
            U(after.requested_draws - before.requested_draws),
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
            U(after.gpu_sdf_bypasses - before.gpu_sdf_bypasses), U(after.draws - before.draws),
            U(hits), !!(flags & SDL_WINDOW_INPUT_FOCUS),
            !(flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)), U(after.image_bytes),
            U(after.live_textures), U(after.descriptor_sets), U(after.upload_allocated_bytes),
            U(after.draw_memory_allocations - before.draw_memory_allocations),
            U(after.draw_resource_creations - before.draw_resource_creations),
            U(after.glyph_upload_bytes - before.glyph_upload_bytes), U(probes), U(probe_pixels));
        if (proof)
        {
            if (!app.device->read_pixels(app.surface->scene_target(), 0, 0, WIDTH, HEIGHT,
                                         pixels.data()))
                return 1;
            if (!capture.empty() && !write_pixels(frame_path(capture, f), pixels)) return 1;
            if (!reference.empty() &&
                (!read_pixels(frame_path(reference, f), expected) || pixels != expected))
            {
                std::fprintf(stderr, "native program pixel mismatch frame=%d\n", f);
                return 1;
            }
        }
        if (hz)
        {
            const u64 elapsed_ns = SDL_GetTicksNS() - frame_start_ns;
            const u64 interval_ns = 1000000000ull / static_cast<u64>(hz);
            if (elapsed_ns < interval_ns) SDL_DelayNS(interval_ns - elapsed_ns);
        }
    }
    const usize additions =
        grow_until ? std::min(static_cast<usize>(grow_until - tiles),
                              frames <= 180 ? usize(0)
                                            : static_cast<usize>((frames - 181) / add_period + 1))
        : frames <= first_add ? 0
                              : (frames - 1 - first_add) / add_period + 1;
    if (g_app.tiles.size() != static_cast<usize>(tiles) + additions || violation_count(app.ctx) ||
        !vulkan_wait_idle(app.device))
        return 1;
    // Seal every CSV row before driver/context teardown. A complete frame
    // stream is part of the measurement protocol, including the final buffer.
    if (std::fflush(stdout) != 0) return 1;
    const auto main_after = cpu_stamp(true);
    const auto cpu_after = cpu_stamp();
    std::fprintf(stderr, "frame_window_cpu_ms=%.6f frame_window_cycles=%llu\n",
                 static_cast<f64>(cpu_after.ticks - cpu_before.ticks) / 10000,
                 U(cpu_after.cycles - cpu_before.cycles));
    std::fprintf(stderr, "frame_window_main_cpu_ms=%.6f frame_window_main_cycles=%llu\n",
                 static_cast<f64>(main_after.ticks - main_before.ticks) / 10000,
                 U(main_after.cycles - main_before.cycles));
    const auto final_stats = vulkan_statistics(app.device);
    std::fprintf(stderr, "resident_images=%llu resident_metadata=%llu resident_upload=%llu\n",
                 U(final_stats.image_bytes), U(final_stats.gpu_metadata_allocated_bytes),
                 U(final_stats.upload_allocated_bytes));
    const bool clean = !diagnostic_errors && !final_stats.validation_errors;
    sdl3_app_shutdown(app.boot);
    return clean ? 0 : 1;
}
