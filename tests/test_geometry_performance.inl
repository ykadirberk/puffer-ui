// Independent r111 geometry references; this file is included only by internal tests.
namespace
{
template <class UvFn>
inline void reference_rounded_fan(rect r, const corner_radii &radii, color c, UvFn &&uv,
                                  std::vector<vertex> &verts, std::vector<i32> &idx)
{
    constexpr i32 SEG = 8;
    // The nine angles of a quarter arc never change: their sines and cosines are
    // computed once (the fan is built for every rounded rect of every frame).
    struct arc_table
    {
        f32 cs[SEG + 1], sn[SEG + 1];
        arc_table()
        {
            for (i32 s = 0; s <= SEG; ++s)
            {
                const f32 ang = (PI * 0.5f) * (static_cast<f32>(s) / static_cast<f32>(SEG));
                cs[s] = cosf(ang);
                sn[s] = sinf(ang);
            }
        }
    };
    static const arc_table arc;
    struct corner
    {
        f32 cx, cy, rad;
        i32 dx, dy;
    };
    const corner corners[4] = {
        {r.right() - radii.br, r.bottom() - radii.br, radii.br, 1, 0},
        {r.x + radii.bl, r.bottom() - radii.bl, radii.bl, 0, 1},
        {r.x + radii.tl, r.y + radii.tl, radii.tl, -1, 0},
        {r.right() - radii.tr, r.y + radii.tr, radii.tr, 0, -1},
    };

    const i32 N = 4 * (SEG + 1);
    verts.reserve(static_cast<usize>(1 + 2 * N));
    idx.reserve(static_cast<usize>(9 * N));

    color c_out = c;
    c_out.a = 0;
    f32 cu = 0.0f, cv = 0.0f;
    uv(r.center_x(), r.center_y(), cu, cv);
    verts.push_back({r.center_x(), r.center_y(), cu, cv, c});
    const i32 ring0 = static_cast<i32>(verts.size());

    for (i32 ring = 0; ring < 2; ++ring)
    {
        const color col = ring == 0 ? c : c_out;
        for (i32 ci = 0; ci < 4; ++ci)
        {
            const f32 rad = ring == 0 ? max2(0.0f, corners[ci].rad - 0.5f) : corners[ci].rad + 0.5f;
            for (i32 s = 0; s <= SEG; ++s)
            {
                const f32 cs = arc.cs[s], sn = arc.sn[s];
                const f32 dx = cs * corners[ci].dx - sn * corners[ci].dy;
                const f32 dy = sn * corners[ci].dx + cs * corners[ci].dy;
                const f32 px = corners[ci].cx + dx * rad;
                const f32 py = corners[ci].cy + dy * rad;
                f32 uu = 0.0f, vv = 0.0f;
                uv(px, py, uu, vv);
                verts.push_back({px, py, uu, vv, col});
            }
        }
    }
    const i32 ring1 = ring0 + N;

    for (i32 i = 0; i < N; ++i)
    {
        const i32 nxt = (i + 1) % N;
        idx.push_back(0);
        idx.push_back(ring0 + i);
        idx.push_back(ring0 + nxt);
    }
    for (i32 i = 0; i < N; ++i)
    {
        const i32 nxt = (i + 1) % N;
        const i32 in1 = ring0 + i, in2 = ring0 + nxt;
        const i32 out1 = ring1 + i, out2 = ring1 + nxt;
        idx.push_back(in1);
        idx.push_back(out1);
        idx.push_back(out2);
        idx.push_back(in1);
        idx.push_back(out2);
        idx.push_back(in2);
    }
}

inline void reference_dl_add(context *c, const vertex *verts, i32 vcount, const i32 *idx,
                             i32 icount)
{
    if (!c->dl || vcount <= 0 || icount <= 0) return;
    draw_list &dl = *c->dl;
    i32 base = static_cast<i32>(dl.verts.size());
    dl.verts.insert(dl.verts.end(), verts, verts + vcount);
    if (c->render_scale != 1.0f)
        for (usize i = dl.verts.size() - static_cast<usize>(vcount); i < dl.verts.size(); ++i)
        {
            dl.verts[i].x *= c->render_scale;
            dl.verts[i].y *= c->render_scale;
        }
    if (c->disabled_depth > 0)
        for (usize i = dl.verts.size() - static_cast<usize>(vcount); i < dl.verts.size(); ++i)
            dl.verts[i].c.a =
                static_cast<u8>(dl.verts[i].c.a * clampf(c->disabled_alpha, 0.0f, 1.0f));
    if (dl.solid_uv_remap)
    {
        // Solid geometry rides in the glyph batch: remap the copied verts' UVs
        // to the atlas's reserved white texel at (0,0) (its center, so any
        // sampler/filtering returns exactly that texel). The copy already
        // happened, so this is arithmetic on the way in — no extra pass cost
        // beyond the uv writes.
        const f32 wx = 0.5f / static_cast<f32>(c->ts->atlas_w);
        const f32 wy = 0.5f / static_cast<f32>(c->ts->atlas_h);
        const usize first = dl.verts.size() - static_cast<usize>(vcount);
        for (usize i = first; i < dl.verts.size(); ++i)
        {
            dl.verts[i].u = wx;
            dl.verts[i].v = wy;
        }
    }
    // Let vector grow geometrically. Reserving exactly size + icount here
    // relocated the entire batch once per primitive while warming up.
    for (i32 i = 0; i < icount; ++i) dl.idx.push_back(idx[i] + base);
}

} // namespace

