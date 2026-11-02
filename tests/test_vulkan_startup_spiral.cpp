// Real showcase startup + unchanged 5000 -> 5003 / 180-frame Add protocol.
// The baseline macro is only for compiling this same workload with frozen old
// sources; it reproduces the old bootstrap without a prepare callback.
#define PUFFERUI_IMPLEMENTATION
#include <pufferui/vulkan.h>
#undef near
#undef far
#undef small
#define main startup_showcase_original_main
#include "../examples/pui_showcase.cpp"
#undef main
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <array>

namespace
{
i32 failures = 0;
void startup_error(i32 code, const char *message, void *)
{
    ++failures;
    std::fprintf(stderr, "startup Vulkan %d: %s\n", code, message);
}
f64 clock_ms()
{
    return std::chrono::duration<f64, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
bool initialize_startup(example_app &app, render_device *device)
{
    app.device = device;
    app.surface = create_vulkan_surface(device, {.width = 1360, .height = 860});
    if (!app.surface) return false;
    app.ctx = create_context(device, app.surface);
    app.win = add_window(app.ctx, nullptr, app.surface, {0, 0, 1360, 860});
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
    while (g_app.tiles.size() < 5000) add_tile(g_app);
    return app.font != FONT_INVALID && app.font_bold != FONT_INVALID;
}
void startup_frame(example_app &app, i32 frame)
{
    app.frame_index = frame;
    app.now = frame / 60.0;
    app.dt = 1.0 / 60.0;
    if (frame >= 90 && (frame - 90) % 30 == 0)
    {
        app.win->focus = "lab_add"_id;
        key_event(*app.win, key::ENTER, true);
    }
    if (frame >= 91 && (frame - 91) % 30 == 0) key_event(*app.win, key::ENTER, false);
    begin_frame(app.ctx, *app.win, app.now, app.dt);
    {
        ui u(app.ctx);
        showcase_frame(u, app);
    }
    end_frame(app.ctx);
}
std::vector<VkBuffer> startup_geometry(example_app &app)
{
    std::vector<VkBuffer> buffers;
    const auto *surface = static_cast<vk_detail::surface *>(app.surface);
    for (const auto &slot : surface->slots)
        for (const auto &page : slot.pages)
            if (page.geometry) buffers.push_back(page.buffer);
    return buffers;
}
bool startup_geometry_matches(example_app &app, const std::vector<VkBuffer> &expected)
{
    usize index = 0;
    const auto *surface = static_cast<vk_detail::surface *>(app.surface);
    for (const auto &slot : surface->slots)
        for (const auto &page : slot.pages)
            if (page.geometry && (index >= expected.size() || page.buffer != expected[index++]))
                return false;
    return index == expected.size();
}
void close_startup(example_app &app)
{
    destroy_context(app.ctx);
    app.device->destroy_surface(app.surface);
    destroy_vulkan_device(app.device);
}
struct startup_sample
{
    f64 ms;
    vulkan_stats before, after;
};
} // namespace

int main(int argc, char **argv)
{
    bool staged = false, cache = false, bench = false, legacy = false;
    const char *reference = nullptr;
    for (i32 i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--staged"))
            staged = true;
        else if (!std::strcmp(argv[i], "--cache"))
            cache = true;
        else if (!std::strcmp(argv[i], "--bench"))
            bench = true;
        else if (!std::strcmp(argv[i], "--legacy"))
            legacy = true;
        else if (!std::strcmp(argv[i], "--reference") && i + 1 < argc)
            reference = argv[++i];
        else
            return 2;
    if (bench && reference) return 2;
    const f64 setup_start = clock_ms();
    vulkan_device_opts opts;
    opts.validation = !bench;
    opts.error_callback = startup_error;
    opts.tile_cache = cache;
    opts.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    example_app app;
    g_start_page = PAGE_LAYOUT;
    g_no_hud = true;
    auto *device = create_vulkan_device(opts);
    if (!device || !initialize_startup(app, device) || !vulkan_wait_idle(device)) return 1;
    const f64 prepare_start = clock_ms();
    bool prepared = false;
#if !defined(PUFFERUI_STARTUP_BASELINE)
    if (!legacy)
    {
        const auto page = g_app.page;
        const auto rng = g_app.rng;
        showcase_program{}.prepare(app); // identical to the real bootstrap callback
        if (g_app.page != page || g_app.rng != rng || g_app.tiles.size() != 5000 ||
            g_app.font != FONT_INVALID || g_app.lab_mode != 4 || g_app.lab_size != 64)
            return 1;
        prepared = true;
    }
#else
    (void)legacy;
#endif
    const f64 prepare_ms = clock_ms() - prepare_start, setup_ms = clock_ms() - setup_start;
    if (vulkan_last_result(device) != VK_SUCCESS) return 1;
    const auto buffers = startup_geometry(app);
    const auto *vertices = app.ctx->dl->verts.data();
    const auto *indices = app.ctx->dl->idx.data();
    const auto capacity = lab_geometry_capacity(5000);
    if (prepared && (app.ctx->dl->prepared_vertices < capacity.vertices ||
                     app.ctx->dl->prepared_indices < capacity.indices))
        return 1;
    const auto *native = static_cast<vk_detail::device *>(device);
    std::fprintf(stderr,
                 "device=%s vendor=%u device=%u driver=%u setup_ms=%.9f preparation_ms=%.9f "
                 "initial_resident_bytes=%llu prepared=%d\n",
                 native->properties.deviceName, native->properties.vendorID,
                 native->properties.deviceID, native->properties.driverVersion, setup_ms,
                 prepare_ms,
                 static_cast<unsigned long long>(vulkan_statistics(device).upload_allocated_bytes),
                 prepared);
    // No readback or second context participates in timing. Correctness mode
    // compares against either immutable files or a separate unprepared context.
    example_app oracle;
    showcase state;
    if (!bench && !reference)
    {
        showcase ready = std::move(g_app);
        auto *other = create_vulkan_device(opts);
        if (!other || !initialize_startup(oracle, other) || !vulkan_wait_idle(other)) return 1;
        state = std::move(g_app);
        g_app = std::move(ready);
    }
    std::vector<u8> actual, expected;
    if (!bench)
    {
        actual.resize(1360 * 860 * 4);
        expected.resize(actual.size());
    }
    std::array<startup_sample, 180> samples{};
    const auto initial = vulkan_statistics(device);
    for (i32 frame = 0; frame < 180; ++frame)
    {
        auto &sample = samples[frame];
        sample.before = vulkan_statistics(device);
        const f64 start = clock_ms();
        startup_frame(app, frame);
        sample.ms = clock_ms() - start;
        sample.after = vulkan_statistics(device);
        if (vulkan_last_result(device) != VK_SUCCESS || violation_count(app.ctx)) return 1;
        if (prepared &&
            (app.ctx->dl->verts.data() != vertices || app.ctx->dl->idx.data() != indices ||
             !startup_geometry_matches(app, buffers)))
            return 1;
        if (bench) continue;
        if (!device->read_pixels(app.surface->scene_target(), 0, 0, 1360, 860, actual.data()))
            return 1;
        if (reference)
        {
            char filename[32];
            std::snprintf(filename, sizeof(filename), "frame-%03d.rgba", frame);
            std::ifstream file(std::filesystem::path(reference) / filename, std::ios::binary);
            file.read(reinterpret_cast<char *>(expected.data()), expected.size());
            if (!file || file.peek() != std::char_traits<char>::eof()) return 1;
        }
        else
        {
            showcase ready = std::move(g_app);
            g_app = std::move(state);
            startup_frame(oracle, frame);
            if (!oracle.device->read_pixels(oracle.surface->scene_target(), 0, 0, 1360, 860,
                                            expected.data()) ||
                violation_count(oracle.ctx))
                return 1;
            state = std::move(g_app);
            g_app = std::move(ready);
        }
        if (actual != expected)
        {
            std::fprintf(stderr, "startup RGBA mismatch at frame %d\n", frame);
            return 1;
        }
    }
    if (!vulkan_wait_idle(device) || failures || g_app.tiles.size() != 5003) return 1;
    const auto final = vulkan_statistics(device);
    if (final.validation_errors) return 1;
    if (bench)
    {
        std::puts(
            "frame,total_ms,submit_ms,slot_wait_ms,vertices,indices,draws,submissions,upload_bytes,"
            "resident_bytes,buffer_allocations,draw_buffer_allocations,glyph_upload_bytes");
        for (i32 f = 0; f < 180; ++f)
        {
            const auto &sample = samples[f];
            const auto &a = sample.after, &b = sample.before;
            std::cout << std::setprecision(12) << f << ',' << sample.ms << ','
                      << a.submit_ms - b.submit_ms << ',' << a.slot_wait_ms - b.slot_wait_ms << ','
                      << a.vertices - b.vertices << ',' << a.indices - b.indices << ','
                      << a.draws - b.draws << ',' << a.submissions - b.submissions << ','
                      << a.upload_bytes - b.upload_bytes << ',' << a.upload_allocated_bytes << ','
                      << a.buffer_allocations - b.buffer_allocations << ','
                      << a.draw_buffer_allocations - b.draw_buffer_allocations << ','
                      << a.glyph_upload_bytes - b.glyph_upload_bytes << '\n';
        }
    }
    std::fprintf(stderr,
                 "gpu_total_ms=%.9f gpu_samples=%llu exact_frames=%d setup_plus_frame0_ms=%.9f\n",
                 final.completed_gpu_ms - initial.completed_gpu_ms,
                 static_cast<unsigned long long>(final.gpu_samples - initial.gpu_samples),
                 bench ? 0 : 180, setup_ms + samples[0].ms);
    if (oracle.device)
    {
        if (!vulkan_wait_idle(oracle.device) || vulkan_statistics(oracle.device).validation_errors)
            return 1;
        close_startup(oracle);
    }
    close_startup(app);
    return 0;
}
