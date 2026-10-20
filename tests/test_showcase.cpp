// Tests and measurements compile the real showcase, including its local helpers.
// The normal example and its command line remain unchanged.
#define main showcase_example_main
#include "../examples/pui_showcase.cpp"
#undef main
#include "test_util.h"
#include <chrono>
#include <new>

namespace
{
thread_local bool track_allocations = false;
thread_local usize allocations = 0;
using measure_clock = std::chrono::steady_clock;
double elapsed_ms(measure_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(measure_clock::now() - start).count();
}
u64 hash_bytes(u64 h, const void *data, usize count)
{
    const auto *p = static_cast<const u8 *>(data);
    for (usize i = 0; i < count; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}
struct geometry_device : null_device
{
    u64 hash = 1469598103934665603ull;
    void draw(texture_handle tex, const vertex *v, i32 vc, const i32 *idx, i32 ic) override
    {
        hash = hash_bytes(hash, v, static_cast<usize>(vc) * sizeof(vertex));
        hash = hash_bytes(hash, idx, static_cast<usize>(ic) * sizeof(i32));
        null_device::draw(tex, v, vc, idx, ic);
    }
};

// Independent original first-fit search: keep the slow scan as the oracle.
static f32 original_grid(const showcase &s, f32 width, std::vector<rect> &out)
{
    out.clear();
    const f32 gap = s.lab_gap;
    const i32 cols = static_cast<i32>(max2(1.0f, std::floor((width + gap) / (s.lab_size + gap))));
    const f32 cell = (width - gap * static_cast<f32>(cols - 1)) / static_cast<f32>(cols);
    f32 height = 0;
    std::vector<u8> used;
    for (const lab_tile &tile : s.tiles)
    {
        if (tile.dying >= 0.0) continue;
        const i32 span = tile.id == s.lab_selected && cols >= 2 ? 2 : 1;
        for (i32 at = 0;; ++at)
        {
            const i32 row = at / cols, col = at % cols;
            if (col + span > cols) continue;
            const usize need = static_cast<usize>((row + span) * cols);
            if (used.size() < need) used.resize(need, 0);
            bool free = true;
            for (i32 y = 0; y < span; ++y)
                for (i32 x = 0; x < span; ++x)
                    if (used[static_cast<usize>((row + y) * cols + col + x)]) free = false;
            if (!free) continue;
            for (i32 y = 0; y < span; ++y)
                for (i32 x = 0; x < span; ++x)
                    used[static_cast<usize>((row + y) * cols + col + x)] = 1;
            const f32 extent = cell * static_cast<f32>(span) + gap * static_cast<f32>(span - 1);
            out.push_back(rect::make(static_cast<f32>(col) * (cell + gap),
                                     static_cast<f32>(row) * (cell + gap), extent, extent));
            height = max2(height, static_cast<f32>(row + span) * (cell + gap) - gap);
            break;
        }
    }
    return height;
}

// Original gradient arithmetic and topology; independent of cached arc tables.
static void original_gradient(ui &u, rect r, f32 radius, color tl, color tr, color br, color bl)
{
    if (r.w <= 0.5f || r.h <= 0.5f) return;
    radius = clampf(radius, 0.0f, min2(r.w, r.h) * 0.5f);
    constexpr i32 seg = 6, ring = 4 * (seg + 1);
    vertex v[1 + ring];
    i32 idx[3 * ring];
    const auto col_at = [&](f32 x, f32 y)
    {
        const f32 fx = sat((x - r.x) / r.w), fy = sat((y - r.y) / r.h);
        return mixc(mixc(tl, tr, fx), mixc(bl, br, fx), fy);
    };
    const vec2 centers[4] = {{r.x + radius, r.y + radius},
                             {r.right() - radius, r.y + radius},
                             {r.right() - radius, r.bottom() - radius},
                             {r.x + radius, r.bottom() - radius}};
    const f32 starts[4] = {PI, PI * 1.5f, 0.0f, PI * 0.5f};
    const color mid = col_at(r.center_x(), r.center_y());
    v[0] = {r.center_x(), r.center_y(), 0, 0, mid};
    i32 n = 1, k = 0;
    for (i32 c = 0; c < 4; ++c)
        for (i32 s = 0; s <= seg; ++s)
        {
            const f32 a = starts[c] + (PI * 0.5f) * static_cast<f32>(s) / static_cast<f32>(seg);
            const f32 x = centers[c].x + std::cos(a) * radius;
            const f32 y = centers[c].y + std::sin(a) * radius;
            v[n++] = {x, y, 0, 0, col_at(x, y)};
        }
    for (i32 i = 0; i < ring; ++i)
    {
        idx[k++] = 0;
        idx[k++] = 1 + i;
        idx[k++] = 1 + (i + 1) % ring;
    }
    u.draw_rounded_rect(r, mid, radius);
    u.draw_triangles(nullptr, v, n, idx, k);
}

PUI_TEST(showcase_grid_matches_original_first_fit)
{
    showcase s;
    seed(s);
    while (s.tiles.size() < 1150) add_tile(s);
    s.lab_mode = 0;
    for (f32 width : {40.0f, 130.0f, 641.25f, 1040.0f})
        for (f32 size : {64.0f, 112.0f, 180.0f})
            for (i32 selection : {-1, 0, 1, 13, 574, 1149})
            {
                s.lab_size = size;
                s.lab_selected = selection < 0 ? 0 : s.tiles[static_cast<usize>(selection)].id;
                s.tiles[3].dying = 1;
                std::vector<rect> expected, actual;
                const f32 h = original_grid(s, width, expected);
                CHECK(lab_layout(s, width, 0, actual) == h);
                CHECK(actual.size() == expected.size());
                CHECK(std::memcmp(actual.data(), expected.data(), actual.size() * sizeof(rect)) ==
                      0);
            }
}

PUI_TEST(showcase_gradient_matches_original_geometry_at_clip_edges)
{
    geometry_device dev;
    dev.my_surface.w = 320;
    dev.my_surface.h = 240;
    context *c = create_context(&dev, dev.create_surface());
    for (rect clip : {rect::make(0, 0, 320, 240), rect::make(10, 10, 100, 70), rect{}})
        for (rect r : {rect::make(15.25f, 13.75f, 63.5f, 47.25f), rect::make(109.5f, 20, 40, 30),
                       rect::make(-20, 10, 20.25f, 40), rect::make(340, 20, 100, 80),
                       rect::make(10, 10, 0.5f, 20)})
            for (f32 radius : {0.0f, 0.4f, 7.125f, 90.0f})
            {
                u64 expected = 0;
                for (i32 pass = 0; pass < 2; ++pass)
                {
                    dev.hash = 1469598103934665603ull;
                    begin_frame(c, 1, 1.0 / 60.0, {0, 0, 320, 240});
                    {
                        ui u(c);
                        region rg = u.region(clip, "clip"_id);
                        const color a{34, 155, 233, 217}, b{244, 17, 121, 90};
                        if (pass == 0)
                            original_gradient(u, r, radius, a, b, b, a);
                        else
                            gradient_round(u, r, radius, a, b, b, a);
                    }
                    end_frame(c);
                    if (pass == 0)
                        expected = dev.hash;
                    else
                        CHECK(dev.hash == expected);
                }
            }
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

template <class State> void check_slot_storage()
{
    // The baseline binary is also built from this harness, before that member exists.
    if constexpr (requires(State s) { s.lab_slots; })
    {
        tf_env env(1360, 860);
        CHECK(!tf_needs_font(env.c));
        State s;
        seed(s);
        while (s.tiles.size() < 1150) add_tile(s);
        s.lab_mode = 4;
        s.lab_size = 64;
        const auto frame = [&](ui &u) { page_layout(u, s, {250, 120, 1080, 100000}, {250, 120}); };
        env.frame(1, frame);
        CHECK(s.lab_slots.size() == 1150);
        const rect *storage = s.lab_slots.data();
        const usize capacity = s.lab_slots.capacity();
        env.frame(2, frame);
        CHECK(s.lab_slots.data() == storage && s.lab_slots.capacity() == capacity);
        track_allocations = true;
        allocations = 0;
        lab_layout(s, 1040, 2, s.lab_slots);
        track_allocations = false;
        CHECK(allocations == 0);
        add_tile(s);
        env.frame(3, frame);
        CHECK(s.lab_slots.size() == 1151);
    }
    else
    {
        CHECK(false && "showcase must retain layout slot storage");
    }
}
PUI_TEST(showcase_reuses_layout_slots)
{
    check_slot_storage<showcase>();
}

// Counts the application's actual draw stream without inserting timers in tile loops.
struct measured_device : render_device
{
    render_device &real;
    i32 calls = 0;
    i64 vertices = 0, indices = 0;
    explicit measured_device(render_device &d) : real(d) {}
    backend_caps caps() const override { return real.caps(); }
    void begin_frame() override { real.begin_frame(); }
    void end_frame() override { real.end_frame(); }
    void set_vsync(bool on) override { real.set_vsync(on); }
    texture_handle create_texture(i32 w, i32 h, const u8 *p) override
    {
        return real.create_texture(w, h, p);
    }
    void update_texture(texture_handle t, i32 x, i32 y, i32 w, i32 h, const u8 *p) override
    {
        real.update_texture(t, x, y, w, h, p);
    }
    void destroy_texture(texture_handle t) override { real.destroy_texture(t); }
    void set_texture_filter(texture_handle t, bool linear) override
    {
        real.set_texture_filter(t, linear);
    }
    texture_handle create_target(i32 w, i32 h) override { return real.create_target(w, h); }
    void destroy_target(texture_handle t) override { real.destroy_target(t); }
    void blit(texture_handle s, const rect *sr, texture_handle d, const rect *dr) override
    {
        real.blit(s, sr, d, dr);
    }
    void set_target(texture_handle t) override { real.set_target(t); }
    texture_handle current_target() const override { return real.current_target(); }
    void set_clip(const rect *r) override { real.set_clip(r); }
    void set_cursor(cursor c) override { real.set_cursor(c); }
    void clear(color c) override { real.clear(c); }
    void draw(texture_handle t, const vertex *v, i32 vc, const i32 *idx, i32 ic) override
    {
        ++calls;
        vertices += vc;
        indices += ic;
        real.draw(t, v, vc, idx, ic);
    }
};

static constexpr i32 golden_frames[] = {89, 91, 98, 104};
// Captured from f876bb2 before the five optimizations (software renderer, bundled fonts).
static constexpr u64 golden_hashes[] = {0x830f2b351fd26655ull, 0x09fd93be5d12d8ffull,
                                        0x4ca58d2303cb9ee8ull, 0xb344d93c2d2e8eccull};
// Captured from 8dd5a0a before r112 (5000 tiles, same scene and frame numbers).
static constexpr u64 golden_hashes_large[] = {0x7dff9989f9d46a84ull, 0xf8218090d31121d5ull,
                                              0x7c2d1f5233eec5d7ull, 0xeca630b97aa34f56ull};

int run_spiral(const char *renderer, bool pixels, const char *capture_prefix, i32 tiles)
{
    example_app app;
    null_device nd;
    const bool headless = std::strcmp(renderer, "null") == 0;
    if (headless)
    {
        nd.my_surface.w = 1360;
        nd.my_surface.h = 860;
        app.ctx = create_context(&nd, nd.create_surface());
        app.win = add_window(app.ctx, nullptr, &nd.my_surface, {0, 0, 1360, 860});
        app.font = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    }
    else
    {
        SDL_SetLogPriorities(SDL_LOG_PRIORITY_ERROR);
        if (std::strcmp(renderer, "software") == 0) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        app.boot.hidden = true;
        app.boot.renderer_name = renderer;
        app.boot.asset_dir = PUFFERUI_ASSET_DIR;
        if (!sdl3_app_init(app.boot, "Spiral regression", 1360, 860, 0, nullptr)) return 1;
        app.ctx = app.boot.ctx;
        app.win = app.boot.win;
        app.device = app.boot.device;
        app.surface = app.boot.surface;
        app.sdl_window = static_cast<SDL_Window *>(app.boot.sdl_window);
        app.font = app.boot.font;
        app.device->set_vsync(false);
    }
    app.font_bold = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
    app.font_oblique = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Oblique.ttf");
    if (app.font == FONT_INVALID || app.font_bold == FONT_INVALID) return 1;
    app.width = 1360;
    app.height = 860;
    app.selftest = true;
    focus_window(app.ctx, *app.win);
    g_app = showcase{};
    seed(g_app);
    g_app.lab_mode = 4;
    g_app.lab_size = 64;
    while (g_app.tiles.size() < static_cast<usize>(tiles)) add_tile(g_app);
    g_start_page = PAGE_LAYOUT;
    g_no_hud = true;
    mouse_move(*app.win, 800, 220);
    measured_device measured(*app.ctx->device);
    app.ctx->device = &measured;
    const i32 frames = pixels ? 105 : 180;
    std::vector<u8> rgba(1360 * 860 * 4);
    i32 failures = 0;
    if (!pixels) std::puts("frame,count,begin_ms,build_ms,end_ms,total_ms,draws,vertices,indices");
    for (i32 f = 0; f < frames; ++f)
    {
        app.frame_index = f;
        app.now = f / 60.0;
        app.dt = 1.0 / 60.0;
        if (f >= 90 && (f - 90) % 30 == 0)
        {
            app.win->focus = "lab_add"_id;
            key_event(*app.win, key::ENTER, true); // invoke the actual Add control
        }
        if (f >= 91 && (f - 91) % 30 == 0) key_event(*app.win, key::ENTER, false);
        measured.calls = 0;
        measured.vertices = measured.indices = 0;
        auto start = measure_clock::now();
        begin_frame(app.ctx, *app.win, app.now, app.dt);
        const double begin = elapsed_ms(start);
        start = measure_clock::now();
        {
            ui u(app.ctx);
            showcase_frame(u, app);
        }
        const double build = elapsed_ms(start);
        start = measure_clock::now();
        end_frame(app.ctx);
        const double end = elapsed_ms(start);
        if (!pixels)
            std::printf("%d,%zu,%.6f,%.6f,%.6f,%.6f,%d,%lld,%lld\n", f, g_app.tiles.size(), begin,
                        build, end, begin + build + end, measured.calls,
                        static_cast<long long>(measured.vertices),
                        static_cast<long long>(measured.indices));
        for (usize i = 0; i < std::size(golden_frames); ++i)
            if (f == golden_frames[i] && !headless)
            {
                if (pixels)
                {
                    if (!measured.real.read_pixels(app.surface->scene_target(), 0, 0, 1360, 860,
                                                   rgba.data()))
                    {
                        ++failures;
                        continue;
                    }
                    const u64 hash = hash_bytes(1469598103934665603ull, rgba.data(), rgba.size());
                    const u64 expected_hash =
                        tiles == 5000 ? golden_hashes_large[i] : golden_hashes[i];
                    if (hash != expected_hash)
                    {
                        std::printf("FAIL spiral frame=%d hash=0x%016llx expected=0x%016llx\n", f,
                                    static_cast<unsigned long long>(hash),
                                    static_cast<unsigned long long>(expected_hash));
                        ++failures;
                    }
                }
                if (capture_prefix)
                {
                    char path[1024];
                    std::snprintf(path, sizeof(path), "%s-%d.bmp", capture_prefix, f);
                    example_screenshot(app, path);
                }
            }
    }
    const usize expected = static_cast<usize>(tiles) + (pixels ? 1 : 3);
    if (g_app.tiles.size() != expected || violation_count(app.ctx)) ++failures;
    app.ctx->device = &measured.real;
    if (headless)
        destroy_context(app.ctx);
    else
        sdl3_app_shutdown(app.boot);
    if (pixels && !failures)
        std::printf("all 4 Spiral golden frames matched (%d tiles + Add)\n", tiles);
    return failures ? 1 : 0;
}
} // namespace

void *operator new(size_t size)
{
    if (track_allocations) ++allocations;
    if (void *p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept
{
    std::free(p);
}
void operator delete(void *p, size_t) noexcept
{
    std::free(p);
}

#include "test_showcase_paint.inl"

int main(int argc, char **argv)
{
    if (argc > 1)
    {
        const bool pixels = std::strcmp(argv[1], "--pixels") == 0;
        if (!pixels && std::strcmp(argv[1], "--bench") != 0) return 1;
        const char *renderer = pixels ? "software" : argc > 2 ? argv[2] : "null";
        const char *capture = nullptr;
        i32 tiles = 1150;
        for (i32 i = 2; i + 1 < argc; ++i)
        {
            if (std::strcmp(argv[i], "--capture") == 0) capture = argv[i + 1];
            if (std::strcmp(argv[i], "--tiles") == 0) tiles = std::atoi(argv[i + 1]);
        }
        if (tiles != 1150 && tiles != 5000) return 1;
        return run_spiral(renderer, pixels, capture, tiles);
    }
    for (const auto &test : test_registry()) test.fn();
    if (!g_failures)
        std::printf("all showcase regressions passed (%zu tests)\n", test_registry().size());
    return g_failures ? 1 : 0;
}
