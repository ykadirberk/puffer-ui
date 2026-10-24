// Renderer-owned rounded gradient fan, preserving the showcase stream.
// Included by pufferui.h inside namespace pui.
// Native representation is selected by the backend factory.
// Four-line slice banner for amalgamation.
void renderer_detail::mesh_painter::rounded_gradient(const paint_rounded_gradient &p)
{
    const rect r = p.area;
    f32 radius = p.radius;
    const color tl = p.top_left, tr = p.top_right, br = p.bottom_right, bl = p.bottom_left;
    const auto sat = [](f32 v) { return clampf(v, 0.0f, 1.0f); };
    const auto mixc = [&](color a, color b, f32 t) { return theme_lerp_color(a, b, sat(t)); };

    if (r.w <= 0.5f || r.h <= 0.5f) return;
    // Keep the rounded underlay's feather (the fan itself stays inside r).
    // State and interaction still run for tiles outside the paint area.
    if (!ui(ctx).is_visible(r.pad(-1.5f))) return;
    radius = clampf(radius, 0.0f, min2(r.w, r.h) * 0.5f);
    const auto col_at = [&](f32 x, f32 y)
    {
        const f32 fx = sat((x - r.x) / r.w), fy = sat((y - r.y) / r.h);
        return mixc(mixc(tl, tr, fx), mixc(bl, br, fx), fy);
    };
    const color mid = col_at(r.center_x(), r.center_y());
#if defined(PUFFERUI_ENABLE_VULKAN)
    if (native_gradient)
    {
        auto selected = p;
        selected.radius = radius;
        if (native_gradient(ctx, selected, mid)) return;
    }
#endif
    constexpr i32 SEG = 6;
    constexpr i32 RING = 4 * (SEG + 1);
    vertex v[1 + RING];
    i32 idx[3 * RING];
    const vec2 centers[4] = {{r.x + radius, r.y + radius},
                             {r.right() - radius, r.y + radius},
                             {r.right() - radius, r.bottom() - radius},
                             {r.x + radius, r.bottom() - radius}};
    struct arc_table
    {
        f32 cs[4][SEG + 1], sn[4][SEG + 1];
        arc_table()
        {
            const f32 starts[4] = {PI, PI * 1.5f, 0.0f, PI * 0.5f};
            for (i32 c = 0; c < 4; ++c)
                for (i32 s = 0; s <= SEG; ++s)
                {
                    // Same expression and rounding as the original per-vertex path.
                    const f32 a =
                        starts[c] + (PI * 0.5f) * static_cast<f32>(s) / static_cast<f32>(SEG);
                    cs[c][s] = std::cos(a);
                    sn[c][s] = std::sin(a);
                }
        }
    };
    static const arc_table arcs;
    v[0] = {r.center_x(), r.center_y(), 0.0f, 0.0f, mid};
    i32 n = 1;
    for (i32 c = 0; c < 4; ++c)
    {
        for (i32 s = 0; s <= SEG; ++s)
        {
            const f32 x = centers[c].x + arcs.cs[c][s] * radius;
            const f32 y = centers[c].y + arcs.sn[c][s] * radius;
            v[n++] = {x, y, 0.0f, 0.0f, col_at(x, y)};
        }
    }
    i32 k = 0;
    for (i32 i = 0; i < RING; ++i)
    {
        idx[k++] = 0;
        idx[k++] = 1 + i;
        idx[k++] = 1 + (i + 1) % RING;
    }
    draw_rounded_rect(r, mid, radius);
    draw_triangles(nullptr, v, n, idx, k);
}
