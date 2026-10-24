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
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    ts->vector_enabled = ts->vector_enabled || (c->painter && c->painter->vector_text());
#endif

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

std::span<const u8> font_bytes(const context *c, font_handle font)
{
    if (!c || !c->ts || font < 0 || font >= static_cast<i32>(c->ts->fonts.size())) return {};
    const auto &fd = c->ts->fonts[static_cast<usize>(font)];
    return fd.ok ? std::span<const u8>(fd.buffer.data(), fd.buffer.size()) : std::span<const u8>{};
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
    return ctx->painter->measure_text_width(s);
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
    ctx->painter->text({r, s, c, a});
}

// Truncates with a trailing ellipsis (U+2026) when the line does not fit.
void ui::text_ellipsis(rect r, std::string_view s, color c, align a)
{
    ctx->painter->text_ellipsis({r, s, c, a});
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
    ctx->painter->text_wrapped({r, s, c, ALIGN_LEFT});
}
