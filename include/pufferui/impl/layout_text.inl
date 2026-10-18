// pufferui/impl/layout_text.inl - part of the PufferUI implementation (see pufferui.h).
// Included by pufferui.h inside `namespace pui` when PUFFERUI_IMPLEMENTATION is
// defined; never include it directly. The slices concatenate to the original
// implementation block in order (tools/amalgamate.ps1 rebuilds the single header).
// ---- tracks & grids --------------------------------------------------------
i32 resolve_track_sizes(f32 available, std::span<const track_size> tracks, f32 *out, i32 max_out)
{
    i32 limit = max_out;
    if (limit < 0) limit = 0;
    const i32 track_count = static_cast<i32>(tracks.size());
    const i32 n = (track_count < limit) ? track_count : limit;
    if (n <= 0) return 0;

    f32 assigned[MAX_TRACKS]{};
    f32 consumed = 0.0f;

    for (i32 i = 0; i < n; ++i)
    {
        const track_size &t = tracks[static_cast<usize>(i)];
        f32 v = 0.0f;
        switch (t.type)
        {
        case track_type::FIXED:
            v = t.value;
            break;
        case track_type::RATIO:
            v = available * t.value;
            break;
        case track_type::FIT_CONTENT:
            // Intrinsic size is unknown to a pure-geometry solver, so the track
            // falls back to its `min_size` (the caller's content estimate).
            v = t.min_size;
            break;
        case track_type::FLEX:
            continue;
        }
        v = clampf(v, t.min_size, t.max_size);
        v = clampf(v, 0.0f, max2(0.0f, available - consumed));
        assigned[i] = v;
        consumed += v;
    }

    f32 pool = max2(0.0f, available - consumed);
    bool open[MAX_TRACKS]{};
    f32 open_weight = 0.0f;
    for (i32 i = 0; i < n; ++i)
    {
        if (tracks[static_cast<usize>(i)].type == track_type::FLEX)
        {
            open[i] = true;
            open_weight += max2(0.001f, tracks[static_cast<usize>(i)].value);
        }
    }

    bool changed = open_weight > 0.0f;
    while (changed)
    {
        changed = false;
        for (i32 i = 0; i < n; ++i)
        {
            if (!open[i]) continue;
            const track_size &t = tracks[static_cast<usize>(i)];
            const f32 w = max2(0.001f, t.value);
            const f32 share = (open_weight > 0.0f) ? (pool * (w / open_weight)) : 0.0f;
            if (share < t.min_size)
            {
                assigned[i] = t.min_size;
                pool = max2(0.0f, pool - t.min_size);
                open_weight = max2(0.0f, open_weight - w);
                open[i] = false;
                changed = true;
            }
            else if (share > t.max_size)
            {
                assigned[i] = t.max_size;
                pool = max2(0.0f, pool - t.max_size);
                open_weight = max2(0.0f, open_weight - w);
                open[i] = false;
                changed = true;
            }
        }
    }

    if (open_weight > 0.0f)
    {
        for (i32 i = 0; i < n; ++i)
        {
            if (!open[i]) continue;
            const track_size &t = tracks[static_cast<usize>(i)];
            assigned[i] =
                clampf((pool / open_weight) * max2(0.001f, t.value), t.min_size, t.max_size);
        }
    }

    for (i32 i = 0; i < n; ++i) out[i] = assigned[i];
    return n;
}

track_row::track_row(rect area, std::span<const track_size> tracks, f32 gap)
    : bounds_(area), gap_(max2(0.0f, gap))
{
    const f32 total_gap = gap_ * static_cast<f32>(tracks.size() > 1 ? tracks.size() - 1 : 0);
    count_ = resolve_track_sizes(max2(0.0f, area.w - total_gap), tracks, sizes_, MAX_TRACKS);
}

rect track_row::next(f32 height)
{
    if (index_ >= count_) return {};
    const f32 w = clampf(sizes_[index_], 0.0f, max2(0.0f, bounds_.w));
    const f32 h = (height > 0.0f) ? min2(height, bounds_.h) : bounds_.h;
    rect r{bounds_.x, bounds_.y, w, h};
    bounds_.x += w + gap_;
    bounds_.w = max2(0.0f, bounds_.w - (w + gap_));
    ++index_;
    return r;
}

rect track_row::remaining() const
{
    f32 w = 0.0f;
    for (i32 i = index_; i < count_; ++i) w += sizes_[i] + (i > index_ ? gap_ : 0.0f);
    return rect{bounds_.x, bounds_.y, min2(w, bounds_.w), bounds_.h};
}

track_column::track_column(rect area, std::span<const track_size> tracks, f32 gap)
    : bounds_(area), gap_(max2(0.0f, gap))
{
    const f32 total_gap = gap_ * static_cast<f32>(tracks.size() > 1 ? tracks.size() - 1 : 0);
    count_ = resolve_track_sizes(max2(0.0f, area.h - total_gap), tracks, sizes_, MAX_TRACKS);
}

