// Actual showcase workload, independent of the unchanged SDL test/golden suite.
#define main showcase_original_main
#include "../examples/pui_showcase.cpp"
#undef main
#include <chrono>

namespace
{
i32 failures = 0;
void diagnostic(i32 code, const char *text, void *)
{
    std::fprintf(stderr, "Vulkan %d: %s\n", code, text);
    ++failures;
}
using clock_type = std::chrono::steady_clock;
f64 ms(clock_type::time_point t)
{
    return std::chrono::duration<f64, std::milli>(clock_type::now() - t).count();
}
bool initialize(example_app &app, render_device *device, i32 tiles)
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
    app.selftest = true;
    focus_window(app.ctx, *app.win);
    mouse_move(*app.win, 800, 220);
    g_app = showcase{};
    seed(g_app);
    g_app.lab_mode = 4;
    g_app.lab_size = 64;
    while (g_app.tiles.size() < static_cast<usize>(tiles)) add_tile(g_app);
    return app.font != FONT_INVALID && app.font_bold != FONT_INVALID;
}
struct result
{
    f64 begin = 0, build = 0, end = 0;
    u64 count = 0, draws = 0, vertices = 0, indices = 0;
    vulkan_stats before{}, after{};
};
result frame(example_app &app, i32 f)
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
    result r;
    r.before = vulkan_statistics(app.device);
    auto start = clock_type::now();
    begin_frame(app.ctx, *app.win, app.now, app.dt);
    r.begin = ms(start);
    start = clock_type::now();
    {
        ui u(app.ctx);
        showcase_frame(u, app);
    }
    r.build = ms(start);
    start = clock_type::now();
    end_frame(app.ctx);
    r.end = ms(start);
    r.count = g_app.tiles.size();
    r.after = vulkan_statistics(app.device);
    r.draws = r.after.requested_draws - r.before.requested_draws;
    r.vertices = r.after.requested_vertices - r.before.requested_vertices;
    r.indices = r.after.requested_indices - r.before.requested_indices;
    return r;
}
void print(i32 f, const result &r, VkPresentModeKHR mode)
{
    const auto &a = r.after, &b = r.before;
    const auto U = [](u64 x) { return static_cast<unsigned long long>(x); };
    std::printf("%d,%llu,%.6f,%.6f,%.6f,%.6f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,"
                "%.6f,%.6f,%.6f,%.6f,%.6f,%llu,%llu,%llu,%llu,%llu,%.6f,%u,%llu,%.6f,%llu\n",
                f, U(r.count), r.begin, r.build, r.end, r.begin + r.build + r.end, U(r.draws),
                U(r.vertices), U(r.indices), U(a.upload_bytes - b.upload_bytes),
                U(a.upload_allocated_bytes), U(a.image_bytes), U(a.retired_bytes),
                U(a.cache_hits - b.cache_hits), U(a.cache_fallbacks - b.cache_fallbacks),
                a.acquire_ms - b.acquire_ms, a.slot_wait_ms - b.slot_wait_ms,
                a.submit_ms - b.submit_ms, a.present_ms - b.present_ms, a.cache_ms - b.cache_ms,
                U(a.cache_metadata_bytes), U(a.peak_cache_metadata_bytes), U(a.descriptor_sets),
                U(a.readbacks - b.readbacks), U(a.gpu_samples - b.gpu_samples),
                a.completed_gpu_ms - b.completed_gpu_ms, static_cast<u32>(mode),
                U(a.device_upload_allocated_bytes), a.cache_prepare_ms - b.cache_prepare_ms,
                U(a.cache_bypasses - b.cache_bypasses));
}
} // namespace
int main(int argc, char **argv)
{
    bool pixels = true, cache = false, validation = true, windowed = false;
    bool offscreen = false, explicit_validation = false, staged = false, upload_reference = false;
    i32 tiles = 1150;
    for (i32 i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--bench") == 0)
        {
            pixels = false;
            validation = false;
            windowed = true;
        }
        if (std::strcmp(argv[i], "--cache") == 0) cache = true;
        if (std::strcmp(argv[i], "--validation") == 0) explicit_validation = true;
        if (std::strcmp(argv[i], "--offscreen") == 0) offscreen = true;
        if (std::strcmp(argv[i], "--staged") == 0) staged = true;
        if (std::strcmp(argv[i], "--upload-reference") == 0) upload_reference = true;
        if (std::strcmp(argv[i], "--tiles") == 0 && i + 1 < argc) tiles = std::atoi(argv[++i]);
    }
    if (tiles != 1150 && tiles != 5000) return 1;
    if (offscreen) windowed = false;
    if (explicit_validation) validation = true;
    if (upload_reference && !pixels) return 1;
    g_start_page = PAGE_LAYOUT;
    g_no_hud = true;
    vulkan_device_opts opts;
    opts.vector_text = false; // This original oracle exercises the bitmap mesh stream.
    opts.validation = validation;
    opts.error_callback = diagnostic;
    opts.tile_cache = cache;
    opts.upload_mode =
        staged && !upload_reference ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    example_app apps[2];
    showcase states[2];
    render_device *device = nullptr;
    render_device *devices[2]{};
    if (windowed)
    {
        auto &app = apps[0];
        app.boot.hidden = true;
        app.boot.asset_dir = PUFFERUI_ASSET_DIR;
        app.boot.use_vulkan = true;
        app.boot.vulkan = opts;
        if (!sdl3_app_init(app.boot, "Vulkan Spiral", 1360, 860, 0, nullptr)) return 1;
        device = app.boot.device;
        devices[0] = device;
        app.device = device;
        app.surface = app.boot.surface;
        app.ctx = app.boot.ctx;
        app.win = app.boot.win;
        app.sdl_window = static_cast<SDL_Window *>(app.boot.sdl_window);
        app.font = app.boot.font;
        app.font_bold = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
        app.font_oblique = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Oblique.ttf");
        app.width = 1360;
        app.height = 860;
        app.selftest = true;
        focus_window(app.ctx, *app.win);
        mouse_move(*app.win, 800, 220);
        g_app = showcase{};
        seed(g_app);
        g_app.lab_mode = 4;
        g_app.lab_size = 64;
        while (g_app.tiles.size() < static_cast<usize>(tiles)) add_tile(g_app);
        std::swap(g_app, states[0]);
    }
    else
    {
        device = create_vulkan_device(opts);
        if (!device) return 1;
        devices[0] = devices[1] = device;
        if (upload_reference)
        {
            const auto native = vulkan_handles(device);
            vulkan_device_opts child = opts;
            child.native = &native;
            child.upload_mode = vulkan_upload_mode::STAGED;
            devices[1] = create_vulkan_device(child);
            if (!devices[1]) return 1;
        }
        for (i32 i = 0; i < (pixels ? 2 : 1); ++i)
        {
            if (!initialize(apps[i], devices[i], tiles)) return 1;
            std::swap(g_app, states[i]);
        }
    }
    device->set_vsync(false);
    const auto native = vulkan_handles(device);
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(native.physical_device, &properties);
    std::fprintf(
        stderr,
        "device=%s vendor=%u device=%u driver=%u windowed=%d cache=%d validation=%d staged=%d\n",
        properties.deviceName, properties.vendorID, properties.deviceID, properties.driverVersion,
        windowed, cache, validation, staged);
    std::vector<u8> rgba[2];
    for (auto &p : rgba) p.resize(1360 * 860 * 4);
    if (!pixels)
        std::puts(
            "frame,count,begin_ms,build_ms,end_ms,total_ms,draws,vertices,indices,upload_"
            "bytes,upload_allocated_bytes,image_bytes,retired_bytes,cache_hits,cache_fallbacks,"
            "acquire_"
            "ms,slot_wait_ms,submit_ms,present_ms,cache_ms,cache_bytes,peak_cache_bytes,descriptor_"
            "sets,readbacks,gpu_samples,completed_gpu_ms,present_mode,device_upload_bytes,cache_"
            "prepare_ms,cache_bypasses");
    for (i32 f = 0; f < 180; ++f)
    {
        result results[2];
        for (i32 variant = 0; variant < (pixels ? 2 : 1); ++variant)
        {
            auto *d = devices[variant];
            vulkan_set_cache(d, upload_reference ? false : (pixels ? variant == 1 : cache));
            std::swap(g_app, states[variant]);
            results[variant] = frame(apps[variant], f);
            std::swap(g_app, states[variant]);
            if (vulkan_last_result(d) != VK_SUCCESS || violation_count(apps[variant].ctx)) return 1;
            if (pixels && !d->read_pixels(apps[variant].surface->scene_target(), 0, 0, 1360, 860,
                                          rgba[variant].data()))
                return 1;
        }
        if (pixels)
        {
            if (rgba[0] != rgba[1] || results[0].count != results[1].count ||
                results[0].draws != results[1].draws ||
                results[0].vertices != results[1].vertices ||
                results[0].indices != results[1].indices)
            {
                std::fprintf(stderr, "cache/reference mismatch at frame %d\n", f);
                return 1;
            }
        }
        else
            print(f, results[0], vulkan_present_mode(apps[0].surface));
    }
    for (i32 i = 0; i < (pixels ? 2 : 1); ++i)
    {
        if (states[i].tiles.size() != static_cast<usize>(tiles + 3)) return 1;
        if (!windowed)
        {
            destroy_context(apps[i].ctx);
            devices[i]->destroy_surface(apps[i].surface);
        }
    }
    if (!vulkan_wait_idle(device) || vulkan_statistics(device).validation_errors || failures)
        return 1;
    const auto stats = vulkan_statistics(device);
    if (stats.peak_cache_metadata_bytes > opts.cache_budget_bytes ||
        stats.peak_upload_allocated_bytes > opts.upload_budget_bytes)
    {
        std::fprintf(stderr, "Vulkan resource budget exceeded\n");
        return 1;
    }
    if (upload_reference)
    {
        if (!vulkan_wait_idle(devices[1])) return 1;
        const auto child_stats = vulkan_statistics(devices[1]);
        if (child_stats.validation_errors ||
            child_stats.peak_upload_allocated_bytes > opts.upload_budget_bytes)
            return 1;
        destroy_vulkan_device(devices[1]);
    }
    if (windowed)
        sdl3_app_shutdown(apps[0].boot);
    else
        destroy_vulkan_device(device);
    if (pixels)
        std::printf("all 180 Vulkan Spiral %s frames matched (%d tiles + three Adds)\n",
                    upload_reference ? "upload/reference" : "cache/reference", tiles);
    return failures ? 1 : 0;
}
