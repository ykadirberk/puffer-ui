// New regressions only. The pre-fix algorithms below are independent oracles;
// existing test bodies and pixel references are left unchanged.
#include <limits>

namespace
{
bool core_same_float(f32 a, f32 b)
{
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

bool core_reference_spring(f32 &x, f32 &v, f32 target, f32 frame_dt, const spring &spec, f32 eps_x,
                           f32 eps_v)
{
    const f32 m = max2(spec.mass, 0.001f);
    const f32 omega = std::sqrt(max2(spec.stiffness, 0.0f) / m);
    const f32 dt = clampf(frame_dt, 0.0f, 1.0f);
    const f32 max_step = min2(1.0f / 120.0f, 0.5f / max2(omega, 1.0f));
    const i32 steps = static_cast<i32>(clampf(std::ceil(dt / max_step), 1.0f, 480.0f));
    const f32 step = dt / static_cast<f32>(steps);
    const f32 d = 2.0f * spec.damping_ratio * std::sqrt(spec.stiffness * m);
    for (i32 i = 0; i < steps; ++i)
    {
        const f32 accel = (spec.stiffness * (target - x) - d * v) / m;
        v += accel * step;
        x += v * step;
    }
    if (std::fabs(target - x) < eps_x && std::fabs(v) < eps_v)
    {
        x = target;
        v = 0;
        return true;
    }
    return false;
}

text_store::text_layout core_reference_layout(context *c, font_handle fh, f32 size,
                                              std::string_view text)
{
    text_store::text_layout out;
    out.builtin = true;
    out.built_size = size;
    const i32 epoch = c->ts->recycled;
    usize at = 0;
    u32 previous = 0;
    while (at < text.size())
    {
        const u32 cp = utf8_decode(text, at);
        if (cp == UTF8_END) break;
        text_store::cached_glyph g;
        g.cp = cp;
        g.kern_in = detail::kern_advance(c, fh, size, previous, cp);
        g.g = detail::get_glyph(c, fh, size, cp);
        out.width += g.kern_in;
        out.width += g.g.advance;
        if (!out.gs.empty() && out.gs.back().g.advance + g.kern_in < 0) out.forward = false;
        if (g.g.w > 0) out.min_xoff = min2(out.min_xoff, g.g.xoff);
        out.gs.push_back(g);
        previous = cp;
    }
    out.epoch = out.build_epoch = epoch;
    return out;
}

void core_check_layout(const text_store::text_layout &a, const text_store::text_layout &b)
{
    CHECK(core_same_float(a.width, b.width));
    CHECK(core_same_float(a.min_xoff, b.min_xoff));
    CHECK(a.forward == b.forward && a.builtin == b.builtin);
    CHECK(a.gs.size() == b.gs.size());
    if (a.gs.size() != b.gs.size()) return;
    for (usize i = 0; i < a.gs.size(); ++i)
    {
        const auto &x = a.gs[i];
        const auto &y = b.gs[i];
        CHECK(x.cp == y.cp && core_same_float(x.kern_in, y.kern_in));
        CHECK(x.g.tex == y.g.tex && x.g.page == y.g.page && x.g.gen == y.g.gen);
        CHECK(core_same_float(x.g.advance, y.g.advance));
        CHECK(core_same_float(x.g.xoff, y.g.xoff) && core_same_float(x.g.yoff, y.g.yoff));
        CHECK(core_same_float(x.g.w, y.g.w) && core_same_float(x.g.h, y.g.h));
        CHECK(core_same_float(x.g.u0, y.g.u0) && core_same_float(x.g.v0, y.g.v0));
        CHECK(core_same_float(x.g.u1, y.g.u1) && core_same_float(x.g.v1, y.g.v1));
    }
}

void core_reference_sector(ui &u, vec2 center, f32 r_in, f32 r_out, f32 a0, f32 a1, color c)
{
    if (a1 < a0) std::swap(a0, a1);
    if (a1 - a0 <= 0 || r_out <= 0 || r_out <= r_in) return;
    if (detail::outside_paint_feathered(u.ctx,
                                        {center.x - r_out, center.y - r_out, r_out * 2, r_out * 2}))
        return;
    const f32 mid = (r_in + r_out) * 0.5f;
    const i32 seg = static_cast<i32>(max2(3.0f, std::ceil((a1 - a0) * max2(2.0f, mid) * 0.25f)));
    color fade = c;
    fade.a = 0;
    struct ring
    {
        f32 r;
        color col;
    };
    ring rings[4];
    i32 rc = 0;
    if (r_in > 1) rings[rc++] = {r_in - 1, fade};
    rings[rc++] = {r_in, c};
    rings[rc++] = {r_out, c};
    rings[rc++] = {r_out + 1, fade};
    std::vector<vertex> verts;
    std::vector<i32> idx;
    const f32 step = (a1 - a0) / static_cast<f32>(seg);
    const f32 cs = std::cos(step), sn = std::sin(step);
    for (i32 ri = 0; ri < rc; ++ri)
    {
        f32 x = std::cos(a0) * rings[ri].r, y = std::sin(a0) * rings[ri].r;
        for (i32 s = 0; s <= seg; ++s)
        {
            verts.push_back({center.x + x, center.y + y, 0, 0, rings[ri].col});
            const f32 nx = x * cs - y * sn;
            y = x * sn + y * cs;
            x = nx;
        }
    }
    for (i32 ri = 0; ri < rc - 1; ++ri)
    {
        const i32 r0 = ri * (seg + 1), r1 = (ri + 1) * (seg + 1);
        for (i32 s = 0; s < seg; ++s)
        {
            const i32 i0 = r0 + s, i1 = r0 + s + 1, o0 = r1 + s, o1 = r1 + s + 1;
            for (i32 i : {i0, o0, o1, i0, o1, i1}) idx.push_back(i);
        }
    }
    u.draw_triangles(nullptr, verts.data(), static_cast<i32>(verts.size()), idx.data(),
                     static_cast<i32>(idx.size()));
}
} // namespace

PUI_TEST(core_perf_long_layout_preserves_metrics_and_shaper_paths)
{
    perf_env e;
    e.begin();
    std::string text;
    for (i32 i = 0; i < 80; ++i) text += "AV To ffi \xC3\xA7\xC4\x9F\xC5\x9F \xE4\xB8\xAD ";
    const font_handle bold = load_font(e.c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
    CHECK(bold != FONT_INVALID);
    set_font_fallbacks(e.c, std::span(&bold, 1));
    for (f32 size : {14.0f, 14.03125f, 14.24f, 21.5f})
        for (f32 density : {1.0f, 1.75f})
        {
            e.c->render_scale = density;
            const auto a = detail::build_layout(e.c, e.c->active_theme.font, size, text);
            const auto b = core_reference_layout(e.c, e.c->active_theme.font, size, text);
            core_check_layout(a, b);
        }
    e.c->render_scale = 1;
    e.c->st->layout_pairs.clear();
    (void)detail::build_layout(e.c, e.c->active_theme.font, 14, "short");
    for (u32 a : e.c->st->layout_pairs.a) CHECK(a == UTF8_END);
    counting_shaper declining;
    substituting_shaper substituting;
    set_text_shaper(e.c, &declining);
    (void)detail::build_layout(e.c, e.c->active_theme.font, 14, text);
    CHECK(declining.calls == 1);
    for (u32 a : e.c->st->layout_pairs.a) CHECK(a == UTF8_END);
    set_text_shaper(e.c, &substituting);
    const auto shaped = detail::build_layout(e.c, e.c->active_theme.font, 14, text);
    CHECK(!shaped.builtin && shaped.gs.front().cp == 'W');
    for (u32 a : e.c->st->layout_pairs.a) CHECK(a == UTF8_END);
    set_text_shaper(e.c, nullptr);
    (void)detail::build_layout(e.c, e.c->active_theme.font, 14, text);
    bool populated = false;
    for (u32 a : e.c->st->layout_pairs.a) populated = populated || a != UTF8_END;
    CHECK(populated);
    end_frame(e.c);
}

PUI_TEST(core_perf_spring_plan_matches_original_with_hitches)
{
    constexpr f32 times[] = {0, 0.0001f, 0.016f, 0.1f, 0.35f, 1.7f};
    for (i32 n = 0; n < 20000; ++n)
    {
        spring spec;
        spec.stiffness = 10.0f + (n % 17) * 40.0f;
        spec.mass = 0.1f + (n % 9) * 0.3f;
        spec.damping_ratio = (n % 11) * 0.15f;
        f32 x = (n % 79) * 0.125f, v = (n % 41) * 0.75f;
        f32 expected_x = x, expected_v = v;
        const f32 target = (n % 31) * -2.3f, dt = times[n % 6];
        const bool expected =
            core_reference_spring(expected_x, expected_v, target, dt, spec, 0.05f, 0.5f);
        const bool actual = detail::spring_plan(dt, spec).advance(x, v, target, spec, 0.05f, 0.5f);
        CHECK(actual == expected);
        CHECK(core_same_float(x, expected_x) && core_same_float(v, expected_v));
    }
}

PUI_TEST(core_perf_rect_motion_matches_original_5000_items)
{
    perf_env e;
    struct axes
    {
        f32 x[4]{}, v[4]{};
    };
    std::vector<axes> expected(5000);
    for (i32 f = 0; f < 180; ++f)
    {
        e.begin(f);
        ui u(e.c);
        e.c->reduced_motion = f == 150;
        for (i32 i = 0; i < 5000; ++i)
        {
            rect_motion m;
            m.origin = {static_cast<f32>(f % 7) * 0.125f, -static_cast<f32>(f % 13) * 0.25f};
            m.spec = {120.0f + (f % 3) * 90.0f, 0.7f + (i % 4) * 0.1f, 0.8f + (i % 3) * 0.25f};
            m.snap = f == 117;
            const rect target{m.origin.x + (i % 100) * 3.1f + (f / 40) * 17.3f,
                              m.origin.y + (i % 70) * 2.3f, 30.0f + i % 7, 20.0f + i % 5};
            const f32 t[] = {target.x - m.origin.x, target.y - m.origin.y, target.w, target.h};
            axes &a = expected[static_cast<usize>(i)];
            if (f == 0) std::memcpy(a.x, t, sizeof(t));
            bool settled = true;
            for (i32 axis = 0; axis < 4; ++axis)
            {
                if (m.snap || e.c->reduced_motion)
                {
                    a.x[axis] = t[axis];
                    a.v[axis] = 0;
                }
                else
                    settled =
                        core_reference_spring(a.x[axis], a.v[axis], t[axis],
                                              static_cast<f32>(e.c->dt), m.spec, 0.05f, 0.5f) &&
                        settled;
            }
            const rect actual = u.animate_rect(i + 1, target, m);
            const auto &entry = e.c->st->rects.map.at(detail::anim_scoped_id(e.c, i + 1, false));
            CHECK(std::memcmp(a.x, entry.x, sizeof(a.x)) == 0);
            CHECK(std::memcmp(a.v, entry.v, sizeof(a.v)) == 0);
            CHECK(entry.settled == settled && entry.last_used == e.c->now);
            CHECK(core_same_float(actual.x, m.origin.x + a.x[0]));
            CHECK(core_same_float(actual.y, m.origin.y + a.x[1]));
        }
        end_frame(e.c);
    }
    e.begin(181);
    ui u(e.c);
    // An overflowing damping coefficient must keep the original NaN behavior,
    // rather than erroneously taking the exact-equilibrium fast path.
    rect_motion extreme;
    extreme.spec = {std::numeric_limits<f32>::max(), 1, std::numeric_limits<f32>::max()};
    (void)u.animate_rect("extreme"_id, {10, 10, 30, 20}, extreme);
    CHECK(std::isnan(e.c->st->rects.map.at(detail::anim_scoped_id(e.c, "extreme"_id, false)).x[0]));
    end_frame(e.c);
}

PUI_TEST(core_perf_sector_cache_preserves_original_stream_and_stays_bounded)
{
    perf_env e;
    e.begin();
    ui u(e.c);
    (void)u.text_width("seed");
    for (f32 density : {1.0f, 1.75f})
        for (i32 disabled : {0, 1})
            for (i32 n = 0; n < 64; ++n)
            {
                e.c->render_scale = density;
                e.c->disabled_depth = disabled;
                e.c->disabled_alpha = 0.45f;
                const f32 inner = n % 4 ? static_cast<f32>(n % 12) : 0;
                const f32 outer = inner + 1.0f + n % 9;
                const f32 a0 = (n % 4) * PI * 0.5f;
                const f32 a1 = a0 + (n % 5 ? PI * 0.5f : PI * 2);
                u.draw_sector({200, 200}, inner, outer, a0, a1, {13, 70, 230, 180});
                const auto vertices = e.c->dl->verts;
                const auto indices = e.c->dl->idx;
                detail::dl_flush(e.c);
                core_reference_sector(u, {200, 200}, inner, outer, a0, a1, {13, 70, 230, 180});
                CHECK(indices == e.c->dl->idx);
                CHECK(vertices.size() == e.c->dl->verts.size());
                CHECK(std::memcmp(vertices.data(), e.c->dl->verts.data(),
                                  vertices.size() * sizeof(vertex)) == 0);
                detail::dl_flush(e.c);
            }
    e.c->render_scale = 1;
    e.c->disabled_depth = 0;
    for (i32 seg : {3, 11, 128, 129, 1024})
        for (i32 rings : {3, 4})
        {
            const auto indices = detail::sector_indices(e.c, seg, rings);
            CHECK(indices.size() == static_cast<usize>((rings - 1) * seg * 6));
            for (i32 i : indices) CHECK(i >= 0 && i < rings * (seg + 1));
        }
    usize capacity = 0;
    for (const auto &slot : e.c->st->sectors)
    {
        CHECK(slot.segments <= 128);
        capacity += slot.indices.capacity();
    }
    CHECK(capacity <= 8 * 128 * 3 * 6 * 2);
    (void)detail::sector_indices(e.c, 3, 4);
    allocation_count = 0;
    count_allocations = true;
    for (i32 i = 0; i < 100; ++i) (void)detail::sector_indices(e.c, 3, 4);
    count_allocations = false;
    CHECK(allocation_count == 0);
    end_frame(e.c);
}

PUI_TEST(core_perf_state_sweep_preserves_exact_expiry_and_skips_live_scans)
{
    struct counted_map : std::unordered_map<uiid, rect_store::entry>
    {
        i32 scans = 0;
        auto begin()
        {
            ++scans;
            return std::unordered_map<uiid, rect_store::entry>::begin();
        }
    } map;
    sweep_clock clock;
    map[1].last_used = 0;
    sweep_unused(map, 0, 5, &clock);
    CHECK(map.scans == 1);
    sweep_unused(map, 5, 5, &clock);
    CHECK(map.scans == 1 && map.size() == 1);
    sweep_unused(map, std::nextafter(5.0, 6.0), 5, &clock);
    CHECK(map.scans == 2 && map.empty());
    std::unordered_map<uiid, rect_store::entry> reference;
    for (i32 f = 0; f < 400; ++f)
    {
        const f64 now = f < 200 ? f * 0.17 : (f - 200) * 0.23; // clock moves backwards
        const f64 ttl = f % 31 ? 5.0 : 2.0;
        map[f % 67].last_used = now;
        reference[f % 67].last_used = now;
        if (f == 250)
        {
            map[1000].last_used = reference[1000].last_used = now + 100;
        }
        sweep_unused(map, now, ttl, &clock);
        for (auto it = reference.begin(); it != reference.end();)
            if (now - it->second.last_used > ttl)
                it = reference.erase(it);
            else
                ++it;
        CHECK(map.size() == reference.size());
        for (const auto &[id, entry] : reference)
        {
            CHECK(map.find(id) != map.end());
            CHECK(map.at(id).last_used == entry.last_used);
        }
    }
    map[2000].last_used = std::numeric_limits<f64>::quiet_NaN();
    sweep_unused(map, std::numeric_limits<f64>::infinity(), 5, &clock);
    CHECK(map.find(2000) != map.end());
    sweep_unused(map, -1, 5, &clock);
    CHECK(clock.valid);
    map.clear();
    sweep_unused(map, 100, 5, &clock);
    map[7].last_used = 101;
    sweep_unused(map, 106, 5, &clock);
    CHECK(map.find(7) != map.end());
    sweep_unused(map, std::nextafter(106.0, 107.0), 5, &clock);
    CHECK(map.empty());
}

PUI_TEST(core_perf_empty_id_clear_preserves_capacity_and_duplicate_lookup)
{
    flat_map<u8> ids;
    ids.clear();
    for (uiid i = 0; i < 200000; ++i) ids.at(i) = 1;
    CHECK(ids.find(0) && *ids.find(199999) == 1);
    const usize capacity = ids.slots.size();
    ids.clear();
    for (i32 f = 0; f < 100; ++f)
    {
        ids.clear();
        CHECK(ids.used == 0 && ids.slots.size() == capacity && !ids.find(1));
    }
    for (uiid i : {0ull, 1ull, 199999ull}) ids.at(i) = 2;
    CHECK(ids.used == 3 && *ids.find(0) == 2 && *ids.find(199999) == 2);
    ids.at(1) = 3;
    CHECK(ids.used == 3 && *ids.find(1) == 3);
    ids.clear();
    CHECK(!ids.find(0));
}

PUI_TEST(core_perf_text_area_numbers_reuse_scratch_and_support_nesting)
{
    perf_env e;
    std::string text;
    for (i32 i = 0; i < 1000; ++i) text += "layout rect cutting\n";
    const auto frame = [&](i32 f)
    {
        e.begin(f);
        ui u(e.c);
        u.text_area({10, 10, 1400, 800}, text, "gutter"_id,
                    {.wrap = false, .read_only = true, .line_numbers = true});
        CHECK(e.c->st->text_area_active == nullptr);
        end_frame(e.c);
    };
    for (i32 f = 0; f < 6; ++f) frame(f);
    const auto *storage = e.c->st->text_areas.numbers.data();
    CHECK(e.c->st->text_areas.numbers.size() > 30);
    allocation_count = 0;
    count_allocations = true;
    frame(6);
    count_allocations = false;
    CHECK(allocation_count == 0);
    CHECK(e.c->st->text_areas.numbers.data() == storage);
    {
        detail::scratch_scope<text_area_scratch> outer(e.c->st->text_areas,
                                                       e.c->st->text_area_active);
        const auto saved = outer.value.numbers;
        {
            detail::scratch_scope<text_area_scratch> inner(e.c->st->text_areas,
                                                           e.c->st->text_area_active);
            CHECK(&inner.value != &outer.value);
            inner.value.numbers.assign(1000, {17, 91});
        }
        CHECK(e.c->st->text_area_active == &outer.value);
        CHECK(outer.value.numbers.data() == storage);
        CHECK(outer.value.numbers.size() == saved.size());
        CHECK(std::memcmp(saved.data(), outer.value.numbers.data(),
                          saved.size() * sizeof(text_area_scratch::number_mark)) == 0);
    }
    CHECK(e.c->st->text_area_active == nullptr);
}
