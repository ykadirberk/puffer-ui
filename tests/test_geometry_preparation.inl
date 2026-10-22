// r116: separate regressions; existing test bodies and golden oracles stay fixed.
#include "test_util.h"
// The implementation's Windows backend headers define RPC's `small` macro.
// The standalone showcase normally sees no platform headers in its TU.
#undef small
#undef near
#undef far
#define main preparation_showcase_main
#include "../examples/pui_showcase.cpp"
#undef main

namespace
{
struct preparation_device : null_device
{
    u64 hash = 1469598103934665603ull;
    usize calls = 0, vertices = 0, indices = 0;
    void bytes(const void *data, usize size)
    {
        const auto *p = static_cast<const u8 *>(data);
        for (usize i = 0; i < size; ++i) hash = (hash ^ p[i]) * 1099511628211ull;
    }
    void draw(texture_handle, const vertex *v, i32 vc, const i32 *idx, i32 ic) override
    {
        ++calls;
        vertices += static_cast<usize>(vc);
        indices += static_cast<usize>(ic);
        bytes(v, static_cast<usize>(vc) * sizeof(vertex));
        bytes(idx, static_cast<usize>(ic) * sizeof(i32));
    }
};
} // namespace

PUI_TEST(preparation_preserves_live_batches_clip_and_exact_transforms)
{
    u64 expected = 0;
    usize calls = 0;
    for (i32 pass = 0; pass < 2; ++pass)
    {
        preparation_device dev;
        context *c = create_context(&dev, dev.create_surface());
        set_violation_handler(c, capture_violation, nullptr);
        const font_handle font = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
        CHECK(font != FONT_INVALID);
        window *w = add_window(c, nullptr, dev.create_surface(), {0, 0, 640, 480});
        set_window_scale(*w, 1.5f);
        begin_frame(c, 1, 1.0 / 60.0, {0, 0, 640, 480});
        CHECK(c->render_scale == 1.5f);
        {
            ui u(c);
            auto clip = u.region({10, 10, 500, 400}, "preparation_clip"_id);
            u.draw_rect({20, 20, 25, 30}, {19, 37, 71, 155});
            const usize vertices = c->dl->verts.size(), indices = c->dl->idx.size();
            const bool solid = c->dl->solid_uv_remap;
            const texture_handle tex = c->dl->cur_tex;
            if (pass)
            {
                CHECK(u.prepare_geometry({.vertices = 65536, .indices = 262144}));
                CHECK(c->dl->verts.size() == vertices && c->dl->idx.size() == indices);
                CHECK(c->dl->cur_tex == tex && c->dl->solid_uv_remap == solid);
                CHECK(dev.calls == 0); // preparation must not flush the pending draw
            }
            {
                auto disabled = u.disabled();
                u.draw_rounded_rect({30.25f, 70.5f, 81.75f, 47.25f}, {77, 99, 121, 191}, 7.5f);
            }
            auto ts = u.text_style(15, font);
            u.text({40, 140, 350, 40}, "Exact UTF-8: \xc3\xa7\xc4\x9f", color::white());
        }
        end_frame(c);
        if (!pass)
        {
            expected = dev.hash;
            calls = dev.calls;
        }
        else
            CHECK(dev.hash == expected && dev.calls == calls);
        CHECK(violation_count(c) == 0);
        destroy_context(c);
    }
}

PUI_TEST(preparation_reuses_storage_across_peak_batches_and_contexts)
{
    tf_env env;
    ui u(env.c);
    CHECK(u.prepare_geometry({.vertices = 1800000, .indices = 8388608}));
    const vertex *storage = env.c->dl->verts.data();
    const i32 *indices = env.c->dl->idx.data();
    CHECK(env.c->dl->verts.empty() && env.c->dl->idx.empty());
    const std::vector<vertex> v(600000, {3, 7, .25f, .5f, {3, 5, 7, 11}});
    const i32 tri[] = {0, 2, 1};
    for (i32 pass = 0; pass < 2; ++pass)
    {
        for (i32 n = 0; n < 3; ++n)
            detail::dl_add(env.c, v.data(), static_cast<i32>(v.size()), tri, 3);
        CHECK(env.c->dl->verts.data() == storage && env.c->dl->idx.data() == indices);
        CHECK(std::memcmp(env.c->dl->verts.data() + 1200000, v.data(), v.size() * sizeof(vertex)) ==
              0);
        CHECK(env.c->dl->idx[6] == 1200000 && env.c->dl->idx[7] == 1200002);
        detail::dl_flush(env.c);
        CHECK(u.prepare_geometry({.vertices = 1800000, .indices = 8388608}));
    }
    null_device other;
    context *c = create_context(&other, other.create_surface());
    CHECK(c->dl->verts.capacity() == 0 && c->dl->idx.capacity() == 0);
    destroy_context(c);
}

