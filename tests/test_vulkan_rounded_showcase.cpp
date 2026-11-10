// The real showcase paint path, with deterministic 5000-tile input and no HUD.
#define main showcase_original_main
#include "../examples/pui_showcase.cpp"
#undef main

#include <array>
#include <chrono>

namespace
{
using clock_type = std::chrono::steady_clock;
constexpr i32 WIDTH = 1360, HEIGHT = 860, FRAME_COUNT = 180;

f64 elapsed_ms(clock_type::time_point start)
{
    return std::chrono::duration<f64, std::milli>(clock_type::now() - start).count();
}

struct spiral_scene
{
    example_app app;
    showcase state;
    showcase_icon_set icons;

    bool open(bool analytic, bool staged, bool validation)
    {
        app.device = create_vulkan_device(
            {.validation = validation,
             .upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT,
             .analytic_rounded_rects = analytic});
        if (!app.device) return false;
        app.surface = create_vulkan_surface(app.device, {.width = WIDTH, .height = HEIGHT});
        if (!app.surface) return false;
        app.ctx = create_context(app.device, app.surface);
        if (!app.ctx) return false;
        app.win = add_window(app.ctx, nullptr, app.surface, {0, 0, WIDTH, HEIGHT});
        if (!app.win) return false;
        app.font = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
        app.font_bold = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
        app.font_oblique = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Oblique.ttf");
        if (app.font == FONT_INVALID || app.font_bold == FONT_INVALID) return false;
        app.width = WIDTH;
        app.height = HEIGHT;
        app.selftest = true;
        focus_window(app.ctx, *app.win);
        mouse_move(*app.win, 800, 220);

        seed(state);
        state.lab_mode = 4;
        state.lab_size = 64;
        while (state.tiles.size() < 5000) add_tile(state);
        std::swap(g_app, state);
        showcase_program{.icons = &icons, .analytic_rounded_rects = analytic}.prepare(app);
        std::swap(g_app, state);
        return vulkan_wait_idle(app.device) && vulkan_last_result(app.device) == VK_SUCCESS;
    }

    bool frame(i32 index)
    {
        app.frame_index = index;
        app.now = index / 60.0;
        app.dt = 1.0 / 60.0;
        // The same three keyboard Add activations as the original Spiral suite.
        if (index >= 90 && (index - 90) % 30 == 0)
        {
            app.win->focus = "lab_add"_id;
            key_event(*app.win, key::ENTER, true);
        }
        if (index >= 91 && (index - 91) % 30 == 0) key_event(*app.win, key::ENTER, false);
        std::swap(g_app, state);
        begin_frame(app.ctx, *app.win, app.now, app.dt);
        ui u(app.ctx);
        showcase_frame(u, app);
        end_frame(app.ctx);
        std::swap(g_app, state);
        return violation_count(app.ctx) == 0 && vulkan_last_result(app.device) == VK_SUCCESS;
    }

    bool read(std::vector<u8> &rgba)
    {
        return app.device->read_pixels(app.surface->scene_target(), 0, 0, WIDTH, HEIGHT,
                                       rgba.data());
    }

    bool finish()
    {
        return state.tiles.size() == 5003 && vulkan_wait_idle(app.device) &&
               vulkan_statistics(app.device).validation_errors == 0;
    }

    ~spiral_scene()
    {
        if (app.ctx)
        {
            ui u(app.ctx);
            icons.release(u);
            destroy_context(app.ctx);
        }
        if (app.device) destroy_vulkan_device(app.device);
    }
};

bool exact_uploads()
{
    spiral_scene direct, staged;
    if (!direct.open(true, false, true) || !staged.open(true, true, true)) return false;
    std::vector<u8> expected(WIDTH * HEIGHT * 4), actual(expected.size());
    for (i32 i = 0; i < FRAME_COUNT; ++i)
    {
        if (!direct.frame(i) || !direct.read(expected) || !staged.frame(i) ||
            !staged.read(actual) || expected != actual)
        {
            std::fprintf(stderr, "analytic Spiral DIRECT/STAGED mismatch at frame %d\n", i);
            return false;
        }
    }
    const auto a = vulkan_statistics(direct.app.device), b = vulkan_statistics(staged.app.device);
    if (!direct.finish() || !staged.finish() || a.rounded_instances < 5000 ||
        a.rounded_instances != b.rounded_instances)
        return false;
    std::puts("Vulkan analytic Spiral: all 180 DIRECT/STAGED frames matched (5000 -> 5003)");
    return true;
}

bool bench(bool analytic, bool staged)
{
    const auto start = clock_type::now();
    spiral_scene scene;
    if (!scene.open(analytic, staged, false)) return false;
    const f64 setup = elapsed_ms(start);
    struct sample
    {
        f64 frame_ms = 0;
        vulkan_stats stats;
    };
    std::array<sample, FRAME_COUNT> samples;
    const auto initial = vulkan_statistics(scene.app.device);
    for (i32 i = 0; i < FRAME_COUNT; ++i)
    {
        const auto t = clock_type::now();
        if (!scene.frame(i)) return false;
        samples[i] = {elapsed_ms(t), vulkan_statistics(scene.app.device)};
    }
    if (!scene.finish()) return false;
    const auto last = vulkan_statistics(scene.app.device);
    const auto U = [](u64 n) { return static_cast<unsigned long long>(n); };
    std::fprintf(stderr, "setup_ms=%.6f gpu_ms=%.6f gpu_samples=%llu resident_start=%llu\n", setup,
                 last.completed_gpu_ms - initial.completed_gpu_ms,
                 U(last.gpu_samples - initial.gpu_samples), U(initial.upload_allocated_bytes));
    std::puts("frame,frame_ms,draws,vertices,indices,upload_bytes,rounded_instances,"
              "draw_buffer_allocations");
    for (i32 i = 0; i < FRAME_COUNT; ++i)
    {
        const auto &a = samples[i].stats, &b = i ? samples[i - 1].stats : initial;
        std::printf("%d,%.9f,%llu,%llu,%llu,%llu,%llu,%llu\n", i, samples[i].frame_ms,
                    U(a.draws - b.draws), U(a.vertices - b.vertices), U(a.indices - b.indices),
                    U(a.upload_bytes - b.upload_bytes),
                    U(a.rounded_instances - b.rounded_instances),
                    U(a.draw_buffer_allocations - b.draw_buffer_allocations));
    }
    return true;
}
} // namespace

int main(int argc, char **argv)
{
    bool timing = false, analytic = true, staged = false;
    for (i32 i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--bench"))
            timing = true;
        else if (!std::strcmp(argv[i], "--mesh-rounds"))
            analytic = false;
        else if (!std::strcmp(argv[i], "--staged"))
            staged = true;
        else
            return 2;
    }
    if (!timing && (!analytic || staged)) return 2;
    g_start_page = PAGE_LAYOUT;
    g_no_hud = true;
    return (timing ? bench(analytic, staged) : exact_uploads()) ? 0 : 1;
}
