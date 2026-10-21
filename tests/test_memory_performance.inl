// New r115 regressions; the pre-existing test bodies and pixel oracles stay fixed.
#include "test_util.h"

PUI_TEST(memory_vertex_capacity_uses_checked_integer_arithmetic)
{
    const usize limit = static_cast<usize>(-1);
    CHECK(detail::dl_vertex_capacity(0, 1, limit) == 64);
    CHECK(detail::dl_vertex_capacity(0, 7, 8) == 8);
    CHECK(detail::dl_vertex_capacity(1000000, 1400000, limit) == 2000000);
    CHECK(detail::dl_vertex_capacity(limit / 2 + 1, limit, limit) == limit);
    CHECK(detail::dl_vertex_capacity(0, 9, 8) == 0);
    CHECK(detail::dl_vertex_capacity(9, 1, 8) == 0);
    const usize exact = static_cast<usize>(16777217);
    CHECK(detail::dl_vertex_capacity(0, exact, limit) == exact);
}

PUI_TEST(memory_vertex_growth_is_geometric_and_preserves_stream)
{
    null_device dev;
    context *c = create_context(&dev, dev.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    const vertex v[] = {{1.25f, 2.5f, .1f, .2f, {1, 2, 3, 201}},
                        {3.75f, 4.5f, .3f, .4f, {4, 5, 6, 127}},
                        {5.25f, 6.5f, .5f, .6f, {7, 8, 9, 255}}};
    const i32 indices[] = {2, 0, 1};
    usize growths = 0;
    for (i32 n = 0; n < 10000; ++n)
    {
        const usize previous = c->dl->verts.capacity();
        detail::dl_add(c, v, 3, indices, 3);
        if (previous != c->dl->verts.capacity())
        {
            ++growths;
            CHECK(!previous || c->dl->verts.capacity() >= previous * 2);
        }
        CHECK(std::memcmp(c->dl->verts.data() + n * 3, v, sizeof(v)) == 0);
        for (i32 k = 0; k < 3; ++k) CHECK(c->dl->idx[n * 3 + k] == n * 3 + indices[k]);
    }
    CHECK(growths <= 10);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(memory_vertex_spiral_peak_reuses_first_growth)
{
    null_device dev;
    context *c = create_context(&dev, dev.create_surface());
    set_violation_handler(c, capture_violation, nullptr);
    // Reproduce the two consecutive peak sizes without timing assertions.
    c->dl->verts.reserve(1000000);
    c->dl->verts.resize(1000000);
    const std::vector<vertex> add(400000, vertex{3, 7, .25f, .5f, {3, 5, 7, 11}});
    const i32 tri[] = {0, 2, 1};
    detail::dl_add(c, add.data(), static_cast<i32>(add.size()), tri, 3);
    const vertex *storage = c->dl->verts.data();
    const usize capacity = c->dl->verts.capacity();
    detail::dl_add(c, add.data(), static_cast<i32>(add.size()), tri, 3);
    CHECK(c->dl->verts.size() == 1800000);
    CHECK(c->dl->verts.data() == storage && c->dl->verts.capacity() == capacity);
    CHECK(std::memcmp(c->dl->verts.data() + 1400000, add.data(), add.size() * sizeof(vertex)) == 0);
    CHECK(c->dl->idx[3] == 1400000 && c->dl->idx[4] == 1400002);
    CHECK(violation_count(c) == 0);
    destroy_context(c);
}

PUI_TEST(memory_sdl_color_prefix_retains_storage_and_exact_conversion)
{
    SDL_Surface *surface = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32);
    CHECK(surface != nullptr);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    CHECK(renderer != nullptr);
    if (!renderer)
    {
        SDL_DestroySurface(surface);
        return;
    }
    {
        sdl3_device d, other;
        d.primary_renderer = other.primary_renderer = renderer;
        std::vector<vertex> v(65536);
        for (usize i = 0; i < v.size(); ++i)
            v[i] = {1,
                    1,
                    0,
                    0,
                    {static_cast<u8>(i), static_cast<u8>(i / 256), static_cast<u8>(255 - i % 256),
                     static_cast<u8>(i * 7)}};
        const i32 idx[] = {0, 1, 2};
        d.draw(nullptr, v.data(), static_cast<i32>(v.size()), idx, 3);
        const auto *storage = d.colors_scratch.data();
        d.colors_scratch.back().pad_ = 42;
        d.draw(nullptr, v.data(), 3, idx, 3);
        CHECK(d.colors_scratch.size() == v.size() && d.colors_scratch.data() == storage);
        d.draw(nullptr, v.data(), static_cast<i32>(v.size() - 1), idx, 3);
        CHECK(d.colors_scratch.back().pad_ == 42);
        for (usize i = 0; i < d.colors_scratch.size(); ++i)
        {
            const SDL_FColor expected{v[i].c.r / 255.0f, v[i].c.g / 255.0f, v[i].c.b / 255.0f,
                                      v[i].c.a / 255.0f};
            CHECK(std::memcmp(&d.colors_scratch[i].col, &expected, sizeof(expected)) == 0);
        }
        other.draw(nullptr, v.data(), 3, idx, 3);
        CHECK(other.colors_scratch.size() == 3 && d.colors_scratch.size() == v.size());
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

int main()
{
    if (!SDL_Init(0)) return 1;
    for (const auto &test : test_registry()) test.fn();
    SDL_Quit();
    if (!g_failures)
        std::printf("all memory performance regressions passed (%zu tests)\n",
                    test_registry().size());
    return g_failures ? 1 : 0;
}