rect track_column::next(f32 width)
{
    if (index_ >= count_) return {};
    const f32 h = clampf(sizes_[index_], 0.0f, max2(0.0f, bounds_.h));
    const f32 w = (width > 0.0f) ? min2(width, bounds_.w) : bounds_.w;
    rect r{bounds_.x, bounds_.y, w, h};
    bounds_.y += h + gap_;
    bounds_.h = max2(0.0f, bounds_.h - (h + gap_));
    ++index_;
    return r;
}

rect track_column::remaining() const
{
    f32 h = 0.0f;
    for (i32 i = index_; i < count_; ++i) h += sizes_[i] + (i > index_ ? gap_ : 0.0f);
    return rect{bounds_.x, bounds_.y, bounds_.w, min2(h, bounds_.h)};
}

grid_cursor auto_fit_grid(rect container, i32 total_items, f32 min_item_w, f32 item_h, f32 gap)
{
    grid_cursor g;
    g.bounds_ = container;
    g.gap_ = gap;
    if (total_items <= 0 || container.w <= 0.0f) return g;
    if (!(min_item_w > 0.0f)) min_item_w = 1.0f; // guards divide-by-zero / NaN
    if (!(gap >= 0.0f) || gap >= min_item_w) gap = 0.0f;
    g.gap_ = gap;

    i32 cols = static_cast<i32>((container.w + gap) / (min_item_w + gap));
    if (cols < 1) cols = 1;
    if (cols > total_items) cols = total_items;
    g.columns_ = cols;
    g.rows_ = (total_items + cols - 1) / cols;
    g.item_w_ = (container.w - gap * static_cast<f32>(cols - 1)) / static_cast<f32>(cols);
    g.item_h_ = item_h;
    g.count_ = total_items;
    return g;
}

rect grid_cursor::cell(i32 i) const
{
    if (i < 0 || i >= count_ || columns_ <= 0) return {};
    const i32 col = i % columns_;
    const i32 row = i / columns_;
    return rect::make(bounds_.x + static_cast<f32>(col) * (item_w_ + gap_),
                      bounds_.y + static_cast<f32>(row) * (item_h_ + gap_), item_w_, item_h_);
}

rect grid_cursor::next()
{
    if (index_ >= count_) return {};
    return cell(index_++);
}

[[nodiscard]] rect column::cut_top(f32 height) &
{
    return next(height);
}

[[nodiscard]] rect column::cut_bottom(f32 height) &
{
    const f32 h = clampf(height, 0.0f, max2(0.0f, bounds_.h));
    detail::report_slice_clamp("column cut_bottom", height, max2(0.0f, bounds_.h));
    rect r = bounds_.cut_bottom(h);
    bounds_.cut_bottom(min2(gap_, bounds_.h));
    return r;
}

[[nodiscard]] rect row::cut_left(f32 width) &
{
    const f32 w = clampf(width, 0.0f, max2(0.0f, bounds_.w));
    detail::report_slice_clamp("row cut_left", width, max2(0.0f, bounds_.w));
    rect r = bounds_.cut_left(w);
    bounds_.cut_left(min2(gap_, bounds_.w));
    return r;
}

[[nodiscard]] rect row::cut_right(f32 width) &
{
    const f32 w = clampf(width, 0.0f, max2(0.0f, bounds_.w));
    detail::report_slice_clamp("row cut_right", width, max2(0.0f, bounds_.w));
    rect r = bounds_.cut_right(w);
    bounds_.cut_right(min2(gap_, bounds_.w));
    return r;
}

void set_report_layout_overflow(context *c, bool enabled)
{
    if (!c) return;
    c->report_layout_overflow = enabled;
    if (enabled) c->overflow_count = 0;
}

i32 layout_overflow_count(context *c)
{
    return c ? c->overflow_count : 0;
}

void draw_violation_overlay(ui &u, rect r)
{
    context *c = u.ctx;
    if (!c || c->violations == 0) return;
    const theme &t = u.th();
    u.draw_rect(r, color{20, 24, 30, 230});
    u.draw_line(r.x, r.y, r.right(), r.y, t.accent, 2.0f);
    char head[96];
    std::snprintf(head, sizeof(head), "PufferUI: %d violation(s) [%s]", c->violations,
                  violation_code_name(c->vlast_code));
    rect body = r.pad(8.0f, 4.0f);
    {
        text_scope ts = u.text_style(12.0f);
        u.text(body.cut_top(16.0f), head, t.accent, ALIGN_LEFT);
        // the rest shows the last message, trimmed to fit
        u.text_ellipsis(body, c->vlast, t.text, ALIGN_LEFT);
    }
}