PUI_TEST(preparation_validates_both_counts_before_mutating_storage)
{
    tf_env env;
    ui u(env.c);
    CHECK(u.prepare_geometry({}));
    CHECK(env.c->dl->verts.capacity() == 0 && env.c->dl->idx.capacity() == 0);
    const usize invalid = static_cast<usize>(0x7fffffff) + 1;
    CHECK(!u.prepare_geometry({.vertices = 64, .indices = invalid}));
    CHECK(!u.prepare_geometry({.vertices = invalid, .indices = 64}));
    CHECK(env.c->dl->verts.capacity() == 0 && env.c->dl->idx.capacity() == 0);
    CHECK(!env.nd.prepare_geometry({.vertices = 64}));
}

PUI_TEST(preparation_normal_growth_invalidates_only_relocated_prefix)
{
    tf_env env;
    ui u(env.c);
    CHECK(u.prepare_geometry({.vertices = 64, .indices = 64}));
    const std::vector<vertex> v(100, {1, 2, .1f, .2f, {3, 4, 5, 6}});
    const std::vector<i32> idx(100, 0);
    detail::dl_add(env.c, v.data(), 100, idx.data(), 100);
    CHECK(env.c->dl->prepared_vertices == 0 && env.c->dl->prepared_indices == 0);
    CHECK(u.prepare_geometry({.vertices = 120, .indices = 120}));
    CHECK(env.c->dl->verts.size() == 100 && env.c->dl->idx.size() == 100);
    CHECK(std::memcmp(env.c->dl->verts.data(), v.data(), v.size() * sizeof(vertex)) == 0);
    CHECK(env.c->dl->prepared_vertices >= 120 && env.c->dl->prepared_indices >= 120);
}

