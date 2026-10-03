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

inline glyph get_glyph(context *c, i32 font_index, f32 size, u32 cp, i32 sub = 0)
{
    text_store *ts = c->ts;
    text_store::glyph_key key{font_index, static_cast<i32>(size * 4.0f), cp, sub};
    auto it = ts->glyphs.find(key);
    if (it != ts->glyphs.end()) return it->second;

    glyph g{};
    font_data &fd = ts->fonts[static_cast<usize>(font_index)];
    f32 scale = stbtt_ScaleForPixelHeight(&fd.info, size);
    i32 advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&fd.info, static_cast<i32>(cp), &advance, &lsb);
    g.advance = static_cast<f32>(advance) * scale;

    i32 gw = 0, gh = 0, xo = 0, yo = 0;
    // `sub` shifts the outline right by sub/4 of a pixel before rasterizing.
    unsigned char *bitmap =
        stbtt_GetCodepointBitmapSubpixel(&fd.info, scale, scale, static_cast<f32>(sub) * 0.25f,
                                         0.0f, static_cast<i32>(cp), &gw, &gh, &xo, &yo);
    g.xoff = static_cast<f32>(xo);
    g.yoff = static_cast<f32>(yo);
    g.w = static_cast<f32>(gw);
    g.h = static_cast<f32>(gh);

    if (bitmap && gw > 0 && gh > 0 && ensure_atlas(c, ts))
    {
        // Pack into the current page; spill to a new page when full.
        if (ts->atlases.back().shelf_x + gw + 1 > ts->atlas_w)
        {
            ts->atlases.back().shelf_y += ts->atlases.back().shelf_h;
            ts->atlases.back().shelf_h = 0;
            ts->atlases.back().shelf_x = 0;
        }
        if (gh > ts->atlases.back().shelf_h)
        {
            ts->atlases.back().shelf_y += ts->atlases.back().shelf_h;
            ts->atlases.back().shelf_h = gh;
            ts->atlases.back().shelf_x = 0;
        }
        if (ts->atlases.back().shelf_y + gh > ts->atlas_h)
        {
            // Page full: a fresh atlas. The glyph is never silently dropped.
            text_store::atlas_page page;
            page.tex = c->device->create_texture(ts->atlas_w, ts->atlas_h, nullptr);
            ts->atlases.push_back(page);
        }
        text_store::atlas_page &page = ts->atlases.back();
        const i32 px = page.shelf_x, py = page.shelf_y;
        page.shelf_x += gw + 1;
        std::vector<u8> rgba(static_cast<usize>(gw) * gh * 4);
        for (i32 n = 0; n < gw * gh; ++n)
        {
            u8 a = bitmap[n];
            rgba[n * 4 + 0] = 255;
            rgba[n * 4 + 1] = 255;
            rgba[n * 4 + 2] = 255;
            rgba[n * 4 + 3] = a;
        }
        c->device->update_texture(page.tex, px, py, gw, gh, rgba.data());
        g.tex = page.tex;
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
    font_data &fd = c->ts->fonts[static_cast<usize>(fh)];
    if (!fd.ok) return 0.0f;
    const f32 scale = stbtt_ScaleForPixelHeight(&fd.info, size);
    return static_cast<f32>(
               stbtt_GetCodepointKernAdvance(&fd.info, static_cast<i32>(a), static_cast<i32>(b))) *
           scale;
}

} // namespace detail

bool ui::has_glyph(u32 cp)
{
    context *c = ctx;
    if (!c->ts) return false;
    font_handle fh = c->active_theme.font;
    if (fh < 0 || fh >= static_cast<i32>(c->ts->fonts.size())) return false;
    font_data &fd = c->ts->fonts[static_cast<usize>(fh)];
    if (!fd.ok) return false;
    return stbtt_FindGlyphIndex(&fd.info, static_cast<i32>(cp)) != 0;
}

