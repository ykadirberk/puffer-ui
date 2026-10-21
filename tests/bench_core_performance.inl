// Compiled with the real implementation, before and after the core fixes.
// No timing assertions: retain every run and compare matching release builds.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace
{
thread_local bool core_track_allocations = false;
thread_local size_t core_allocations = 0;
volatile double core_bench_sink = 0;
} // namespace

void *operator new(size_t n)
{
    if (core_track_allocations) ++core_allocations;
    if (void *p = std::malloc(n ? n : 1)) return p;
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

namespace
{
using namespace pui;
struct core_bench_env
{
    null_device dev;
    context *c;
    core_bench_env()
    {
        dev.my_surface.w = 1600;
        dev.my_surface.h = 1000;
        c = create_context(&dev, dev.create_surface());
        theme t = default_dark();
        t.font = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
        if (t.font == FONT_INVALID) std::abort();
        set_theme(c, t);
    }
    ~core_bench_env()
    {
        if (violation_count(c)) std::abort();
        destroy_context(c);
    }
    void begin(i32 f) { begin_frame(c, f / 60.0, 1.0 / 60.0, {0, 0, 1600, 1000}); }
};
template <class Fn> void core_measure(const char *name, i32 iterations, Fn &&fn)
{
    core_allocations = 0;
    core_track_allocations = true;
    const auto start = std::chrono::steady_clock::now();
    for (i32 i = 0; i < iterations; ++i) fn(i);
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    core_track_allocations = false;
    std::printf("%s,%d,%.9f,%.6f\n", name, iterations, ms / iterations,
                static_cast<double>(core_allocations) / iterations);
}
} // namespace

int main()
{
    std::puts("case,iterations,ms_per_iteration,allocations_per_iteration");
    {
        core_bench_env e;
        e.begin(0);
        std::string text;
        for (i32 i = 0; i < 1000; ++i) text += "layout rect engine draws every frame ";
        (void)detail::build_layout(e.c, e.c->active_theme.font, 14, text);
        core_measure("layout_37000", 40,
                     [&](i32)
                     {
                         auto layout = detail::build_layout(e.c, e.c->active_theme.font, 14, text);
                         core_bench_sink = layout.width;
                     });
        end_frame(e.c);
    }
    for (bool moving : {false, true})
    {
        core_bench_env e;
        const auto frame = [&](i32 f)
        {
            e.begin(f);
            ui u(e.c);
            for (i32 i = 0; i < 5000; ++i)
            {
                const rect target{static_cast<f32>(i % 100) + (moving ? (f % 2) * 400.0f : 0),
                                  static_cast<f32>(i % 70), 30, 20};
                core_bench_sink = u.animate_rect(i + 1, target).x;
            }
            end_frame(e.c);
        };
        for (i32 f = 0; f < 8; ++f) frame(f);
        core_measure(moving ? "motion_moving_5000" : "motion_still_5000", 300,
                     [&](i32 f) { frame(f + 8); });
    }
    {
        core_bench_env e;
        e.begin(0);
        ui u(e.c);
        (void)u.text_width("seed");
        const auto frame = [&](i32)
        {
            for (i32 i = 0; i < 5000; ++i)
                u.draw_sector({30.0f + (i % 50) * 30.0f, 30.0f + (i % 30) * 30.0f}, 10, 12, 0,
                              PI * 0.5f, color::white());
            detail::dl_flush(e.c);
        };
        frame(0);
        core_measure("sectors_5000", 150, frame);
        end_frame(e.c);
    }
    for (i32 count : {5000, 100000})
    {
        core_bench_env e;
        for (i32 i = 0; i < count; ++i) e.c->st->rects.map[i].last_used = 2;
        begin_frame(e.c, 2.0, 1.0 / 60.0, {0, 0, 1600, 1000});
        end_frame(e.c);
        core_measure(count == 5000 ? "state_live_5000" : "state_live_100000", 300,
                     [&](i32 f)
                     {
                         begin_frame(e.c, 2.1 + f / 1000.0, 1.0 / 60.0, {0, 0, 1600, 1000});
                         end_frame(e.c);
                         core_bench_sink = static_cast<double>(e.c->st->rects.map.size());
                     });
    }
    {
        flat_map<u8> map;
        for (i32 i = 0; i < 200000; ++i) map.at(i) = 1;
        map.clear();
        core_measure("empty_ids_after_200000", 1000,
                     [&](i32)
                     {
                         map.clear();
                         core_bench_sink = static_cast<double>(map.used);
                     });
    }
    {
        core_bench_env e;
        std::string text;
        for (i32 i = 0; i < 1000; ++i) text += "layout rect cutting\n";
        const auto frame = [&](i32 f)
        {
            e.begin(f);
            ui u(e.c);
            u.text_area({10, 10, 1400, 800}, text, "gutter_bench"_id,
                        {.wrap = false, .read_only = true, .line_numbers = true});
            end_frame(e.c);
        };
        for (i32 f = 0; f < 8; ++f) frame(f);
        core_measure("text_area_gutter", 180, [&](i32 f) { frame(f + 8); });
    }
}