// ---- text ----
font_handle load_font(context *c, const char *path)
{
    if (!c->ts) c->ts = new text_store();
    text_store *ts = c->ts;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return FONT_INVALID;
    std::streamsize size = file.tellg();
    if (size <= 0) return FONT_INVALID;
    file.seekg(0, std::ios::beg);

    font_data fd;
    fd.buffer.resize(static_cast<usize>(size));
    if (!file.read(reinterpret_cast<char *>(fd.buffer.data()), size)) return FONT_INVALID;
    if (!stbtt_InitFont(&fd.info, fd.buffer.data(), 0)) return FONT_INVALID;

    fd.ok = true;
    ts->fonts.push_back(std::move(fd));
    return static_cast<font_handle>(ts->fonts.size() - 1);
}

namespace detail
{

inline bool ensure_atlas(context *c, text_store *ts)
{
    if (!ts->atlases.empty()) return true;
    if (!c->device) return false;
    text_store::atlas_page page;
    page.tex = c->device->create_texture(1024, 1024, nullptr);
    if (!page.tex) return false;
    ts->atlases.push_back(page);
    ts->cur_page = 0;
    ts->atlas_w = 1024;
    ts->atlas_h = 1024;
    // Reserve the (0,0) texel as pure white: solid geometry samples it (see
    // draw_triangles' UV remap), so glyph packing starts at (1,1) and never
    // touches it.
    const u8 white[4] = {255, 255, 255, 255};
    c->device->update_texture(page.tex, 0, 0, 1, 1, white);
    ts->atlases.back().shelf_x = 1;
    ts->atlases.back().shelf_y = 1;
    return true;
}

inline font_handle font_for(context *c, font_handle primary, u32 cp)
{
    if (!c->ts) return primary;
    const auto has = [&](font_handle f)
    {
        return f >= 0 && f < static_cast<i32>(c->ts->fonts.size()) && c->ts->fonts[f].ok &&
               stbtt_FindGlyphIndex(&c->ts->fonts[f].info, static_cast<i32>(cp)) != 0;
    };
    if (has(primary)) return primary;
    for (font_handle f : c->ts->fallbacks)
        if (has(f)) return f;
    return primary;
}

inline i32 gamma_q(const context *c)
{
    return static_cast<i32>(c->active_theme.text_gamma * 100.0f + 0.5f);
}

inline const u8 *coverage_curve(text_store *ts, f32 gamma)
{
    // Keep the raw input, not just gamma_q: two values in the same glyph-key
    // bin can still produce different coverage for a newly rasterized glyph.
    if (!ts->coverage_valid || ts->coverage_gamma != gamma)
    {
        const f32 inv = 1.0f / max2(0.1f, gamma);
        const bool identity = static_cast<i32>(gamma * 100.0f + 0.5f) == 100;
        for (i32 v = 0; v < 256; ++v)
            ts->coverage_curve[v] =
                identity
                    ? static_cast<u8>(v)
                    : static_cast<u8>(std::pow(static_cast<f32>(v) / 255.0f, inv) * 255.0f + 0.5f);
        ts->coverage_gamma = gamma;
        ts->coverage_valid = true;
    }
    return ts->coverage_curve;
}

inline u64 layout_key(context *c, font_handle font, f32 size, std::string_view text)
{
    u64 key = (static_cast<u64>(static_cast<u32>(font)) << 48) ^
              (static_cast<u64>(static_cast<u32>(static_cast<i32>(size * 4.0f) & 0xFFFF)) << 32) ^
              hash(text);
    // Identity in the common case (the goldens' keys stay put); a scale, a
    // coverage curve or a shaper of its own makes a different layout.
    if (c->render_scale != 1.0f) key = id_child(key, static_cast<uiid>(c->render_scale * 1024));
    if (gamma_q(c) != 100) key = id_child(key, static_cast<uiid>(gamma_q(c)) + 0x67616D00ull);
    return key;
}

// A cached glyph copy is stale when its page has been recycled since.
inline bool glyph_live(const text_store *ts, const glyph &g)
{
    return !g.tex || (g.page >= 0 && g.page < static_cast<i32>(ts->atlases.size()) &&
                      ts->atlases[static_cast<usize>(g.page)].gen == g.gen);
}

// Reuses the least recently drawn page for new glyphs - never one used in this
// frame, whose quads may still be queued. False when every page is in use now.
inline bool recycle_atlas_page(context *c, text_store *ts)
{
    i32 victim = -1;
    u64 oldest = ~0ull;
    for (i32 i = 0; i < static_cast<i32>(ts->atlases.size()); ++i)
    {
        const text_store::atlas_page &p = ts->atlases[static_cast<usize>(i)];
        if (p.last_used >= c->frame) continue;
        if (p.last_used < oldest)
        {
            oldest = p.last_used;
            victim = i;
        }
    }
    if (victim < 0) return false;
    text_store::atlas_page &p = ts->atlases[static_cast<usize>(victim)];
    p.gen += 1;
    p.shelf_x = p.shelf_y = victim == 0 ? 1 : 0; // page 0 keeps its white texel at (0,0)
    p.shelf_h = 0;
    for (auto it = ts->glyphs.begin(); it != ts->glyphs.end();)
    {
        if (it->second.page == victim)
            it = ts->glyphs.erase(it);
        else
            ++it;
    }
    ts->cur_page = victim;
    ts->recycled += 1;
    return true;
}

inline glyph get_glyph(context *c, i32 font_index, f32 size, u32 cp, i32 sub = 0)
{
    text_store *ts = c->ts;
    font_index = font_for(c, font_index, cp);
    const f32 density = c->render_scale;
    text_store::glyph_key key{font_index, static_cast<i32>(size * 4.0f),    cp,
                              sub,        static_cast<i32>(density * 1024), gamma_q(c)};
    auto it = ts->glyphs.find(key);
    if (it != ts->glyphs.end()) return it->second;

    glyph g{};
    font_data &fd = ts->fonts[static_cast<usize>(font_index)];
    f32 scale = stbtt_ScaleForPixelHeight(&fd.info, size * density);
    i32 advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&fd.info, static_cast<i32>(cp), &advance, &lsb);
    g.advance = static_cast<f32>(advance) * stbtt_ScaleForPixelHeight(&fd.info, size);

