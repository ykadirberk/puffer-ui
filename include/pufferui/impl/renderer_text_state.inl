// Renderer-owned font rasterization, atlas and prepared text caches.
// Included by pufferui.h inside namespace pui.
// Shared mesh algorithms, explicitly selected by backend painters.
// Four-line slice banner for amalgamation.
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
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    if (c->ts->vector_enabled)
    {
        u32 bits;
        std::memcpy(&bits, &size, sizeof(bits));
        key = id_child(key, static_cast<uiid>(bits) + 0x484252554eull);
    }
#endif
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

#if defined(PUFFERUI_ENABLE_HARFBUZZ)
inline glyph get_glyph(context *c, i32 font_index, f32 size, u32 cp, i32 sub = 0,
                       bool indexed = false)
#else
inline glyph get_glyph(context *c, i32 font_index, f32 size, u32 cp, i32 sub = 0)
#endif
{
    text_store *ts = c->ts;
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    if (!indexed)
#endif
        font_index = font_for(c, font_index, cp);
    const f32 density = c->render_scale;
    text_store::glyph_key key{font_index, static_cast<i32>(size * 4.0f),    cp,
                              sub,        static_cast<i32>(density * 1024), gamma_q(c)};
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    key.glyph_index = indexed;
#endif
    auto it = ts->glyphs.find(key);
    if (it != ts->glyphs.end()) return it->second;

    glyph g{};
    font_data &fd = ts->fonts[static_cast<usize>(font_index)];
    f32 scale = stbtt_ScaleForPixelHeight(&fd.info, size * density);
    i32 advance = 0, lsb = 0;
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    if (indexed)
        stbtt_GetGlyphHMetrics(&fd.info, static_cast<i32>(cp), &advance, &lsb);
    else
#endif
        stbtt_GetCodepointHMetrics(&fd.info, static_cast<i32>(cp), &advance, &lsb);
    g.advance = static_cast<f32>(advance) * stbtt_ScaleForPixelHeight(&fd.info, size);

    i32 gw = 0, gh = 0, xo = 0, yo = 0;
    // `sub` shifts the outline right by sub/4 of a pixel before rasterizing.
    unsigned char *bitmap =
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
        indexed
            ? stbtt_GetGlyphBitmapSubpixel(&fd.info, scale, scale, static_cast<f32>(sub) * 0.25f,
                                           0.0f, static_cast<i32>(cp), &gw, &gh, &xo, &yo)
            :
#endif
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

inline f32 memoized_kern(context *c, font_handle fh, f32 size, u32 a, u32 b, kerning_memo &memo)
{
    const u32 slot = (a * 0x9E3779B1u ^ b * 0x85EBCA77u) % kerning_memo::PAIRS;
    if (memo.a[slot] != a || memo.b[slot] != b)
    {
        memo.a[slot] = a;
        memo.b[slot] = b;
        memo.kern[slot] = kern_advance(c, fh, size, a, b);
    }
    return memo.kern[slot];
}

// The glyph run of `s`: the installed shaper's, or codepoint by codepoint with
// kerning pairs. Shared by measuring and drawing (one cache entry serves both).
inline text_store::text_layout build_layout(context *c, font_handle fh, f32 size,
                                            std::string_view s)
{
    text_store::text_layout built;
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    if (!c->ts->shaper && c->ts->vector_enabled && harfbuzz_layout(c, fh, size, s, built))
        return built;
#endif
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
    // Short labels keep their existing small path. A shaper (even one which
    // declines this run) retains its original invocation and lookup behavior.
    kerning_memo *memo = !c->ts->shaper && s.size() >= 256 ? &c->st->layout_pairs : nullptr;
    if (memo) memo->clear();
    usize i = 0;
    u32 prev = 0;
    while (i < s.size())
    {
        const u32 cp = utf8_decode(s, i);
        if (cp == UTF8_END) break;
        add(cp, memo ? memoized_kern(c, fh, size, prev, cp, *memo)
                     : kern_advance(c, fh, size, prev, cp));
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
