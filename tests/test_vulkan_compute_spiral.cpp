// Frozen-reference capture and all-frame comparison for the GPU painter work.
// This harness includes the real showcase; existing tests/oracles stay fixed.
#define main showcase_original_main
#include "../examples/pui_showcase.cpp"
#include "vulkan_test_options.h"
#undef main
#include <filesystem>
#include <fstream>
#include <chrono>
#include "golden/vulkan_painter_rx9070xt.inl"

namespace
{
namespace fs = std::filesystem;
constexpr i32 WIDTH = 1360, HEIGHT = 860, FRAMES = 180;
constexpr usize PIXEL_BYTES = static_cast<usize>(WIDTH) * HEIGHT * 4;
i32 diagnostic_errors = 0;
i32 first_add = 90, add_period = 30;
bool growth_workload = false;
void diagnostic(i32 code, const char *message, void *)
{
    std::fprintf(stderr, "Vulkan %d: %s\n", code, message);
    ++diagnostic_errors;
}
bool initialize(example_app &app, render_device *device, i32 tiles)
{
    app.device = device;
    app.surface = create_vulkan_surface(device, {.width = WIDTH, .height = HEIGHT});
    if (!app.surface) return false;
    app.ctx = create_context(device, app.surface);
    app.win = add_window(app.ctx, nullptr, app.surface, rect::make(0, 0, WIDTH, HEIGHT));
    app.font = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
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
    if (growth_workload)
        g_app.tiles.clear();
    else
        while (g_app.tiles.size() < static_cast<usize>(tiles)) add_tile(g_app);
    return app.font != FONT_INVALID && app.font_bold != FONT_INVALID &&
           app.font_oblique != FONT_INVALID;
}
struct frame_times
{
    f64 begin = 0, build = 0, end = 0;
};
bool render(example_app &app, i32 frame, std::vector<u8> &pixels, bool readback = true,
            frame_times *timing = nullptr)
{
    app.frame_index = frame;
    app.now = frame / 60.0;
    app.dt = 1.0 / 60.0;
    if (growth_workload && frame < 50)
        while (g_app.tiles.size() < static_cast<usize>((frame + 1) * 100)) add_tile(g_app);
    if (frame >= first_add && (frame - first_add) % add_period == 0)
    {
        app.win->focus = "lab_add"_id;
        key_event(*app.win, key::ENTER, true);
    }
    if (frame >= first_add + 1 && (frame - first_add - 1) % add_period == 0)
        key_event(*app.win, key::ENTER, false);
    using timer = std::chrono::steady_clock;
    timer::time_point start;
    const auto elapsed = [&]()
    { return std::chrono::duration<f64, std::milli>(timer::now() - start).count(); };
    if (timing) start = timer::now();
    begin_frame(app.ctx, *app.win, app.now, app.dt);
    if (timing)
    {
        timing->begin = elapsed();
        start = timer::now();
    }
    {
        ui u(app.ctx);
        showcase_frame(u, app);
    }
    if (timing)
    {
        timing->build = elapsed();
        start = timer::now();
    }
    end_frame(app.ctx);
    if (timing) timing->end = elapsed();
    return !violation_count(app.ctx) && vulkan_last_result(app.device) == VK_SUCCESS &&
           (!readback || app.device->read_pixels(app.surface->scene_target(), 0, 0, WIDTH, HEIGHT,
                                                 pixels.data()));
}
fs::path frame_path(const fs::path &directory, i32 frame)
{
    char name[48];
    std::snprintf(name, sizeof(name), "frame_%03d.rgba", frame);
    return directory / name;
}
bool write_pixels(const fs::path &path, const std::vector<u8> &pixels)
{
    if (fs::exists(path)) return false; // never replace a golden
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(pixels.data()), pixels.size());
    return output.good();
}
bool read_pixels(const fs::path &path, std::vector<u8> &pixels)
{
    std::error_code error;
    if (fs::file_size(path, error) != PIXEL_BYTES || error) return false;
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char *>(pixels.data()), pixels.size());
    return input.good();
}
} // namespace