bool ui::is_visible(rect r) const
{
    if (ctx->clip_depth == 0) return true;
    const rect x = rect::intersect(r, ctx->clip_stack[ctx->clip_depth - 1]);
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
    const u64 key = (static_cast<u64>(static_cast<u32>(fh)) << 48) ^
                    (static_cast<u64>(static_cast<u32>(
                         static_cast<i32>(c->active_theme.text_size * 4.0f) & 0xFFFF))
                     << 32) ^
                    hash(s);
    if (text_store::text_layout *lay = c->ts->layout_cache.find(key)) return lay->width;

    text_store::text_layout built;
    usize i = 0;
    u32 prev = 0;
    while (i < s.size())
    {
        u32 cp = utf8_decode(s, i);
        if (cp == UTF8_END) break;
        text_store::cached_glyph e;
        e.kern_in = detail::kern_advance(c, fh, c->active_theme.text_size, prev, cp);
        e.cp = cp;
        e.g = detail::get_glyph(c, fh, c->active_theme.text_size, cp);
        // Two separate additions, in this order: float associativity would drift
        // the pen by ULPs, visible as edge coverage changes in the golden scenes.
        built.width += e.kern_in;
        built.width += e.g.advance;
        built.gs.push_back(e);
        prev = cp;
    }
    c->ts->layout_cache.put(key, built);
    if (text_store::text_layout *lay = c->ts->layout_cache.find(key)) return lay->width;
    return 0.0f;
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
    const u64 key =
        (static_cast<u64>(static_cast<u32>(fh)) << 48) ^
        (static_cast<u64>(static_cast<u32>(static_cast<i32>(size * 4.0f) & 0xFFFF)) << 32) ^
        hash(s);
    text_store::text_layout *lay = cc->ts->layout_cache.find(key);
    if (!lay)
    {
        text_store::text_layout built;
        usize i = 0;
        u32 prev = 0;
        while (i < s.size())
        {
            u32 cp = utf8_decode(s, i);
            if (cp == UTF8_END) break;
            text_store::cached_glyph e;
            e.kern_in = detail::kern_advance(cc, fh, size, prev, cp);
            e.cp = cp;
            e.g = detail::get_glyph(cc, fh, size, cp);
            built.width += e.kern_in; // two additions: same order as before
            built.width += e.g.advance;
            built.gs.push_back(e);
            prev = cp;
        }
        cc->ts->layout_cache.put(key, built);
        lay = cc->ts->layout_cache.find(key);
        if (!lay) return; // cache full? rebuild path guarantees a hit; paranoia
    }

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
    const f32 base_y = std::floor(baseline + 0.5f);
    if (bounded)
    {
        // generous glyph overhang: ascenders/diacritics above, descenders below
        const f32 over = size * 0.8f;
        if (pen - over >= pb.right() || pen + width + over <= pb.x ||
            base_y - size * 1.6f >= pb.bottom() || base_y + size * 0.8f <= pb.y)
            return;
    }
    for (text_store::cached_glyph &e : lay->gs)
    {
        pen += e.kern_in;
        f32 ix = std::floor(pen);
        i32 bin = static_cast<i32>((pen - ix) * 4.0f + 0.5f);
        if (bin >= 4)
        {
            bin = 0;
            ix += 1.0f;
        }
        if (bounded && e.g.w > 0.0f)
        {
            // the bin-0 bitmap's box (other bins differ by under a pixel)
            const f32 x0 = ix + e.g.xoff - 1.0f, y0 = base_y + e.g.yoff;
            if (x0 >= pb.right() || x0 + e.g.w + 2.0f <= pb.x || y0 >= pb.bottom() ||
                y0 + e.g.h <= pb.y)
            {
                pen += e.g.advance;
                continue;
            }
        }
        const glyph *gp = &e.g;
        if (bin > 0)
        {
            if (!(e.have & (1u << bin)))
            {
                e.sub[bin - 1] = detail::get_glyph(cc, fh, size, e.cp, bin);
                e.have = static_cast<u8>(e.have | (1u << bin));
            }
            gp = &e.sub[bin - 1];
        }
        const glyph &g = *gp;
        if (g.tex)
        {
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
    while (i < s.size())
    {
        u32 cp = utf8_decode(s, i);
        if (cp == UTF8_END) break;
        x += detail::kern_advance(cc, fh, cc->active_theme.text_size, prev, cp);
        x += detail::get_glyph(cc, fh, cc->active_theme.text_size, cp).advance;
        if (x + ell_w <= r.w) cut = i; // i is past this codepoint now
        prev = cp;
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