PUI_TEST(preparation_sdl_retains_initialized_prefix_padding_and_exact_colors)
{
    SDL_Surface *surface = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer *renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    CHECK(renderer != nullptr);
    if (!renderer)
    {
        SDL_DestroySurface(surface);
        return;
    }
    {
        sdl3_device d, other;
        d.primary_renderer = other.primary_renderer = renderer;
        const std::vector<vertex> initial(4096, {1, 1, 0, 0, {11, 22, 33, 44}});
        const i32 tri[] = {0, 1, 2};
        d.draw(nullptr, initial.data(), 4096, tri, 3);
        d.colors_scratch[4095].col = {1, 2, 3, 4};
        d.colors_scratch[4095].pad_ = 42;
        CHECK(d.prepare_geometry({.vertices = 2000000, .indices = 8388608}));
        CHECK(d.colors_scratch[4095].col.r == 1 && d.colors_scratch[4095].pad_ == 42);
        const auto *storage = d.colors_scratch.data();
        std::vector<vertex> v(1800000);
        for (usize i = 0; i < v.size(); ++i)
            v[i] = {1,
                    1,
                    0,
                    0,
                    {static_cast<u8>(i), static_cast<u8>(i >> 8), static_cast<u8>(255 - i % 256),
                     static_cast<u8>(i * 7)}};
        for (i32 count : {1000000, 3, 1400000, 1800000})
        {
            d.draw(nullptr, v.data(), count, tri, 3);
            CHECK(d.colors_scratch.data() == storage && d.colors_scratch.size() == 2000000);
        }
        for (usize i = 0; i < v.size(); ++i)
        {
            const SDL_FColor expected{v[i].c.r / 255.0f, v[i].c.g / 255.0f, v[i].c.b / 255.0f,
                                      v[i].c.a / 255.0f};
            CHECK(std::memcmp(&expected, &d.colors_scratch[i].col, sizeof(expected)) == 0);
        }
        CHECK(d.colors_scratch[4095].pad_ == 42);
        CHECK(other.prepare_geometry({}));
        CHECK(other.colors_scratch.empty());
        CHECK(d.prepare_geometry({.vertices = 64}));
        const usize invalid = static_cast<usize>(0x7fffffff) + 1;
        CHECK(!d.prepare_geometry({.vertices = invalid}));
        CHECK(!d.prepare_geometry({.vertices = 64, .indices = invalid}));
        CHECK(!other.prepare_geometry({.vertices = 64, .indices = invalid}));
        CHECK(other.colors_scratch.empty());
        CHECK(d.colors_scratch.data() == storage && d.colors_scratch.size() == 2000000);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

PUI_TEST(preparation_spiral_budget_is_bounded_and_covers_real_tile_geometry)
{
    const auto zero = lab_geometry_capacity(0), a = lab_geometry_capacity(127),
               b = lab_geometry_capacity(128), c = lab_geometry_capacity(129),
               peak = lab_geometry_capacity(5000),
               huge = lab_geometry_capacity(static_cast<usize>(-1));
    CHECK(zero.vertices == 0 && zero.indices == 0);
    CHECK(a.vertices == b.vertices && a.indices == b.indices);
    CHECK(c.vertices > b.vertices && c.indices > b.indices);
    CHECK(peak.vertices == huge.vertices && peak.indices == huge.indices);
    CHECK(peak.vertices * sizeof(vertex) * 2 + peak.indices * sizeof(i32) <= 128 * 1024 * 1024);
    CHECK(peak.vertices >= 1963939 && peak.indices >= 8351460);
    for (i32 state = 0; state < 3; ++state)
    {
        preparation_device dev;
        context *ctx = create_context(&dev, dev.create_surface());
        set_violation_handler(ctx, capture_violation, nullptr);
        showcase s;
        s.bold = load_font(ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
        s.lab_mode = 4;
        lab_tile tile;
        tile.id = 0xffffffffu;
        s.lab_selected = state == 2 ? tile.id : 0;
        begin_frame(ctx, 1, 1.0 / 60.0, {0, 0, 1000, 1000});
        {
            ui u(ctx);
            const rect r{100, 100, 512, 180};
            soft_shadow(u, r, 16, 18, .8f, 10);
            lab_tile_draw(u, s, tile, r, state == 1, 1);
        }
        end_frame(ctx);
        CHECK(dev.vertices * 128 <= b.vertices && dev.indices * 128 <= b.indices);
        CHECK(violation_count(ctx) == 0);
        destroy_context(ctx);
    }
}

PUI_TEST(preparation_actual_5000_spiral_adds_keep_core_storage_at_the_peaks)
{
    null_device dev;
    dev.my_surface.w = 1360;
    dev.my_surface.h = 860;
    example_app app;
    app.ctx = create_context(&dev, dev.create_surface());
    set_violation_handler(app.ctx, capture_violation, nullptr);
    app.win = add_window(app.ctx, nullptr, &dev.my_surface, {0, 0, 1360, 860});
    app.font = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    app.font_bold = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
    app.font_oblique = load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Oblique.ttf");
    app.width = 1360;
    app.height = 860;
    app.selftest = true;
    focus_window(app.ctx, *app.win);
    showcase saved = std::move(g_app);
    const i32 saved_page = g_start_page;
    const bool saved_hud = g_no_hud;
    g_app = showcase{};
    seed(g_app);
    g_app.lab_mode = 4;
    g_app.lab_size = 64;
    while (g_app.tiles.size() < 5000) add_tile(g_app);
    g_start_page = PAGE_LAYOUT;
    g_no_hud = true;
    mouse_move(*app.win, 800, 220);
    const vertex *vertices = nullptr;
    const i32 *indices = nullptr;
    const geometry_capacity budget = lab_geometry_capacity(5000);
    for (i32 f = 0; f < 180; ++f)
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
        if (!f)
        {
            vertices = app.ctx->dl->verts.data();
            indices = app.ctx->dl->idx.data();
        }
        CHECK(app.ctx->dl->verts.data() == vertices && app.ctx->dl->idx.data() == indices);
        CHECK(app.ctx->dl->prepared_vertices >= budget.vertices);
        CHECK(app.ctx->dl->prepared_indices >= budget.indices);
    }
    CHECK(g_app.tiles.size() == 5003);
    CHECK(violation_count(app.ctx) == 0);
    destroy_context(app.ctx);
    g_app = std::move(saved);
    g_start_page = saved_page;
    g_no_hud = saved_hud;
}

int main()
{
    if (!SDL_Init(0)) return 1;
    for (const auto &test : test_registry()) test.fn();
    SDL_Quit();
    if (!g_failures)
        std::printf("all geometry preparation regressions passed (%zu tests)\n",
                    test_registry().size());
    return g_failures ? 1 : 0;
}
