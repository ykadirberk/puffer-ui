// pufferui/impl/draw_primitives.inl - part of the PufferUI implementation (see pufferui.h).
// Included by pufferui.h inside `namespace pui` when PUFFERUI_IMPLEMENTATION is
// defined; never include it directly. The slices concatenate to the original
// implementation block in order (tools/amalgamate.ps1 rebuilds the single header).
// ---- draw primitives ----
void ui::draw_triangles(texture_handle tex, const vertex *vertices, i32 vertex_count,
                        const i32 *indices, i32 index_count)
{
    if (vertex_count <= 0 || index_count <= 0) return;
    // Cull whole batches that cannot paint: a batch whose bounding box misses the
    // client area or the active clip is dropped here — the renderer never sees it
    // (this is what keeps off-window and clipped-out scroll/panel content cheap).
    // An empty clip (w/h 0: a collapsed region) paints nothing, so everything is
    // culled; the device must never see it, because "no scissor" is how a
    // device spells "unclipped".
    rect bounds;
    if (detail::paint_bounds(ctx, bounds))
    {
        if (bounds.w <= 0.0f || bounds.h <= 0.0f) return;
        f32 minx = vertices[0].x, maxx = minx, miny = vertices[0].y, maxy = miny;
        for (i32 i = 1; i < vertex_count; ++i)
        {
            minx = min2(minx, vertices[i].x);
            maxx = max2(maxx, vertices[i].x);
            miny = min2(miny, vertices[i].y);
            maxy = max2(maxy, vertices[i].y);
        }
        const rect aabb{minx, miny, maxx - minx, maxy - miny};
        const rect isect = rect::intersect(aabb, bounds);
        if (isect.w <= 0.0f || isect.h <= 0.0f) return;
    }
    // Solid geometry (tex == nullptr) joins the glyph batch when an atlas
    // exists: the atlas's reserved (0,0) texel is pure white, and dl_add
    // remaps the copied verts' UVs to it. Without an atlas (no font loaded)
    // solid geometry goes out in the nullptr-texture batch.
    if (ctx->dl && tex == nullptr && ctx->ts && !ctx->ts->atlases.empty())
    {
        tex = ctx->ts->atlases.front().tex;
        ctx->dl->solid_uv_remap = true;
    }
    const rect *clip = (ctx->clip_depth > 0) ? &ctx->clip_stack[ctx->clip_depth - 1] : nullptr;
    detail::dl_prepare(ctx, tex, clip);
    detail::dl_add(ctx, vertices, vertex_count, indices, index_count);
    if (ctx->dl) ctx->dl->solid_uv_remap = false;
}

void ui::draw_rect(rect r, color c)
{
    PUFFERUI_CHECK(VIOL_INVALID_RECT, r.is_valid(), "draw_rect with invalid rect");
    vertex v[4] = {
        {r.x, r.y, 0.0f, 0.0f, c},
        {r.right(), r.y, 1.0f, 0.0f, c},
        {r.right(), r.bottom(), 1.0f, 1.0f, c},
        {r.x, r.bottom(), 0.0f, 1.0f, c},
    };
    i32 idx[6] = {0, 1, 2, 0, 2, 3};
    draw_triangles(nullptr, v, 4, idx, 6);
}

void ui::draw_line(f32 x0, f32 y0, f32 x1, f32 y1, color c, f32 thickness)
{
    // Axis-aligned lines stay flat rects; anything else (a diagonal, a dot)
    // goes through detail::thick_line's half-plane split, so a diagonal segment
    // is a real drawn line instead of silently drawing nothing.
    const f32 dx = x1 - x0, dy = y1 - y0;
    if (dx == 0.0f && dy == 0.0f)
    {
        // A dot: a small square centered on the point.
        const f32 h = thickness * 0.5f;
        draw_rect({x0 - h, y0 - h, thickness, thickness}, c);
        return;
    }
    if (y0 == y1)
    {
        draw_rect({min2(x0, x1), y0 - thickness * 0.5f, (dx > 0.0f ? dx : -dx), thickness}, c);
        return;
    }
    if (x0 == x1)
    {
        draw_rect({x0 - thickness * 0.5f, min2(y0, y1), thickness, (dy > 0.0f ? dy : -dy)}, c);
        return;
    }
    detail::thick_line(*this, {x0, y0}, {x1, y1}, thickness, c);
}