    i32 gw = 0, gh = 0, xo = 0, yo = 0;
    // `sub` shifts the outline right by sub/4 of a pixel before rasterizing.
    unsigned char *bitmap =
        stbtt_GetCodepointBitmapSubpixel(&fd.info, scale, scale, static_cast<f32>(sub) * 0.25f,
                                         0.0f, static_cast<i32>(cp), &gw, &gh, &xo, &yo);
    g.xoff = static_cast<f32>(xo) / density;
    g.yoff = static_cast<f32>(yo) / density;
    g.w = static_cast<f32>(gw) / density;
    g.h = static_cast<f32>(gh) / density;

    if (bitmap && gw > 0 && gh > 0 && ensure_atlas(c, ts))
    {
        // Pack into the current page; when it is full, recycle the least
        // recently drawn page (at the budget) or open a fresh one. The glyph is
        // never silently dropped: past the budget a page is added when every
        // page is in use this frame.
        text_store::atlas_page *cur = &ts->atlases[static_cast<usize>(ts->cur_page)];
        if (cur->shelf_x + gw + 1 > ts->atlas_w)
        {
            cur->shelf_y += cur->shelf_h;
            cur->shelf_h = 0;
            cur->shelf_x = 0;
        }
        if (gh > cur->shelf_h)
        {
            cur->shelf_y += cur->shelf_h;
            cur->shelf_h = gh;
            cur->shelf_x = 0;
        }
        if (cur->shelf_y + gh > ts->atlas_h)
        {
            const bool at_budget =
                ts->budget > 0 && static_cast<i32>(ts->atlases.size()) >= ts->budget;
            if (!(at_budget && recycle_atlas_page(c, ts)))
            {
                text_store::atlas_page fresh;
                fresh.tex = c->device->create_texture(ts->atlas_w, ts->atlas_h, nullptr);
                ts->atlases.push_back(fresh);
                ts->cur_page = static_cast<i32>(ts->atlases.size()) - 1;
            }
            cur = &ts->atlases[static_cast<usize>(ts->cur_page)];
            cur->shelf_h = gh;
        }
        text_store::atlas_page &page = *cur;
        const i32 px = page.shelf_x, py = page.shelf_y;
        page.shelf_x += gw + 1;
        page.last_used = c->frame;
        // The coverage curve (theme::text_gamma) as a lookup table.
        const u8 *curve = coverage_curve(ts, c->active_theme.text_gamma);
        std::vector<u8> rgba(static_cast<usize>(gw) * gh * 4);
        for (i32 n = 0; n < gw * gh; ++n)
        {
            u8 a = curve[bitmap[n]];
            rgba[n * 4 + 0] = 255;
            rgba[n * 4 + 1] = 255;
            rgba[n * 4 + 2] = 255;
            rgba[n * 4 + 3] = a;
        }
        c->device->update_texture(page.tex, px, py, gw, gh, rgba.data());
        g.tex = page.tex;
        g.page = ts->cur_page;
        g.gen = page.gen;
        g.u0 = static_cast<f32>(px) / ts->atlas_w;
        g.v0 = static_cast<f32>(py) / ts->atlas_h;
        g.u1 = static_cast<f32>(px + gw) / ts->atlas_w;
        g.v1 = static_cast<f32>(py + gh) / ts->atlas_h;
    }
    if (bitmap) stbtt_FreeBitmap(bitmap, nullptr);

    ts->glyphs[key] = g;
    return g;
}

// Kerning between two codepoints in the active font, in pixels.
inline f32 kern_advance(context *c, font_handle fh, f32 size, u32 a, u32 b)
{
    if (!a || !b || !c->ts || fh < 0 || fh >= static_cast<i32>(c->ts->fonts.size())) return 0.0f;
    const font_handle af = font_for(c, fh, a), bf = font_for(c, fh, b);
    if (af != bf) return 0.0f;
    fh = af;
    font_data &fd = c->ts->fonts[static_cast<usize>(fh)];
    if (!fd.ok) return 0.0f;
    const f32 scale = stbtt_ScaleForPixelHeight(&fd.info, size);
    return static_cast<f32>(
               stbtt_GetCodepointKernAdvance(&fd.info, static_cast<i32>(a), static_cast<i32>(b))) *
           scale;
}

