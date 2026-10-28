// Renderer-owned rings, strokes and gradients.
// Included by pufferui.h inside namespace pui.
// Algorithms explicitly selected by mesh backends.
// Four-line slice banner for amalgamation.
namespace detail
{
inline void ring_hband(renderer_detail::mesh_painter &u, f32 x0, f32 x1, f32 y0, f32 y1, color ca,
                       color cb)
{
    vertex v[4] = {
        {x0, y0, 0.0f, 0.0f, ca},
        {x1, y0, 1.0f, 0.0f, ca},
        {x1, y1, 1.0f, 1.0f, cb},
        {x0, y1, 0.0f, 1.0f, cb},
    };
    i32 idx[6] = {0, 1, 2, 0, 2, 3};
    u.draw_triangles(nullptr, v, 4, idx, 6);
}
inline void ring_vband(renderer_detail::mesh_painter &u, f32 x0, f32 x1, f32 y0, f32 y1, color ca,
                       color cb)
{
    vertex v[4] = {
        {x0, y0, 0.0f, 0.0f, ca},
        {x1, y0, 1.0f, 0.0f, cb},
        {x1, y1, 1.0f, 1.0f, cb},
        {x0, y1, 0.0f, 1.0f, ca},
    };
    i32 idx[6] = {0, 1, 2, 0, 2, 3};
    u.draw_triangles(nullptr, v, 4, idx, 6);
}
} // namespace detail
void renderer_detail::mesh_painter::ring(const paint_ring &p)
{
    mesh_painter &u = *this;
    const rect r = p.area;
    const color c = p.tint;
    const corner_radii &radii_in = p.radii;
    const f32 thickness = p.thickness;

    if (c.a == 0 || thickness <= 0.0f) return;
    if (detail::outside_paint_feathered(u.ctx, r)) return; // all eight pieces would be culled
    const f32 t = min2(thickness, min2(r.w, r.h) * 0.5f);
    if (t <= 0.0f) return;
    const corner_radii q = detail::clamp_radii(r, radii_in);

    if (q.tl == 0.0f && q.tr == 0.0f && q.br == 0.0f && q.bl == 0.0f)
    {
        u.draw_rect({r.x, r.y, r.w, t}, c);
        u.draw_rect({r.x, r.bottom() - t, r.w, t}, c);
        u.draw_rect({r.x, r.y + t, t, r.h - 2.0f * t}, c);
        u.draw_rect({r.right() - t, r.y + t, t, r.h - 2.0f * t}, c);
        return;
    }

    // Straight bands with the same 1px feathers the corner arcs get from
    // draw_sector (transparent 1px outside the outer edge and, when the hole
    // is wide enough, 1px inside the inner edge). Without them the hard strip
    // edges met the soft arc edges in a crisp step toward the inside.
    const f32 widest = max2(max2(q.tl, q.tr), max2(q.br, q.bl));
    const bool feather_in = max2(0.0f, widest - t) > 1.0f; // must match draw_sector's ring rule
    color fade = c;
    fade.a = 0;

    // extents of the four bands
    const f32 top_x0 = r.x + q.tl, top_x1 = r.right() - q.tr;
    const f32 bot_x0 = r.x + q.bl, bot_x1 = r.right() - q.br;
    const f32 left_y0 = r.y + (q.tl > 0.0f ? q.tl : t),
              left_y1 = r.bottom() - (q.bl > 0.0f ? q.bl : t);
    const f32 right_y0 = r.y + (q.tr > 0.0f ? q.tr : t),
              right_y1 = r.bottom() - (q.br > 0.0f ? q.br : t);

    // top / bottom
    detail::ring_hband(u, top_x0, top_x1, r.y - 1.0f, r.y, fade, c);
    detail::ring_hband(u, top_x0, top_x1, r.y, r.y + t, c, c);
    if (feather_in) detail::ring_hband(u, top_x0, top_x1, r.y + t, r.y + t + 1.0f, c, fade);
    detail::ring_hband(u, bot_x0, bot_x1, r.bottom(), r.bottom() + 1.0f, c, fade);
    detail::ring_hband(u, bot_x0, bot_x1, r.bottom() - t, r.bottom(), c, c);
    if (feather_in)
        detail::ring_hband(u, bot_x0, bot_x1, r.bottom() - t - 1.0f, r.bottom() - t, fade, c);

    // left / right
    detail::ring_vband(u, r.x - 1.0f, r.x, left_y0, left_y1, fade, c);
    detail::ring_vband(u, r.x, r.x + t, left_y0, left_y1, c, c);
    if (feather_in) detail::ring_vband(u, r.x + t, r.x + t + 1.0f, left_y0, left_y1, c, fade);
    detail::ring_vband(u, r.right(), r.right() + 1.0f, right_y0, right_y1, c, fade);
    detail::ring_vband(u, r.right() - t, r.right(), right_y0, right_y1, c, c);
    if (feather_in)
        detail::ring_vband(u, r.right() - t - 1.0f, r.right() - t, right_y0, right_y1, fade, c);

    if (q.tl > 0.0f)
        u.draw_sector(vec2{r.x + q.tl, r.y + q.tl}, max2(0.0f, q.tl - t), q.tl, PI, PI * 1.5f, c);
    if (q.tr > 0.0f)
        u.draw_sector(vec2{r.right() - q.tr, r.y + q.tr}, max2(0.0f, q.tr - t), q.tr, PI * 1.5f,
                      PI * 2.0f, c);
    if (q.br > 0.0f)
        u.draw_sector(vec2{r.right() - q.br, r.bottom() - q.br}, max2(0.0f, q.br - t), q.br, 0.0f,
                      PI * 0.5f, c);
    if (q.bl > 0.0f)
        u.draw_sector(vec2{r.x + q.bl, r.bottom() - q.bl}, max2(0.0f, q.bl - t), q.bl, PI * 0.5f,
                      PI, c);
}
void renderer_detail::mesh_painter::stroke(const paint_line &p)
{
    const vec2 a = p.from, b = p.to;
    const f32 thickness = p.thickness;
    const color c = p.tint;

    vec2 d{b.x - a.x, b.y - a.y};
    const f32 l = max2(std::sqrt(d.x * d.x + d.y * d.y), 0.001f);
    const vec2 n{-d.y / l * thickness * 0.5f, d.x / l * thickness * 0.5f};
    const vec2 pts[4] = {{a.x + n.x, a.y + n.y},
                         {b.x + n.x, b.y + n.y},
                         {b.x - n.x, b.y - n.y},
                         {a.x - n.x, a.y - n.y}};
    draw_polygon(std::span<const vec2>(pts, 4), c);
}
void renderer_detail::mesh_painter::gradient(const paint_gradient &p)
{
    const rect r = p.area;
    const color c0 = p.tl, c1 = p.tr, c2 = p.br, c3 = p.bl;

    vertex v[4] = {
        {r.x, r.y, 0, 0, c0},
        {r.right(), r.y, 0, 0, c1},
        {r.right(), r.bottom(), 0, 0, c2},
        {r.x, r.bottom(), 0, 0, c3},
    };
    const i32 idx[6] = {0, 1, 2, 0, 2, 3};
    draw_triangles(nullptr, v, 4, idx, 6);
}
