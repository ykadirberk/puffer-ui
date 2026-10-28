// pufferui/impl/renderer_mesh_state.inl - explicit mesh backend preparation.
// Included by pufferui.h inside `namespace pui` when PUFFERUI_IMPLEMENTATION is
// defined; never include it directly. The slices concatenate to the original
// implementation block in order (tools/amalgamate.ps1 rebuilds the single header).
// ---- draw list (batching by clip + texture) ----
struct draw_list
{
    std::vector<vertex> verts;
    std::vector<i32> idx;
    texture_handle cur_tex = nullptr;
    bool tex_known = false;
    bool clip_active = false;
    rect clip{};
    bool clip_known = false;
    bool solid_uv_remap = false; // the in-flight draw_triangles call is solid
    // Reusable scratch for primitives that build local geometry before the
    // batch copy (draw_sector / draw_polygon): no per-call heap allocation
    // after warm-up.
    std::vector<vertex> scratch_v;
    std::vector<i32> scratch_i;
    u8 sector_preparation = 0; // 0 unused, 1 first use, 2 bounded domain prepared
    // Cold setup metadata must not displace the hot texture/clip batch fields.
    usize prepared_vertices = 0, prepared_indices = 0;
};

namespace renderer_detail
{
struct mesh_painter : render_painter
{
    context *ctx;
    draw_list storage;
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    render_device *native_device = nullptr;
    bool (*native_glyph)(render_device *, context *, font_handle, u32, vec2, f32, color) = nullptr;
#endif
    bool vectors = false;
    explicit mesh_painter(context &c) : ctx(&c) {}
    void begin() override;
    void end() override;
    void invalidate() override;
    void flush() override;
    bool prepare(const geometry_capacity &) override;
    draw_list *geometry() override { return &storage; }
    bool vector_text() const override { return vectors; }
    f32 measure_text_width(std::string_view s) override { return text_width(s); }
    f32 glyph_advance(const paint_glyph_metric &) override;
    void release_effects(blur_store *&) override;
    void release_text() override;
    void mesh(const paint_mesh &p) override
    {
        draw_triangles(p.texture, p.vertices, p.vertex_count, p.indices, p.index_count);
    }
    void rectangle(const paint_rect &p) override { draw_rect(p.area, p.tint); }
    void line(const paint_line &p) override
    {
        draw_line(p.from.x, p.from.y, p.to.x, p.to.y, p.tint, p.thickness);
    }
    void rounded_rectangle(const paint_rounded_rect &p) override
    {
        draw_rounded_rect(p.area, p.tint, p.radii);
    }
    void image(const paint_image &p) override { draw_image(p.image, p.area); }
    rect fitted_image(const paint_fitted_image &p) override
    {
        return draw_fitted_image(p.area, p.image, p.options);
    }
    void nine_slice(const paint_image &p) override { draw_nine_slice(p.image, p.area); }
    void polygon(const paint_polygon &p) override { draw_polygon(p.points, p.tint); }
    void sector(const paint_sector &p) override
    {
        draw_sector(p.center, p.inner_radius, p.outer_radius, p.start_angle, p.end_angle, p.tint);
    }
    void arc(const paint_arc &p) override
    {
        draw_arc(p.center, p.radius, p.thickness, p.start_angle, p.end_angle, p.tint);
    }
    void blur(const paint_blur &p) override { blur(p.area, p.radius, p.radii, p.alpha); }
    void text(const paint_text &p) override { text(p.area, p.text, p.tint, p.alignment); }
    void text_ellipsis(const paint_text &p) override
    {
        text_ellipsis(p.area, p.text, p.tint, p.alignment);
    }
    void text_wrapped(const paint_text &p) override { text_wrapped(p.area, p.text, p.tint); }
    void ring(const paint_ring &) override;
    void stroke(const paint_line &) override;
    void gradient(const paint_gradient &) override;
    void rounded_gradient(const paint_rounded_gradient &) override;
    void draw_triangles(texture_handle, const vertex *, i32, const i32 *, i32);
    void append_triangles(texture_handle, const vertex *, i32, const i32 *, i32);
    void draw_rect(rect, color);
    void draw_line(f32, f32, f32, f32, color, f32);
    void draw_rounded_rect(rect, color, f32);
    void draw_rounded_rect(rect, color, const corner_radii &);
    void draw_image(const skin_image &, rect);
    rect draw_fitted_image(rect, const image_handle &, const image_opts &);
    void draw_nine_slice(const skin_image &, rect);
    void draw_polygon(std::span<const vec2>, color);
    void draw_sector(vec2, f32, f32, f32, f32, color);
    void draw_arc(vec2, f32, f32, f32, f32, color);
    void blur(rect, f32, f32, f32);
    void blur(rect, f32, const corner_radii &, f32);
    void text(rect, std::string_view, color, align);
    void text_ellipsis(rect, std::string_view, color, align);
    void text_wrapped(rect, std::string_view, color);
    f32 text_width(std::string_view s);
    f32 line_height() { return ui(ctx).line_height(); }
};
} // namespace renderer_detail