// The glyph run of `s`: the installed shaper's, or codepoint by codepoint with
// kerning pairs. Shared by measuring and drawing (one cache entry serves both).
inline text_store::text_layout build_layout(context *c, font_handle fh, f32 size,
                                            std::string_view s)
{
    text_store::text_layout built;
    // Fresh copies; a page recycled while they are fetched leaves the epoch
    // behind, so the first draw checks them.
    const i32 epoch = c->ts->recycled;
    const auto add = [&](u32 cp, f32 kern_in)
    {
        text_store::cached_glyph e;
        e.kern_in = kern_in;
        e.cp = cp;
        e.g = get_glyph(c, fh, size, cp);
        // Two separate additions, in this order: float associativity would drift
        // the pen by ULPs, visible as edge coverage changes in the golden scenes.
        built.width += e.kern_in;
        built.width += e.g.advance;
        if (!built.gs.empty() && built.gs.back().g.advance + e.kern_in < 0.0f)
            built.forward = false;
        if (e.g.w > 0.0f) built.min_xoff = min2(built.min_xoff, e.g.xoff);
        built.gs.push_back(e);
    };
    if (c->ts->shaper &&
        c->ts->shaper->shape(c, fh, size, s, [&](const shaped_glyph &g) { add(g.cp, g.kern_in); }))
    {
        built.epoch = epoch;
        return built;
    }
    built = {};
    built.builtin = true;
    built.built_size = size;
    usize i = 0;
    u32 prev = 0;
    while (i < s.size())
    {
        const u32 cp = utf8_decode(s, i);
        if (cp == UTF8_END) break;
        add(cp, kern_advance(c, fh, size, prev, cp));
        prev = cp;
    }
    built.epoch = epoch;
    built.build_epoch = epoch;
    return built;
}

inline text_store::text_layout *get_layout(context *c, font_handle fh, f32 size, std::string_view s)
{
    text_store &ts = *c->ts;
    const u64 key = layout_key(c, fh, size, s);
    if (auto *lay = ts.layout_cache.find(key)) return lay;

    // A normal associative cache would change primary hit/miss history. Since
    // the legacy key quantizes size but kerning uses the raw size, that can
    // change pixels. Retain the original primary slot, and only replace a
    // fresh build with an exactly equivalent, live built-in run.
    text_store::text_layout built;
    const u64 mixed = decltype(ts.layout_cache)::mix(key);
    bool reused = false;
    if (!ts.shaper)
        if (auto *v = ts.layout_victims.find(mixed);
            v && v->builtin && v->built_size == size && v->build_epoch == ts.recycled)
        {
            built = std::move(*v);
            ts.layout_victims.slot_for(mixed).tag = 0;
            reused = true;
        }
    if (!reused) built = build_layout(c, fh, size, s);
    // Build before evicting, as before. A custom shaper keeps its original
    // invocation pattern and is never served by the victim cache.
    auto &primary = ts.layout_cache.slot_for(key);
    if (primary.tag && !ts.shaper) ts.layout_victims.put(primary.tag, std::move(primary.value));
    ts.layout_cache.put(key, std::move(built));
    return ts.layout_cache.find(key);
}
} // namespace detail

void set_text_shaper(context *c, text_shaper *shaper)
{
    if (!c) return;
    if (!c->ts) c->ts = new text_store();
    c->ts->shaper = shaper;
    c->ts->clear_layouts();
    c->redraw_requested = true;
}

bool set_font_fallbacks(context *c, std::span<const font_handle> fonts)
{
    if (!c || !c->ts) return fonts.empty();
    for (font_handle f : fonts)
        if (f < 0 || f >= static_cast<i32>(c->ts->fonts.size()) || !c->ts->fonts[f].ok)
            return false;
    c->ts->fallbacks.assign(fonts.begin(), fonts.end());
    c->ts->clear_layouts();
    c->redraw_requested = true;
    return true;
}

void set_text_atlas_budget(context *c, i32 pages)
{
    if (!c) return;
    if (!c->ts) c->ts = new text_store(); // before the first load_font is fine too
    c->ts->budget = pages < 0 ? 0 : pages;
}

i32 text_atlas_pages(const context *c)
{
    return c && c->ts ? static_cast<i32>(c->ts->atlases.size()) : 0;
}

bool ui::has_glyph(u32 cp)
{
    context *c = ctx;
    if (!c->ts) return false;
    font_handle fh = c->active_theme.font;
    if (fh < 0 || fh >= static_cast<i32>(c->ts->fonts.size())) return false;
    fh = detail::font_for(c, fh, cp);
    font_data &fd = c->ts->fonts[static_cast<usize>(fh)];
    if (!fd.ok) return false;
    return stbtt_FindGlyphIndex(&fd.info, static_cast<i32>(cp)) != 0;
}