namespace detail
{
// The antialiased fan used for rounded fills: a center vertex, an inner ring on
// the edge of the shape and an outer ring one pixel out at alpha 0 (the feather).
// Corners are walked in the order BR, BL, TL, TR; a square corner (radius 0)
// collapses its arc to the corner point. `uv(x, y, u, v)` supplies texture
// coordinates (the blur composite samples the scene; plain fills use none).
template <class UvFn>
inline void build_rounded_fan(rect r, const corner_radii &radii, color c, UvFn &&uv,
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
} // namespace detail

void ui::draw_rounded_rect(rect r, color c, f32 radius)
{
    draw_rounded_rect(r, c, corner_radii::all(radius));
}

void ui::draw_rounded_rect(rect r, color c, const corner_radii &radii_in)
{
    PUFFERUI_CHECK(VIOL_INVALID_RECT, r.is_valid(), "draw_rounded_rect with invalid rect");
    if (detail::outside_paint(ctx, r.pad(-1.5f))) return; // before the fan is built
    const corner_radii radii = detail::clamp_radii(r, radii_in);
    if (radii.tl == 0.0f && radii.tr == 0.0f && radii.br == 0.0f && radii.bl == 0.0f)
    {
        draw_rect(r, c);
        return;
    }
    if (!ctx->dl) return;
    // the draw list's scratch buffers: no heap allocation per rounded shape
    std::vector<vertex> &verts = ctx->dl->scratch_v;
    std::vector<i32> &idx = ctx->dl->scratch_i;
    verts.clear();
    idx.clear();
    detail::build_rounded_fan(
        r, radii, c, [](f32, f32, f32 &u, f32 &v) { u = v = 0.0f; }, verts, idx);
    draw_triangles(nullptr, verts.data(), static_cast<i32>(verts.size()), idx.data(),
                   static_cast<i32>(idx.size()));
}

void ui::draw_image(const skin_image &img, rect dst)
{
    if (!img.valid() || dst.w <= 0.0f || dst.h <= 0.0f) return;
    PUFFERUI_CHECK(VIOL_INVALID_RECT, dst.is_valid(), "draw_image with invalid rect");
    vertex v[4] = {
        {dst.x, dst.y, img.uv.x, img.uv.y, color::white()},
        {dst.right(), dst.y, img.uv.right(), img.uv.y, color::white()},
        {dst.right(), dst.bottom(), img.uv.right(), img.uv.bottom(), color::white()},
        {dst.x, dst.bottom(), img.uv.x, img.uv.bottom(), color::white()},
    };
    i32 idx[6] = {0, 1, 2, 0, 2, 3};
    draw_triangles(img.tex, v, 4, idx, 6);
}

void ui::draw_nine_slice(const skin_image &img, rect dst)
{
    if (!img.valid() || dst.w <= 0.0f || dst.h <= 0.0f) return;
    if (img.src_w <= 0.0f || img.src_h <= 0.0f) return;
    PUFFERUI_CHECK(VIOL_INVALID_RECT, dst.is_valid(), "draw_nine_slice with invalid rect");

    // Source slices (pixels), clamped so the border insets never overlap.
    const f32 l = clampf(img.inset_l, 0.0f, img.src_w);
    const f32 r = clampf(img.inset_r, 0.0f, img.src_w - l);
    const f32 t = clampf(img.inset_t, 0.0f, img.src_h);
    const f32 b = clampf(img.inset_b, 0.0f, img.src_h - t);
    const f32 cw = img.src_w - l - r;
    const f32 ch = img.src_h - t - b;

    // Destination corner sizes: 1:1 with the source, clamped into the rect.
    const f32 dl = min2(l, dst.w * 0.5f);
    const f32 dr = min2(r, dst.w * 0.5f);
    const f32 dt = min2(t, dst.h * 0.5f);
    const f32 db = min2(b, dst.h * 0.5f);
    const f32 mid_w = max2(0.0f, dst.w - dl - dr);
    const f32 mid_h = max2(0.0f, dst.h - dt - db);

    const f32 us = img.uv.w / img.src_w; // uv per source pixel
    const f32 vs = img.uv.h / img.src_h;

    auto quad = [&](f32 x, f32 y, f32 w, f32 h, f32 su, f32 sv, f32 sw, f32 sh)
    {
        if (w <= 0.0f || h <= 0.0f || sw <= 0.0f || sh <= 0.0f) return;
        const f32 ua = img.uv.x + su * us, ub = img.uv.x + (su + sw) * us;
        const f32 va = img.uv.y + sv * vs, vb = img.uv.y + (sv + sh) * vs;
        vertex v[4] = {
            {x, y, ua, va, color::white()},
            {x + w, y, ub, va, color::white()},
            {x + w, y + h, ub, vb, color::white()},
            {x, y + h, ua, vb, color::white()},
        };
        i32 idx[6] = {0, 1, 2, 0, 2, 3};
        draw_triangles(img.tex, v, 4, idx, 6);
    };

    // Tile/stretch one source span along one axis: fn(dst_off, dst_len, src_off, src_len).
    auto spans = [&](f32 dst_total, f32 src_total, auto &&fn)
    {
        if (dst_total <= 0.0f || src_total <= 0.0f) return;
        if (img.center_mode == SKIN_CENTER_STRETCH)
        {
            fn(0.0f, dst_total, 0.0f, src_total);
        }
        else if (img.center_mode == SKIN_CENTER_NONE)
        {
            const f32 len = min2(dst_total, src_total);
            fn(0.0f, len, 0.0f, len);
        }
        else
        { // SKIN_CENTER_TILE
            for (f32 x = 0.0f; x < dst_total; x += src_total)
            {
                const f32 len = min2(src_total, dst_total - x);
                fn(x, len, 0.0f, len);
            }
        }
    };

    // 4 corners (never scaled)
    quad(dst.x, dst.y, dl, dt, 0.0f, 0.0f, l, t);
    quad(dst.right() - dr, dst.y, dr, dt, l + cw, 0.0f, r, t);
    quad(dst.x, dst.bottom() - db, dl, db, 0.0f, t + ch, l, b);
    quad(dst.right() - dr, dst.bottom() - db, dr, db, l + cw, t + ch, r, b);

    // 4 edges (tile/stretch along their varying axis)
    spans(mid_h, ch, [&](f32 oy, f32 oh, f32 soy, f32 soh)
          { quad(dst.x, dst.y + dt + oy, dl, oh, 0.0f, t + soy, l, soh); });
    spans(mid_h, ch, [&](f32 oy, f32 oh, f32 soy, f32 soh)
          { quad(dst.right() - dr, dst.y + dt + oy, dr, oh, l + cw, t + soy, r, soh); });
    spans(mid_w, cw, [&](f32 ox, f32 ow, f32 sox, f32 sow)
          { quad(dst.x + dl + ox, dst.y, ow, dt, l + sox, 0.0f, sow, t); });
    spans(mid_w, cw, [&](f32 ox, f32 ow, f32 sox, f32 sow)
          { quad(dst.x + dl + ox, dst.bottom() - db, ow, db, l + sox, t + ch, sow, b); });

    // center (tile/stretch on both axes)
    spans(mid_w, cw,
          [&](f32 ox, f32 ow, f32 sox, f32 sow)
          {
              spans(
                  mid_h, ch, [&](f32 oy, f32 oh, f32 soy, f32 soh)
                  { quad(dst.x + dl + ox, dst.y + dt + oy, ow, oh, l + sox, t + soy, sow, soh); });
          });
}

void ui::draw_polygon(std::span<const vec2> points, color c)
{
    if (points.size() < 3) return;
    const usize n = points.size();
    vec2 ctr{0.0f, 0.0f};
    for (const vec2 &p : points)
    {
        ctr.x += p.x;
        ctr.y += p.y;
    }
    ctr.x /= static_cast<f32>(n);
    ctr.y /= static_cast<f32>(n);

    color c_out = c;
    c_out.a = 0;

    // center + inner ring (the polygon) + a 1px feathered outer ring
    if (!ctx->dl) return;
    std::vector<vertex> &verts = ctx->dl->scratch_v;
    std::vector<i32> &idx = ctx->dl->scratch_i;
    verts.clear();
    idx.clear();
    verts.reserve(2 * n + 1);
    verts.push_back({ctr.x, ctr.y, 0.0f, 0.0f, c});
    for (const vec2 &p : points) verts.push_back({p.x, p.y, 0.0f, 0.0f, c});
    for (usize i = 0; i < n; ++i)
    {
        const vec2 &a = points[(i + n - 1) % n];
        const vec2 &b = points[i];
        const vec2 &d = points[(i + 1) % n];
        const vec2 e1{b.x - a.x, b.y - a.y};
        const vec2 e2{d.x - b.x, d.y - b.y};
        const f32 l1 = max2(std::sqrt(e1.x * e1.x + e1.y * e1.y), 0.001f);
        const f32 l2 = max2(std::sqrt(e2.x * e2.x + e2.y * e2.y), 0.001f);
        vec2 nrm{e1.y / l1 + e2.y / l2, -e1.x / l1 - e2.x / l2};
        const f32 ln = max2(std::sqrt(nrm.x * nrm.x + nrm.y * nrm.y), 0.001f);
        nrm.x /= ln;
        nrm.y /= ln;
        // point away from the centroid (assumes a convex polygon)
        if (nrm.x * (b.x - ctr.x) + nrm.y * (b.y - ctr.y) < 0.0f)
        {
            nrm.x = -nrm.x;
            nrm.y = -nrm.y;
        }
        verts.push_back({b.x + nrm.x, b.y + nrm.y, 0.0f, 0.0f, c_out});
    }

    idx.reserve(n * 6 + n * 6);
    const i32 inner0 = 1, outer0 = 1 + static_cast<i32>(n);
    for (usize i = 0; i < n; ++i)
    {
        const i32 in0 = inner0 + static_cast<i32>(i);
        const i32 in1 = inner0 + static_cast<i32>((i + 1) % n);
        idx.push_back(0);
        idx.push_back(in0);
        idx.push_back(in1);
    }
    for (usize i = 0; i < n; ++i)
    {
        const i32 in0 = inner0 + static_cast<i32>(i);
        const i32 in1 = inner0 + static_cast<i32>((i + 1) % n);
        const i32 out0 = outer0 + static_cast<i32>(i);
        const i32 out1 = outer0 + static_cast<i32>((i + 1) % n);
        idx.push_back(in0);
        idx.push_back(out0);
        idx.push_back(out1);
        idx.push_back(in0);
        idx.push_back(out1);
        idx.push_back(in1);
    }
    draw_triangles(nullptr, verts.data(), static_cast<i32>(verts.size()), idx.data(),
                   static_cast<i32>(idx.size()));
}

void ui::draw_sector(vec2 center, f32 r_in, f32 r_out, f32 a0, f32 a1, color c)
{
    if (a1 < a0)
    {
        const f32 t = a0;
        a0 = a1;
        a1 = t;
    }
    if (a1 - a0 <= 0.0f || r_out <= 0.0f || r_out <= r_in) return; // degenerate: nothing to fill
    {
        const f32 reach = r_out + 1.5f; // outer radius plus the feather
        if (detail::outside_paint(
                ctx, rect::make(center.x - reach, center.y - reach, reach * 2.0f, reach * 2.0f)))
            return;
    }
    const f32 mid = (r_in + r_out) * 0.5f;
    const i32 seg = static_cast<i32>(max2(3.0f, std::ceil((a1 - a0) * max2(2.0f, mid) * 0.25f)));

    color c_out = c;
    c_out.a = 0;

    // rings: inner feather (optional), inner edge, outer edge, outer feather
    struct ring
    {
        f32 r;
        color col;
    };
    ring rings[4];
    i32 rc = 0;
    if (r_in > 1.0f) rings[rc++] = {r_in - 1.0f, c_out};
    rings[rc++] = {r_in, c};
    rings[rc++] = {r_out, c};
    rings[rc++] = {r_out + 1.0f, c_out};

    std::vector<vertex> &verts = ctx->dl->scratch_v;
    std::vector<i32> &idx = ctx->dl->scratch_i;
    verts.clear();
    idx.clear();
    verts.reserve(static_cast<usize>(rc) * static_cast<usize>(seg + 1));

    // Rotation recurrence: two trig evaluations per CALL instead of two per
    // vertex — each step rotates the radius vector by the fixed step angle.
    // Accumulated drift for UI-sized arcs is ~1e-4 px (subpixel).
    const f32 step = (a1 - a0) / static_cast<f32>(seg);
    const f32 cs = std::cos(step), sn = std::sin(step);
    for (i32 ri = 0; ri < rc; ++ri)
    {
        f32 x = std::cos(a0) * rings[ri].r, y = std::sin(a0) * rings[ri].r;
        for (i32 s = 0; s <= seg; ++s)
        {
            verts.push_back({center.x + x, center.y + y, 0.0f, 0.0f, rings[ri].col});
            const f32 nx = x * cs - y * sn;
            y = x * sn + y * cs;
            x = nx;
        }
    }

    idx.reserve(static_cast<usize>(rc - 1) * static_cast<usize>(seg) * 6);
    for (i32 ri = 0; ri < rc - 1; ++ri)
    {
        const i32 r0 = ri * (seg + 1), r1 = (ri + 1) * (seg + 1);
        for (i32 s = 0; s < seg; ++s)
        {
            const i32 i0 = r0 + s, i1 = r0 + s + 1, o0 = r1 + s, o1 = r1 + s + 1;
            idx.push_back(i0);
            idx.push_back(o0);
            idx.push_back(o1);
            idx.push_back(i0);
            idx.push_back(o1);
            idx.push_back(i1);
        }
    }
    draw_triangles(nullptr, verts.data(), static_cast<i32>(verts.size()), idx.data(),
                   static_cast<i32>(idx.size()));
}

void ui::draw_arc(vec2 center, f32 radius, f32 thickness, f32 a0, f32 a1, color c)
{
    draw_sector(center, max2(0.0f, radius - thickness * 0.5f), radius + thickness * 0.5f, a0, a1,
                c);
}

namespace detail
{

inline blur_store *ensure_blur(context *c, i32 ow, i32 oh)
{
    if (!c->device) return nullptr;
    if (!c->blur) c->blur = new blur_store();
    blur_store *bs = c->blur;
    if (bs->w == ow && bs->h == oh && bs->half && bs->quarter) return bs;

    if (bs->half) c->device->destroy_target(bs->half);
    if (bs->quarter) c->device->destroy_target(bs->quarter);
    if (bs->eighth) c->device->destroy_target(bs->eighth);
    if (bs->sixteenth) c->device->destroy_target(bs->sixteenth);

    const i32 hw = (ow + 1) / 2, hh = (oh + 1) / 2;
    const i32 qw = (hw + 1) / 2, qh = (hh + 1) / 2;
    const i32 ew = (qw + 1) / 2, eh = (qh + 1) / 2;
    const i32 sw = (ew + 1) / 2, sh = (eh + 1) / 2;

    bs->half = c->device->create_target(hw, hh);
    bs->quarter = c->device->create_target(qw, qh);
    bs->eighth = c->device->create_target(ew, eh);
    bs->sixteenth = c->device->create_target(sw, sh);
    // A new render target holds whatever the allocator handed out. The blur's
    // scaled blits do not cover every texel of a target (the destination rects
    // are fractional) and bilinear sampling at the edge of the written area
    // reads the rest, so clear them: the result must not depend on stale memory.
    texture_handle previous = c->device->current_target();
    for (texture_handle t : {bs->half, bs->quarter, bs->eighth, bs->sixteenth})
    {
        if (!t) continue;
        c->device->set_target(t);
        c->device->clear(color{0, 0, 0, 0});
    }
    c->device->set_target(previous);
    bs->w = ow;
    bs->h = oh;
    return (bs->half && bs->quarter) ? bs : nullptr;
}

} // namespace detail

void ui::blur(rect r, f32 blur_radius, f32 corner_radius, f32 alpha)
{
    blur(r, blur_radius, corner_radii::all(corner_radius), alpha);
}

void ui::blur(rect r, f32 blur_radius, const corner_radii &radii_in, f32 alpha)
{
    context *c = ctx;
    if (blur_radius < 0.5f || alpha <= 0.0f) return;
    render_surface *surf = c->current_window ? c->current_window->surface : c->surface;
    if (!c->device || !surf) return;
    PUFFERUI_CHECK(VIOL_INVALID_RECT, r.is_valid(), "blur with invalid rect");
    if (detail::outside_paint(c, r)) return; // the composite could not change a pixel

    // No render targets: defined fallback -> solid translucent tint.
    if (!has_cap(c->device->caps(), backend_caps::RENDER_TARGETS))
    {
        draw_rounded_rect(r, color{20, 24, 30, static_cast<u8>(alpha * 255.0f)}, radii_in);
        return;
    }

    texture_handle scene = surf->scene_target();
    if (!scene) return;

    i32 ow = 0, oh = 0;
    surf->output_size(ow, oh);
    if (ow <= 2 || oh <= 2) return;

    detail::dl_flush(c); // everything drawn so far must land in the scene

    const i32 margin = static_cast<i32>(max2(0.0f, min2(blur_radius, 16.0f)));
    i32 rx = static_cast<i32>(std::floor(r.x)) - margin;
    i32 ry = static_cast<i32>(std::floor(r.y)) - margin;
    i32 rw = static_cast<i32>(std::ceil(r.right())) + margin - rx;
    i32 rh = static_cast<i32>(std::ceil(r.bottom())) + margin - ry;
    if (rx < 0)
    {
        rw += rx;
        rx = 0;
    }
    if (ry < 0)
    {
        rh += ry;
        ry = 0;
    }
    if (rx + rw > ow) rw = ow - rx;
    if (ry + rh > oh) rh = oh - ry;
    if (rw <= 2 || rh <= 2) return;

    blur_store *bs = detail::ensure_blur(c, ow, oh);
    if (!bs) return;

    const f32 fw = static_cast<f32>(rw), fh = static_cast<f32>(rh);
    const rect src{static_cast<f32>(rx), static_cast<f32>(ry), fw, fh};
    const rect half_dst{0.0f, 0.0f, fw * 0.5f, fh * 0.5f};
    const rect quarter_dst{0.0f, 0.0f, fw * 0.25f, fh * 0.25f};
    const rect eighth_dst{0.0f, 0.0f, fw * 0.125f, fh * 0.125f};
    const rect sixteenth_dst{0.0f, 0.0f, fw * 0.0625f, fh * 0.0625f};

    // A scaled blit is the only sampling primitive a backend must provide, so the
    // requested radius is approximated by how deep the pyramid goes (each level
    // roughly doubles the smoothing) plus extra down/up round trips at that level
    // for finer steps. The response is monotonic but stepwise, not linear.
    const i32 level = (blur_radius > 20.0f && bs->sixteenth) ? 4
                      : (blur_radius > 4.0f && bs->eighth)   ? 3
                                                             : 2;
    i32 extra = 0;
    if (level == 2)
        extra = static_cast<i32>(blur_radius - 2.0f);
    else if (level == 3)
        extra = static_cast<i32>((blur_radius - 6.0f) / 4.0f);
    else
        extra = static_cast<i32>((blur_radius - 20.0f) / 8.0f);
    if (extra < 0) extra = 0;
    if (extra > 4) extra = 4;

    c->device->blit(scene, &src, bs->half, &half_dst);
    c->device->blit(bs->half, &half_dst, bs->quarter, &quarter_dst);
    if (level >= 3) c->device->blit(bs->quarter, &quarter_dst, bs->eighth, &eighth_dst);
    if (level >= 4) c->device->blit(bs->eighth, &eighth_dst, bs->sixteenth, &sixteenth_dst);

    for (i32 i = 0; i < extra; ++i)
    {
        if (level >= 4)
        {
            c->device->blit(bs->sixteenth, &sixteenth_dst, bs->eighth, &eighth_dst);
            c->device->blit(bs->eighth, &eighth_dst, bs->sixteenth, &sixteenth_dst);
        }
        else if (level >= 3)
        {
            c->device->blit(bs->eighth, &eighth_dst, bs->quarter, &quarter_dst);
            c->device->blit(bs->quarter, &quarter_dst, bs->eighth, &eighth_dst);
        }
        else
        {
            c->device->blit(bs->quarter, &quarter_dst, bs->half, &half_dst);
            c->device->blit(bs->half, &half_dst, bs->quarter, &quarter_dst);
        }
    }

    // Upsample back to the quarter, which the composite samples.
    if (level >= 4) c->device->blit(bs->sixteenth, &sixteenth_dst, bs->eighth, &eighth_dst);
    if (level >= 3) c->device->blit(bs->eighth, &eighth_dst, bs->quarter, &quarter_dst);

    // composite the blurred quarter back over the scene
    c->device->set_target(scene);
    const i32 qw = (ow + 3) / 4, qh = (oh + 3) / 4;
    // The quarter texture's origin is the *source* rect, which sits `margin`
    // pixels up/left of the user rect (sampling must reach outside for a correct
    // blur). UVs therefore come from each vertex's scene position - using 0..1/pts
    // would shift the blurred image by `margin` and pull outside content in.
    const f32 du = 0.25f / static_cast<f32>(qw);
    const f32 dv = 0.25f / static_cast<f32>(qh);
    const f32 ox = static_cast<f32>(rx), oy = static_cast<f32>(ry);
    const color col{255, 255, 255, static_cast<u8>(clampf(alpha * 255.0f, 0.0f, 255.0f))};

    const corner_radii radii = detail::clamp_radii(r, radii_in);
    if (radii.tl == 0.0f && radii.tr == 0.0f && radii.br == 0.0f && radii.bl == 0.0f)
    {
        vertex v[4] = {
            {r.x, r.y, (r.x - ox) * du, (r.y - oy) * dv, col},
            {r.right(), r.y, (r.right() - ox) * du, (r.y - oy) * dv, col},
            {r.right(), r.bottom(), (r.right() - ox) * du, (r.bottom() - oy) * dv, col},
            {r.x, r.bottom(), (r.x - ox) * du, (r.bottom() - oy) * dv, col},
        };
        i32 idx[6] = {0, 1, 2, 0, 2, 3};
        c->device->draw(bs->quarter, v, 4, idx, 6);
        return;
    }

    // Rounded composite: the same fan + 1px feather ring draw_rounded_rect uses,
    // with UVs taken from each vertex's scene position.
    if (!c->dl) return;
    std::vector<vertex> &verts = c->dl->scratch_v;
    std::vector<i32> &idx = c->dl->scratch_i;
    verts.clear();
    idx.clear();
    detail::build_rounded_fan(
        r, radii, col,
        [&](f32 x, f32 y, f32 &out_u, f32 &out_v)
        {
            out_u = (x - ox) * du;
            out_v = (y - oy) * dv;
        },
        verts, idx);
    c->device->draw(bs->quarter, verts.data(), static_cast<i32>(verts.size()), idx.data(),
                    static_cast<i32>(idx.size()));
}