// Open-addressing map with inline values: power-of-two capacity, linear
// probing, no tombstones (wholesale clears or rebuilds only). Keys are uiid
// hashes (u64); the value type must be trivially relocatable enough for the
// growth re-insert. O(1) expected; two cache lines worst case.

namespace detail
{

inline void mesh_flush(context *c)
{
    if (!c->dl) return;
    draw_list &dl = *c->dl;
    if (!dl.verts.empty() && c->device)
    {
        c->device->draw(dl.tex_known ? dl.cur_tex : nullptr, dl.verts.data(),
                        static_cast<i32>(dl.verts.size()), dl.idx.data(),
                        static_cast<i32>(dl.idx.size()));
    }
    dl.verts.clear();
    dl.idx.clear();
}

inline void dl_flush(context *c)
{
    if (c->painter) c->painter->flush();
}

inline void dl_prepare(context *c, texture_handle tex, const rect *clip)
{
    if (!c->dl) return;
    draw_list &dl = *c->dl;

    bool want_active = clip != nullptr && clip->w > 0.0f && clip->h > 0.0f;
    rect want = want_active ? *clip : rect{};
    bool clip_changed = !dl.clip_known || dl.clip_active != want_active ||
                        (want_active && (dl.clip.x != want.x || dl.clip.y != want.y ||
                                         dl.clip.w != want.w || dl.clip.h != want.h));
    if (clip_changed)
    {
        dl_flush(c);
        const f32 s = c->render_scale;
        const rect pixels{want.x * s, want.y * s, want.w * s, want.h * s};
        if (c->device) c->device->set_clip(want_active ? &pixels : nullptr);
        dl.clip_active = want_active;
        dl.clip = want;
        dl.clip_known = true;
    }
    if (!dl.tex_known || dl.cur_tex != tex)
    {
        dl_flush(c);
        dl.cur_tex = tex;
        dl.tex_known = true;
    }
}

// The area a draw can still change: the open window frame's client area
// intersected with the active clip. A zero-size client area paints nothing. False
// when nothing bounds the draw (no frame open, no clip): `screen` is only
// meaningful while a frame is open, it is left stale after end_frame.
inline bool paint_bounds(const context *c, rect &out)
{
    bool bounded = false;
    rect r{};
    if (c->current_window)
    {
        // A popup recording into an overlay paints its own rect, which lies
        // (partly) outside the window by definition.
        r = c->overlay_depth > 0 ? c->overlay_area : c->screen;
        bounded = true;
    }
    if (c->clip_depth > 0)
    {
        const rect &top = c->clip_stack[c->clip_depth - 1];
        r = bounded ? rect::intersect(r, top) : top;
        bounded = true;
    }
    out = r;
    return bounded;
}

// True when a draw confined to `box` (feather included) cannot change a pixel: it
// lies wholly outside the client area or the active clip, or the clip is empty.
// Callers skip the draw (and the geometry they would build) entirely.
inline bool outside_paint(const context *c, const rect &box)
{
    rect pb;
    if (!paint_bounds(c, pb)) return false;
    if (pb.w <= 0.0f || pb.h <= 0.0f) return true;
    return box.right() <= pb.x || box.x >= pb.right() || box.bottom() <= pb.y ||
           box.y >= pb.bottom();
}

// How far past a shape's geometric bounds its antialiasing feather can reach.
inline constexpr f32 FEATHER_REACH = 1.5f;

// outside_paint for a shape whose geometry (feather excluded) fits in `box`.
inline bool outside_paint_feathered(const context *c, const rect &box)
{
    return outside_paint(c, box.pad(-FEATHER_REACH));
}

inline usize dl_vertex_capacity(usize current, usize required, usize limit)
{
    if (current > limit || required > limit) return 0;
    if (required <= current) return current;
    const usize grown = current <= limit / 2 ? current * 2 : limit;
    const usize initial = limit < 64 ? limit : 64;
    const usize target = grown > initial ? grown : initial;
    return required > target ? required : target;
}

// Commit the requested prefix while preserving live elements and logical size.
// The marker survives clear(), but a normal relocation invalidates its tail.
template <class T>
inline void prepare_geometry_storage(std::vector<T> &storage, usize count, usize &prepared)
{
    if (count <= prepared) return;
    const usize live = storage.size();
    if (count > storage.capacity())
    {
        storage.reserve(count);
        prepared = live;
    }
    if (count > live)
    {
        storage.resize(count);
        storage.resize(live);
    }
    prepared = count > live ? count : live;
}

inline void dl_add(context *c, const vertex *verts, i32 vcount, const i32 *idx, i32 icount)
{
    if (!c->dl || vcount <= 0 || icount <= 0) return;
    draw_list &dl = *c->dl;
    i32 base = static_cast<i32>(dl.verts.size());
    // Match the index buffer's geometric growth. MSVC's smaller insert growth
    // otherwise relocates the same large batch in consecutive motion frames.
    const usize required_vertices = dl.verts.size() + static_cast<usize>(vcount);
    if (required_vertices > dl.verts.capacity())
    {
        dl.verts.reserve(
            dl_vertex_capacity(dl.verts.capacity(), required_vertices, dl.verts.max_size()));
        dl.prepared_vertices = dl.verts.size();
    }
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
    // Grow geometrically, then append in one pass without a capacity check per
    // index. Exact-size reserve on every primitive would relocate the batch.
    const usize first_index = dl.idx.size();
    const usize required = first_index + static_cast<usize>(icount);
    if (required > dl.idx.capacity())
    {
        const usize limit = dl.idx.max_size();
        const usize grown = dl.idx.capacity() <= limit / 2 ? dl.idx.capacity() * 2 : limit;
        dl.idx.reserve(std::max(required, std::max(static_cast<usize>(64), grown)));
        dl.prepared_indices = dl.idx.size();
    }
    dl.idx.resize(required);
    i32 *out = dl.idx.data() + first_index;
    for (i32 i = 0; i < icount; ++i) out[i] = idx[i] + base;
}

} // namespace detail