#if !defined(PUFFERUI_COMPUTE_SPIRAL_HELPERS_ONLY)
int main(int argc, char **argv)
{
    fs::path capture, reference;
    i32 tiles = 5000, frames = FRAMES;
    bool adaptive = false, golden = false;
    bool shadow = false, sdf = false, compute = false, vector_text = false, trace_frames = false,
         cache = false, staged = false;
    for (i32 i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc)
            capture = argv[++i];
        else if (std::strcmp(argv[i], "--reference") == 0 && i + 1 < argc)
            reference = argv[++i];
        else if (std::strcmp(argv[i], "--tiles") == 0 && i + 1 < argc)
            tiles = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--shadow") == 0)
            shadow = true;
        else if (std::strcmp(argv[i], "--golden") == 0)
            golden = true;
        else if (std::strcmp(argv[i], "--growth") == 0)
            growth_workload = true;
        else if (std::strcmp(argv[i], "--sdf") == 0)
            sdf = true;
        else if (std::strcmp(argv[i], "--compute") == 0)
            sdf = compute = true;
        else if (std::strcmp(argv[i], "--adaptive") == 0)
            sdf = compute = adaptive = cache = true;
        else if (std::strcmp(argv[i], "--long-run") == 0)
        {
            frames = 3600;
            first_add = 30;
            add_period = 60;
        }
        else if (std::strcmp(argv[i], "--vector-text") == 0)
            vector_text = true;
        else if (std::strcmp(argv[i], "--trace-frames") == 0)
            trace_frames = true;
        else if (std::strcmp(argv[i], "--cache") == 0)
            cache = true;
        else if (std::strcmp(argv[i], "--staged") == 0)
            staged = true;
        else
            return 1;
    }
    if ((tiles != 1150 && tiles != 5000) || (!capture.empty() && !reference.empty())) return 1;
    if (growth_workload && (tiles != 5000 || golden || frames != FRAMES)) return 1;
    if (!capture.empty())
    {
        if (fs::exists(capture) || !fs::create_directories(capture)) return 1;
    }
    if (!reference.empty() && !fs::is_directory(reference)) return 1;
    g_start_page = PAGE_LAYOUT;
    g_no_hud = true;
    vulkan_device_opts options = test_detail::reference_options();
    options.validation = true;
    options.error_callback = diagnostic;
    options.painter = shadow ? vulkan_painter_mode::COMMAND_SHADOW : vulkan_painter_mode::REFERENCE;
    if (sdf)
        options.painter =
            compute ? vulkan_painter_mode::COMPUTE_SDF : vulkan_painter_mode::SDF_RASTER;
    options.vector_text = vector_text;
    options.tile_cache = cache;
    options.adaptive_compute = adaptive;
    options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    render_device *device = create_vulkan_device(options);
    if (!device) return 1;
    device->set_vsync(false);
    const auto native = vulkan_handles(device);
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(native.physical_device, &properties);
    if (golden && (frames != FRAMES || (sdf && !vector_text) || properties.vendorID != 4098 ||
                   properties.deviceID != 30032 || properties.driverVersion != 8389024))
    {
        std::fprintf(stderr, "golden domain requires qualified RX9070XT driver and visual mode\n");
        return 2;
    }
    std::printf("device=%s vendor=%u device_id=%u driver=%u frames=%d tiles=%d\n",
                properties.deviceName, properties.vendorID, properties.deviceID,
                properties.driverVersion, frames, tiles);
    const i32 variants = capture.empty() && reference.empty() ? 2 : 1;
    render_device *devices[2] = {device, device};
    if (variants == 2)
    {
        vulkan_device_opts child = options;
        child.native = &native;
        child.painter =
            sdf ? (compute ? vulkan_painter_mode::SDF_RASTER : vulkan_painter_mode::COMPUTE_SDF)
                : vulkan_painter_mode::COMMAND_SHADOW;
        devices[1] = create_vulkan_device(child);
        if (!devices[1]) return 1;
    }
    example_app apps[2];
    showcase states[2];
    std::vector<u8> pixels[2];
    for (i32 v = 0; v < variants; ++v)
    {
        pixels[v].resize(PIXEL_BYTES);
        if (!initialize(apps[v], devices[v], tiles)) return 1;
        std::swap(g_app, states[v]);
    }
    if (!reference.empty()) pixels[1].resize(PIXEL_BYTES);
    for (i32 f = 0; f < frames; ++f)
    {
        if (trace_frames) std::fprintf(stderr, "frame=%d\n", f);
        for (i32 v = 0; v < variants; ++v)
        {
            std::swap(g_app, states[v]);
            if (!render(apps[v], f, pixels[v])) return 1;
            std::swap(g_app, states[v]);
        }
        if (!capture.empty() && !write_pixels(frame_path(capture, f), pixels[0])) return 1;
        if (!reference.empty() && !read_pixels(frame_path(reference, f), pixels[1])) return 1;
        if (golden)
        {
            u64 hash = 14695981039346656037ull;
            for (const auto byte : pixels[0]) hash = (hash ^ byte) * 1099511628211ull;
            if (hash != painter_goldens[sdf ? 1 : 0][tiles == 5000 ? 1 : 0][f])
            {
                std::fprintf(stderr, "immutable native golden mismatch frame=%d\n", f);
                return 1;
            }
        }
        if (capture.empty() && pixels[0] != pixels[1])
        {
            usize first = 0;
            while (pixels[0][first] == pixels[1][first]) ++first;
            std::fprintf(stderr,
                         "frame=%d mismatch pixel=(%zu,%zu) channel=%zu actual=%u expected=%u\n", f,
                         (first / 4) % WIDTH, (first / 4) / WIDTH, first % 4, pixels[0][first],
                         pixels[1][first]);
            return 1;
        }
    }
    for (i32 v = 0; v < variants; ++v)
    {
        if (states[v].tiles.size() != static_cast<usize>(tiles + (frames == FRAMES ? 3 : 60)))
            return 1;
        if (!vulkan_wait_idle(devices[v])) return 1;
        const auto final_stats = vulkan_statistics(devices[v]);
        if (sdf && !adaptive && ((compute && v == 0) || (!compute && v == 1)) &&
            (!final_stats.gpu_sdf_batches || !final_stats.gpu_sdf_pixels))
        {
            std::fprintf(stderr, "no actual compute scene writes in Spiral\n");
            return 1;
        }
        destroy_context(apps[v].ctx);
        devices[v]->destroy_surface(apps[v].surface);
    }
    for (i32 v = 0; v < variants; ++v)
    {
        if (!vulkan_wait_idle(devices[v])) return 1;
        const auto stats = vulkan_statistics(devices[v]);
        if (stats.validation_errors || stats.shadow_overflows) return 1;
        if (!sdf && (shadow || v == 1) && !stats.shadow_commands) return 1;
        std::printf(
            "variant=%d gpu_batches=%llu gpu_pixels=%llu vertex_requests=%llu metadata_peak=%llu\n",
            v, static_cast<unsigned long long>(stats.gpu_sdf_batches),
            static_cast<unsigned long long>(stats.gpu_sdf_pixels),
            static_cast<unsigned long long>(stats.requested_vertices),
            static_cast<unsigned long long>(stats.peak_gpu_metadata_allocated_bytes));
        std::printf("variant=%d shadow_commands=%llu shadow_bytes=%llu overflows=%llu\n", v,
                    static_cast<unsigned long long>(stats.shadow_commands),
                    static_cast<unsigned long long>(stats.shadow_payload_bytes),
                    static_cast<unsigned long long>(stats.shadow_overflows));
        std::printf("variant=%d cache_hits=%llu cached_pixels=%llu avoided_vertices=%llu\n", v,
                    static_cast<unsigned long long>(stats.gpu_sdf_cache_hits),
                    static_cast<unsigned long long>(stats.gpu_sdf_cached_pixels),
                    static_cast<unsigned long long>(stats.sdf_avoided_vertices));
    }
    if (diagnostic_errors) return 1;
    if (variants == 2) destroy_vulkan_device(devices[1]);
    destroy_vulkan_device(device);
    std::printf("all %d %d-item Spiral frames %s\n", frames, tiles,
                capture.empty() ? "matched" : "captured (raw RGBA8)");
    return 0;
}
#endif