bool ui::is_visible(rect r) const
{
    rect pb;
    if (!detail::paint_bounds(ctx, pb)) return true;
    const rect x = rect::intersect(r, pb);
    return x.w > 0.0f && x.h > 0.0f;
}

vec2 ui::button_size(std::string_view label)
{
    const button_style &s = ctx->active_theme.button;
    return vec2{text_width(label) + s.pad_x * 2.0f, ctx->active_theme.control_h};
}

vec2 ui::text_size(std::string_view s)
{
    return vec2{text_width(s), line_height()};
}

f32 ui::text_width(std::string_view s)
{
    context *c = ctx;
    if (!c->ts) return 0.0f;
    font_handle fh = c->active_theme.font;
    if (fh < 0 || fh >= static_cast<i32>(c->ts->fonts.size()) ||
        !c->ts->fonts[static_cast<usize>(fh)].ok)
        return 0.0f;
    if (s.empty()) return 0.0f;

    // The layout cache keys on (font, size, string hash); advances never
    // change for a loaded font, so entries cannot go stale. Building the
    // layout here is fine: a measure almost always precedes a draw of the
    // same string, and the built glyphs serve the emit path next call.
    return detail::get_layout(c, fh, c->active_theme.text_size, s)->width;
}

void ui::textf(rect r, color c, align a, const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt ? fmt : "", args);
    va_end(args);
    text(r, buf, c, a);
}