bool renderer_detail::mesh_painter::prepare(const geometry_capacity &capacity)
{
    if (!ctx || !ctx->dl) return false;
    draw_list &dl = *ctx->dl;
    constexpr usize limit = static_cast<usize>(0x7fffffff);
    if (capacity.vertices > limit || capacity.indices > limit ||
        capacity.vertices > dl.verts.max_size() || capacity.indices > dl.idx.max_size())
        return false;
    detail::prepare_geometry_storage(dl.verts, capacity.vertices, dl.prepared_vertices);
    detail::prepare_geometry_storage(dl.idx, capacity.indices, dl.prepared_indices);
    return true;
}

void renderer_detail::mesh_painter::begin()
{
    if (storage.sector_preparation == 1)
    {
        // The bounded arc domain is known after its first use. Prepare the
        // whole domain once at a frame boundary, including colliding topology
        // slots, so changing angle/radius does not grow storage while drawing.
        storage.scratch_v.reserve(4 * 129);
        storage.scratch_i.reserve(128 * 3 * 6);
        for (auto &slot : ctx->st->sectors) slot.indices.reserve(128 * 3 * 6);
        storage.sector_preparation = 2;
    }
    if (ctx->overlay_depth == 0 && !ctx->blur) ctx->blur = new blur_store();
    if (ctx->device && ctx->overlay_depth == 0) ctx->device->clear(ctx->active_theme.bg);
    storage.verts.clear();
    storage.idx.clear();
    storage.clip_known = false;
    storage.clip_active = false;
    storage.tex_known = false;
    storage.cur_tex = nullptr;
}
void renderer_detail::mesh_painter::end()
{
    if (ctx->device) ctx->device->set_clip(nullptr);
    invalidate();
}
void renderer_detail::mesh_painter::invalidate()
{
    storage.clip_known = false;
    storage.clip_active = false;
    storage.tex_known = false;
    storage.cur_tex = nullptr;
}
void renderer_detail::mesh_painter::flush()
{
    detail::mesh_flush(ctx);
}
void renderer_detail::mesh_painter::release_effects(blur_store *&bs)
{
    if (!bs) return;
    if (ctx->device)
        for (texture_handle t : {bs->half, bs->quarter, bs->eighth, bs->sixteenth})
            if (t) ctx->device->destroy_target(t);
    delete bs;
    bs = nullptr;
}
void renderer_detail::mesh_painter::release_text()
{
    if (!ctx->ts) return;
    auto &ts = *ctx->ts;
    for (auto &page : ts.atlases)
        if (ctx->device && page.tex) ctx->device->destroy_texture(page.tex);
    ts.atlases.clear();
    ts.glyphs.clear();
    ts.clear_layouts();
    ts.atlas_w = ts.atlas_h = ts.cur_page = 0;
    ++ts.recycled;
}
render_painter *create_mesh_painter(context &c)
{
    return new renderer_detail::mesh_painter(c);
}
