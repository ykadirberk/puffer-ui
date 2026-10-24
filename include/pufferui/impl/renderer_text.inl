// Renderer-owned text emission. Layout measurement remains shared with UI.
// Included by pufferui.h inside namespace pui.
// One implementation translation unit.
// Four-line slice banner for amalgamation.

f32 renderer_detail::mesh_painter::text_width(std::string_view s)
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

f32 renderer_detail::mesh_painter::glyph_advance(const paint_glyph_metric &p)
{
    return detail::get_glyph(ctx, p.font, p.size, p.codepoint).advance;
}

void renderer_detail::mesh_painter::text(rect r, std::string_view s, color c, align a)
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
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
        if (e.vector)
        {
            const bool emitted =
                cc->dl && cc->dl->painter &&
                cc->dl->painter->glyph(cc, fh, e.glyph_index,
                                       {pen + e.offset.x, baseline - e.offset.y}, scale, c);
            if (!emitted)
            {
                // Overlay recorders and user wrappers have no native sink.
                // Preserve shaping via a bitmap of the actual glyph ID.
                const f32 x = pen + e.offset.x;
                f32 ix = std::floor(x * density);
                i32 sub = static_cast<i32>((x * density - ix) * 4 + 0.5f);
                if (sub >= 4)
                {
                    sub = 0;
                    ix += 1;
                }
                const auto g = detail::get_glyph(cc, fh, size, e.glyph_index, sub, true);
                if (g.tex)
                {
                    ts->atlases[static_cast<usize>(g.page)].last_used = cc->frame;
                    detail::dl_prepare(cc, g.tex, clip);
                    const f32 gx = ix / density + g.xoff,
                              gy = std::floor((baseline - e.offset.y) * density + 0.5f) / density +
                                   g.yoff;
                    const vertex v[] = {{gx, gy, g.u0, g.v0, c},
                                        {gx + g.w, gy, g.u1, g.v0, c},
                                        {gx + g.w, gy + g.h, g.u1, g.v1, c},
                                        {gx, gy + g.h, g.u0, g.v1, c}};
                    constexpr i32 indices[] = {0, 1, 2, 0, 2, 3};
                    detail::dl_add(cc, v, 4, indices, 6);
                }
            }
            pen += e.g.advance;
            continue;
        }
#endif
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

void renderer_detail::mesh_painter::text_ellipsis(rect r, std::string_view s, color c, align a)
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

void renderer_detail::mesh_painter::text_wrapped(rect r, std::string_view s, color c)
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