void ui::text(rect r, std::string_view s, color c, align a)
{
    context *cc = ctx;
    if (s.empty() || !cc->ts) return;
    font_handle fh = cc->active_theme.font;
    if (fh < 0 || fh >= static_cast<i32>(cc->ts->fonts.size()) ||
        !cc->ts->fonts[static_cast<usize>(fh)].ok)
    {
        // Text with no working font draws nothing at all: an authoring
        // mistake the overlay and a debugger should surface.
        report_violation(VIOL_NO_FONT, "text with no font",
                         "text drawn while the theme has no loaded font "
                         "(load_font + set_theme)",
                         "", 0, false);
        return;
    }

    const f32 size = cc->active_theme.text_size;
    // The layout cache is shared with text_width (same key): a hit emits
    // from cached glyph copies — no decode, no glyph-map lookups, no stb
    // calls.
    text_store::text_layout *lay = detail::get_layout(cc, fh, size, s);

    const f32 width = lay->width;
    f32 pen = (a == ALIGN_CENTER)  ? r.x + (r.w - width) * 0.5f
              : (a == ALIGN_RIGHT) ? r.right() - width
                                   : r.x;

    i32 ascent = 0, descent = 0, line_gap = 0;
    font_data &fd = cc->ts->fonts[static_cast<usize>(fh)];
    stbtt_GetFontVMetrics(&fd.info, &ascent, &descent, &line_gap);
    const f32 scale = stbtt_ScaleForPixelHeight(&fd.info, size);
    const f32 baseline = r.y + (r.h - size) * 0.5f + static_cast<f32>(ascent) * scale;

    const rect *clip = (cc->clip_depth > 0) ? &cc->clip_stack[cc->clip_depth - 1] : nullptr;
    // What this run can still paint: the client area intersected with the clip. An
    // empty area paints nothing; a run wholly outside it, or a glyph outside it,
    // is skipped (no quad, no batch break, no rasterization of its subpixel bin).
    rect pb{};
    const bool bounded = detail::paint_bounds(cc, pb);
    if (bounded && (pb.w <= 0.0f || pb.h <= 0.0f)) return;

    // Every quad is drawn on whole pixels. A glyph bitmap sampled at a fractional
    // position is smeared across two pixel columns and looks heavier or lighter
    // depending on where its pen happened to fall; instead the pen is split into
    // a whole pixel and a quarter-pixel bin, and the glyph rasterized for that
    // bin (cached lazily) is stamped at the whole pixel. The baseline is rounded
    // once per line for the same reason. Kerning moves a glyph relative to the
    // one before it, so it is applied before the glyph is placed (text_width sums
    // the same terms, so measurement and drawing agree).
    const f32 density = cc->render_scale;
    const f32 base_y = std::floor(baseline * density + 0.5f) / density;
    if (bounded)
    {
        // The font's own extents plus half an em of slack for marks that overhang
        // them (diacritics above, tails below) and for sideways overhang.
        const f32 slack = size * 0.5f;
        const f32 up = static_cast<f32>(ascent) * scale + slack;
        const f32 down = static_cast<f32>(-descent) * scale + slack;
        if (pen - slack >= pb.right() || pen + width + slack <= pb.x ||
            base_y - up >= pb.bottom() || base_y + down <= pb.y)
            return;
    }
    // Only a run that reaches past the right edge can end early (see below).
    const bool may_exit = bounded && lay->forward && pen + width + size >= pb.right();
    // Glyph copies go stale only when an atlas page is recycled (a budget was
    // reached). While none was since the run was last validated, skip the
    // per-glyph checks; otherwise check every glyph this loop reaches, and
    // when that was all of them, the run is current again.
    text_store *ts = cc->ts;
    i32 seen = ts->recycled;
    bool checking = lay->epoch != seen;
    bool all_checked = true;
    const auto fetched = [&]() // after any get_glyph: it may have recycled a page
    {
        if (ts->recycled == seen) return;
        seen = ts->recycled;
        checking = true;
        all_checked = false; // glyphs passed earlier went unchecked
    };
    i32 stamped = -1; // the page last given this frame's LRU stamp
    for (text_store::cached_glyph &e : lay->gs)
    {
        pen += e.kern_in;
        f32 ix = std::floor(pen * density);
        i32 bin = static_cast<i32>((pen * density - ix) * 4.0f + 0.5f);
        if (bin >= 4)
        {
            bin = 0;
            ix += 1.0f;
        }
        ix /= density;
        // The bin-0 bitmap's box; a sub-pixel bin's bitmap differs from it by
        // under a pixel, so GLYPH_BIN_SLACK on every side keeps it inside.
        constexpr f32 GLYPH_BIN_SLACK = 1.0f;
        // Past the right edge for good: with a pen that only moves right, no
        // later glyph's box can start left of this one's leftmost possible box,
        // so the rest of the run would be culled glyph by glyph below.
        if (may_exit && ix + lay->min_xoff - GLYPH_BIN_SLACK >= pb.right())
        {
            all_checked = false;
            break;
        }
        if (bounded && e.g.w > 0.0f)
        {
            const f32 x0 = ix + e.g.xoff - GLYPH_BIN_SLACK;
            const f32 y0 = base_y + e.g.yoff - GLYPH_BIN_SLACK;
            const f32 w = e.g.w + 2.0f * GLYPH_BIN_SLACK, h = e.g.h + 2.0f * GLYPH_BIN_SLACK;
            if (x0 >= pb.right() || x0 + w <= pb.x || y0 >= pb.bottom() || y0 + h <= pb.y)
            {
                all_checked = false;
                pen += e.g.advance;
                continue;
            }
        }
        if (checking)
        {
            if (!detail::glyph_live(ts, e.g)) // its page was recycled: fetch it again
            {
                e.g = detail::get_glyph(cc, fh, size, e.cp);
                e.have = 0;
                fetched();
            }
            for (i32 b = 1; b < 4; ++b) // and forget sub-bin copies that went stale
                if ((e.have & (1u << b)) && !detail::glyph_live(ts, e.sub[b - 1]))
                    e.have = static_cast<u8>(e.have & ~(1u << b));
        }
        const glyph *gp = &e.g;
        if (bin > 0)
        {
            if (!(e.have & (1u << bin)))
            {
                e.sub[bin - 1] = detail::get_glyph(cc, fh, size, e.cp, bin);
                e.have = static_cast<u8>(e.have | (1u << bin));
                fetched();
            }
            gp = &e.sub[bin - 1];
        }
        const glyph &g = *gp;
        if (g.tex)
        {
            if (g.page != stamped) // LRU stamp, once per page and run
            {
                ts->atlases[static_cast<usize>(g.page)].last_used = cc->frame;
                stamped = g.page;
            }
            detail::dl_prepare(cc, g.tex, clip);
            const f32 gx = ix + g.xoff;
            const f32 gy = base_y + g.yoff;
            vertex v[4] = {
                {gx, gy, g.u0, g.v0, c},
                {gx + g.w, gy, g.u1, g.v0, c},
                {gx + g.w, gy + g.h, g.u1, g.v1, c},
                {gx, gy + g.h, g.u0, g.v1, c},
            };
            i32 idx[6] = {0, 1, 2, 0, 2, 3};
            detail::dl_add(cc, v, 4, idx, 6);
        }
        pen += e.g.advance;
    }
    if (checking && all_checked && ts->recycled == seen) lay->epoch = seen;
}

