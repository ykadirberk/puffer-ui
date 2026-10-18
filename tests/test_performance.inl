// Allocation/caching regressions, plus identical before/after workloads.
// Included by src/pufferui_impl.cpp only for the internal-test target. This
// inspects caches without adding test-only APIs/counters to the shipped library.
#include <cstdlib>
#include <new>
#include <chrono>
#include <utility>

namespace
{
thread_local bool count_allocations = false;
thread_local size_t allocation_count = 0;
thread_local size_t allocation_bytes = 0;
} // namespace

void *operator new(size_t n)
{
    if (count_allocations)
    {
        ++allocation_count;
        allocation_bytes += n;
    }
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

#include "test_util.h"

namespace
{
using bench_clock = std::chrono::steady_clock;
struct perf_device : null_device
{
    i32 target_creates = 0, blits = 0;
    usize target_bytes = 0;
    texture_handle create_target(i32 w, i32 h) override
    {
        ++target_creates;
        target_bytes += static_cast<usize>(w) * h * 4;
        return null_device::create_target(w, h);
    }
    void blit(texture_handle, const rect *, texture_handle, const rect *) override { ++blits; }
};

struct perf_env
{
    perf_device dev;
    context *c;
    perf_env()
    {
        dev.my_surface.w = 1600;
        dev.my_surface.h = 1000;
        c = create_context(&dev, dev.create_surface());
        set_violation_handler(c, capture_violation, nullptr);
        theme t = default_dark();
        t.font = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
        CHECK(t.font != FONT_INVALID);
        set_theme(c, t);
    }
    ~perf_env()
    {
        CHECK(violation_count(c) == 0);
        destroy_context(c);
    }
    void begin(i32 frame = 0)
    {
        begin_frame(c, frame / 60.0, 1.0 / 60.0, rect::make(0, 0, 1600, 1000));
    }
};

std::pair<std::string, std::string> colliding_lines(context *c)
{
    std::vector<std::string> slots(4096);
    const std::string base(12000, 'A');
    for (i32 i = 0; i < 4097; ++i)
    {
        std::string s = base + std::to_string(i);
        const u64 key = detail::layout_key(c, c->active_theme.font, c->active_theme.text_size, s);
        const usize slot = direct_cache<i32, 4096>::mix(key) & 4095;
        if (!slots[slot].empty()) return {slots[slot], s};
        slots[slot] = std::move(s);
    }
    return {};
}

template <class Fn> void measure(const char *name, i32 iterations, Fn &&fn)
{
    allocation_count = allocation_bytes = 0;
    count_allocations = true;
    const auto start = bench_clock::now();
    for (i32 i = 0; i < iterations; ++i) fn(i);
    const double ms = std::chrono::duration<double, std::milli>(bench_clock::now() - start).count();
    count_allocations = false;
    std::printf("%s iterations=%d ms=%.6f allocations=%zu bytes=%zu\n", name, iterations, ms,
                allocation_count, allocation_bytes);
}

int benchmark()
{
    {
        perf_env e;
        e.begin();
        ui u(e.c);
        measure("cold_indices", 30,
                [&](i32)
                {
                    std::vector<i32>().swap(e.c->dl->idx);
                    for (i32 i = 0; i < 4000; ++i) u.draw_rect({10, 10, 20, 20}, color::white());
                    detail::dl_flush(e.c);
                });
        end_frame(e.c);
    }
    {
        perf_env e;
        e.begin();
        ui u(e.c);
        auto [a, b] = colliding_lines(e.c);
        for (i32 i = 0; i < 4; ++i) u.text({0, 0, 1400, 20}, i % 2 ? a : b, color::white());
        detail::dl_flush(e.c);
        measure("colliding_lines", 200,
                [&](i32 i)
                {
                    u.text({0, 0, 1400, 20}, i % 2 ? a : b, color::white());
                    detail::dl_flush(e.c);
                });
        end_frame(e.c);
    }
    {
        auto cache = new direct_cache<text_store::text_layout, 16>();
        measure("layout_insert", 200,
                [&](i32)
                {
                    text_store::text_layout lay;
                    lay.gs.resize(12000);
                    cache->put(1, std::move(lay));
                });
        delete cache;
    }
    {
        perf_env e;
        e.begin();
        ui u(e.c);
        const std::string label(12000, 'A');
        u.text_ellipsis({0, 0, 100, 20}, label, color::white());
        detail::dl_flush(e.c);
        measure("ellipsis", 200,
                [&](i32)
                {
                    u.text_ellipsis({0, 0, 100, 20}, label, color::white());
                    detail::dl_flush(e.c);
                });
        end_frame(e.c);
    }
    {
        perf_env e;
        e.begin();
        e.c->active_theme.text_gamma = 1.35f;
        measure("gamma_glyphs", 100,
                [&](i32 i)
                {
                    for (u32 cp = 'A'; cp <= 'Z'; ++cp)
                        (void)detail::get_glyph(e.c, e.c->active_theme.font, 14.0f + i * 0.25f, cp);
                });
        end_frame(e.c);
    }
    {
        perf_env e;
        const char *headers[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L"};
        const auto frame = [&](i32 f)
        {
            e.begin(f);
            ui u(e.c);
            comp::table(u, {0, 0, 1500, 800}, headers, 20, [](ui &uu, rect r, i32, i32)
                        { uu.draw_rect(r.pad(4), color::white()); }, {.id = "bench_table"_id});
            end_frame(e.c);
        };
        for (i32 i = 0; i < 5; ++i) frame(i);
        measure("table", 200, [&](i32 i) { frame(i + 5); });
    }
    {
        perf_env e;
        e.begin();
        ui u(e.c);
        u.blur({100, 100, 100, 100}, 2, 0, 1);
        end_frame(e.c);
        std::printf("blur targets=%d bytes=%zu blits=%d\n", e.dev.target_creates,
                    e.dev.target_bytes, e.dev.blits);
    }
    return g_failures ? 1 : 0;
}
} // namespace

PUI_TEST(test_perf_index_growth_and_exact_indices)
{
    perf_env e;
    e.begin();
    ui u(e.c);
    usize growths = 0;
    for (i32 i = 0; i < 4000; ++i)
    {
        const usize capacity = e.c->dl->idx.capacity();
        u.draw_rect({10, 10, 20, 20}, color::white());
        growths += e.c->dl->idx.capacity() != capacity;
    }
    CHECK(growths < 40);
    CHECK(e.c->dl->idx.size() == 24000);
    CHECK(e.c->dl->verts.size() == 16000);
    constexpr i32 quad[] = {0, 1, 2, 0, 2, 3};
    for (usize i = 0; i < e.c->dl->idx.size(); ++i)
        CHECK(e.c->dl->idx[i] == static_cast<i32>(i / 6 * 4) + quad[i % 6]);
    end_frame(e.c);
}

PUI_TEST(test_perf_cache_transfers_storage)
{
    direct_cache<std::vector<i32>, 8> cache;
    u64 keys[3]{};
    usize count = 0;
    for (u64 k = 1; count < 3; ++k)
        if ((decltype(cache)::mix(k) & 7) == 0) keys[count++] = k;
    std::vector<i32> data(12000, 42);
    const i32 *storage = data.data();
    cache.put(keys[0], std::move(data));
    CHECK(cache.find(keys[0])->data() == storage);
    CHECK(data.empty());
    cache.put(keys[1], std::vector<i32>{7});
    CHECK(cache.find(keys[0]) == nullptr);
    cache.put(keys[2], std::vector<i32>{9});
    CHECK(cache.find(keys[0]) == nullptr);
    CHECK(cache.find(keys[1]) == nullptr);
    CHECK(cache.find(keys[2])->front() == 9);
    const std::vector<i32> copy{11};
    cache.put(keys[2], copy); // lvalue insertion still copies
    CHECK(cache.find(keys[2])->front() == 11);
    CHECK(copy.front() == 11);
}

namespace
{
struct counting_shaper : text_shaper
{
    i32 calls = 0;
    bool shape(context *, font_handle, f32, std::string_view,
               function_ref<void(const shaped_glyph &)>) override
    {
        ++calls;
        return false;
    }
};

struct substituting_shaper : text_shaper
{
    bool shape(context *, font_handle, f32, std::string_view text,
               function_ref<void(const shaped_glyph &)> emit) override
    {
        usize i = 0;
        while (i < text.size())
        {
            const u32 cp = utf8_decode(text, i);
            if (cp == UTF8_END) break;
            emit({cp == 'A' ? 'W' : cp, cp == 'V' ? -3.0f : 0.0f});
        }
        return true;
    }
};

// The pre-optimization ellipsis algorithm, deliberately independent of the
// optimized run/cut caches. It also defines the custom-shaper fallback.
std::string reference_ellipsis(ui &u, std::string_view s, f32 width)
{
    if (u.text_width(s) <= width) return std::string(s);
    const f32 tail = u.text_width("\xE2\x80\xA6");
    usize i = 0, cut = 0;
    f32 x = 0;
    u32 prev = 0;
    while (i < s.size())
    {
        const u32 cp = utf8_decode(s, i);
        if (cp == UTF8_END) break;
        x += detail::kern_advance(u.ctx, u.th().font, u.th().text_size, prev, cp);
        x += detail::get_glyph(u.ctx, u.th().font, u.th().text_size, cp).advance;
        if (x + tail <= width) cut = i;
        prev = cp;
    }
    return std::string(s.substr(0, cut)) + "\xE2\x80\xA6";
}
} // namespace

PUI_TEST(test_perf_colliding_layouts_and_invalidation)
{
    perf_env e;
    counting_shaper shaper;
    const auto [a, b] = colliding_lines(e.c);
    e.begin();
    ui u(e.c);
    const auto draw = [&](i32 i)
    {
        u.text({0, 0, 1400, 20}, i % 2 ? a : b, color::white());
        detail::dl_flush(e.c);
    };
    for (i32 i = 0; i < 4; ++i) draw(i);
    allocation_count = allocation_bytes = 0;
    count_allocations = true;
    for (i32 i = 0; i < 20; ++i) draw(i);
    count_allocations = false;
    // Debug STL iterator proxies may allocate during vector ownership moves;
    // rebuilding either 12k-glyph run would allocate several megabytes.
    CHECK(allocation_bytes < 4096);
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
    CHECK(allocation_count == 0);
#endif
    set_text_shaper(e.c, &shaper);
    (void)u.text_width(a);
    CHECK(shaper.calls == 1);
    set_font_fallbacks(e.c, {});
    (void)u.text_width(a);
    CHECK(shaper.calls == 2);
    set_text_shaper(e.c, &shaper);
    (void)u.text_width(b);
    CHECK(shaper.calls == 3);
    end_frame(e.c);
    set_text_shaper(e.c, nullptr);
}

PUI_TEST(test_perf_ellipsis_matches_reference)
{
    perf_env e;
    vertex_log_device log;
    e.c->device = &log;
    counting_shaper fallback;
    substituting_shaper shaped;
    e.begin();
    ui u(e.c);
    const char *texts[] = {"AV To ffi a long label", "\xC3\xA7\xC4\x9F\xC5\x9F AV long text", "",
                           "W", "malformed \xE2\x80"};
    for (i32 mode = 0; mode < 3; ++mode)
    {
        text_shaper *shaper = mode == 0   ? nullptr
                              : mode == 1 ? static_cast<text_shaper *>(&fallback)
                                          : static_cast<text_shaper *>(&shaped);
        set_text_shaper(e.c, shaper);
        for (f32 size : {14.0f, 14.03125f, 14.0625f, 21.0f})
        {
            e.c->active_theme.text_size = size;
            for (const char *text : texts)
                for (f32 width : {0.0f, 12.0f, 31.25f, 99.0f, 700.0f})
                    for (align a : {ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT})
                    {
                        const rect r{11.25f, 20, width, 30};
                        const std::string expected = reference_ellipsis(u, text, width);
                        // Warm subpixel variants before comparing UVs too.
                        u.text(r, expected, color::white(), a);
                        detail::dl_flush(e.c);
                        log.log.clear();
                        u.text(r, expected, color::white(), a);
                        detail::dl_flush(e.c);
                        const auto vertices = log.log;
                        for (i32 repeat = 0; repeat < 2; ++repeat)
                        {
                            log.log.clear();
                            u.text_ellipsis(r, text, color::white(), a);
                            detail::dl_flush(e.c);
                            CHECK(log.log.size() == vertices.size());
                            if (log.log.size() == vertices.size() && !vertices.empty())
                                CHECK(std::memcmp(log.log.data(), vertices.data(),
                                                  vertices.size() * sizeof(vertex)) == 0);
                        }
                    }
        }
    }
    end_frame(e.c);
    set_text_shaper(e.c, nullptr);
    e.c->device = &e.dev;
}

PUI_TEST(test_perf_gamma_exact_raw_values)
{
    perf_env e;
    for (f32 gamma : {-1.0f, 1.0f, 1.351f, 1.354f, 1.351f, 0.05f, 0.1f, 2.0f, 1.0f})
    {
        const u8 *actual = detail::coverage_curve(e.c->ts, gamma);
        const f32 inv = 1.0f / max2(0.1f, gamma);
        for (i32 v = 0; v < 256; ++v)
        {
            const u8 expected =
                static_cast<i32>(gamma * 100.0f + 0.5f) == 100
                    ? static_cast<u8>(v)
                    : static_cast<u8>(std::pow(static_cast<f32>(v) / 255.0f, inv) * 255.0f + 0.5f);
            CHECK(actual[v] == expected);
        }
        CHECK(detail::coverage_curve(e.c->ts, gamma) == actual);
    }
}

PUI_TEST(test_perf_cache_preserves_quantized_size_history)
{
    perf_env e;
    e.c->active_theme.text_size = 14.0f;
    std::string base;
    for (i32 i = 0; i < 6000; ++i) base += "AV";
    std::vector<std::string> slots(4096);
    std::string a, b;
    for (i32 i = 0; b.empty(); ++i)
    {
        std::string s = base + std::to_string(i);
        const auto key = detail::layout_key(e.c, e.c->active_theme.font, 14.0f, s);
        const usize slot = direct_cache<i32, 4096>::mix(key) & 4095;
        if (!slots[slot].empty())
        {
            a = slots[slot];
            b = std::move(s);
        }
        else
            slots[slot] = std::move(s);
    }
    e.begin();
    ui u(e.c);
    (void)u.text_width(a);
    e.c->active_theme.text_size = 14.125f; // same quantized key, different kerning
    (void)u.text_width(b);                 // evicts a in the original direct-mapped cache
    const f32 expected = detail::build_layout(e.c, u.th().font, u.th().text_size, a).width;
    CHECK(u.text_width(a) == expected);
    end_frame(e.c);
}

PUI_TEST(test_perf_nested_tables_reuse_storage)
{
    perf_env e;
    const char *headers[40];
    // Keep labels inside their columns: MSVC's debug string iterator proxies
    // allocate for an ellipsis temporary, independently of table storage.
    for (auto &h : headers) h = "A";
    const char *nested_headers[] = {"", ""};
    const auto frame = [&](i32 f)
    {
        e.begin(f);
        ui u(e.c);
        comp::table(u, {0, 0, 1500, 800}, headers, 2,
                    [&](ui &uu, rect r, i32 row, i32 col)
                    {
                        if (row == 0 && col == 0)
                        {
                            const auto &outer = e.c->st->tables;
                            const auto *ptr = outer.shown.data();
                            const f32 last = outer.xs.back();
                            comp::table(uu, r, nested_headers, 0, {},
                                        {.id = uu.local("inner"), .header_h = 12});
                            CHECK(outer.shown.data() == ptr);
                            CHECK(outer.shown.size() == 40);
                            CHECK(outer.xs.back() == last);
                        }
                    },
                    {.id = "outer"_id});
        CHECK(e.c->st->table_active == nullptr);
        end_frame(e.c);
    };
    for (i32 f = 0; f < 4; ++f) frame(f);
    allocation_count = 0;
    count_allocations = true;
    frame(4);
    count_allocations = false;
    CHECK(allocation_count == 0);
}

PUI_TEST(test_perf_blur_lazy_levels_resize_and_failure)
{
    struct target_device : perf_device
    {
        texture_handle current = nullptr;
        i32 clears = 0, destroyed = 0, fail_width = -1;
        texture_handle create_target(i32 w, i32 h) override
        {
            if (w == fail_width) return nullptr;
            return perf_device::create_target(w, h);
        }
        void set_target(texture_handle t) override { current = t; }
        texture_handle current_target() const override { return current; }
        void clear(color c) override
        {
            CHECK(c.r == 0 && c.g == 0 && c.b == 0 && c.a == 0);
            ++clears;
        }
        void destroy_target(texture_handle) override { ++destroyed; }
    } dev;
    perf_env e;
    e.c->device = &dev;
    const auto original = reinterpret_cast<texture_handle>(static_cast<usize>(42));
    dev.current = original;
    auto *bs = detail::ensure_blur(e.c, 641, 361, 2);
    CHECK(bs && bs->half && bs->quarter && !bs->eighth && !bs->sixteenth);
    CHECK(dev.target_creates == 2 && dev.clears == 2 && dev.current == original);
    (void)detail::ensure_blur(e.c, 641, 361, 4);
    CHECK(dev.target_creates == 2 && dev.clears == 2);
    (void)detail::ensure_blur(e.c, 641, 361, 6);
    CHECK(bs->eighth && !bs->sixteenth && dev.target_creates == 3);
    (void)detail::ensure_blur(e.c, 641, 361, 24);
    CHECK(bs->sixteenth && dev.target_creates == 4 && dev.clears == 4);
    (void)detail::ensure_blur(e.c, 641, 361, 2);
    CHECK(dev.target_creates == 4 && dev.clears == 4);
    (void)detail::ensure_blur(e.c, 801, 601, 2);
    CHECK(dev.destroyed == 4 && dev.target_creates == 6);
    CHECK(!bs->eighth && !bs->sixteenth);
    dev.fail_width = 101; // eighth at 801 pixels wide
    (void)detail::ensure_blur(e.c, 801, 601, 6);
    CHECK(!bs->eighth && dev.current == original);
    dev.fail_width = -1;
    (void)detail::ensure_blur(e.c, 801, 601, 6);
    CHECK(bs->eighth && dev.current == original);
    detail::free_blur_store(e.c, e.c->blur);
    e.c->device = &e.dev;
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0) return benchmark();
    for (const test_entry &t : test_registry()) t.fn();
    if (g_failures) return 1;
    std::printf("all performance regression tests passed (%zu tests)\n", test_registry().size());
    return 0;
}