PUI_TEST(rounded_fan_preserves_original_vertices_uvs_and_topology)
{
    for (rect r : {rect{0, 0, 1, 1}, rect{3.125f, -8.75f, 73.5f, 46.25f}, rect{-100, 40, 0, 0},
                   rect{0, 0, 640, 480}})
        for (corner_radii radii : {corner_radii{}, corner_radii::all(0.25f), corner_radii::all(13),
                                   corner_radii{1, 3, 8, 16}})
            for (i32 prefix : {0, 3})
            {
                std::vector<vertex> expected_v(static_cast<usize>(prefix));
                std::vector<vertex> actual_v = expected_v;
                std::vector<i32> expected_i = {6, 4, 2}, actual_i = expected_i;
                const auto uv = [](f32 x, f32 y, f32 &u, f32 &v)
                {
                    u = 0.125f + x * 0.003f;
                    v = 0.25f + y * 0.002f;
                };
                const color c{57, 134, 213, 127};
                reference_rounded_fan(r, radii, c, uv, expected_v, expected_i);
                detail::build_rounded_fan(r, radii, c, uv, actual_v, actual_i);
                CHECK(actual_i == expected_i);
                CHECK(actual_v.size() == expected_v.size());
                CHECK(std::memcmp(actual_v.data(), expected_v.data(),
                                  actual_v.size() * sizeof(vertex)) == 0);
                actual_v.clear();
                actual_i.clear();
                count_allocations = true;
                allocation_count = 0;
                detail::build_rounded_fan(r, radii, c, uv, actual_v, actual_i);
                count_allocations = false;
                CHECK(allocation_count == 0);
            }
}

PUI_TEST(draw_list_bulk_indices_preserve_base_order_and_vertex_transforms)
{
    perf_env env;
    env.begin();
    context *c = env.c;
    draw_list *actual = c->dl;
    for (f32 scale : {1.0f, 1.5f, 2.0f})
        for (bool remap : {false, true})
            for (bool disabled : {false, true})
            {
                actual->verts.clear();
                actual->idx.clear();
                actual->solid_uv_remap = remap;
                draw_list expected;
                expected.solid_uv_remap = remap;
                c->render_scale = scale;
                c->disabled_depth = disabled ? 1 : 0;
                c->disabled_alpha = 0.35f;
                const vertex verts[] = {{1.25f, 2.75f, 0.1f, 0.2f, {1, 2, 3, 201}},
                                        {4.5f, 8.25f, 0.3f, 0.4f, {4, 5, 6, 127}},
                                        {7.75f, 9.5f, 0.5f, 0.6f, {7, 8, 9, 255}}};
                for (i32 count : {3, 0, 324, 6, 12000, 3})
                {
                    std::vector<i32> indices(static_cast<usize>(count));
                    for (i32 i = 0; i < count; ++i) indices[static_cast<usize>(i)] = (i * 7) % 3;
                    c->dl = &expected;
                    reference_dl_add(c, verts, 3, indices.data(), count);
                    c->dl = actual;
                    detail::dl_add(c, verts, 3, indices.data(), count);
                    CHECK(actual->idx == expected.idx);
                    CHECK(actual->verts.size() == expected.verts.size());
                    CHECK(std::memcmp(actual->verts.data(), expected.verts.data(),
                                      actual->verts.size() * sizeof(vertex)) == 0);
                }
            }
    c->disabled_depth = 0;
    c->render_scale = 1;
    actual->solid_uv_remap = false;
    actual->verts.clear();
    actual->idx.clear();
    std::vector<i32>().swap(actual->idx);
    const vertex v[3]{};
    const i32 tri[] = {0, 2, 1};
    count_allocations = true;
    allocation_count = 0;
    for (i32 i = 0; i < 2000; ++i) detail::dl_add(c, v, 3, tri, 3);
    count_allocations = false;
    CHECK(allocation_count < 40); // growth stays geometric across small submissions
    CHECK(actual->idx.size() == 6000);
    for (usize i = 0; i < actual->idx.size(); ++i)
        CHECK(actual->idx[i] == static_cast<i32>(i / 3) * 3 + tri[i % 3]);
    actual->verts.clear();
    actual->idx.clear();
    end_frame(c);
}