// Truncates with a trailing ellipsis (U+2026) when the line does not fit.
void ui::text_ellipsis(rect r, std::string_view s, color c, align a)
{
    if (s.empty()) return;
    if (text_width(s) <= r.w)
    {
        text(r, s, c, a);
        return;
    }
    static constexpr char ELLIPSIS[] = "\xE2\x80\xA6";
    const f32 ell_w = text_width(ELLIPSIS);
    // One forward pass accumulating the same per-glyph advances text_width
    // would sum (kern pairs included): stop at the last codepoint boundary
    // whose prefix still fits next to the ellipsis. No prefix re-measures.
    context *cc = ctx;
    font_handle fh = cc->active_theme.font;
    usize i = 0, cut = 0;
    f32 x = 0.0f;
    u32 prev = 0;
    // Re-find after measuring the ellipsis: that lookup can evict a cache
    // entry. Never retain a layout pointer across another layout insertion.
    const f32 size = cc->active_theme.text_size;
    text_store::text_layout *lay =
        cc->ts ? cc->ts->layout_cache.find(detail::layout_key(cc, fh, size, s)) : nullptr;
    if (lay && lay->ellipsis_valid && lay->ellipsis_width == r.w && lay->ellipsis_size == size &&
        lay->ellipsis_tail_width == ell_w && lay->ellipsis_epoch == cc->ts->recycled)
        cut = lay->ellipsis_cut;
    else
    {
        usize glyph_index = 0;
        const bool cached =
            lay && lay->builtin && lay->built_size == size && lay->epoch == cc->ts->recycled;
        while (i < s.size())
        {
            const u32 cp = utf8_decode(s, i);
            if (cp == UTF8_END) break;
            if (cached)
            {
                const auto &g = lay->gs[glyph_index++];
                x += g.kern_in;
                x += g.g.advance;
            }
            else // Preserve the original codepoint cut for custom shapers.
            {
                x += detail::kern_advance(cc, fh, size, prev, cp);
                x += detail::get_glyph(cc, fh, size, cp).advance;
            }
            if (x + ell_w <= r.w) cut = i;
            prev = cp;
        }
        if (lay)
        {
            lay->ellipsis_width = r.w;
            lay->ellipsis_size = size;
            lay->ellipsis_cut = cut;
            lay->ellipsis_tail_width = ell_w;
            // A slow scan may recycle an atlas halfway through. Keep the
            // source's validation epoch so such a cut is retried next time.
            lay->ellipsis_epoch = lay->epoch;
            lay->ellipsis_valid = true;
        }
    }
    std::string out(s.substr(0, cut));
    out += ELLIPSIS;
    text(r, out, c, a);
}

text_scope::text_scope(ui &u, f32 size, font_handle font) : u_(&u), size_(size), font_(font)
{
    old_size_ = u.ctx->active_theme.text_size;
    old_font_ = u.ctx->active_theme.font;
    u.ctx->active_theme.text_size = size;
    if (font != FONT_INVALID) u.ctx->active_theme.font = font;
}

text_scope::~text_scope()
{
    if (!u_ || !u_->ctx) return;
    u_->ctx->active_theme.text_size = old_size_;
    u_->ctx->active_theme.font = old_font_;
}

text_scope ui::text_style(f32 size, font_handle font)
{
    return text_scope(*this, size, font);
}

measure_size ui::measure_text(std::string_view s, f32 available_width)
{
    const f32 line_h = line_height();
    if (s.empty()) return {0.0f, line_h};
    if (available_width <= 0.0f) available_width = 1.0e9f;

    const f32 space_w = text_width(" ");
    f32 line_w = 0.0f, max_w = 0.0f;
    i32 lines = 1;
    std::string word;

    auto flush = [&]()
    {
        if (word.empty()) return;
        const f32 ww = text_width(word);
        if (line_w > 0.0f && line_w + space_w + ww > available_width)
        {
            max_w = max2(max_w, line_w);
            line_w = ww;
            lines += 1;
        }
        else
        {
            line_w += (line_w > 0.0f ? space_w : 0.0f) + ww;
        }
        word.clear();
    };

    for (char ch : s)
    {
        if (ch == ' ' || ch == '\n')
        {
            flush();
            if (ch == '\n')
            {
                max_w = max2(max_w, line_w);
                line_w = 0.0f;
                lines += 1;
            }
        }
        else
        {
            word.push_back(ch);
        }
    }
    flush();
    max_w = max2(max_w, line_w);
    return {min2(max_w, available_width), static_cast<f32>(lines) * line_h};
}

measure_size ui::measure(function_ref<measure_size(f32)> fn, f32 available_width)
{
    if (!fn) return {};
    return fn(available_width);
}

void ui::text_wrapped(rect r, std::string_view s, color c)
{
    if (s.empty()) return;
    const f32 line_h = line_height();
    const f32 space_w = text_width(" ");
    f32 x = r.x, y = r.y, line_w = 0.0f;
    std::string word;

    auto flush = [&]()
    {
        if (word.empty()) return;
        const f32 ww = text_width(word);
        if (line_w > 0.0f && line_w + space_w + ww > r.w)
        {
            y += line_h;
            x = r.x;
            line_w = 0.0f;
        }
        if (y + line_h <= r.bottom() + 0.5f)
        {
            text(rect::make(x, y, ww, line_h), word, c, ALIGN_LEFT);
            x += ww + space_w;
            line_w += (line_w > 0.0f ? space_w : 0.0f) + ww;
        }
        word.clear();
    };

    for (char ch : s)
    {
        if (ch == ' ' || ch == '\n')
        {
            flush();
            if (ch == '\n')
            {
                y += line_h;
                x = r.x;
                line_w = 0.0f;
            }
        }
        else
        {
            word.push_back(ch);
        }
    }
    flush();
}
